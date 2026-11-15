#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "DatabaseConfig.h"
#include "auth/PasswordHasher.h"
#include "models/Invoice.h"
#include "models/Booking.h"
#include "models/Payment.h"
#include "repositories/BookingRepository.h"
#include "repositories/InvoiceRepository.h"
#include "repositories/MaintenanceRepository.h"
#include "repositories/PaymentRepository.h"
#include "repositories/RentalRepository.h"
#include "repositories/VehicleRepository.h"
#include "services/AuthService.h"
#include "services/InvoiceService.h"
#include "services/InvoiceDeliveryService.h"
#include "services/EmailService.h"
#include "services/InvoicePdfGenerator.h"
#include "services/ChatbotService.h"
#include "services/GeminiClient.h"
#include "services/PostgresChatbotDataSource.h"
#include "services/MaintenanceService.h"
#include "services/PaymentService.h"
#include "services/RentalService.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr std::size_t MAX_REQUEST_BYTES = 1024 * 1024;
std::atomic<unsigned long long> idCounter{0};
vrms::AuthService& authService() {
    static vrms::AuthService service;
    return service;
}

vrms::ChatbotRateLimiter& chatbotRateLimiter() {
    static vrms::ChatbotRateLimiter limiter(12);
    return limiter;
}

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string clientAddress;
};

