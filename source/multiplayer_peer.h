// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
// One connection's state. Internal to the session and its socket tests.
#ifndef NEXAMAP_MULTIPLAYER_PEER_H
#define NEXAMAP_MULTIPLAYER_PEER_H
#include "multiplayer_session.h"
#include "multiplayer_crypto.h"
#include "basemap.h"
#include <wx/socket.h>
#include <chrono>

inline uint64_t nowMs() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct MultiplayerSession::Peer {
	using Bytes = Multiplayer::Bytes;
	using Digest = Multiplayer::Digest;
	using Identity = Multiplayer::Identity;
	struct Transfer {
		std::shared_ptr<const Bytes> bytes;
		size_t offset = 0;
		bool started = false;
		Digest digest;
	};
	wxSocketBase* socket = nullptr;
	int eventId = wxNewId();
	Multiplayer::FrameReader frames;
	std::deque<Bytes> writes;
	std::deque<Transfer> transfers;
	std::deque<Bytes> deferred;
	size_t writeOffset = 0, pendingBytes = 0, transferBytes = 0, deferredBytes = 0;
	Bytes receiving;
	std::string source;
	Multiplayer::HandshakeKeys keys;
	uint64_t sendSequence = 0, receiveSequence = 0;
	bool keyed = false, sendSealed = false, receiveSealed = false;
	Digest receivingDigest {};
	uint32_t receivingSize = 0;
	uint64_t receivingSince = 0;
	Identity token {}, offeredSession {};
	Bytes challenge;
	uint32_t id = UINT32_MAX;
	uint64_t generation = 0;
	Multiplayer::Role role = Multiplayer::Role::Viewer;
	std::string name;
	bool authenticated = false, live = false, dead = false, connecting = false, snapshot = false, catchup = false;
	MapIterator iterator;
	uint64_t syncRevision = 0;
	uint32_t sequence = 0;
	Bytes metadata;
	size_t metadataOffset = 0;
	uint64_t created = nowMs(), lastReceived = created, lastPong = created, pingSent = 0, rateWindow = created;
	uint32_t rateCount = 0;
	const Digest& sendKey(bool hosting) const {
		return hosting ? keys.hostToClient : keys.clientToHost;
	}
	const Digest& receiveKey(bool hosting) const {
		return hosting ? keys.clientToHost : keys.hostToClient;
	}
	~Peer() {
		Multiplayer::wipe({ reinterpret_cast<uint8_t*>(&keys), sizeof(keys) });
		if (socket) {
			socket->Notify(false);
			socket->Destroy();
		}
	}
};
#endif
