
#include <iostream>
#include <memory>
#include <vector>
#include <exception>
#include <string>
#include <chrono>
#include <array>
#include <barrier>
#include <limits>
#include <stdexcept>
#include <thread>

#include "Database.h"
#include "auth/PasswordHasher.h"
#include "models/Car.h"
#include "models/Bike.h"
#include "models/Customer.h"
#include "models/Admin.h"
#include "models/Booking.h"
#include "models/Rental.h"
#include "models/Maintenance.h"
#include "models/Payment.h"
#include "models/Invoice.h"
#include "models/AuditLog.h"

#include "repositories/VehicleRepository.h"
#include "repositories/CustomerRepository.h"
#include "repositories/BookingRepository.h"
#include "repositories/AuditLogRepository.h"
#include "repositories/InvoiceRepository.h"
#include "repositories/RentalRepository.h"

#include "services/AuthService.h"
#include "services/AuthorizationService.h"
#include "services/PaymentService.h"
#include "services/MaintenanceService.h"

template <typename Action>
bool rejectsInvalidArgument(Action&& action) {
    try {
        action();
    }
    catch (const std::invalid_argument&) {
        return true;
    }
    catch (...) {
    }

    return false;
}

int runMaintenanceRetrievalTests() {
    try {
        vrms::MaintenanceService maintenanceService;
        auto allMaintenance = maintenanceService.getAllMaintenance();

        if (allMaintenance.empty()) {
            std::cerr << "[FAIL] Retrieve all maintenance records: "
                      << "no records are available to verify\n";
            return 1;
        }

        const auto& expected = allMaintenance.front();
        auto maintenanceById = maintenanceService.getMaintenanceById(
            expected.maintenanceId
        );

        bool specificRecordMatches =
            maintenanceById.maintenanceId == expected.maintenanceId &&
            maintenanceById.vehicleId == expected.vehicleId &&
            maintenanceById.serviceDate == expected.serviceDate &&
            maintenanceById.description == expected.description &&
            maintenanceById.status == expected.status &&
            maintenanceById.maintenanceCost == expected.maintenanceCost &&
            maintenanceById.completionDate == expected.completionDate;

        auto vehicleHistory = maintenanceService.getVehicleMaintenance(
            expected.vehicleId
        );

        bool recordFoundInVehicleHistory = false;
        for (const auto& record : vehicleHistory) {
            if (record.maintenanceId == expected.maintenanceId) {
                recordFoundInVehicleHistory = true;
                break;
            }
        }

        std::cout << "[PASS] Retrieve all maintenance records ("
                  << allMaintenance.size() << " records)\n";
        std::cout << (specificRecordMatches ? "[PASS]" : "[FAIL]")
                  << " Retrieve a maintenance record by ID ("
                  << expected.maintenanceId << ")\n";
        std::cout << (recordFoundInVehicleHistory ? "[PASS]" : "[FAIL]")
                  << " Retrieve maintenance history for vehicle ("
                  << expected.vehicleId << ", "
                  << vehicleHistory.size() << " records)\n";

        return specificRecordMatches && recordFoundInVehicleHistory ? 0 : 1;
    }
    catch (const std::exception& e) {
        std::cerr << "[FAIL] Maintenance retrieval test: "
                  << e.what() << '\n';
        return 1;
    }
}

