#ifndef GEMINI_CLIENT_H
#define GEMINI_CLIENT_H

#include "services/ChatbotService.h"

#include <memory>
#include <string>
#include <vector>

namespace vrms {

struct GeminiHttpResponse {
    long statusCode = 0;
    std::string body;
    bool timedOut = false;
    bool transportFailed = false;
    // A libcurl error code/description only; never contains request headers or body.
    std::string transportError;
};

class GeminiHttpTransport {
public:
    virtual ~GeminiHttpTransport() = default;
    virtual GeminiHttpResponse post(const std::string& url,
                                    const std::vector<std::string>& headers,
                                    const std::string& requestBody,
                                    long timeoutSeconds) = 0;
};

class GeminiClient final : public ChatbotGenerator {
public:
    GeminiClient();
    GeminiClient(std::shared_ptr<GeminiHttpTransport> transport,
                 std::string apiKey,
                 std::string model,
                 long timeoutSeconds = 20);

    std::string generate(const std::string& systemInstruction,
                         const std::string& userContent) override;

    static std::string modelFromEnvironment();

private:
    std::shared_ptr<GeminiHttpTransport> transport;
    std::string apiKey;
    std::string model;
    long timeoutSeconds;
};

} // namespace vrms

#endif
