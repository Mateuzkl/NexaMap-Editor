// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_crypto.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif
#include <algorithm>

namespace Multiplayer {
#ifdef _WIN32
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    explicit Algorithm(ULONG flags) {
        if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, flags) < 0) throw Error("Cannot initialize system cryptography.");
    }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
}
#endif
Identity randomIdentity() {
    Identity id;
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, id.data(), static_cast<ULONG>(id.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw Error("System random generator failed.");
#else
    if (RAND_bytes(id.data(), static_cast<int>(id.size())) != 1) throw Error("System random generator failed.");
#endif
    return id;
}
Digest sha256(std::span<const uint8_t> data) {
    Digest result;
#ifdef _WIN32
    Algorithm alg(0);
    if (BCryptHash(alg.handle, nullptr, 0, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), result.data(), static_cast<ULONG>(result.size())) < 0) throw Error("SHA-256 failed.");
#else
    unsigned int size = 0;
    if (EVP_Digest(data.data(), data.size(), result.data(), &size, EVP_sha256(), nullptr) != 1 || size != result.size()) throw Error("SHA-256 failed.");
#endif
    return result;
}
Digest authenticate(const std::string& password, const Identity& session, std::span<const uint8_t> challenge) {
    Digest key {}, result {};
#ifdef _WIN32
    Algorithm alg(BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (BCryptDeriveKeyPBKDF2(alg.handle, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()), const_cast<PUCHAR>(session.data()), static_cast<ULONG>(session.size()), 100000, key.data(), static_cast<ULONG>(key.size()), 0) < 0) throw Error("Password key derivation failed.");
    const auto status = BCryptHash(alg.handle, key.data(), static_cast<ULONG>(key.size()), const_cast<PUCHAR>(challenge.data()), static_cast<ULONG>(challenge.size()), result.data(), static_cast<ULONG>(result.size()));
    SecureZeroMemory(key.data(), key.size());
    if (status < 0) throw Error("Authentication failed.");
#else
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), session.data(), static_cast<int>(session.size()), 100000, EVP_sha256(), static_cast<int>(key.size()), key.data()) != 1) throw Error("Password key derivation failed.");
    unsigned int size = 0;
    auto ok = HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), challenge.data(), challenge.size(), result.data(), &size);
    OPENSSL_cleanse(key.data(), key.size());
    if (!ok || size != result.size()) throw Error("Authentication failed.");
#endif
    return result;
}
bool secureEqual(const Digest& a, const Digest& b) {
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < a.size(); ++i) difference = static_cast<uint8_t>(difference | (a[i] ^ b[i]));
    return difference == 0;
}
std::string hex(std::span<const uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result; result.reserve(bytes.size() * 2);
    for (auto b : bytes) { result += digits[b >> 4]; result += digits[b & 15]; }
    return result;
}
}
