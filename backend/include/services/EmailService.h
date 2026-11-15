#ifndef EMAIL_SERVICE_H
#define EMAIL_SERVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vrms {

enum class SmtpSecurity { StartTls, ImplicitTls, None };

struct SmtpConfig {
    std::string host;
    int port = 0;
    std::string username;
    std::string password;
    std::string fromEmail;
    std::string fromName;
    SmtpSecurity security = SmtpSecurity::StartTls;
    long timeoutSeconds = 20;

    static SmtpConfig fromEnvironment();
    void validate() const;
};

struct EmailAttachment {
    std::string filename;
    std::string mimeType;
    std::vector<std::uint8_t> content;
};

struct EmailMessage {
    std::string recipient;
    std::string subject;
    std::string textBody;
    EmailAttachment attachment;
};

class SmtpTransport {
public:
    virtual ~SmtpTransport() = default;
    virtual void send(const SmtpConfig& config, const EmailMessage& message) = 0;
};

class EmailService {
public:
    EmailService();
    explicit EmailService(std::shared_ptr<SmtpTransport> transport);

    void sendInvoice(
        const std::string& recipient,
        const std::string& invoiceId,
        const std::vector<std::uint8_t>& pdfBytes
    );

private:
    std::shared_ptr<SmtpTransport> transport;
};

}

#endif