struct Response {
    int status = 200;
    std::string body = "{}";
    std::string contentType = "application/json; charset=utf-8";
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseRequest(const std::string& raw, Request& request) {
    const auto headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;
    const auto firstEnd = raw.find("\r\n");
    if (firstEnd == std::string::npos || firstEnd > headerEnd) return false;

    std::istringstream firstLine(raw.substr(0, firstEnd));
    std::string version;
    if (!(firstLine >> request.method >> request.target >> version) ||
        version.rfind("HTTP/1.", 0) != 0) return false;
    request.path = request.target.substr(0, request.target.find('?'));

    std::size_t position = firstEnd + 2;
    while (position < headerEnd) {
        const auto lineEnd = raw.find("\r\n", position);
        const auto end = lineEnd == std::string::npos || lineEnd > headerEnd
            ? headerEnd : lineEnd;
        const auto colon = raw.find(':', position);
        if (colon == std::string::npos || colon >= end) return false;
        const std::string key = lower(trim(raw.substr(position, colon - position)));
        request.headers[key] = trim(raw.substr(colon + 1, end - colon - 1));
        position = end + 2;
    }

    if (request.headers.count("transfer-encoding") != 0) return false;
    std::size_t contentLength = 0;
    const auto length = request.headers.find("content-length");
    if (length != request.headers.end()) {
        try {
            std::size_t consumed = 0;
            contentLength = std::stoul(length->second, &consumed);
            if (consumed != length->second.size()) return false;
        } catch (...) { return false; }
    }
    if (contentLength > MAX_REQUEST_BYTES) return false;
    const auto bodyStart = headerEnd + 4;
    if (raw.size() < bodyStart + contentLength) return false;
    request.body.assign(raw, bodyStart, contentLength);
    return true;
}

bool parseString(const std::string& source, std::size_t& position,
                 std::string& value) {
    if (position >= source.size() || source[position] != '"') return false;
    ++position;
    value.clear();
    while (position < source.size()) {
        const char c = source[position++];
        if (c == '"') return true;
        if (static_cast<unsigned char>(c) < 0x20) return false;
        if (c != '\\') { value.push_back(c); continue; }
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
            if (position + 4 > source.size()) return false;
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
                const char h = source[position++];
                code <<= 4;
                if (h >= '0' && h <= '9') code += h - '0';
                else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
                else return false;
            }
            if (code >= 0xd800 && code <= 0xdbff) {
                if (position + 6 > source.size() || source[position] != '\\' ||
                    source[position + 1] != 'u') return false;
                position += 2;
                unsigned int low = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = source[position++];
                    low <<= 4;
                    if (h >= '0' && h <= '9') low += h - '0';
                    else if (h >= 'a' && h <= 'f') low += h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') low += h - 'A' + 10;
                    else return false;
                }
                if (low < 0xdc00 || low > 0xdfff) return false;
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            } else if (code >= 0xdc00 && code <= 0xdfff) {
                return false;
            }
            if (code <= 0x7f) value.push_back(static_cast<char>(code));
            else if (code <= 0x7ff) {
                value.push_back(static_cast<char>(0xc0 | (code >> 6)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else if (code <= 0xffff) {
                value.push_back(static_cast<char>(0xe0 | (code >> 12)));
                value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else {
                value.push_back(static_cast<char>(0xf0 | (code >> 18)));
                value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            }
            break;
        }
        default: return false;
        }
    }
    return false;
}

using JsonObject = std::unordered_map<std::string, std::string>;

bool parseJsonObject(const std::string& source, JsonObject& output) {
    std::size_t p = 0;
    auto skip = [&] { while (p < source.size() && std::isspace(
        static_cast<unsigned char>(source[p]))) ++p; };
    skip();
    if (p >= source.size() || source[p++] != '{') return false;
    skip();
    if (p < source.size() && source[p] == '}') return ++p == source.size();
    while (p < source.size()) {
        std::string key, value;
        if (!parseString(source, p, key)) return false;
        skip();
        if (p >= source.size() || source[p++] != ':') return false;
        skip();
        if (p < source.size() && source[p] == '"') {
            if (!parseString(source, p, value)) return false;
        } else {
            const auto start = p;
            while (p < source.size() && source[p] != ',' && source[p] != '}' &&
                   !std::isspace(static_cast<unsigned char>(source[p]))) ++p;
            if (start == p) return false;
            value = source.substr(start, p - start);
            if (value == "null") value.clear();
        }
        output[key] = value;
        skip();
        if (p >= source.size()) return false;
        if (source[p] == '}') return ++p == source.size();
        if (source[p++] != ',') return false;
        skip();
    }
    return false;
}

std::string json(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u" << std::hex << std::setw(4)
                              << std::setfill('0') << static_cast<int>(c);
            else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

std::string number(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string field(const JsonObject& object, const std::string& key) {
    const auto found = object.find(key);
    return found == object.end() ? std::string{} : found->second;
}

Response result(int status, const std::string& body) {
    return {status, body, "application/json; charset=utf-8"};
}

Response error(int status, const std::string& message) {
    return result(status, "{\"error\":" + json(message) + "}");
}

double invoiceTaxPercent() {
    const char* configured = std::getenv("VRMS_INVOICE_TAX_PERCENT");
    if (!configured || !configured[0]) return 0.0;
    try {
        std::size_t consumed = 0;
        const double value = std::stod(configured, &consumed);
        if (consumed != std::string(configured).size() ||
            !std::isfinite(value) || value < 0.0 || value > 100.0)
            throw std::invalid_argument("tax percentage");
        return value;
    } catch (...) {
        throw std::invalid_argument("VRMS_INVOICE_TAX_PERCENT must be between 0 and 100");
    }
}

std::string newId(const std::string& prefix) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix + std::to_string(now) + "-" +
        std::to_string(++idCounter);
}

std::string bearer(const Request& request) {
    const auto found = request.headers.find("authorization");
    if (found == request.headers.end()) return {};
    constexpr char prefix[] = "Bearer ";
    if (found->second.size() <= sizeof(prefix) - 1 ||
        lower(found->second.substr(0, sizeof(prefix) - 1)) != "bearer ") return {};
    return trim(found->second.substr(sizeof(prefix) - 1));
}

struct Principal { std::string id; std::string role; };

Principal authenticate(const Request& request) {
    const auto session = bearer(request);
    if (session.empty()) return {};
    auto& auth = authService();
    Principal principal{auth.getSessionUserId(session), auth.getSessionUserRole(session)};
    if (principal.id.empty() || principal.role.empty()) return {};
    return principal;
}

std::string vehicleJson(const vrms::VehicleRecord& v) {
    return "{\"id\":" + json(v.id) + ",\"brand\":" + json(v.brand) +
        ",\"model\":" + json(v.model) + ",\"type\":" + json(v.type) +
        ",\"dailyRate\":" + number(v.dailyRate) +
        ",\"available\":" + (v.available ? "true" : "false") + "}";
}

std::string bookingJson(const vrms::BookingRecord& b) {
    return "{\"bookingId\":" + json(b.bookingId) +
        ",\"customerId\":" + json(b.customerId) +
        ",\"vehicleId\":" + json(b.vehicleId) +
        ",\"startDate\":" + json(b.startDate) +
        ",\"endDate\":" + json(b.endDate) +
        ",\"status\":" + json(b.status) + "}";
}

template <typename Records, typename Serializer>
std::string arrayJson(const Records& records, Serializer serialize) {
    std::string output = "[";
    bool first = true;
    for (const auto& record : records) {
        if (!first) output += ',';
        first = false;
        output += serialize(record);
    }
    output += ']';
    return output;
}

bool ownsBooking(const std::vector<vrms::BookingRecord>& bookings,
                 const std::string& id) {
    return std::any_of(bookings.begin(), bookings.end(), [&](const auto& b) {
        return b.bookingId == id;
    });
}

std::unordered_set<std::string> customerBookingIds(
    const std::vector<vrms::BookingRecord>& bookings) {
    std::unordered_set<std::string> ids;
    for (const auto& booking : bookings) ids.insert(booking.bookingId);
    return ids;
}

Response dispatch(const Request& request) {
    if (request.path == "/api/chat" && request.body.size() > 12000)
        return error(413, "Chat request is too large. Keep the message and recent history shorter.");

    JsonObject body;
    if (!request.body.empty() && !parseJsonObject(request.body, body))
        return error(400, "Request body must be a valid JSON object");

    if (request.method == "GET" && request.path == "/api/health") {
        try {
            auto connection = vrms::createDatabaseConnection();
            pqxx::work transaction(connection);
            const auto db = transaction.exec("SELECT current_database()");
            const auto database = db[0][0].as<std::string>();
            transaction.commit();
            return result(200, "{\"status\":\"ok\",\"database\":" +
                          json(database) + "}");
        } catch (...) { return error(503, "Database is not available"); }
    }

    if (request.method == "POST" && request.path == "/api/auth/register") {
        auto registration = authService().registerCustomer(
            field(body, "userId"), field(body, "name"), field(body, "email"),
            field(body, "password"), "Regular");
        if (!registration.success) {
            if (registration.message == "Invalid registration details")
                return error(400, registration.message);
            if (registration.message == "User ID or email already exists")
                return error(409, registration.message);
            return error(503, "Registration service is not available");
        }
        return result(201, "{\"userId\":" + json(registration.userId) +
            ",\"name\":" + json(registration.name) +
            ",\"role\":\"CUSTOMER\",\"message\":" +
            json(registration.message) + "}");
    }

    if (request.method == "POST" && request.path == "/api/auth/login") {
        auto login = authService().login(field(body, "email"), field(body, "password"));
        if (!login.success) {
            if (login.message == "Invalid email or password")
                return error(401, login.message);
            return error(503, "Authentication service is not available");
        }
        return result(200, "{\"userId\":" + json(login.userId) +
            ",\"name\":" + json(login.name) + ",\"role\":" +
            json(login.role) + ",\"sessionId\":" + json(login.sessionId) + "}");
    }

    if (request.method == "POST" && request.path == "/api/auth/bootstrap-admin") {
        const auto token = request.headers.find("x-admin-bootstrap-token");
        auto registration = authService().registerInitialAdmin(
            field(body, "userId"), field(body, "name"), field(body, "email"),
            field(body, "password"),
            token == request.headers.end() ? std::string{} : token->second);
        if (!registration.success) {
            if (registration.message == "Initial admin provisioning is not authorized")
                return error(403, registration.message);
            if (registration.message == "Initial admin has already been provisioned" ||
                registration.message == "User ID or email already exists")
                return error(409, registration.message);
            if (registration.message == "Invalid admin details")
                return error(400, registration.message);
            return error(503, "Initial admin provisioning is not available");
        }
        return result(201, "{\"userId\":" + json(registration.userId) +
            ",\"name\":" + json(registration.name) +
            ",\"role\":\"ADMIN\",\"sessionId\":" +
            json(registration.sessionId) + "}");
    }

    if (request.method == "POST" && request.path == "/api/auth/logout") {
        const auto session = bearer(request);
        if (session.empty()) return error(401, "Authentication required");
        if (!authService().logout(session)) return error(401, "Session is invalid or expired");
        return result(200, "{\"success\":true}");
    }

    const Principal principal = authenticate(request);
    const bool needsAuth = request.path != "/api/vehicles" &&
                           request.path != "/api/chat";
    if (needsAuth && principal.id.empty()) return error(401, "Authentication required");
    const bool isAdmin = principal.role == "ADMIN";
    const bool isCustomer = principal.role == "CUSTOMER";

    if (request.method == "GET" && request.path == "/api/vehicles") {
        vrms::VehicleRepository repository;
        auto vehicles = repository.getAvailableVehicles();
        return result(200, arrayJson(vehicles, vehicleJson));
    }

    if (request.method == "POST" && request.path == "/api/chat") {
        if (!bearer(request).empty() && principal.id.empty())
            return error(401, "The supplied session is invalid or expired");

        const auto message = field(body, "message");
        const auto history = field(body, "history");
        if (message.empty() || message.size() > vrms::ChatbotService::MaxMessageLength)
            return error(400, "Message must contain 1 to 1000 UTF-8 bytes");
        if (history.size() > vrms::ChatbotService::MaxHistoryLength)
            return error(400, "Conversation history cannot exceed 4000 UTF-8 bytes");
        try {
            vrms::ChatbotService::validateRequest(message, history);
        } catch (const vrms::ChatbotError& failure) {
            return error(failure.httpStatus(), failure.what());
        }

        const auto rateLimitKey = principal.id.empty()
            ? request.clientAddress : principal.id;
        if (!chatbotRateLimiter().allow(
                rateLimitKey.empty() ? "unknown-client" : rateLimitKey))
            return error(429, "Chat message limit reached. Please wait a minute and try again.");

        try {
            vrms::PostgresChatbotDataSource dataSource;
            if (vrms::ChatbotService::isDatabaseAnswerableQuestion(message)) {
                vrms::ChatbotService service(dataSource,
                    static_cast<vrms::ChatbotGenerator*>(nullptr));
                const auto answer = service.reply(
                    message, history,
                    isCustomer ? principal.id : std::string{},
                    isCustomer ? principal.role : std::string{});
                return result(200, "{\"reply\":" + json(answer) + "}");
            }
            vrms::GeminiClient generator;
            vrms::ChatbotService service(dataSource, generator);
            const auto answer = service.reply(
                message, history,
                isCustomer ? principal.id : std::string{},
                isCustomer ? principal.role : std::string{});
            return result(200, "{\"reply\":" + json(answer) + "}");
        } catch (const vrms::ChatbotError& failure) {
            return error(failure.httpStatus(), failure.what());
        } catch (const std::invalid_argument&) {
            return error(503,
                "AI support configuration is invalid. Check GEMINI_API_KEY and GEMINI_MODEL on the API server.");
        } catch (const std::exception& failure) {
            const std::string detail = failure.what();
            if (detail.find("Missing required Gemini environment variable") != std::string::npos)
                return error(503,
                    "AI support is not configured. Set GEMINI_API_KEY and GEMINI_MODEL in the API server environment.");
            return error(503,
                "AI support is temporarily unavailable. Please try again shortly.");
        }
    }

    if (request.method == "GET" && request.path == "/api/bookings") {
        if (!isCustomer && !isAdmin) return error(403, "Access denied");
        vrms::BookingRepository repository;
        const auto bookings = isAdmin ? repository.getAllBookings()
                                      : repository.getCustomerBookings(principal.id);
        return result(200, arrayJson(bookings, bookingJson));
    }

    if (request.method == "POST" && request.path == "/api/bookings") {
        if (!isCustomer) return error(403, "Customer account required");
        const auto vehicleId = field(body, "vehicleId");
        const auto startDate = field(body, "startDate");
        const auto endDate = field(body, "endDate");
        const auto bookingId = newId("BK-");
        try {
            vrms::Booking booking(bookingId, principal.id, vehicleId, startDate, endDate);
            vrms::BookingRepository repository;
            repository.createBooking(bookingId, principal.id, vehicleId, startDate, endDate);
            return result(201, bookingJson({bookingId, principal.id, vehicleId,
                                            startDate, endDate, "CONFIRMED"}));
        } catch (const std::invalid_argument& e) { return error(400, e.what()); }
          catch (const std::exception& e) {
            const std::string message = e.what();
            if (message.find("already booked") != std::string::npos ||
                message.find("unavailable") != std::string::npos)
                return error(409, message);
            if (message.find("Vehicle not found") != std::string::npos)
                return error(404, message);
            return error(400, message);
        }
    }

    constexpr const char* cancelPrefix = "/api/bookings/";
    if (request.method == "POST" && request.path.rfind(cancelPrefix, 0) == 0 &&
        request.path.size() > std::string(cancelPrefix).size() + 8 &&
        request.path.substr(request.path.size() - 7) == "/cancel") {
        const auto bookingId = request.path.substr(
            std::string(cancelPrefix).size(),
            request.path.size() - std::string(cancelPrefix).size() - 7);
        vrms::BookingRepository repository;
        if (!isAdmin && (!isCustomer || !ownsBooking(
                repository.getCustomerBookings(principal.id), bookingId)))
            return error(404, "Booking not found");
        repository.cancelBooking(bookingId);
        return result(200, "{\"success\":true}");
    }

    if (request.method == "GET" && request.path == "/api/payments") {
        if (!isCustomer && !isAdmin) return error(403, "Access denied");
        vrms::BookingRepository bookingsRepository;
        vrms::PaymentService service;
        const auto allowed = isAdmin ? std::unordered_set<std::string>{}
            : customerBookingIds(bookingsRepository.getCustomerBookings(principal.id));
        const auto records = service.getAllPayments();
        std::string output = "[";
        bool first = true;
        for (const auto& p : records) {
            if (!isAdmin && !allowed.count(p.bookingId)) continue;
            if (!first) output += ',';
            first = false;
            output += "{\"paymentId\":" + json(p.paymentId) +
                ",\"bookingId\":" + json(p.bookingId) +
                ",\"amount\":" + number(p.amount) +
                ",\"method\":" + json(p.method) +
                ",\"status\":" + json(p.status) + "}";
        }
        return result(200, output + "]");
    }

    if (request.method == "POST" && request.path == "/api/payments") {
        if (!isCustomer) return error(403, "Customer account required");
        const auto bookingId = field(body, "bookingId");
        const auto method = field(body, "method");
        try {
            auto connection = vrms::createDatabaseConnection();
            pqxx::work transaction(connection);
            auto booking = transaction.exec(
                "SELECT b.customer_id, b.status, "
                "GREATEST(b.end_date - b.start_date, 1) AS days, "
                "to_char(b.start_date, 'YYYY-MM-DD') AS start_date, "
                "v.daily_rate FROM bookings b JOIN vehicles v USING (vehicle_id) "
                "WHERE b.booking_id = $1 FOR UPDATE OF b",
                pqxx::params{bookingId});
            if (booking.empty() || booking[0]["customer_id"].as<std::string>() != principal.id)
                return error(404, "Booking not found");
            if (booking[0]["days"].as<int>() < 1)
                return error(400, "Booking dates are invalid");
            if (booking[0]["status"].as<std::string>() != "CONFIRMED")
                return error(409, "Only a confirmed booking can be paid");
            const int days = booking[0]["days"].as<int>();
            const double dailyRate = booking[0]["daily_rate"].as<double>();
            transaction.commit();

            vrms::PaymentService paymentService;
            auto successfulPayment = paymentService.getSuccessfulPaymentForBooking(bookingId);
            bool paymentWasAlreadySaved = successfulPayment.has_value();
            std::string paymentId;
            std::string paymentStatus;
            double paidAmount = 0.0;
            std::unique_ptr<vrms::Invoice> invoice;

            if (successfulPayment) {
                paymentId = successfulPayment->paymentId;
                paymentStatus = successfulPayment->status;
                paidAmount = successfulPayment->amount;
                // Recovery path: payment committed but the invoice creation step did not.
                invoice = std::make_unique<vrms::Invoice>(
                    newId("INV-"), bookingId, paidAmount, 0.0, 0.0);
            } else {
                std::unique_ptr<vrms::PaymentStrategy> strategy;
                if (method == "UPI") strategy = std::make_unique<vrms::UpiPayment>();
                else if (method == "Card") strategy = std::make_unique<vrms::CardPayment>();
                else if (method == "Cash") strategy = std::make_unique<vrms::CashPayment>();
                else return error(400, "method must be Card, UPI or Cash");

                const double subtotal = days * dailyRate;
                invoice = std::make_unique<vrms::Invoice>(
                    newId("INV-"), bookingId, subtotal, 0.0, invoiceTaxPercent());
                paidAmount = invoice->calculateTotal();
                vrms::Payment payment(newId("PAY-"), bookingId, paidAmount, std::move(strategy));
                try {
                    if (!paymentService.processPayment(payment))
                        return error(402, "Simulated payment failed");
                    paymentId = payment.getPaymentId();
                    paymentStatus = payment.getStatusString();
                } catch (const std::exception& paymentError) {
                    // If another request won the booking-row lock, reuse its successful payment.
                    successfulPayment = paymentService.getSuccessfulPaymentForBooking(bookingId);
                    if (!successfulPayment) {
                        const std::string detail = paymentError.what();
                        if (detail.find("already has a successful payment") != std::string::npos)
                            return error(409, "Booking has already been paid");
                        return error(503, "Payment could not be recorded");
                    }
                    paymentWasAlreadySaved = true;
                    paymentId = successfulPayment->paymentId;
                    paymentStatus = successfulPayment->status;
                    paidAmount = successfulPayment->amount;
                    invoice = std::make_unique<vrms::Invoice>(
                        newId("INV-"), bookingId, paidAmount, 0.0, 0.0);
                }
            }

            vrms::InvoiceService invoiceService;
            const auto invoiceResult = invoiceService.createOrGetInvoice(*invoice);
            vrms::InvoiceDeliveryResult delivery{
                invoiceResult.invoice.emailStatus,
                invoiceResult.invoice.emailStatus == "FAILED"
                    ? "Invoice is saved; email delivery failed. An administrator can retry it."
                    : invoiceResult.invoice.emailStatus == "SENT"
                        ? "Invoice was already emailed to the registered email address."
                        : "Invoice email delivery is already in progress.",
                false
            };
            if (invoiceResult.created || invoiceResult.invoice.emailStatus == "PENDING") {
                vrms::EmailService emailService;
                vrms::InvoiceDeliveryService deliveryService(invoiceService, emailService);
                delivery = deliveryService.sendInvoice(invoiceResult.invoice.invoiceId);
            }

            const int statusCode = paymentWasAlreadySaved ? 200 : 201;
            return result(statusCode,
                "{\"paymentId\":" + json(paymentId) +
                ",\"invoiceId\":" + json(invoiceResult.invoice.invoiceId) +
                ",\"bookingId\":" + json(bookingId) +
                ",\"amount\":" + number(paidAmount) +
                ",\"status\":" + json(paymentStatus) +
                ",\"simulation\":true,\"emailStatus\":" + json(delivery.status) +
                ",\"emailMessage\":" + json(delivery.message) + "}");
        } catch (const std::invalid_argument& errorMessage) {
            return error(400, errorMessage.what());
        } catch (const std::exception&) {
            return error(503, "Payment or invoice could not be completed; any saved payment remains available for safe recovery.");
        }
    }

    if (request.method == "GET" && request.path == "/api/invoices") {
        if (!isCustomer && !isAdmin) return error(403, "Access denied");
        vrms::InvoiceService service;
        vrms::BookingRepository bookingsRepository;
        const auto allowed = isAdmin ? std::unordered_set<std::string>{}
            : customerBookingIds(bookingsRepository.getCustomerBookings(principal.id));
        const auto invoices = service.getAllInvoices();
        std::string output = "[";
        bool first = true;
        for (const auto& i : invoices) {
            if (!isAdmin && !allowed.count(i.bookingId)) continue;
            if (!first) output += ',';
            first = false;
            output += "{\"invoiceId\":" + json(i.invoiceId) +
                ",\"bookingId\":" + json(i.bookingId) +
                ",\"subtotal\":" + number(i.subtotal) +
                ",\"discount\":" + number(i.discount) +
                ",\"tax\":" + number(i.tax) +
                ",\"totalAmount\":" + number(i.totalAmount) +
                ",\"emailStatus\":" + json(i.emailStatus) +
                ",\"emailAttempts\":" + std::to_string(i.emailAttempts) + "}";
        }
        return result(200, output + "]");
    }

    if (request.path.rfind("/api/admin/", 0) == 0 && !isAdmin)
        return error(403, "Administrator account required");

    constexpr const char* invoiceRetryPrefix = "/api/admin/invoices/";
    if (request.method == "POST" &&
        request.path.rfind(invoiceRetryPrefix, 0) == 0 &&
        request.path.size() > std::string(invoiceRetryPrefix).size() + 12 &&
        request.path.substr(request.path.size() - 12) == "/retry-email") {
        const auto invoiceId = request.path.substr(
            std::string(invoiceRetryPrefix).size(),
            request.path.size() - std::string(invoiceRetryPrefix).size() - 12);
        try {
            vrms::InvoiceService invoiceService;
            vrms::EmailService emailService;
            vrms::InvoiceDeliveryService deliveryService(invoiceService, emailService);
            const auto delivery = deliveryService.sendInvoice(invoiceId);
            return result(200, "{\"invoiceId\":" + json(invoiceId) +
                ",\"emailStatus\":" + json(delivery.status) +
                ",\"message\":" + json(delivery.message) + "}");
        } catch (const std::exception&) {
            return error(503, "Invoice email retry could not be completed");
        }
    }

    if (request.method == "POST" && request.path == "/api/admin/users") {
        auto registration = authService().registerAdmin(
            field(body, "userId"), field(body, "name"), field(body, "email"),
            field(body, "password"), bearer(request));
        if (!registration.success) {
            if (registration.message == "Invalid admin details")
                return error(400, registration.message);
            if (registration.message == "User ID or email already exists")
                return error(409, registration.message);
            return error(503, "Admin provisioning is not available");
        }
        return result(201, "{\"userId\":" + json(registration.userId) +
                      ",\"role\":\"ADMIN\"}");
    }

    if (request.method == "GET" && request.path == "/api/admin/vehicles") {
        vrms::VehicleRepository repository;
        const auto vehicles = repository.getAllVehicles();
        return result(200, arrayJson(vehicles, vehicleJson));
    }

    if (request.method == "POST" && request.path == "/api/admin/vehicles") {
        const auto rate = field(body, "dailyRate");
        double dailyRate = 0;
        try { dailyRate = std::stod(rate); }
        catch (...) { return error(400, "dailyRate must be a positive number"); }
        try {
            vrms::VehicleRepository repository;
            repository.saveVehicle(field(body, "id"), field(body, "brand"),
                field(body, "model"), field(body, "type"), dailyRate);
            return result(201, "{\"success\":true}");
        } catch (const std::invalid_argument& e) { return error(400, e.what()); }
    }

    if (request.method == "GET" && request.path == "/api/admin/bookings") {
        vrms::BookingRepository repository;
        const auto bookings = repository.getAllBookings();
        return result(200, arrayJson(bookings, bookingJson));
    }

    if (request.method == "GET" && request.path == "/api/admin/maintenance") {
        vrms::MaintenanceService service;
        const auto records = service.getAllMaintenance();
        std::string output = "[";
        bool first = true;
        for (const auto& m : records) {
            if (!first) output += ',';
            first = false;
            output += "{\"maintenanceId\":" + json(m.maintenanceId) +
                ",\"vehicleId\":" + json(m.vehicleId) +
                ",\"serviceDate\":" + json(m.serviceDate) +
                ",\"description\":" + json(m.description) +
                ",\"status\":" + json(m.status) +
                ",\"maintenanceCost\":" + number(m.maintenanceCost) +
                ",\"completionDate\":" + json(m.completionDate) + "}";
        }
        return result(200, output + "]");
    }

    if (request.method == "GET" && request.path == "/api/admin/rentals") {
        vrms::RentalService service;
        const auto records = service.getAllRentals();
        std::string output = "[";
        bool first = true;
        for (const auto& r : records) {
            if (!first) output += ',';
            first = false;
            output += "{\"rentalId\":" + json(r.rentalId) +
                ",\"bookingId\":" + json(r.bookingId) +
                ",\"plannedReturnDate\":" + json(r.plannedReturnDate) +
                ",\"actualReturnDate\":" + json(r.actualReturnDate) +
                ",\"dailyLateFee\":" + number(r.dailyLateFee) +
                ",\"returned\":" + (r.returned ? "true" : "false") + "}";
        }
        return result(200, output + "]");
    }

    if (request.method == "POST" && request.path == "/api/admin/rentals") {
        try {
            const auto fee = std::stod(field(body, "dailyLateFee"));
            vrms::RentalService service;
            const auto rentalId = newId("REN-");
            service.createRental(rentalId, field(body, "bookingId"),
                                 field(body, "plannedReturnDate"), fee);
            return result(201, "{\"rentalId\":" + json(rentalId) + "}");
        } catch (const std::exception&) { return error(400, "Rental could not be created"); }
    }

    if (request.method == "POST" && request.path.rfind("/api/admin/rentals/", 0) == 0 &&
        request.path.size() > 19 && request.path.substr(request.path.size() - 7) == "/return") {
        const auto rentalId = request.path.substr(19, request.path.size() - 19 - 7);
        try {
            vrms::RentalService service;
            const auto lateFee = service.completeReturn(rentalId, field(body, "actualReturnDate"));
            return result(200, "{\"success\":true,\"lateFee\":" + number(lateFee) + "}");
        } catch (const std::exception&) { return error(400, "Rental return could not be completed"); }
    }

    if (request.path.rfind("/api/", 0) == 0) return error(404, "API route not found");
    return error(404, "Not found");
}

std::string reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 503: return "Service Unavailable";
    default: return "Internal Server Error";
    }
}

