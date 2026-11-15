#include "services/EmailService.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace vrms {
namespace {

std::string requiredEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !value[0]) {
        throw std::runtime_error(std::string("Missing required SMTP environment variable: ") + name);
    }
    return value;
}

std::string optionalEnvironment(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && value[0] ? value : fallback;
}

bool hasHeaderBreak(const std::string& value) {
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

bool looksLikeEmail(const std::string& value) {
    if (value.empty() || hasHeaderBreak(value)) return false;
    const auto at = value.find('@');
    const auto dot = value.rfind('.');
    return at != std::string::npos && at > 0 && dot > at + 1 && dot + 1 < value.size();
}

struct CurlHandle {
    CURL* value = nullptr;
    ~CurlHandle() { if (value) curl_easy_cleanup(value); }
};

struct MimeHandle {
    curl_mime* value = nullptr;
    ~MimeHandle() { if (value) curl_mime_free(value); }
};

struct HeaderList {
    curl_slist* value = nullptr;
    ~HeaderList() { if (value) curl_slist_free_all(value); }
    void append(const std::string& header) {
        auto* newList = curl_slist_append(value, header.c_str());
        if (!newList) throw std::runtime_error("SMTP message header allocation failed");
        value = newList;
    }
};

std::string quoteDisplayName(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (char c : value) {
        if (c == '\\' || c == '"') result.push_back('\\');
        if (static_cast<unsigned char>(c) >= 0x20 && c != '\r' && c != '\n')
            result.push_back(c);
    }
    result.push_back('"');
    return result;
}

class CurlSmtpTransport final : public SmtpTransport {
public:
    void send(const SmtpConfig& config, const EmailMessage& message) override {
        static std::once_flag curlInit;
        static CURLcode initCode = CURLE_OK;
        std::call_once(curlInit, [] { initCode = curl_global_init(CURL_GLOBAL_DEFAULT); });
        if (initCode != CURLE_OK) throw std::runtime_error("SMTP transport initialization failed");

        CurlHandle easy{curl_easy_init()};
        if (!easy.value) throw std::runtime_error("SMTP transport could not allocate a connection");

        const std::string scheme = config.security == SmtpSecurity::ImplicitTls ? "smtps://" : "smtp://";
        const std::string host = config.host.find(':') != std::string::npos &&
                                 config.host.front() != '['
            ? "[" + config.host + "]" : config.host;
        const std::string url = scheme + host + ":" + std::to_string(config.port);
        const std::string fromHeader = "From: " + quoteDisplayName(config.fromName) +
                                       " <" + config.fromEmail + ">";
        const std::string toHeader = "To: <" + message.recipient + ">";
        const std::string subjectHeader = "Subject: " + message.subject;

        HeaderList headers;
        headers.append(fromHeader);
        headers.append(toHeader);
        headers.append(subjectHeader);

        MimeHandle mime{curl_mime_init(easy.value)};
        if (!mime.value) throw std::runtime_error("SMTP message allocation failed");

        curl_mimepart* body = curl_mime_addpart(mime.value);
        if (!body || curl_mime_data(body, message.textBody.c_str(), CURL_ZERO_TERMINATED) != CURLE_OK ||
            curl_mime_type(body, "text/plain; charset=utf-8") != CURLE_OK)
            throw std::runtime_error("SMTP message body could not be prepared");

        curl_mimepart* attachment = curl_mime_addpart(mime.value);
        if (!attachment ||
            curl_mime_data(attachment,
                reinterpret_cast<const char*>(message.attachment.content.data()),
                message.attachment.content.size()) != CURLE_OK ||
            curl_mime_filename(attachment, message.attachment.filename.c_str()) != CURLE_OK ||
            curl_mime_type(attachment, message.attachment.mimeType.c_str()) != CURLE_OK ||
            curl_mime_encoder(attachment, "base64") != CURLE_OK)
            throw std::runtime_error("SMTP PDF attachment could not be prepared");

        curl_slist* recipients = curl_slist_append(nullptr, message.recipient.c_str());
        if (!recipients) throw std::runtime_error("SMTP recipient allocation failed");
        struct RecipientsGuard {
            curl_slist* items;
            ~RecipientsGuard() { curl_slist_free_all(items); }
        } recipientsGuard{recipients};

        CURLcode code = CURLE_OK;
        code = curl_easy_setopt(easy.value, CURLOPT_URL, url.c_str());
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_USERNAME, config.username.c_str());
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_PASSWORD, config.password.c_str());
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_MAIL_FROM, config.fromEmail.c_str());
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_MAIL_RCPT, recipients);
        // SMTP MIME document headers belong on CURLOPT_HTTPHEADER; MIME-part
        // headers would incorrectly place From/To/Subject inside the body part.
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_HTTPHEADER, headers.value);
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_MIMEPOST, mime.value);
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_CONNECTTIMEOUT, config.timeoutSeconds);
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_TIMEOUT, config.timeoutSeconds);
        if (code == CURLE_OK) code = curl_easy_setopt(easy.value, CURLOPT_NOSIGNAL, 1L);
        if (code == CURLE_OK && config.security != SmtpSecurity::None)
            code = curl_easy_setopt(easy.value, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        if (code != CURLE_OK) throw std::runtime_error("SMTP connection could not be configured");

        code = curl_easy_perform(easy.value);
        if (code != CURLE_OK) {
            // Avoid forwarding library/server diagnostics that may contain deployment details.
            throw std::runtime_error("SMTP server did not accept the invoice message");
        }
    }
};

} // namespace

