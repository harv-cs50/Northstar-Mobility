#include "services/GeminiClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace vrms {
namespace {

constexpr std::size_t kMaxResponseBytes = 128 * 1024;

std::string curlFailure(CURLcode code) {
    return "CURLcode " + std::to_string(static_cast<int>(code)) + ": " +
        curl_easy_strerror(code);
}

class CurlGeminiHttpTransport final : public GeminiHttpTransport {
public:
    GeminiHttpResponse post(const std::string& url,
                            const std::vector<std::string>& requestHeaders,
                            const std::string& requestBody,
                            long timeoutSeconds) override {
        static std::once_flag initialized;
        static CURLcode initResult = CURLE_OK;
        std::call_once(initialized, [] {
            initResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        });
        if (initResult != CURLE_OK)
            return {0, {}, false, true, curlFailure(initResult)};

        struct CurlGuard {
            CURL* handle = curl_easy_init();
            ~CurlGuard() { if (handle) curl_easy_cleanup(handle); }
        } curl;
        if (!curl.handle)
            return {0, {}, false, true, curlFailure(CURLE_FAILED_INIT)};

        struct HeaderGuard {
            curl_slist* headers = nullptr;
            ~HeaderGuard() { if (headers) curl_slist_free_all(headers); }
        } headers;
        for (const auto& requestHeader : requestHeaders) {
            auto* appended = curl_slist_append(headers.headers, requestHeader.c_str());
            if (!appended)
                return {0, {}, false, true, "Could not allocate Gemini HTTP headers"};
            headers.headers = appended;
        }

        struct ResponseBuffer {
            std::string body;
            bool tooLarge = false;
        } response;

        const auto write = +[](char* bytes, std::size_t size,
                               std::size_t count, void* userData) -> std::size_t {
            auto* buffer = static_cast<ResponseBuffer*>(userData);
            if (size != 0 && count > static_cast<std::size_t>(-1) / size) {
                buffer->tooLarge = true;
                return 0;
            }
            const std::size_t length = size * count;
            if (length > kMaxResponseBytes - buffer->body.size()) {
                buffer->tooLarge = true;
                return 0;
            }
            buffer->body.append(bytes, length);
            return length;
        };

        CURLcode code = curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_POST, 1L);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, requestBody.data());
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDSIZE_LARGE,
            static_cast<curl_off_t>(requestBody.size()));
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_HTTPHEADER, headers.headers);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &response);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_CONNECTTIMEOUT, 5L);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, timeoutSeconds);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_NOSIGNAL, 1L);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_SSL_VERIFYPEER, 1L);
        if (code == CURLE_OK) code = curl_easy_setopt(curl.handle, CURLOPT_SSL_VERIFYHOST, 2L);
        if (code != CURLE_OK)
            return {0, {}, false, true, curlFailure(code)};

        code = curl_easy_perform(curl.handle);
        long status = 0;
        curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
        if (code == CURLE_OPERATION_TIMEDOUT)
            return {status, {}, true, false, curlFailure(code)};
        if (code != CURLE_OK)
            return {status, {}, false, true, curlFailure(code)};
        if (response.tooLarge)
            return {status, {}, false, true,
                    "Gemini response exceeded the 131072-byte response limit"};
        return {status, std::move(response.body), false, false};
    }
};

std::string requiredEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !value[0])
        throw std::runtime_error(std::string("Missing required Gemini environment variable: ") + name);
    return value;
}

void validateModel(const std::string& model) {
    if (model.rfind("gemini-", 0) != 0 || model.size() > 80 ||
        !std::all_of(model.begin(), model.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '.';
        })) {
        throw std::invalid_argument(
            "GEMINI_MODEL must be a Gemini model ID such as gemini-3.8-flash");
    }
}

std::string escapeJson(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 16);
    for (unsigned char c : value) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                output += "\\u00";
                output.push_back(hex[(c >> 4) & 0x0f]);
                output.push_back(hex[c & 0x0f]);
            } else output.push_back(static_cast<char>(c));
        }
    }
    return output;
}