int runMaintenanceConcurrencyTests() {
    try {
        vrms::VehicleRepository vehicleRepository;
        auto availableVehicles = vehicleRepository.getAvailableVehicles();

        if (availableVehicles.empty()) {
            std::cerr << "[FAIL] Concurrent scheduling test: "
                      << "no available vehicle is available for the test\n";
            return 1;
        }

        const std::string vehicleId = availableVehicles.front().id;
        const std::string serviceDate = "2099-01-01";

        auto timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

        const std::string maintenanceIdBase =
            "M" + std::to_string(timestamp);
        const std::array<std::string, 2> maintenanceIds = {
            maintenanceIdBase + "A",
            maintenanceIdBase + "B"
        };

        std::array<std::unique_ptr<vrms::MaintenanceService>, 2> services = {
            std::make_unique<vrms::MaintenanceService>(),
            std::make_unique<vrms::MaintenanceService>()
        };
        std::array<bool, 2> scheduled = {false, false};
        std::array<std::string, 2> errors;
        std::barrier startTogether(3);

        auto schedule = [&](size_t index) {
            startTogether.arrive_and_wait();

            try {
                services[index]->scheduleMaintenance(
                    maintenanceIds[index],
                    vehicleId,
                    serviceDate,
                    "Concurrent maintenance scheduling test",
                    1.00
                );
                scheduled[index] = true;
            }
            catch (const std::exception& e) {
                errors[index] = e.what();
            }
        };

        std::thread firstRequest(schedule, 0);
        std::thread secondRequest(schedule, 1);
        startTogether.arrive_and_wait();
        firstRequest.join();
        secondRequest.join();

        int scheduledCount = 0;
        int unavailableRejectionCount = 0;

        for (size_t i = 0; i < scheduled.size(); ++i) {
            if (scheduled[i]) {
                ++scheduledCount;
            }
            else if (
                errors[i].find(
                    "Vehicle is currently unavailable for maintenance scheduling"
                ) != std::string::npos
            ) {
                ++unavailableRejectionCount;
            }
        }

        bool testPassed =
            scheduledCount == 1 && unavailableRejectionCount == 1;

        // Complete any accepted test records so the chosen vehicle is available again.
        for (size_t i = 0; i < scheduled.size(); ++i) {
            if (!scheduled[i]) {
                continue;
            }

            try {
                services[i]->completeMaintenance(
                    maintenanceIds[i],
                    serviceDate
                );
            }
            catch (const std::exception& e) {
                testPassed = false;
                std::cerr << "[CLEANUP ERROR] " << e.what() << '\n';
            }
        }

        if (testPassed) {
            std::cout << "[PASS] Concurrent scheduling accepted one request "
                      << "and rejected the other for vehicle "
                      << vehicleId << ".\n";
            return 0;
        }

        std::cerr << "[FAIL] Concurrent scheduling expected one accepted "
                  << "request and one unavailable-vehicle rejection; got "
                  << scheduledCount << " accepted and "
                  << unavailableRejectionCount
                  << " expected rejections.\n";

        for (size_t i = 0; i < errors.size(); ++i) {
            if (!errors[i].empty()) {
                std::cerr << "Request " << (i + 1)
                          << " error: " << errors[i] << '\n';
            }
        }

        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "[FAIL] Concurrent scheduling test: "
                  << e.what() << '\n';
        return 1;
    }
}

bool containsMaintenanceAudit(
    const std::vector<vrms::AuditLog>& logs,
    const std::string& action,
    const std::string& maintenanceId,
    const std::string& userId
) {
    const std::string maintenanceMarker =
        "Maintenance ID: " + maintenanceId;

    for (const auto& log : logs) {
        if (
            log.getAction() == action &&
            log.getUserId() == userId &&
            log.getDescription().find(maintenanceMarker) !=
                std::string::npos
        ) {
            return true;
        }
    }

    return false;
}

