
cmake_minimum_required(VERSION 3.20)

project(VehicleRentalSystem)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

find_path(ARGON2_INCLUDE_DIR
    NAMES argon2.h
    HINTS "C:/msys64/ucrt64/include"
)

find_library(ARGON2_LIBRARY
    NAMES argon2 libargon2
    HINTS "C:/msys64/ucrt64/lib"
)

if(NOT ARGON2_INCLUDE_DIR OR NOT ARGON2_LIBRARY)
    message(FATAL_ERROR "Argon2 library not found")
endif()

find_path(HPDF_INCLUDE_DIR
    NAMES hpdf.h
    HINTS "C:/msys64/ucrt64/include"
)
find_library(HPDF_LIBRARY
    NAMES hpdf libhpdf
    HINTS "C:/msys64/ucrt64/lib"
)
if(NOT HPDF_INCLUDE_DIR OR NOT HPDF_LIBRARY)
    message(FATAL_ERROR "libHaru not found. Install mingw-w64-ucrt-x86_64-libharu.")
endif()

add_executable(VehicleRentalSystem
    src/main.cpp
    src/Database.cpp

    src/models/Vehicle.cpp
    src/models/Car.cpp
    src/models/Bike.cpp

    src/models/User.cpp
    src/models/Customer.cpp
    src/models/Admin.cpp

    src/models/Booking.cpp
    src/models/Rental.cpp

    src/models/Payment.cpp
    src/models/Invoice.cpp

    src/repositories/VehicleRepository.cpp
    src/repositories/CustomerRepository.cpp
    src/repositories/BookingRepository.cpp
    src/repositories/RentalRepository.cpp
    src/repositories/PaymentRepository.cpp
    src/repositories/InvoiceRepository.cpp

    src/services/RentalService.cpp
    src/services/PaymentService.cpp
    src/services/InvoiceService.cpp
    src/services/MaintenanceService.cpp
    src/services/AuthService.cpp
    src/services/AuthorizationService.cpp
    src/services/SessionManager.cpp

    src/auth/PasswordHasher.cpp
    src/models/AuditLog.cpp
    src/repositories/AuditLogRepository.cpp
    src/models/Maintenance.cpp
src/repositories/MaintenanceRepository.cpp
)

# A separate API executable keeps the existing console demonstration intact.
add_executable(VRMSApi
    src/api_main.cpp
    src/Database.cpp
    src/models/Vehicle.cpp
    src/models/Car.cpp
    src/models/Bike.cpp
    src/models/User.cpp
    src/models/Customer.cpp
    src/models/Admin.cpp
    src/models/Booking.cpp
    src/models/Rental.cpp
    src/models/Payment.cpp
    src/models/Invoice.cpp
    src/models/AuditLog.cpp
    src/models/Maintenance.cpp
    src/repositories/VehicleRepository.cpp
    src/repositories/CustomerRepository.cpp
    src/repositories/BookingRepository.cpp
    src/repositories/RentalRepository.cpp
    src/repositories/PaymentRepository.cpp
    src/repositories/InvoiceRepository.cpp
    src/repositories/MaintenanceRepository.cpp
    src/repositories/AuditLogRepository.cpp
    src/services/RentalService.cpp
    src/services/PaymentService.cpp
    src/services/InvoiceService.cpp
    src/services/MaintenanceService.cpp
    src/services/AuthService.cpp
    src/services/AuthorizationService.cpp
    src/services/SessionManager.cpp
    src/auth/PasswordHasher.cpp
    src/services/InvoicePdfGenerator.cpp
    src/services/EmailService.cpp
    src/services/InvoiceDeliveryService.cpp
    src/services/ChatbotService.cpp
    src/services/GeminiClient.cpp
    src/services/PostgresChatbotDataSource.cpp
)

target_include_directories(VRMSApi PRIVATE include ${ARGON2_INCLUDE_DIR} ${HPDF_INCLUDE_DIR})
target_link_libraries(VRMSApi PRIVATE PkgConfig::PQXX PkgConfig::CURL ${HPDF_LIBRARY} ${ARGON2_LIBRARY} bcrypt ws2_32)

target_include_directories(VehicleRentalSystem PRIVATE
    include
    ${ARGON2_INCLUDE_DIR}
)

target_link_libraries(VehicleRentalSystem PRIVATE
    PkgConfig::PQXX
    ${ARGON2_LIBRARY}
    bcrypt
)

enable_testing()

add_executable(BookingTests
    tests/BookingTests.cpp
    src/models/Booking.cpp
    src/repositories/BookingRepository.cpp
    src/repositories/AuditLogRepository.cpp
    src/models/AuditLog.cpp
)

target_include_directories(BookingTests PRIVATE include)
target_link_libraries(BookingTests PRIVATE PkgConfig::PQXX)

add_executable(AuthTests
    tests/AuthTests.cpp
    src/models/User.cpp
    src/models/Customer.cpp
    src/models/Admin.cpp
    src/models/AuditLog.cpp
    src/repositories/AuditLogRepository.cpp
    src/services/AuthService.cpp
    src/services/AuthorizationService.cpp
    src/services/SessionManager.cpp
    src/auth/PasswordHasher.cpp
)
target_include_directories(AuthTests PRIVATE include ${ARGON2_INCLUDE_DIR})
target_link_libraries(AuthTests PRIVATE PkgConfig::PQXX ${ARGON2_LIBRARY} bcrypt)

add_test(
    NAME BookingModelValidation
    COMMAND BookingTests --model-only
)

add_executable(InvoiceDeliveryTests
    tests/InvoiceDeliveryTests.cpp
    src/services/InvoicePdfGenerator.cpp
    src/services/EmailService.cpp
    src/services/InvoiceDeliveryService.cpp
)
target_include_directories(InvoiceDeliveryTests PRIVATE include ${HPDF_INCLUDE_DIR})
target_link_libraries(InvoiceDeliveryTests PRIVATE PkgConfig::CURL ${HPDF_LIBRARY})

add_test(
    NAME InvoiceDeliveryTests
    COMMAND InvoiceDeliveryTests
)

add_executable(ChatbotTests
    tests/ChatbotTests.cpp
    src/services/ChatbotService.cpp
    src/services/GeminiClient.cpp
)
target_include_directories(ChatbotTests PRIVATE include)
target_link_libraries(ChatbotTests PRIVATE PkgConfig::CURL)

add_test(
    NAME ChatbotTests
    COMMAND ChatbotTests
)

add_test(
    NAME AuthProvisioningUnitTests
    COMMAND AuthTests --unit
)