void appendUtf8(std::string& output, unsigned int codepoint) {
    if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

bool readHexCodepoint(const std::string& source, std::size_t& position,
                      unsigned int& codepoint) {
    if (position + 4 > source.size()) return false;
    codepoint = 0;
    for (int index = 0; index < 4; ++index) {
        const char c = source[position++];
        codepoint <<= 4;
        if (c >= '0' && c <= '9') codepoint += static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') codepoint += static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') codepoint += static_cast<unsigned int>(c - 'A' + 10);
        else return false;
    }
    return true;
}

bool readJsonString(const std::string& source, std::size_t& position,
                    std::string& value) {
    if (position >= source.size() || source[position++] != '"') return false;
    value.clear();
    while (position < source.size()) {
        const unsigned char c = static_cast<unsigned char>(source[position++]);
        if (c == '"') return true;
        if (c < 0x20) return false;
        if (c != '\\') {
            value.push_back(static_cast<char>(c));
            continue;
        }
        if (position >= source.size()) return false;
        const char escaped = source[position++];
        switch (escaped) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u': {
            unsigned int first = 0;
            if (!readHexCodepoint(source, position, first)) return false;
            if (first >= 0xd800 && first <= 0xdbff &&
                position + 6 <= source.size() && source[position] == '\\' &&
                source[position + 1] == 'u') {
                position += 2;
                unsigned int second = 0;
                if (!readHexCodepoint(source, position, second) ||
                    second < 0xdc00 || second > 0xdfff) return false;
                first = 0x10000 + ((first - 0xd800) << 10) + (second - 0xdc00);
            } else if (first >= 0xd800 && first <= 0xdfff) {
                first = 0xfffd;
            }
            appendUtf8(value, first);
            break;
        }
        default: return false;
        }
    }
    return false;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void skipJsonWhitespace(const std::string& source, std::size_t& position) {
    while (position < source.size() &&
           std::isspace(static_cast<unsigned char>(source[position]))) ++position;
}

bool skipJsonValue(const std::string& source, std::size_t& position,
                   unsigned int depth = 0) {
    if (depth > 64) return false;
    skipJsonWhitespace(source, position);
    if (position >= source.size()) return false;

    if (source[position] == '"') {
        std::string ignored;
        return readJsonString(source, position, ignored);
    }
    if (source[position] == '{') {
        ++position;
        skipJsonWhitespace(source, position);
        if (position < source.size() && source[position] == '}') {
            ++position;
            return true;
        }
        while (position < source.size()) {
            std::string key;
            if (!readJsonString(source, position, key)) return false;
            skipJsonWhitespace(source, position);
            if (position >= source.size() || source[position++] != ':') return false;
            if (!skipJsonValue(source, position, depth + 1)) return false;
            skipJsonWhitespace(source, position);
            if (position >= source.size()) return false;
            if (source[position] == '}') {
                ++position;
                return true;
            }
            if (source[position++] != ',') return false;
            skipJsonWhitespace(source, position);
        }
        return false;
    }
    if (source[position] == '[') {
        ++position;
        skipJsonWhitespace(source, position);
        if (position < source.size() && source[position] == ']') {
            ++position;
            return true;
        }
        while (position < source.size()) {
            if (!skipJsonValue(source, position, depth + 1)) return false;
            skipJsonWhitespace(source, position);
            if (position >= source.size()) return false;
            if (source[position] == ']') {
                ++position;
                return true;
            }
            if (source[position++] != ',') return false;
        }
        return false;
    }

    const std::size_t start = position;
    while (position < source.size() && source[position] != ',' &&
           source[position] != ']' && source[position] != '}' &&
           !std::isspace(static_cast<unsigned char>(source[position]))) ++position;
    return position > start;
}

bool objectFieldPosition(const std::string& source, std::size_t objectPosition,
                         const std::string& field, std::size_t& valuePosition) {
    skipJsonWhitespace(source, objectPosition);
    if (objectPosition >= source.size() || source[objectPosition++] != '{') return false;
    skipJsonWhitespace(source, objectPosition);
    if (objectPosition < source.size() && source[objectPosition] == '}') return false;

    while (objectPosition < source.size()) {
        std::string key;
        if (!readJsonString(source, objectPosition, key)) return false;
        skipJsonWhitespace(source, objectPosition);
        if (objectPosition >= source.size() || source[objectPosition++] != ':') return false;
        skipJsonWhitespace(source, objectPosition);
        const std::size_t valueStart = objectPosition;
        if (key == field) {
            valuePosition = valueStart;
            return true;
        }
        if (!skipJsonValue(source, objectPosition)) return false;
        skipJsonWhitespace(source, objectPosition);
        if (objectPosition >= source.size()) return false;
        if (source[objectPosition] == '}') return false;
        if (source[objectPosition++] != ',') return false;
        skipJsonWhitespace(source, objectPosition);
    }
    return false;
}