int runMaintenanceAuditTests() {
    try {
        vrms::AuthService authService;
        auto login = authService.login(
            "auth.test@example.com",
            "TestPass#2026"
        );

        if (!login.success || login.userId.empty()) {
            std::cerr << "[FAIL] Audit tests need an authenticated test user: "
                      << login.message << '\n';
            return 1;
        }

        vrms::VehicleRepository vehicleRepository;
        auto availableVehicles = vehicleRepository.getAvailableVehicles();

        if (availableVehicles.empty()) {
            std::cerr << "[FAIL] Maintenance audit test: "
                      << "no available vehicle is available for the test\n";
            return 1;
        }

        auto timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

        const std::string maintenanceIdBase =
            "M" + std::to_string(timestamp);
        const std::string vehicleId = availableVehicles.front().id;
        const std::string serviceDate = "2099-01-01";
        const std::string successfulMaintenanceId =
            maintenanceIdBase + "A";
        const std::string unauthenticatedFailureId =
            maintenanceIdBase + "B";
        const std::string invalidCompletionId =
            maintenanceIdBase + "E";
        const std::string failedAuditId =
            maintenanceIdBase + "C";
        const std::string failedSuccessAuditId =
            maintenanceIdBase + "D";

        vrms::MaintenanceService maintenanceService;
        vrms::AuditLogRepository auditRepository;
        bool allPassed = true;

        maintenanceService.scheduleMaintenance(
            successfulMaintenanceId,
            vehicleId,
            serviceDate,
            "Maintenance audit success test",
            1.00,
            login.userId
        );

        auto logs = auditRepository.getAllLogs();
        bool scheduleAuditPassed = containsMaintenanceAudit(
            logs,
            "MAINTENANCE_SCHEDULED",
            successfulMaintenanceId,
            login.userId
        );
        std::cout << (scheduleAuditPassed ? "[PASS]" : "[FAIL]")
                  << " Successful scheduling audit includes authenticated user ID.\n";
        allPassed = allPassed && scheduleAuditPassed;

        maintenanceService.completeMaintenance(
            successfulMaintenanceId,
            serviceDate,
            login.userId
        );

        logs = auditRepository.getAllLogs();
        bool completionAuditPassed = containsMaintenanceAudit(
            logs,
            "MAINTENANCE_COMPLETED",
            successfulMaintenanceId,
            login.userId
        );
        std::cout << (completionAuditPassed ? "[PASS]" : "[FAIL]")
                  << " Successful completion audit includes authenticated user ID.\n";
        allPassed = allPassed && completionAuditPassed;

        bool invalidInputRejected = false;
        try {
            maintenanceService.scheduleMaintenance(
                unauthenticatedFailureId,
                vehicleId,
                "2026-02-30",
                "Invalid date audit test",
                1.00
            );
        }
        catch (const std::exception&) {
            invalidInputRejected = true;
        }

        logs = auditRepository.getAllLogs();
        bool unauthenticatedFailureLogged =
            invalidInputRejected &&
            containsMaintenanceAudit(
                logs,
                "MAINTENANCE_SCHEDULE_FAILED",
                unauthenticatedFailureId,
                ""
            );
        std::cout << (unauthenticatedFailureLogged ? "[PASS]" : "[FAIL]")
                  << " Invalid input is audited without inventing a user ID.\n";
        allPassed = allPassed && unauthenticatedFailureLogged;

        bool invalidCompletionRejected = false;
        try {
            maintenanceService.completeMaintenance(
                invalidCompletionId,
                "2026-02-30",
                login.userId
            );
        }
        catch (const std::exception&) {
            invalidCompletionRejected = true;
        }

        logs = auditRepository.getAllLogs();
        bool invalidCompletionLogged =
            invalidCompletionRejected &&
            containsMaintenanceAudit(
                logs,
                "MAINTENANCE_COMPLETION_FAILED",
                invalidCompletionId,
                login.userId
            );
        std::cout << (invalidCompletionLogged ? "[PASS]" : "[FAIL]")
                  << " Invalid completion input is recorded as a failed operation.\n";
        allPassed = allPassed && invalidCompletionLogged;

        // The audit schema limits user_id to 20 characters. Use an overlong
        // test value to force a deterministic audit storage failure.
        const std::string invalidActor(21, 'X');
        bool failureAuditErrorReported = false;
        try {
            maintenanceService.scheduleMaintenance(
                failedAuditId,
                vehicleId,
                "2026-02-30",
                "Failure audit storage test",
                1.00,
                invalidActor
            );
        }
        catch (const std::exception& e) {
            const std::string error = e.what();
            failureAuditErrorReported =
                error.find("failure audit logging also failed") !=
                    std::string::npos &&
                error.find("valid YYYY-MM-DD date") !=
                    std::string::npos;
        }
        std::cout << (failureAuditErrorReported ? "[PASS]" : "[FAIL]")
                  << " Failure audit storage errors are reported with the operation error.\n";
        allPassed = allPassed && failureAuditErrorReported;

        bool successAuditErrorReported = false;
        try {
            maintenanceService.scheduleMaintenance(
                failedSuccessAuditId,
                vehicleId,
                serviceDate,
                "Success audit storage test",
                1.00,
                invalidActor
            );
        }
        catch (const std::exception& e) {
            successAuditErrorReported =
                std::string(e.what()).find(
                    "success audit logging failed"
                ) != std::string::npos;
        }

        bool committedRecordCleanedUp = false;
        try {
            auto record = maintenanceService.getMaintenanceById(
                failedSuccessAuditId
            );

            if (record.status == "SCHEDULED") {
                maintenanceService.completeMaintenance(
                    failedSuccessAuditId,
                    serviceDate,
                    login.userId
                );
                committedRecordCleanedUp = true;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[CLEANUP ERROR] " << e.what() << '\n';
        }

        bool successAuditFailurePassed =
            successAuditErrorReported && committedRecordCleanedUp;
        std::cout << (successAuditFailurePassed ? "[PASS]" : "[FAIL]")
                  << " Success audit storage errors are reported after commit.\n";
        allPassed = allPassed && successAuditFailurePassed;

        return allPassed ? 0 : 1;
    }
    catch (const std::exception& e) {
        std::cerr << "[FAIL] Maintenance audit test: "
                  << e.what() << '\n';
        return 1;
    }
}

int runMaintenanceValidationTests() {
    bool allPassed = true;
    auto report = [&](const std::string& name, bool passed) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << name << '\n';
        allPassed = allPassed && passed;
    };

    const std::array<double, 3> nonFiniteCosts = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    const std::array<const char*, 3> nonFiniteCostNames = {
        "NaN", "+infinity", "-infinity"
    };

    report(
        "Model rejects zero maintenance cost",
        rejectsInvalidArgument([] {
            vrms::Maintenance maintenance(
                "MODEL_ZERO_COST", "B101", "2026-01-01", "Test", 0.0
            );
        })
    );
    report(
        "Model rejects negative maintenance cost",
        rejectsInvalidArgument([] {
            vrms::Maintenance maintenance(
                "MODEL_NEG_COST", "B101", "2026-01-01", "Test", -1.0
            );
        })
    );
    for (std::size_t i = 0; i < nonFiniteCosts.size(); ++i) {
        const double cost = nonFiniteCosts[i];
        report(
            std::string("Model rejects ") + nonFiniteCostNames[i] + " cost",
            rejectsInvalidArgument([cost, i] {
                vrms::Maintenance maintenance(
                    "MODEL_NONFINITE_" + std::to_string(i),
                    "B101",
                    "2026-01-01",
                    "Test",
                    cost
                );
            })
        );
    }
    report(
        "Model rejects impossible calendar dates",
        rejectsInvalidArgument([] {
            vrms::Maintenance maintenance(
                "MODEL_BAD_DATE", "B101", "2026-02-30", "Test", 1.0
            );
        })
    );
    report(
        "Model rejects year 0000",
        rejectsInvalidArgument([] {
            vrms::Maintenance maintenance(
                "MODEL_YEAR_ZERO", "B101", "0000-01-01", "Test", 1.0
            );
        })
    );

    vrms::Maintenance model(
        "MODEL_STATUS",
        "B101",
        "2099-01-01",
        "Status consistency test",
        1.0
    );
    report(
        "Model scheduled status uses the repository value",
        model.getStatus() == "SCHEDULED" && !model.isCompleted()
    );
    report(
        "Model rejects completion before its service date",
        rejectsInvalidArgument([&] {
            model.completeMaintenance("2098-12-31");
        })
    );
    report(
        "Model rejects year 0000 as a completion date",
        rejectsInvalidArgument([&] {
            model.completeMaintenance("0000-01-01");
        })
    );
    model.completeMaintenance("2099-01-01");
    report(
        "Model completed status uses the repository value",
        model.getStatus() == "COMPLETED" && model.isCompleted()
    );

    try {
        vrms::MaintenanceService maintenanceService;

        report(
            "Service rejects zero maintenance cost",
            rejectsInvalidArgument([&] {
                maintenanceService.scheduleMaintenance(
                    "SERVICE_ZERO_COST",
                    "B101",
                    "2026-01-01",
                    "Test",
                    0.0
                );
            })
        );
        report(
            "Service rejects negative maintenance cost",
            rejectsInvalidArgument([&] {
                maintenanceService.scheduleMaintenance(
                    "SERVICE_NEG_COST",
                    "B101",
                    "2026-01-01",
                    "Test",
                    -1.0
                );
            })
        );
        for (std::size_t i = 0; i < nonFiniteCosts.size(); ++i) {
            const double cost = nonFiniteCosts[i];
            report(
                std::string("Service rejects ") +
                    nonFiniteCostNames[i] + " cost",
                rejectsInvalidArgument([&, cost, i] {
                    maintenanceService.scheduleMaintenance(
                        "SERVICE_NONFINITE_" + std::to_string(i),
                        "B101",
                        "2026-01-01",
                        "Test",
                        cost
                    );
                })
            );
        }
        report(
            "Service rejects impossible calendar dates",
            rejectsInvalidArgument([&] {
                maintenanceService.scheduleMaintenance(
                    "SERVICE_BAD_DATE",
                    "B101",
                    "2026-02-30",
                    "Test",
                    1.0
                );
            })
        );
        report(
            "Service rejects year 0000",
            rejectsInvalidArgument([&] {
                maintenanceService.scheduleMaintenance(
                    "SERVICE_YEAR_ZERO",
                    "B101",
                    "0000-01-01",
                    "Test",
                    1.0
                );
            })
        );
        report(
            "Service rejects invalid completion dates",
            rejectsInvalidArgument([&] {
                maintenanceService.completeMaintenance(
                    "SERVICE_INVALID_COMPLETION",
                    "2026-02-30"
                );
            })
        );
        report(
            "Service rejects year 0000 as a completion date",
            rejectsInvalidArgument([&] {
                maintenanceService.completeMaintenance(
                    "SERVICE_YEAR_ZERO_COMPLETION",
                    "0000-01-01"
                );
            })
        );

        vrms::MaintenanceRepository maintenanceRepository;
        for (std::size_t i = 0; i < nonFiniteCosts.size(); ++i) {
            const double cost = nonFiniteCosts[i];
            report(
                std::string("Repository rejects ") +
                    nonFiniteCostNames[i] + " cost",
                rejectsInvalidArgument([&, cost, i] {
                    maintenanceRepository.scheduleMaintenance(
                        "REPOSITORY_NONFINITE_" + std::to_string(i),
                        "B101",
                        "2026-01-01",
                        "Test",
                        cost
                    );
                })
            );
        }

        vrms::VehicleRepository vehicleRepository;
        auto availableVehicles = vehicleRepository.getAvailableVehicles();
        if (availableVehicles.empty()) {
            report(
                "Service rejects completion before the service date",
                false
            );
            return 1;
        }

        auto timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
        const std::string maintenanceId =
            "MVAL" + std::to_string(timestamp);
        const std::string serviceDate = "2099-01-01";

        maintenanceService.scheduleMaintenance(
            maintenanceId,
            availableVehicles.front().id,
            serviceDate,
            "Service date validation test",
            1.0
        );

        bool earlierCompletionRejected = rejectsInvalidArgument([&] {
            maintenanceService.completeMaintenance(
                maintenanceId,
                "2098-12-31"
            );
        });
        report(
            "Service rejects completion before the service date",
            earlierCompletionRejected
        );

        maintenanceService.completeMaintenance(
            maintenanceId,
            serviceDate
        );

        auto completedRecord = maintenanceService.getMaintenanceById(
            maintenanceId
        );
        report(
            "Service and repository return the same completed status value",
            completedRecord.status == "COMPLETED"
        );
    }
    catch (const std::exception& e) {
        std::cerr << "[FAIL] Maintenance validation test: "
                  << e.what() << '\n';
        return 1;
    }

    return allPassed ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (
        argc == 2 &&
        std::string(argv[1]) == "--test-maintenance-retrieval"
    ) {
        return runMaintenanceRetrievalTests();
    }
    if (
        argc == 2 &&
        std::string(argv[1]) == "--test-maintenance-concurrency"
    ) {
        return runMaintenanceConcurrencyTests();
    }
    if (
        argc == 2 &&
        std::string(argv[1]) == "--test-maintenance-audit"
    ) {
        return runMaintenanceAuditTests();
    }
    if (
        argc == 2 &&
        std::string(argv[1]) == "--test-maintenance-validation"
    ) {
        return runMaintenanceValidationTests();
    }

    std::cout << "===== VEHICLE RENTAL SYSTEM =====\n";

    const std::string demoPassword = "VrmsDemo@2026";
    const std::string demoPasswordHash =
        vrms::PasswordHasher::hashPassword(demoPassword);

    std::cout << "\n===== ARGON2ID PASSWORD HASH TEST =====\n";
    std::cout << "Password hashed successfully: Yes\n";
    std::cout << "Correct password accepted: "
              << (vrms::PasswordHasher::verifyPassword(
                      demoPassword, demoPasswordHash) ? "Yes" : "No")
              << "\n";
    std::cout << "Incorrect password rejected: "
              << (!vrms::PasswordHasher::verifyPassword(
                      "WrongPassword@123", demoPasswordHash) ? "Yes" : "No")
              << "\n";

    std::vector<std::unique_ptr<vrms::Vehicle>> vehicles;

    vehicles.push_back(
        std::make_unique<vrms::Car>("C101", "Toyota", "Camry", 1800)
    );

    vehicles.push_back(
        std::make_unique<vrms::Bike>(
            "B101", "Royal Enfield", "Classic 350", 700
        )
    );

    std::cout << "\n===== VEHICLES =====\n";

    for (const auto& vehicle : vehicles) {
        std::cout << vehicle->getVehicleType() << ": "
                  << vehicle->getBrand() << " "
                  << vehicle->getModel() << "\n";
    }

    vrms::Customer customer(
        "U101", "Srushti", "customer@example.com", "Gold"
    );

    vrms::Booking booking(
        "BK101", customer.getUserId(), "C101",
        "2026-11-15", "2026-11-18"
    );

    booking.confirmBooking();

    std::cout << "\n===== BOOKING =====\n";
    std::cout << "Booking ID: " << booking.getBookingId() << "\n";
    std::cout << "Status: " << booking.getStatusString() << "\n";

    vrms::Rental rental("R101", "BK101", "2026-11-18", 500);
    rental.completeReturn("2026-11-20");

    std::cout << "\n===== RENTAL =====\n";
    std::cout << "Late days: " << rental.calculateLateDays() << "\n";
    std::cout << "Late fee: Rs. " << rental.calculateLateFee() << "\n";

    vrms::Invoice invoice("INV101", "BK101", 5400, 10, 18);
    invoice.displayInvoice();

    try {
        vrms::InvoiceRepository invoiceRepository;

        invoiceRepository.saveInvoice(
            "INV101",
            "BK101",
            5400.00,
            540.00,
            874.80,
            invoice.calculateTotal()
        );
    }
    catch (const std::exception& e) {
        std::cerr << "Invoice Repository Error: "
                  << e.what() << "\n";
    }

    vrms::Payment payment(
        "P101",
        "BK101",
        invoice.calculateTotal(),
        std::make_unique<vrms::UpiPayment>()
    );

    vrms::PaymentService paymentService;

    bool paymentSuccessful = paymentService.processPayment(payment);

    std::cout << "Payment Processing: "
              << (paymentSuccessful ? "Successful" : "Failed")
              << "\n";

    std::cout << "\n===== PAYMENT DETAILS =====\n";
    std::cout << "Payment ID: " << payment.getPaymentId() << "\n";
    std::cout << "Method: " << payment.getMethod() << "\n";
    std::cout << "Amount: Rs. " << payment.getAmount() << "\n";
    std::cout << "Status: " << payment.getStatusString() << "\n";

    try {
        vrms::Database database;

        if (database.isConnected()) {
            database.testConnection();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Database Error: " << e.what() << "\n";
    }

    try {
        vrms::VehicleRepository repository;

        repository.saveVehicle(
            "C101", "Toyota", "Camry", "Car", 1800
        );

        repository.saveVehicle(
            "B101", "Royal Enfield", "Classic 350", "Bike", 700
        );

        std::cout << "\n===== DATABASE VEHICLES =====\n";

        auto vehicleRecords = repository.getAvailableVehicles();

        for (const auto& v : vehicleRecords) {
            std::cout << "\nID: " << v.id;
            std::cout << "\nBrand: " << v.brand;
            std::cout << "\nModel: " << v.model;
            std::cout << "\nType: " << v.type;
            std::cout << "\nDaily Rate: Rs. " << v.dailyRate;
            std::cout << "\nAvailable: "
                      << (v.available ? "Yes" : "No") << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Repository Error: " << e.what() << "\n";
    }

    try {
        std::cout << "\n===== MAINTENANCE MANAGEMENT TEST =====\n";

        vrms::MaintenanceService maintenanceService;
        vrms::VehicleRepository vehicleRepository;

        std::cout << "\n===== INVALID MAINTENANCE DATE TEST =====\n";

        try {
            maintenanceService.scheduleMaintenance(
                "INVALID_DATE_TEST",
                "B101",
                "2026-02-30",
                "Testing invalid date",
                1000.00
            );

            std::cout << "ERROR: Invalid date was accepted!\n";
        }
        catch (const std::exception& e) {
            std::cout << "Invalid date rejected: "
                      << e.what() << '\n';
        }

        std::cout << "\n===== INVALID MAINTENANCE COST TEST =====\n";

        try {
            maintenanceService.scheduleMaintenance(
                "INVALID_COST_TEST",
                "B101",
                "2026-11-13",
                "Testing negative maintenance cost",
                -500.00
            );

            std::cout << "ERROR: Negative cost was accepted!\n";
        }
        catch (const std::exception& e) {
            std::cout << "Negative cost rejected: "
                      << e.what() << '\n';
        }

        auto maintenanceTimestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

        std::string maintenanceId =
            "M" + std::to_string(maintenanceTimestamp);

        maintenanceService.scheduleMaintenance(
            maintenanceId,
            "B101",
            "2026-11-13",
            "Routine servicing and inspection",
            2500.00
        );

        auto scheduledMaintenance =
            maintenanceService.getMaintenanceById(maintenanceId);

        std::cout << "Maintenance scheduled: Yes\n";
        std::cout << "Maintenance ID: "
                  << scheduledMaintenance.maintenanceId << "\n";
        std::cout << "Vehicle ID: "
                  << scheduledMaintenance.vehicleId << "\n";
        std::cout << "Status: "
                  << scheduledMaintenance.status << "\n";

        auto availableAfterScheduling =
            vehicleRepository.getAvailableVehicles();

        bool unavailableDuringMaintenance = true;

        for (const auto& vehicle : availableAfterScheduling) {
            if (vehicle.id == "B101") {
                unavailableDuringMaintenance = false;
                break;
            }
        }

        std::cout << "Vehicle unavailable during maintenance: "
                  << (unavailableDuringMaintenance ? "Yes" : "No")
                  << "\n";
        std::cout << "\n===== UNAVAILABLE VEHICLE TEST =====\n";

try {
    maintenanceService.scheduleMaintenance(
        "UNAVAILABLE_VEHICLE_TEST",
        "B101",
        "2026-11-13",
        "Testing overlapping maintenance",
        1500.00
    );

    std::cout << "ERROR: Unavailable vehicle was accepted!\n";
}
catch (const std::exception& e) {
    std::cout << "Unavailable vehicle rejected: "
              << e.what() << '\n';
}

        maintenanceService.completeMaintenance(
            maintenanceId,
            "2026-11-13"
        );
        std::cout << "\n===== DUPLICATE MAINTENANCE ID TEST =====\n";

try {
    maintenanceService.scheduleMaintenance(
        maintenanceId,
        "B101",
        "2026-11-13",
        "Testing duplicate maintenance ID",
        1500.00
    );

    std::cout << "ERROR: Duplicate maintenance ID was accepted!\n";
}
catch (const std::exception& e) {
    std::cout << "Duplicate maintenance ID rejected: "
              << e.what() << '\n';
}
        try {
            maintenanceService.completeMaintenance(
                maintenanceId,
                "2026-11-13"
            );

            std::cout << "ERROR: Duplicate completion was allowed!\n";
        }
        catch (const std::exception& e) {
            std::cout << "Duplicate completion rejected: "
                      << e.what() << '\n';
        }

        auto completedMaintenance =
            maintenanceService.getMaintenanceById(maintenanceId);

        std::cout << "Maintenance completed: "
                  << (completedMaintenance.status == "COMPLETED"
                          ? "Yes" : "No") << "\n";

        std::cout << "Completion date: "
                  << completedMaintenance.completionDate << "\n";

        auto availableAfterCompletion =
            vehicleRepository.getAvailableVehicles();

        bool availableAfterMaintenance = false;

        for (const auto& vehicle : availableAfterCompletion) {
            if (vehicle.id == "B101") {
                availableAfterMaintenance = true;
                break;
            }
        }

        std::cout << "Vehicle available after maintenance: "
                  << (availableAfterMaintenance ? "Yes" : "No")
                  << "\n";

        auto vehicleMaintenance =
            maintenanceService.getVehicleMaintenance("B101");

        std::cout << "Maintenance records for B101: "
                  << vehicleMaintenance.size() << "\n";

    }
    catch (const std::exception& e) {
        std::cerr << "Maintenance Test Error: " << e.what() << "\n";
    }

    try {
        vrms::CustomerRepository customerRepository;

        customerRepository.saveCustomer(
            customer,
            demoPasswordHash
        );

        std::cout << "\n===== DATABASE CUSTOMERS =====\n";

        auto customers = customerRepository.getAllCustomers();

        for (const auto& c : customers) {
            std::cout << "\nCustomer ID: " << c.id;
            std::cout << "\nName: " << c.name;
            std::cout << "\nEmail: " << c.email;
            std::cout << "\nMembership: " << c.membership << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Customer Repository Error: "
                  << e.what() << "\n";
    }

    try {
        vrms::BookingRepository bookingRepository;

        bookingRepository.createBooking(
            booking.getBookingId(),
            customer.getUserId(),
            booking.getVehicleId(),
            booking.getStartDate(),
            booking.getEndDate()
        );

        vrms::RentalRepository rentalRepository;

        rentalRepository.createRental(
            "R101",
            "BK101",
            "2026-11-18",
            500.00
        );

        rentalRepository.completeReturn(
            "R101",
            "2026-11-20"
        );

        std::cout << "\n===== DATABASE BOOKINGS =====\n";

        auto bookings = bookingRepository.getCustomerBookings(
            customer.getUserId()
        );

        for (const auto& b : bookings) {
            std::cout << "\nBooking ID: " << b.bookingId;
            std::cout << "\nCustomer ID: " << b.customerId;
            std::cout << "\nVehicle ID: " << b.vehicleId;
            std::cout << "\nStart Date: " << b.startDate;
            std::cout << "\nEnd Date: " << b.endDate;
            std::cout << "\nStatus: " << b.status << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Booking Repository Error: "
                  << e.what() << "\n";
    }

    try {
        std::cout << "\n===== AUTHENTICATION TEST =====\n";

        vrms::AuthService authService;

        auto registration = authService.registerCustomer(
            "AUTH001",
            "Test Customer",
            "auth.test@example.com",
            "TestPass#2026",
            "Gold"
        );

        std::cout << "Registration: "
                  << registration.message << "\n";

        auto login = authService.login(
            "auth.test@example.com",
            "TestPass#2026"
        );

        std::cout << "Correct Login: "
                  << (login.success ? "Successful" : "Failed") << "\n";

        if (login.success) {
            std::cout << "User ID: " << login.userId << "\n";
            std::cout << "Role: " << login.role << "\n";

            std::cout << "\n===== SESSION MANAGEMENT TEST =====\n";

            std::cout << "Session created: "
                      << (!login.sessionId.empty() ? "Yes" : "No") << "\n";

            std::cout << "Session ID: "
                      << login.sessionId << "\n";

            std::cout << "Session valid: "
                      << (authService.isSessionValid(login.sessionId)
                              ? "Yes" : "No") << "\n";

            std::cout << "Session user ID: "
                      << authService.getSessionUserId(login.sessionId) << "\n";

            std::cout << "Session user role: "
                      << authService.getSessionUserRole(login.sessionId) << "\n";

            bool logoutSuccessful =
                authService.logout(login.sessionId);

            std::cout << "Logout successful: "
                      << (logoutSuccessful ? "Yes" : "No") << "\n";

            std::cout << "Session valid after logout: "
                      << (authService.isSessionValid(login.sessionId)
                              ? "Yes" : "No") << "\n";
        }

        auto wrongLogin = authService.login(
            "auth.test@example.com",
            "WrongPassword123"
        );

        std::cout << "Incorrect Password Rejected: "
                  << (!wrongLogin.success ? "Yes" : "No") << "\n";

    }
    catch (const std::exception& e) {
        std::cerr << "Authentication Error: " << e.what() << "\n";
    }

    try {
        std::cout << "\n===== ROLE-BASED ACCESS CONTROL TEST =====\n";

        vrms::AuthorizationService authorizationService;

        vrms::Customer rbacCustomer(
            "RBAC_C1",
            "RBAC Test Customer",
            "rbac.customer@example.com",
            "Regular"
        );

        vrms::Admin level1Admin(
            "RBAC_A1",
            "Level One Admin",
            "admin1@example.com",
            1
        );

        vrms::Admin level2Admin(
            "RBAC_A2",
            "Level Two Admin",
            "admin2@example.com",
            2
        );

        vrms::Admin level4Admin(
            "RBAC_A4",
            "Level Four Admin",
            "admin4@example.com",
            4
        );

        vrms::Admin level5Admin(
            "RBAC_A5",
            "Level Five Admin",
            "admin5@example.com",
            5
        );

        std::cout << "Customer access to admin panel: "
                  << (authorizationService.canAccessAdminPanel(rbacCustomer)
                          ? "Allowed" : "Denied") << "\n";

        std::cout << "Admin access to admin panel: "
                  << (authorizationService.canAccessAdminPanel(level1Admin)
                          ? "Allowed" : "Denied") << "\n";

        std::cout << "Level 1 managing vehicles: "
                  << (authorizationService.canManageVehicles(level1Admin)
                          ? "Allowed" : "Denied") << "\n";

        std::cout << "Level 2 managing vehicles: "
                  << (authorizationService.canManageVehicles(level2Admin)
                          ? "Allowed" : "Denied") << "\n";

        std::cout << "Level 4 managing users: "
                  << (authorizationService.canManageUsers(level4Admin)
                          ? "Allowed" : "Denied") << "\n";

        std::cout << "Level 5 viewing reports: "
                  << (authorizationService.canViewReports(level5Admin)
                          ? "Allowed" : "Denied") << "\n";

    }
    catch (const std::exception& e) {
        std::cerr << "Authorization Test Error: " << e.what() << "\n";
    }

    try {
        std::cout << "\n===== AUDIT LOG TEST =====\n";

        vrms::AuditLogRepository auditRepository;

        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<
            std::chrono::milliseconds
        >(now.time_since_epoch()).count();

        std::string logId = "AUD" + std::to_string(timestamp);

        vrms::AuditLog auditLog(
            logId,
            "AUTH001",
            "LOGIN",
            "Customer logged in successfully"
        );

        auditRepository.saveLog(auditLog);

        std::cout << "Audit log saved: Yes\n";
        std::cout << "Log ID: " << auditLog.getLogId() << "\n";

        auto allLogs = auditRepository.getAllLogs();

        std::cout << "Total audit logs retrieved: "
                  << allLogs.size() << "\n";

        auto userLogs = auditRepository.getLogsByUser("AUTH001");

        std::cout << "Logs for AUTH001: "
                  << userLogs.size() << "\n";

        if (!userLogs.empty()) {
            std::cout << "\nLatest matching audit log:\n";
            std::cout << "User ID: "
                      << userLogs.back().getUserId() << "\n";
            std::cout << "Action: "
                      << userLogs.back().getAction() << "\n";
            std::cout << "Description: "
                      << userLogs.back().getDescription() << "\n";
        }

    }
    catch (const std::exception& e) {
        std::cerr << "Audit Log Error: " << e.what() << "\n";
    }

    return 0;
}