SmtpConfig SmtpConfig::fromEnvironment() {
    SmtpConfig config;
    config.host = requiredEnvironment("VRMS_SMTP_HOST");
    const auto portText = requiredEnvironment("VRMS_SMTP_PORT");
    config.username = requiredEnvironment("VRMS_SMTP_USERNAME");
    config.password = requiredEnvironment("VRMS_SMTP_PASSWORD");
    config.fromEmail = requiredEnvironment("VRMS_SMTP_FROM_EMAIL");
    config.fromName = requiredEnvironment("VRMS_SMTP_FROM_NAME");

    try {
        std::size_t consumed = 0;
        config.port = std::stoi(portText, &consumed);
        if (consumed != portText.size()) throw std::invalid_argument("port");
    } catch (...) {
        throw std::invalid_argument("VRMS_SMTP_PORT must be a number from 1 to 65535");
    }

    const auto security = optionalEnvironment("VRMS_SMTP_SECURITY", "starttls");
    if (security == "starttls") config.security = SmtpSecurity::StartTls;
    else if (security == "ssl") config.security = SmtpSecurity::ImplicitTls;
    else if (security == "none") config.security = SmtpSecurity::None;
    else throw std::invalid_argument("VRMS_SMTP_SECURITY must be starttls, ssl, or none");

    const auto timeout = optionalEnvironment("VRMS_SMTP_TIMEOUT_SECONDS", "20");
    try {
        std::size_t consumed = 0;
        config.timeoutSeconds = std::stol(timeout, &consumed);
        if (consumed != timeout.size()) throw std::invalid_argument("timeout");
    } catch (...) {
        throw std::invalid_argument("VRMS_SMTP_TIMEOUT_SECONDS must be a positive integer");
    }
    config.validate();
    return config;
}

void SmtpConfig::validate() const {
    if (host.empty() || host.find_first_of("/\\\r\n") != std::string::npos)
        throw std::invalid_argument("VRMS_SMTP_HOST must be a host name or IP address");
    if (port < 1 || port > 65535)
        throw std::invalid_argument("VRMS_SMTP_PORT must be a number from 1 to 65535");
    if (username.empty()) throw std::invalid_argument("VRMS_SMTP_USERNAME cannot be empty");
    if (password.empty()) throw std::invalid_argument("VRMS_SMTP_PASSWORD cannot be empty");
    if (!looksLikeEmail(fromEmail))
        throw std::invalid_argument("VRMS_SMTP_FROM_EMAIL must be a valid email address");
    if (fromName.empty() || hasHeaderBreak(fromName))
        throw std::invalid_argument("VRMS_SMTP_FROM_NAME cannot be empty or contain line breaks");
    if (timeoutSeconds < 1 || timeoutSeconds > 300)
        throw std::invalid_argument("VRMS_SMTP_TIMEOUT_SECONDS must be between 1 and 300");
}

EmailService::EmailService() : transport(std::make_shared<CurlSmtpTransport>()) {}

EmailService::EmailService(std::shared_ptr<SmtpTransport> smtpTransport)
    : transport(std::move(smtpTransport)) {
    if (!transport) throw std::invalid_argument("Email service requires an SMTP transport");
}

void EmailService::sendInvoice(
    const std::string& recipient,
    const std::string& invoiceId,
    const std::vector<std::uint8_t>& pdfBytes
) {
    const auto config = SmtpConfig::fromEnvironment();
    if (!looksLikeEmail(recipient)) throw std::invalid_argument("Invoice recipient email is invalid");
    if (invoiceId.empty() || invoiceId.find_first_of("/\\\r\n") != std::string::npos)
        throw std::invalid_argument("Invoice ID is not valid for an email attachment");
    if (pdfBytes.size() < 5 || std::string(pdfBytes.begin(), pdfBytes.begin() + 5) != "%PDF-")
        throw std::invalid_argument("Invoice email attachment is not a valid PDF");

    EmailMessage message;
    message.recipient = recipient;
    message.subject = "Your vehicle rental invoice " + invoiceId;
    message.textBody = "Hello,\n\nYour vehicle rental invoice " + invoiceId +
        " is attached as a PDF.\n\nThis application uses simulated payments; no real payment was processed.\n";
    message.attachment.filename = "invoice-" + invoiceId + ".pdf";
    message.attachment.mimeType = "application/pdf";
    message.attachment.content = pdfBytes;
    transport->send(config, message);
}

}