bool firstArrayValuePosition(const std::string& source, std::size_t arrayPosition,
                             std::size_t& valuePosition) {
    skipJsonWhitespace(source, arrayPosition);
    if (arrayPosition >= source.size() || source[arrayPosition++] != '[') return false;
    skipJsonWhitespace(source, arrayPosition);
    if (arrayPosition >= source.size() || source[arrayPosition] == ']') return false;
    valuePosition = arrayPosition;
    return true;
}

bool responseText(const std::string& body, std::string& answer,
                  std::string& parseReason) {
    std::size_t candidatesPosition = 0;
    if (!objectFieldPosition(body, 0, "candidates", candidatesPosition)) {
        parseReason = "missing candidates property";
        return false;
    }
    std::size_t candidatePosition = 0;
    if (!firstArrayValuePosition(body, candidatesPosition, candidatePosition)) {
        parseReason = "candidates array is empty or malformed";
        return false;
    }

    std::size_t contentPosition = 0;
    if (!objectFieldPosition(body, candidatePosition, "content", contentPosition)) {
        parseReason = "candidates[0].content is missing or malformed";
        return false;
    }
    std::size_t partsPosition = 0;
    if (!objectFieldPosition(body, contentPosition, "parts", partsPosition)) {
        parseReason = "candidates[0].content.parts is missing or malformed";
        return false;
    }
    std::size_t cursor = partsPosition;
    skipJsonWhitespace(body, cursor);
    if (cursor >= body.size() || body[cursor++] != '[') {
        parseReason = "candidates[0].content.parts is not an array";
        return false;
    }

    bool foundText = false;
    bool closedParts = false;
    while (cursor < body.size()) {
        skipJsonWhitespace(body, cursor);
        if (cursor >= body.size()) break;
        if (body[cursor] == ']') {
            ++cursor;
            closedParts = true;
            break;
        }

        const std::size_t partPosition = cursor;
        if (!skipJsonValue(body, cursor)) {
            parseReason = "malformed candidate part";
            return false;
        }
        if (body[partPosition] == '{') {
            std::size_t textPosition = 0;
            if (objectFieldPosition(body, partPosition, "text", textPosition)) {
                std::string partText;
                if (!readJsonString(body, textPosition, partText)) {
                    parseReason = "candidate part text is not a valid JSON string";
                    return false;
                }
                answer += partText;
                foundText = true;
            }
        }
        skipJsonWhitespace(body, cursor);
        if (cursor < body.size() && body[cursor] == ',') {
            ++cursor;
            skipJsonWhitespace(body, cursor);
            if (cursor >= body.size() || body[cursor] == ']') {
                parseReason = "malformed candidates[0].content.parts array";
                return false;
            }
            continue;
        }
        if (cursor < body.size() && body[cursor] == ']') {
            ++cursor;
            closedParts = true;
            break;
        }
        parseReason = "malformed candidates[0].content.parts array";
        return false;
    }
    if (!closedParts) {
        parseReason = "unterminated candidates[0].content.parts array";
        return false;
    }

    std::size_t validationPosition = 0;
    if (!skipJsonValue(body, validationPosition)) {
        parseReason = "Gemini response is not valid JSON";
        return false;
    }
    skipJsonWhitespace(body, validationPosition);
    if (validationPosition != body.size()) {
        parseReason = "Gemini response contains trailing data";
        return false;
    }
    if (!foundText || answer.empty()) {
        parseReason = "candidates[0].content.parts contains no text";
        return false;
    }
    return true;
}

