// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_TRANSPORT_H
#define NEXAMAP_MULTIPLAYER_TRANSPORT_H
#include "multiplayer_protocol.h"
#include <wx/socket.h>
#include <deque>
#include <string>
#include <vector>
#include <atomic>
#include <memory>

namespace Multiplayer {
	constexpr uint16_t DefaultPort = 49171;
	struct AddressResolution {
		std::atomic<bool> done = false;
		std::string address;
	};
	// Only DNS runs off-thread. The worker owns this result, never a session/widget.
	std::shared_ptr<AddressResolution> resolveIPv4(const std::string& hostname);
	// Takes ownership. Preserves a partially written frame before the rejection.
	// An independent, bounded event handler finishes even if the session is gone.
	void closeSocket(wxSocketBase* socket, std::deque<Bytes> writes, size_t offset, const std::string& reason);
	std::vector<std::string> localIPv4Addresses();
	std::string socketFailure(wxSocketBase& socket, bool connecting);
}
#endif
