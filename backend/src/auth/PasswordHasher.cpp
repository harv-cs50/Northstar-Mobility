
#include "auth/PasswordHasher.h"

#include <argon2.h>
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace vrms {

std::string PasswordHasher::hashPassword(
    const std::string& password
) {
    if (password.empty()) {
        throw std::invalid_argument("Password cannot be empty");
    }

    constexpr std::size_t SALT_LENGTH = 16;
    constexpr std::size_t HASH_LENGTH = 32;
    constexpr std::uint32_t ITERATIONS = 3;
    constexpr std::uint32_t MEMORY_COST = 65536;
    constexpr std::uint32_t PARALLELISM = 1;

    std::array<std::uint8_t, SALT_LENGTH> salt{};

    NTSTATUS randomResult = BCryptGenRandom(
        nullptr,
        salt.data(),
        static_cast<ULONG>(salt.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (randomResult != 0) {
        throw std::runtime_error("Secure salt generation failed");
    }

    char encodedHash[256]{};

    int result = argon2id_hash_encoded(
        ITERATIONS,
        MEMORY_COST,
        PARALLELISM,
        password.data(),
        password.size(),
        salt.data(),
        salt.size(),
        HASH_LENGTH,
        encodedHash,
        sizeof(encodedHash)
    );

    if (result != ARGON2_OK) {
        throw std::runtime_error(
            std::string("Password hashing failed: ") +
            argon2_error_message(result)
        );
    }

    return std::string(encodedHash);
}

bool PasswordHasher::verifyPassword(
    const std::string& password,
    const std::string& storedHash
) {
    if (password.empty() || storedHash.empty()) {
        return false;
    }

    int result = argon2id_verify(
        storedHash.c_str(),
        password.data(),
        password.size()
    );

    return result == ARGON2_OK;
}

}