bool stringField(const std::string& source, std::size_t objectPosition,
                 const std::string& key, std::string& value) {
    std::size_t position = 0;
    if (!objectFieldPosition(source, objectPosition, key, position)) return false;
    return readJsonString(source, position, value);
}

std::string safeProviderErrorCategory(const std::string& message) {
    const auto lowered = lowercase(message);
    if (lowered.find("temperature") != std::string::npos &&
        (lowered.find("unknown name") != std::string::npos ||
         lowered.find("not supported") != std::string::npos ||
         lowered.find("cannot find field") != std::string::npos))
        return "unsupported_generation_config_temperature";
    if (lowered.find("quota") != std::string::npos ||
        lowered.find("resource_exhausted") != std::string::npos)
        return "quota_or_rate_limit";
    if (lowered.find("api key") != std::string::npos ||
        lowered.find("apikey") != std::string::npos ||
        lowered.find("authorization key") != std::string::npos ||
        lowered.find("access_token_type_unsupported") != std::string::npos ||
        lowered.find("api_key_invalid") != std::string::npos)
        return "credential_rejected";
    if (lowered.find("model") != std::string::npos ||
        lowered.find("endpoint") != std::string::npos)
        return "model_or_endpoint_rejected";
    if (lowered.find("unknown name") != std::string::npos ||
        lowered.find("cannot find field") != std::string::npos)
        return "unsupported_request_field";
    return "provider_rejected_request";
}

std::string sanitizedErrorBody(const std::string& body) {
    std::size_t errorPosition = 0;
    std::string output = "{\"body_bytes\":" + std::to_string(body.size());
    if (!objectFieldPosition(body, 0, "error", errorPosition)) {
        output += ",\"provider_body\":\"[redacted or unparseable]\"";
        output += ",\"body_format\":\"nonstandard_error_response\"";
        output += ",\"category\":\"" + safeProviderErrorCategory(body) + "\"}";
        return output;
    }

    std::string code;
    std::size_t codePosition = 0;
    if (objectFieldPosition(body, errorPosition, "code", codePosition)) {
        const auto start = codePosition;
        while (codePosition < body.size() &&
               std::isdigit(static_cast<unsigned char>(body[codePosition])))
            ++codePosition;
        if (codePosition > start && codePosition - start <= 3) {
            const auto numericCode = std::stoi(body.substr(start, codePosition - start));
            if (numericCode >= 100 && numericCode <= 599)
                code = std::to_string(numericCode);
        }
    }
    std::string status;
    stringField(body, errorPosition, "status", status);
    const bool safeStatus = status == "INVALID_ARGUMENT" ||
        status == "RESOURCE_EXHAUSTED" || status == "PERMISSION_DENIED" ||
        status == "UNAUTHENTICATED" || status == "NOT_FOUND" ||
        status == "FAILED_PRECONDITION" || status == "INTERNAL" ||
        status == "UNAVAILABLE" || status == "DEADLINE_EXCEEDED" ||
        status == "UNKNOWN" || status == "OUT_OF_RANGE" ||
        status == "ABORTED" || status == "CANCELLED";
    std::string message;
    stringField(body, errorPosition, "message", message);

    output += ",\"error_code\":" + (code.empty() ? "null" : code);
    output += ",\"error_status\":\"" +
        (safeStatus ? status : std::string("redacted")) + "\"";
    output += ",\"category\":\"" + safeProviderErrorCategory(message) + "\"";
    output += ",\"message\":\"[redacted]\"}";
    return output;
}

std::string sanitizedTransportError(const std::string& details) {
    if (details.empty()) return "unavailable";
    std::string safe;
    safe.reserve(std::min<std::size_t>(details.size(), 160));
    for (unsigned char c : details) {
        if (safe.size() == 160) break;
        if (std::isalnum(c) || c == ' ' || c == '.' || c == ':' ||
            c == '_' || c == '-') safe.push_back(static_cast<char>(c));
        else safe.push_back('_');
    }
    return safe;
}

void logHttpResponse(const std::string& model, long statusCode,
                     std::size_t responseBytes) {
    std::cerr << "[GeminiClient] model=" << model
              << " http_status=" << statusCode
              << " response_bytes=" << responseBytes << '\n';
}

} // namespace

