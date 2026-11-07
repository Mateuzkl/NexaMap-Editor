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
#include <string_view>
#include <vector>

namespace Multiplayer {
#ifdef _WIN32
	namespace {
		struct Algorithm {
			BCRYPT_ALG_HANDLE handle = nullptr;
			explicit Algorithm(ULONG flags) {
				if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, flags) < 0) {
					throw Error("Cannot initialize system cryptography.");
				}
			}
			~Algorithm() {
				if (handle) {
					BCryptCloseAlgorithmProvider(handle, 0);
				}
			}
		};

		NTSTATUS HashBufferWithBCrypt(BCRYPT_ALG_HANDLE algorithm, PUCHAR secret, ULONG secretSize, PUCHAR input, ULONG inputSize, PUCHAR output, ULONG outputSize) {
			DWORD objectSize = 0;
			DWORD bytesWritten = 0;
			auto status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytesWritten, 0);
			if (status < 0) {
				return status;
			}

			std::vector<UCHAR> hashObject(objectSize);
			BCRYPT_HASH_HANDLE hash = nullptr;
			status = BCryptCreateHash(algorithm, &hash, hashObject.data(), static_cast<ULONG>(hashObject.size()), secret, secretSize, 0);
			if (status < 0) {
				return status;
			}

			status = BCryptHashData(hash, input, inputSize, 0);
			if (status >= 0) {
				status = BCryptFinishHash(hash, output, outputSize, 0);
			}
			BCryptDestroyHash(hash);
			return status;
		}
	}
#endif
	Identity randomIdentity() {
		Identity id;
#ifdef _WIN32
		if (BCryptGenRandom(nullptr, id.data(), static_cast<ULONG>(id.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
			throw Error("System random generator failed.");
		}
#else
		if (RAND_bytes(id.data(), static_cast<int>(id.size())) != 1) {
			throw Error("System random generator failed.");
		}
#endif
		return id;
	}
	Digest sha256(std::span<const uint8_t> data) {
		Digest result;
#ifdef _WIN32
		Algorithm alg(0);
		if (HashBufferWithBCrypt(alg.handle, nullptr, 0, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), result.data(), static_cast<ULONG>(result.size())) < 0) {
			throw Error("SHA-256 failed.");
		}
#else
		unsigned int size = 0;
		if (EVP_Digest(data.data(), data.size(), result.data(), &size, EVP_sha256(), nullptr) != 1 || size != result.size()) {
			throw Error("SHA-256 failed.");
		}
#endif
		return result;
	}
	Digest deriveSessionKey(const std::string& password, const Identity& session) {
		Digest key {};
#ifdef _WIN32
		Algorithm alg(BCRYPT_ALG_HANDLE_HMAC_FLAG);
		if (BCryptDeriveKeyPBKDF2(alg.handle, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()), const_cast<PUCHAR>(session.data()), static_cast<ULONG>(session.size()), 100000, key.data(), static_cast<ULONG>(key.size()), 0) < 0) {
			throw Error("Password key derivation failed.");
		}
#else
		if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), session.data(), static_cast<int>(session.size()), 100000, EVP_sha256(), static_cast<int>(key.size()), key.data()) != 1) {
			throw Error("Password key derivation failed.");
		}
#endif
		return key;
	}
	Digest hmacSha256(const Digest& key, std::span<const uint8_t> data) {
		Digest result {};
#ifdef _WIN32
		// Opening a provider is slow and this runs once per network frame.
		static Algorithm alg(BCRYPT_ALG_HANDLE_HMAC_FLAG);
		if (HashBufferWithBCrypt(alg.handle, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), result.data(), static_cast<ULONG>(result.size())) < 0) {
			throw Error("HMAC-SHA256 failed.");
		}
#else
		unsigned int size = 0;
		if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data.data(), data.size(), result.data(), &size) || size != result.size()) {
			throw Error("HMAC-SHA256 failed.");
		}
#endif
		return result;
	}
	HandshakeKeys deriveHandshake(const Digest& sessionKey, const Identity& session, std::span<const uint8_t> nonce, std::span<const uint8_t> hello) {
		Writer exchange(MaxPacket);
		exchange.raw(session);
		exchange.bytes(nonce);
		exchange.bytes(hello);
		const auto transcript = sha256(exchange.data);
		const auto derive = [&](std::string_view label) {
			Writer input;
			input.raw({ reinterpret_cast<const uint8_t*>(label.data()), label.size() });
			input.raw(transcript);
			return hmacSha256(sessionKey, input.data);
		};
		return { derive("NexaMap-2 client proof"), derive("NexaMap-2 host proof"), derive("NexaMap-2 client to host"), derive("NexaMap-2 host to client") };
	}
	namespace {
		Digest frameMac(const Digest& key, uint64_t sequence, std::span<const uint8_t> body) {
			Writer input(MaxPacket + sizeof(sequence));
			input.u64(sequence);
			input.raw(body);
			return hmacSha256(key, input.data);
		}
	}
	Bytes sealFrame(const Digest& key, uint64_t sequence, Packet type, std::span<const uint8_t> payload) {
		if (payload.size() + 1 + MacSize > MaxPacket) {
			throw Error("Network packet too large.");
		}
		Writer out(MaxPacket + 4);
		out.u32(static_cast<uint32_t>(payload.size() + 1 + MacSize));
		out.u8(static_cast<uint8_t>(type));
		out.raw(payload);
		out.raw(frameMac(key, sequence, std::span(out.data).subspan(4)));
		return std::move(out.data);
	}
	bool openFrame(const Digest& key, uint64_t sequence, Bytes& frameBody) {
		if (frameBody.size() <= MacSize) {
			return false;
		}
		const auto bodySize = frameBody.size() - MacSize;
		Digest received;
		std::copy(frameBody.begin() + bodySize, frameBody.end(), received.begin());
		if (!secureEqual(received, frameMac(key, sequence, std::span(frameBody).first(bodySize)))) {
			return false;
		}
		frameBody.resize(bodySize);
		return true;
	}
	bool secureEqual(const Digest& a, const Digest& b) {
		volatile uint8_t difference = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			difference = static_cast<uint8_t>(difference | (a[i] ^ b[i]));
		}
		return difference == 0;
	}
	void wipe(std::span<uint8_t> secret) {
#ifdef _WIN32
		SecureZeroMemory(secret.data(), secret.size());
#else
		OPENSSL_cleanse(secret.data(), secret.size());
#endif
	}
	std::string hex(std::span<const uint8_t> bytes) {
		constexpr char digits[] = "0123456789abcdef";
		std::string result;
		result.reserve(bytes.size() * 2);
		for (auto b : bytes) {
			result += digits[b >> 4];
			result += digits[b & 15];
		}
		return result;
	}
}
