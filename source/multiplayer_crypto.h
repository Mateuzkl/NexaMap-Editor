// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_CRYPTO_H
#define NEXAMAP_MULTIPLAYER_CRYPTO_H
#include "multiplayer_protocol.h"
namespace Multiplayer {
	Identity randomIdentity();
	Digest sha256(std::span<const uint8_t> data);
	Digest authenticate(const std::string& password, const Identity& session, std::span<const uint8_t> challenge);
	bool secureEqual(const Digest& a, const Digest& b);
	std::string hex(std::span<const uint8_t> bytes);
}
#endif