GeminiClient::GeminiClient()
    : transport(std::make_shared<CurlGeminiHttpTransport>()),
      apiKey(requiredEnvironment("GEMINI_API_KEY")),
      model(modelFromEnvironment()), timeoutSeconds(20) {
    validateModel(model);
}

GeminiClient::GeminiClient(std::shared_ptr<GeminiHttpTransport> httpTransport,
                           std::string key,
                           std::string modelId,
                           long timeout)
    : transport(std::move(httpTransport)), apiKey(std::move(key)),
      model(std::move(modelId)), timeoutSeconds(timeout) {
    if (!transport) throw std::invalid_argument("Gemini client requires an HTTP transport");
    if (apiKey.empty()) throw std::invalid_argument("Gemini API key cannot be empty");
    validateModel(model);
    if (timeoutSeconds < 1 || timeoutSeconds > 120)
        throw std::invalid_argument("Gemini timeout must be between 1 and 120 seconds");
}

std::string GeminiClient::modelFromEnvironment() {
    return requiredEnvironment("GEMINI_MODEL");
}

std::string GeminiClient::generate(const std::string& systemInstruction,
                                   const std::string& userContent) {
    const std::string url =
        "https://generativelanguage.googleapis.com/v1beta/models/" +
        model + ":generateContent";
    const std::string requestBody =
        "{\"systemInstruction\":{\"parts\":[{\"text\":\"" +
        escapeJson(systemInstruction) +
        "\"}]},\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"" +
        escapeJson(userContent) +
        "\"}]}],\"generationConfig\":{\"maxOutputTokens\":512}}";

    const std::vector<std::string> headers = {
        "Content-Type: application/json",
        "x-goog-api-key: " + apiKey
    };
    const auto response = transport->post(url, headers, requestBody, timeoutSeconds);
    if (response.timedOut) {
        std::cerr << "[GeminiClient] model=" << model
                  << " http_status=" << response.statusCode
                  << " transport_failure=timeout timeout_seconds=" << timeoutSeconds
                  << " curl_error=\""
                  << sanitizedTransportError(response.transportError) << "\"\n";
        throw ChatbotError(504, "The AI support service timed out. Please try again.");
    }
    if (response.transportFailed) {
        std::cerr << "[GeminiClient] model=" << model
                  << " http_status=" << response.statusCode
                  << " transport_failure=true timeout_seconds=" << timeoutSeconds
                  << " curl_error=\""
                  << sanitizedTransportError(response.transportError) << "\"\n";
        throw ChatbotError(503, "AI support is temporarily unavailable. Please try again shortly.");
    }

    logHttpResponse(model, response.statusCode, response.body.size());
    const auto lowerBody = lowercase(response.body);
    const bool quotaError = lowerBody.find("resource_exhausted") != std::string::npos ||
                            lowerBody.find("quota") != std::string::npos;
    if (response.statusCode == 429 ||
        ((response.statusCode == 403 || response.statusCode == 429) && quotaError)) {
        std::cerr << "[GeminiClient] sanitized_gemini_error="
                  << sanitizedErrorBody(response.body) << '\n';
        throw ChatbotError(429,
            "AI support has reached a temporary rate or free-tier quota limit. Please try again later.");
    }
    if (response.statusCode < 200 || response.statusCode >= 300) {
        std::cerr << "[GeminiClient] sanitized_gemini_error="
                  << sanitizedErrorBody(response.body) << '\n';
        if (response.statusCode >= 500)
            throw ChatbotError(503, "The AI support service is temporarily unavailable.");
        if (response.statusCode == 401 || response.statusCode == 403)
            throw ChatbotError(503,
                "AI support is not configured correctly. Check the server-side Gemini key and model settings.");
        throw ChatbotError(502,
            "The AI support service could not process this message. Please try again.");
    }

    std::string answer;
    std::string parseReason;
    if (!responseText(response.body, answer, parseReason)) {
        std::cerr << "[GeminiClient] JSON parsing failure http_status="
                  << response.statusCode << " response_bytes="
                  << response.body.size() << " reason=\""
                  << parseReason << "\"\n";
        throw ChatbotError(502,
            "The AI support service returned no answer. Please try again.");
    }
    return answer;
}

} // namespace vrms