void sendResponse(SOCKET socket, const Request& request, const Response& response) {
    const auto origin = request.headers.find("origin");
    const char* configuredOrigin = std::getenv("VRMS_ALLOWED_ORIGIN");
    const std::string allowed = configuredOrigin && configuredOrigin[0]
        ? configuredOrigin : "http://localhost:5173";
    std::ostringstream headers;
    headers << "HTTP/1.1 " << response.status << ' ' << reason(response.status) << "\r\n"
        << "Content-Type: " << response.contentType << "\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "X-Content-Type-Options: nosniff\r\n"
        << "Connection: close\r\n";
    if (origin != request.headers.end() && origin->second == allowed) {
        headers << "Access-Control-Allow-Origin: " << allowed << "\r\n"
                << "Access-Control-Allow-Headers: Authorization, Content-Type, X-Admin-Bootstrap-Token\r\n"
                << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                << "Vary: Origin\r\n";
    }
    headers << "\r\n" << response.body;
    const std::string wire = headers.str();
    std::size_t sent = 0;
    while (sent < wire.size()) {
        const int n = send(socket, wire.data() + sent,
            static_cast<int>(std::min<std::size_t>(wire.size() - sent, 32768)), 0);
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
}

void serveClient(SOCKET client) {
    std::string raw;
    raw.reserve(8192);
    char buffer[8192];
    bool done = false;
    while (raw.size() <= MAX_REQUEST_BYTES + 16384) {
        const int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(n));
        const auto end = raw.find("\r\n\r\n");
        if (end != std::string::npos) {
            std::size_t contentLength = 0;
            const std::string head = lower(raw.substr(0, end));
            const auto marker = head.find("content-length:");
            if (marker != std::string::npos) {
                const auto lineEnd = head.find("\r\n", marker);
                try {
                    contentLength = std::stoul(trim(head.substr(marker + 15,
                        lineEnd - marker - 15)));
                } catch (...) { break; }
            }
            if (contentLength > MAX_REQUEST_BYTES) break;
            if (raw.size() >= end + 4 + contentLength) { done = true; break; }
        }
    }

    Request request;
    Response response;
    if (!done || !parseRequest(raw, request)) {
        response = error(400, "Malformed or oversized HTTP request");
    } else {
        sockaddr_in peerAddress{};
        int peerAddressLength = sizeof(peerAddress);
        char clientAddress[INET_ADDRSTRLEN]{};
        if (getpeername(client, reinterpret_cast<sockaddr*>(&peerAddress),
                        &peerAddressLength) == 0 &&
            InetNtopA(AF_INET, &peerAddress.sin_addr, clientAddress,
                      static_cast<DWORD>(sizeof(clientAddress)))) {
            request.clientAddress = clientAddress;
        }
        if (request.method == "OPTIONS") {
            response = {204, "", "application/json"};
        } else {
            try { response = dispatch(request); }
            catch (const std::exception& e) {
                std::cerr << "[API] Request failed: " << e.what() << '\n';
                response = error(500, "The request could not be completed");
            } catch (...) { response = error(500, "The request could not be completed"); }
        }
    }
    sendResponse(client, request, response);
    shutdown(client, SD_SEND);
    closesocket(client);
}

} // namespace

int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Winsock initialization failed\n";
        return 1;
    }

    const char* hostEnv = std::getenv("VRMS_API_HOST");
    const char* portEnv = std::getenv("VRMS_API_PORT");
    const std::string host = hostEnv && hostEnv[0] ? hostEnv : "127.0.0.1";
    const std::string portText = portEnv && portEnv[0] ? portEnv : "8080";
    unsigned short port = 8080;
    try {
        const auto parsed = std::stoul(portText);
        if (parsed == 0 || parsed > 65535) throw std::out_of_range("port");
        port = static_cast<unsigned short>(parsed);
    } catch (...) {
        std::cerr << "VRMS_API_PORT must be between 1 and 65535\n";
        WSACleanup();
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "Could not create API socket\n";
        WSACleanup();
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(server, 32) == SOCKET_ERROR) {
        std::cerr << "Could not bind API to " << host << ':' << port
                  << " (check VRMS_API_HOST and VRMS_API_PORT)\n";
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "VRMS REST API listening at http://" << host << ':' << port << '\n';
    std::cout << "Default bind is loopback only. Press Ctrl+C to stop.\n";
    for (;;) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        std::thread(serveClient, client).detach();
    }
}
