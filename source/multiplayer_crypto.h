// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_CRYPTO_H
#define NEXAMAP_MULTIPLAYER_CRYPTO_H
#include "multiplayer_protocol.h"
namespace Multiplayer {
	Identity randomIdentity();
	Digest sha256(std::span<const uint8_t> data);
	struct HandshakeKeys {
		Digest clientProof {};
		Digest hostProof {};
		Digest clientToHost {};
		Digest hostToClient {};
	};
	// PBKDF2 (expensive). The host runs it once per session, never per login.
	Digest deriveSessionKey(const std::string& password, const Identity& session);
	Digest hmacSha256(const Digest& key, std::span<const uint8_t> data);
	// Both proofs and both directional keys are bound to this connection's nonces and Hello.
	HandshakeKeys deriveHandshake(const Digest& sessionKey, const Identity& session, std::span<const uint8_t> nonce, std::span<const uint8_t> hello);
	// The sequence number stays off the wire: a dropped, replayed or reordered frame fails the MAC.
	Bytes sealFrame(const Digest& key, uint64_t sequence, Packet type, std::span<const uint8_t> payload);
	// Takes a frame body from FrameReader and removes its MAC only if the MAC verifies.
	bool openFrame(const Digest& key, uint64_t sequence, Bytes& frameBody);
	bool secureEqual(const Digest& a, const Digest& b);
	void wipe(std::span<uint8_t> secret);
	std::string hex(std::span<const uint8_t> bytes);
}
#endif
