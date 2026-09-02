// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#include "main.h"
#include "multiplayer_session.h"
#include "multiplayer_codec.h"
#include "multiplayer_crypto.h"
#include "multiplayer_window.h"
#include "editor.h"
#include "editor_resource_session.h"
#include "gui.h"
#include "application.h"
#include "map_tab.h"
#include "map_display.h"
#include "iomap_otbm.h"
#include "complexitem.h"
#include <wx/socket.h>
#include <chrono>
#include <filesystem>
#ifndef _WIN32
	#include <netinet/tcp.h>
	#include <sys/socket.h>
#endif

using namespace Multiplayer;
namespace {
	MultiplayerSession* activeSession = nullptr;
	uint64_t nowMs() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}
	uint64_t keyOf(const Position& p) {
		return tileKey(static_cast<uint16_t>(p.x), static_cast<uint16_t>(p.y), static_cast<uint8_t>(p.z));
	}
	Position posOf(uint64_t key) {
		if (!validTileKey(key)) {
			throw Error("Invalid position.");
		}
		return { tileX(key), tileY(key), tileZ(key) };
	}
	uint32_t playerColor(uint32_t id) {
		constexpr uint32_t colors[] = { 0x45c8ff, 0xff626b, 0xffd166, 0x86df92, 0xcc99ff, 0xffad66 };
		return colors[id % 6];
	}
	struct BoolScope {
		bool& flag;
		bool previous;
		explicit BoolScope(bool& flag) :
			flag(flag), previous(flag) {
			flag = true;
		}
		~BoolScope() {
			flag = previous;
		}
	};
	constexpr int SocketEventId = wxID_HIGHEST + 4100;
	constexpr uint64_t IdleTimeout = 45000;
	constexpr size_t TransferChunk = 128 * 1024;
}

struct MultiplayerSession::Peer {
	struct Transfer {
		std::shared_ptr<const Bytes> bytes;
		size_t offset = 0;
		bool started = false;
		Digest digest;
	};
	wxSocketBase* socket = nullptr;
	FrameReader frames;
	std::deque<Bytes> writes;
	std::deque<Transfer> transfers;
	std::deque<Bytes> deferred;
	size_t writeOffset = 0, pendingBytes = 0, transferBytes = 0, deferredBytes = 0;
	Bytes receiving;
	Digest receivingDigest {};
	uint32_t receivingSize = 0;
	uint64_t receivingSince = 0;
	Identity identity {};
	Bytes challenge;
	uint32_t id = UINT32_MAX;
	uint64_t generation = 0;
	Role role = Role::Viewer;
	std::string name;
	bool authenticated = false, live = false, dead = false, connecting = false, snapshot = false, catchup = false;
	MapIterator iterator;
	uint64_t syncRevision = 0;
	uint32_t sequence = 0;
	Bytes metadata;
	size_t metadataOffset = 0;
	uint64_t lastReceived = nowMs(), lastPong = nowMs(), pingSent = 0, rateWindow = nowMs();
	uint32_t rateCount = 0;
	~Peer() {
		if (socket) {
			socket->Notify(false);
			socket->Close();
			socket->Destroy();
		}
	}
};

MultiplayerSession::MultiplayerSession(Editor& editor) :
	editor(editor), timer(this) {
	identity = randomIdentity();
	Bind(wxEVT_SOCKET, &MultiplayerSession::onSocket, this, SocketEventId);
	Bind(wxEVT_TIMER, &MultiplayerSession::onTimer, this);
}
MultiplayerSession::~MultiplayerSession() {
	disconnect();
	if (window) {
		window->detach();
		window->Destroy();
		window = nullptr;
	}
	DeletePendingEvents();
}
MultiplayerSession* MultiplayerSession::current() {
	return activeSession;
}
bool MultiplayerSession::permitsResourceSession(const std::shared_ptr<EditorResourceSession>& r) {
	return !activeSession || activeSession->resources == r;
}
bool MultiplayerSession::host(const Options& opts, std::string& error) {
	try {
		if (activeSession) {
			throw Error("Disconnect the existing multiplayer session first.");
		}
		if (!validText(opts.name, MaxName) || opts.password.size() > 256 || !opts.port || opts.maxPlayers < 2 || opts.maxPlayers > MaxPlayers) {
			throw Error("Invalid session settings.");
		}
		options = opts;
		hosting = true;
		session = randomIdentity();
		signature = assetSignature();
		metadata = encodeMetadata(editor.map);
		validateMetadata(metadata);
		resources = GetActiveEditorResourceSession();
		wxIPV4address address;
		address.AnyAddress();
		address.Service(options.port);
		listener = new wxSocketServer(address, wxSOCKET_NOWAIT | wxSOCKET_REUSEADDR);
		if (!listener->IsOk()) {
			throw Error("Cannot listen on this port. Check whether another session is using it.");
		}
		listener->SetEventHandler(*this, SocketEventId);
		listener->SetNotify(wxSOCKET_CONNECTION_FLAG);
		listener->Notify(true);
		editor.actionQueue->clear();
		running = ready = true;
		activeSession = this;
		localId = 0;
		participants[0] = { 0, options.name, Role::Host, {}, playerColor(0) };
		connectionStatus = "Hosting";
		lastBackup = lastActivity = nowMs();
		timer.Start(50);
		log("Host started on port " + std::to_string(options.port) + ".");
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		disconnect(error);
		return false;
	}
}
bool MultiplayerSession::join(const Options& opts, std::string& error) {
	try {
		if (activeSession) {
			throw Error("Disconnect the existing multiplayer session first.");
		}
		if (!validText(opts.name, MaxName) || opts.address.empty() || opts.address.size() > 253 || opts.password.size() > 256 || !opts.port) {
			throw Error("Invalid connection settings.");
		}
		if (editor.map.getTileCount() != 0 || editor.map.hasFile()) {
			throw Error("Join from a new, empty map tab so an existing map is never replaced.");
		}
		options = opts;
		signature = assetSignature();
		resources = GetActiveEditorResourceSession();
		editor.actionQueue->clear();
		running = true;
		hosting = false;
		ready = false;
		activeSession = this;
		lastActivity = nowMs();
		connectClient();
		timer.Start(50);
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		disconnect(error);
		return false;
	}
}
void MultiplayerSession::disconnect(const std::string& reason) {
	timer.Stop();
	running = ready = false;
	reconnectAt = 0;
	if (listener) {
		listener->Notify(false);
		listener->Close();
		listener->Destroy();
		listener = nullptr;
	}
	for (auto& [socket, peer] : peers) {
		if (!peer->dead) {
			try {
				Writer out;
				out.string(reason);
				send(*peer, Packet::Disconnect, out);
				write(*peer);
			} catch (...) { }
		}
	}
	peers.clear();
	participants.clear();
	visibleLocks.clear();
	locationPings.clear();
	approvals.clear();
	pending.reset();
	history.clear();
	historyBytes = historyPosition = 0;
	if (activeSession == this) {
		activeSession = nullptr;
	}
	std::fill(options.password.begin(), options.password.end(), '\0');
	options.password.clear();
	connectionStatus = "Disconnected";
	log(reason);
	needsRefresh = true;
}
void MultiplayerSession::configureSocket(Peer& peer) {
	auto* socket = peer.socket;
	socket->SetFlags(wxSOCKET_NOWAIT);
	socket->SetEventHandler(*this, SocketEventId);
	socket->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_OUTPUT_FLAG | wxSOCKET_LOST_FLAG | wxSOCKET_CONNECTION_FLAG);
	socket->Notify(true);
	int enabled = 1;
	socket->SetOption(IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
	socket->SetOption(SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
}
void MultiplayerSession::connectClient() {
	if (!running || hosting) {
		return;
	}
	connectionStatus = "Connecting";
	reconnectAt = 0;
	++generation;
	wxIPV4address address;
	if (!address.Hostname(wxstr(options.address))) {
		throw Error("Cannot resolve the host address.");
	}
	address.Service(options.port);
	auto peer = std::make_unique<Peer>();
	auto* socket = new wxSocketClient(wxSOCKET_NOWAIT);
	peer->socket = socket;
	peer->connecting = true;
	configureSocket(*peer);
	peers.emplace(socket, std::move(peer));
	if (!socket->Connect(address, false) && socket->LastError() != wxSOCKET_WOULDBLOCK && !socket->IsConnected()) {
		// A nonblocking connect can finish later via wxSOCKET_CONNECTION.
		log("Connecting to " + options.address + ":" + std::to_string(options.port) + "...");
	}
}
void MultiplayerSession::send(Peer& peer, Packet type, const Writer& payload) {
	if (peer.dead) {
		return;
	}
	auto packet = frame(type, payload.data);
	if (peer.pendingBytes + packet.size() > MaxPendingBytes || peer.writes.size() >= MaxPendingPackets) {
		drop(peer, "Slow connection: outgoing queue limit reached.");
		return;
	}
	peer.pendingBytes += packet.size();
	peer.writes.push_back(std::move(packet));
}
void MultiplayerSession::broadcast(Packet type, const Writer& payload, uint32_t except) {
	for (auto& [socket, peer] : peers) {
		if (peer->authenticated && !peer->dead && peer->id != except) {
			send(*peer, type, payload);
		}
	}
}
void MultiplayerSession::write(Peer& peer) {
	size_t budget = 256 * 1024;
	while (!peer.dead && !peer.connecting && !peer.writes.empty() && budget) {
		auto& front = peer.writes.front();
		auto n = std::min(budget, front.size() - peer.writeOffset);
		peer.socket->Write(front.data() + peer.writeOffset, static_cast<wxUint32>(n));
		const auto written = peer.socket->LastWriteCount();
		peer.writeOffset += written;
		peer.pendingBytes -= written;
		budget -= written;
		if (peer.writeOffset == front.size()) {
			peer.writes.pop_front();
			peer.writeOffset = 0;
		}
		if (peer.socket->Error() && peer.socket->LastError() != wxSOCKET_WOULDBLOCK) {
			drop(peer, "Connection write failed.");
			break;
		}
		if (!written) {
			break;
		}
	}
}
void MultiplayerSession::read(Peer& peer) {
	std::array<uint8_t, 16384> buffer;
	size_t budget = 256 * 1024;
	while (!peer.dead && budget) {
		auto n = std::min({ buffer.size(), peer.frames.needed(), budget });
		peer.socket->Read(buffer.data(), static_cast<wxUint32>(n));
		auto got = peer.socket->LastReadCount();
		if (got) {
			peer.lastReceived = nowMs();
			peer.frames.append(std::span(buffer).first(got));
			budget -= got;
		}
		if (peer.frames.ready()) {
			auto bytes = peer.frames.take();
			if (editDepth && bytes[0] != uint8_t(Packet::Ping) && bytes[0] != uint8_t(Packet::Pong)) {
				if (peer.deferredBytes + bytes.size() > MaxPendingBytes) {
					throw Error("Incoming queue limit reached during local operation.");
				}
				peer.deferredBytes += bytes.size();
				peer.deferred.push_back(std::move(bytes));
			} else {
				process(peer, bytes);
			}
		}
		if (peer.socket->Error() && peer.socket->LastError() != wxSOCKET_WOULDBLOCK) {
			drop(peer, "Connection read failed.");
			break;
		}
		if (!got) {
			break;
		}
	}
}
void MultiplayerSession::onSocket(wxSocketEvent& event) {
	if (!running) {
		return;
	}
	auto* socket = event.GetSocket();
	if (listener && socket == listener && event.GetSocketEvent() == wxSOCKET_CONNECTION) {
		auto* accepted = listener->Accept(false);
		if (!accepted) {
			return;
		}
		if (peers.size() >= options.maxPlayers + 4) {
			accepted->Destroy();
			return;
		}
		auto peer = std::make_unique<Peer>();
		peer->socket = accepted;
		configureSocket(*peer);
		auto* ptr = peer.get();
		peers.emplace(accepted, std::move(peer));
		try {
			challenge(*ptr);
			write(*ptr);
		} catch (const std::exception& e) {
			drop(*ptr, e.what());
		}
		return;
	}
	const auto found = peers.find(socket);
	if (found == peers.end() || found->second->dead) {
		return;
	}
	auto& peer = *found->second;
	try {
		switch (event.GetSocketEvent()) {
			case wxSOCKET_CONNECTION:
				peer.connecting = false;
				configureSocket(peer);
				connectionStatus = "Authenticating";
				break;
			case wxSOCKET_INPUT:
				read(peer);
				break;
			case wxSOCKET_OUTPUT:
				write(peer);
				break;
			case wxSOCKET_LOST:
				drop(peer, "Connection lost.");
				break;
		}
	} catch (const std::exception& e) {
		drop(peer, e.what(), false);
	}
}
void MultiplayerSession::drop(Peer& peer, const std::string& reason, bool reconnect) {
	if (peer.dead) {
		return;
	}
	peer.dead = true;
	peer.socket->Notify(false);
	peer.socket->Close();
	peer.writes.clear();
	peer.transfers.clear();
	peer.pendingBytes = peer.transferBytes = 0;
	peer.deferred.clear();
	peer.deferredBytes = 0;
	peer.receiving.clear();
	if (hosting) {
		const auto id = peer.id;
		participants.erase(id);
		regionLocks.releaseOwner(id);
		std::erase_if(approvals, [id](const auto& pair) { return pair.second.transaction.author == id; });
		log((peer.name.empty() ? "Connection" : peer.name) + ": " + reason);
		sendPlayers();
		sendLocks();
	} else {
		ready = false;
		participants.clear();
		visibleLocks.clear();
		receivingSnapshot = false;
		if (reconnect && running) {
			reconnectAt = nowMs() + reconnectDelay;
			reconnectDelay = std::min<uint64_t>(30000, reconnectDelay * 2);
			connectionStatus = "Reconnecting";
		} else {
			connectionStatus = "Disconnected: " + reason;
			reconnectAt = 0;
		}
		log(reason);
	}
	needsRefresh = true;
}
void MultiplayerSession::sendTransaction(Peer& peer, const Transaction& tx) {
	auto bytes = std::make_shared<const Bytes>(encodeTransaction(tx));
	if (peer.transferBytes + bytes->size() > MaxJournalBytes || peer.transfers.size() >= 1024) {
		drop(peer, "Slow connection: transaction queue limit reached.");
		return;
	}
	peer.transferBytes += bytes->size();
	peer.transfers.push_back({ bytes, 0, false, sha256(*bytes) });
}
void MultiplayerSession::pumpTransactions(Peer& peer) {
	while (!peer.dead && !peer.transfers.empty() && peer.pendingBytes < 512 * 1024) {
		auto& transfer = peer.transfers.front();
		if (!transfer.started) {
			Writer out;
			out.u32(static_cast<uint32_t>(transfer.bytes->size()));
			out.raw(transfer.digest);
			send(peer, Packet::TransactionBegin, out);
			transfer.started = true;
		}
		auto n = std::min(TransferChunk, transfer.bytes->size() - transfer.offset);
		Writer chunk;
		chunk.u32(static_cast<uint32_t>(transfer.offset));
		chunk.raw(std::span(*transfer.bytes).subspan(transfer.offset, n));
		send(peer, Packet::TransactionChunk, chunk);
		transfer.offset += n;
		if (peer.dead) {
			return;
		}
		if (transfer.offset == transfer.bytes->size()) {
			Writer out;
			send(peer, Packet::TransactionEnd, out);
			peer.transferBytes -= transfer.bytes->size();
			peer.transfers.pop_front();
		}
	}
}
void MultiplayerSession::challenge(Peer& peer) {
	Writer out;
	out.u32(ProtocolVersion);
	out.raw(session);
	auto a = randomIdentity();
	auto b = randomIdentity();
	peer.challenge.assign(a.begin(), a.end());
	peer.challenge.insert(peer.challenge.end(), b.begin(), b.end());
	out.raw(peer.challenge);
	send(peer, Packet::Challenge, out);
}
void MultiplayerSession::login(Peer& peer, Reader& in) {
	if (!hosting || peer.authenticated || peer.challenge.empty()) {
		throw Error("Unexpected handshake.");
	}
	auto body = in.bytes(4096);
	auto proof = in.fixed<32>();
	in.finish();
	Writer signedData;
	signedData.raw(peer.challenge);
	signedData.raw(body);
	if (!secureEqual(proof, authenticate(options.password, session, signedData.data))) {
		throw Error("Incorrect session password.");
	}
	Reader hello(body);
	if (hello.u32() != ProtocolVersion || hello.u32() != __RME_VERSION_ID__) {
		throw Error("NexaMap editor/protocol version differs from the host.");
	}
	if (hello.u32() != static_cast<uint32_t>(editor.map.getVersion().client)) {
		throw Error("Tibia client version differs from the host.");
	}
	if (hello.fixed<32>() != signature) {
		throw Error("Item definitions or item-count settings differ from the host.");
	}
	auto clientIdentity = hello.fixed<16>();
	auto clientGeneration = hello.u64();
	auto resumeSession = hello.fixed<16>();
	auto lastRevision = hello.u64();
	auto name = hello.string(MaxName);
	hello.finish();
	if (!validText(name, MaxName) || !clientGeneration) {
		throw Error("Invalid participant identity/name.");
	}
	auto known = identities.find(clientIdentity);
	if (known != identities.end()) {
		if (clientGeneration <= known->second.generation) {
			throw Error("Stale connection generation.");
		}
		peer.id = known->second.id;
		known->second.generation = clientGeneration;
		for (auto& [socket, previous] : peers) {
			if (previous.get() != &peer && !previous->dead && previous->id == peer.id) {
				drop(*previous, "Replaced by a newer authenticated connection.");
			}
		}
	} else {
		if (participants.size() >= options.maxPlayers || identities.size() >= 1024) {
			throw Error("Session is full.");
		}
		peer.id = nextClientId++;
		identities.emplace(clientIdentity, IdentityState { peer.id, clientGeneration });
	}
	peer.identity = clientIdentity;
	peer.generation = clientGeneration;
	peer.name = name;
	peer.role = options.defaultRole;
	peer.authenticated = true;
	peer.challenge.clear();
	participants[peer.id] = { peer.id, name, peer.role, {}, playerColor(peer.id) };
	Writer out;
	out.u32(peer.id);
	out.u8(static_cast<uint8_t>(peer.role));
	out.u64(clientGeneration);
	out.u64(revisions.lastTransaction(peer.id));
	out.string(options.name, MaxName);
	send(peer, Packet::Welcome, out);
	log(name + " authenticated.");
	if (resumeSession == session && journal.covers(lastRevision, revisions.current())) {
		peer.syncRevision = lastRevision;
		peer.catchup = true;
		Writer resume;
		resume.u64(lastRevision);
		send(peer, Packet::Resume, resume);
		finishCatchup(peer);
	} else {
		startSnapshot(peer);
	}
	sendPlayers();
	sendLocks();
}

void MultiplayerSession::process(Peer& peer, const Bytes& bytes) {
	Reader in(bytes);
	auto type = static_cast<Packet>(in.u8());
	const auto now = nowMs();
	if (now - peer.rateWindow >= 1000) {
		peer.rateWindow = now;
		peer.rateCount = 0;
	}
	if (++peer.rateCount > 4096) {
		throw Error("Packet rate limit exceeded.");
	}
	if (type == Packet::Disconnect) {
		auto reason = in.string(MaxChat);
		in.finish();
		drop(peer, reason, false);
		return;
	}
	if (type == Packet::Challenge) {
		if (hosting || peer.authenticated || !peer.challenge.empty()) {
			throw Error("Unexpected server challenge.");
		}
		if (in.u32() != ProtocolVersion) {
			throw Error("NexaMap multiplayer protocol mismatch.");
		}
		const auto previousSession = session;
		session = in.fixed<16>();
		auto nonce = in.raw(32);
		peer.challenge.assign(nonce.begin(), nonce.end());
		in.finish();
		if (previousSession != Identity {} && previousSession != session) {
			pending.reset();
			history.clear();
			historyPosition = historyBytes = 0;
			acknowledgedRevision = 0;
			log("Host started a new session; loading a fresh snapshot.");
		}
		Writer body;
		body.u32(ProtocolVersion);
		body.u32(__RME_VERSION_ID__);
		body.u32(static_cast<uint32_t>(editor.map.getVersion().client));
		body.raw(signature);
		body.raw(identity);
		body.u64(generation);
		body.raw(previousSession);
		body.u64(acknowledgedRevision);
		body.string(options.name, MaxName);
		Writer signedData;
		signedData.raw(peer.challenge);
		signedData.raw(body.data);
		Writer hello;
		hello.bytes(body.data, 4096);
		hello.raw(authenticate(options.password, session, signedData.data));
		send(peer, Packet::Hello, hello);
		return;
	}
	if (type == Packet::Hello) {
		login(peer, in);
		return;
	}
	if (type == Packet::Welcome) {
		if (hosting || peer.authenticated || peer.challenge.empty()) {
			throw Error("Unexpected welcome packet.");
		}
		localId = in.u32();
		const auto role = in.u8();
		if (!localId || role < 1 || role > 3 || in.u64() != generation) {
			throw Error("Invalid client identity/role.");
		}
		peer.role = static_cast<Role>(role);
		peer.id = 0;
		lastAcceptedTransaction = in.u64();
		peer.name = in.string(MaxName);
		in.finish();
		peer.authenticated = true;
		peer.connecting = false;
		peer.challenge.clear();
		nextTransaction = std::max(nextTransaction, lastAcceptedTransaction + 1);
		participants[localId] = { localId, options.name, peer.role, cursor, playerColor(localId) };
		connectionStatus = "Synchronizing";
		return;
	}
	if (!peer.authenticated) {
		throw Error("Authenticate before sending session packets.");
	}
	switch (type) {
		case Packet::Ping: {
			Writer out;
			out.u64(in.u64());
			in.finish();
			send(peer, Packet::Pong, out);
			break;
		}
		case Packet::Pong: {
			const auto echoed = in.u64();
			in.finish();
			if (!peer.pingSent || echoed != peer.pingSent) {
				throw Error("Unexpected heartbeat response.");
			}
			peer.lastPong = now;
			auto p = participants.find(hosting ? peer.id : 0);
			if (p != participants.end()) {
				p->second.latency = static_cast<uint32_t>(std::min<uint64_t>(now - echoed, UINT32_MAX));
			}
			peer.pingSent = 0;
			break;
		}
		case Packet::SnapshotBegin: {
			if (hosting || editDepth) {
				throw Error("Unexpected snapshot.");
			}
			if (in.fixed<16>() != session) {
				throw Error("Snapshot session mismatch.");
			}
			snapshotRevision = in.u64();
			auto width = in.u16();
			auto height = in.u16();
			auto version = in.u32();
			auto client = in.u32();
			auto storage = in.u8();
			auto idSpace = in.u8();
			auto spawn = in.u8();
			if (!width || !height || version > MAP_OTBM_5 || client != static_cast<uint32_t>(editor.map.getVersion().client) || storage > 2 || idSpace > 2 || spawn > 2) {
				throw Error("Unsupported snapshot map format.");
			}
			const auto name = in.string(1024);
			auto focus = posOf(in.u64());
			metadataSize = in.u32();
			if (metadataSize > MaxMetadata) {
				throw Error("Snapshot metadata exceeds its size limit.");
			}
			in.finish();
			resetClientMap();
			editor.map.setWidth(width);
			editor.map.setHeight(height);
			editor.map.mapVersion.otbm = static_cast<MapVersionID>(version);
			editor.map.setStorageFormat(static_cast<MapStorageFormat>(storage));
			editor.map.setItemIdSpace(static_cast<ItemIdSpace>(idSpace));
			editor.map.setSpawnFormat(static_cast<SpawnFormat>(spawn));
			editor.map.setName(name + " (multiplayer)");
			incomingMetadata.clear();
			incomingMetadata.reserve(metadataSize);
			receivingSnapshot = true;
			snapshotSequence = 0;
			ready = false;
			history.clear();
			historyPosition = historyBytes = 0;
			connectionStatus = "Receiving map";
			if (g_gui.GetCurrentEditor() == &editor) {
				g_gui.SetScreenCenterPosition(focus);
			}
			log("Map synchronization started.");
			break;
		}
		case Packet::SnapshotChunk: {
			if (hosting || !receivingSnapshot || in.u32() != snapshotSequence++) {
				throw Error("Snapshot chunk is out of sequence.");
			}
			auto kind = in.u8();
			if (kind == 0) {
				const auto offset = in.u32();
				auto data = in.raw(in.remaining());
				if (offset != incomingMetadata.size() || data.size() > metadataSize - incomingMetadata.size()) {
					throw Error("Invalid metadata chunk.");
				}
				incomingMetadata.insert(incomingMetadata.end(), data.begin(), data.end());
				if (incomingMetadata.size() == metadataSize) {
					validateMetadata(incomingMetadata);
					BoolScope scope(applying);
					applyMetadata(editor.map, incomingMetadata);
					metadata = incomingMetadata;
				}
			} else if (kind == 1) {
				if (incomingMetadata.size() != metadataSize) {
					throw Error("Tile received before metadata.");
				}
				auto count = in.u16();
				if (!count || count > 256) {
					throw Error("Invalid snapshot tile count.");
				}
				Transaction tx;
				tx.author = 0;
				std::set<uint64_t> keys;
				while (count--) {
					TileEdit tile;
					tile.key = in.u64();
					tile.after = in.bytes(MaxTile);
					if (!keys.insert(tile.key).second) {
						throw Error("Duplicate snapshot tile.");
					}
					tx.tiles.push_back(std::move(tile));
				}
				in.finish();
				apply(tx);
			} else {
				throw Error("Unknown snapshot chunk type.");
			}
			needsRefresh = true;
			break;
		}
		case Packet::SnapshotEnd: {
			if (hosting || !receivingSnapshot || in.u32() != snapshotSequence || incomingMetadata.size() != metadataSize) {
				throw Error("Incomplete snapshot.");
			}
			in.finish();
			acknowledgedRevision = snapshotRevision;
			receivingSnapshot = false;
			connectionStatus = "Catching up";
			break;
		}
		case Packet::Resume: {
			if (hosting || receivingSnapshot) {
				throw Error("Unexpected resume packet.");
			}
			const auto after = in.u64();
			in.finish();
			if (after != acknowledgedRevision) {
				throw Error("Resume revision mismatch.");
			}
			ready = false;
			connectionStatus = "Catching up";
			break;
		}
		case Packet::Ready: {
			if (hosting) {
				auto after = in.u64();
				auto full = in.boolean();
				in.finish();
				peer.live = false;
				if (peer.snapshot || peer.catchup || !peer.transfers.empty()) {
					break;
				}
				if (full || !journal.covers(after, revisions.current())) {
					startSnapshot(peer);
				} else {
					peer.syncRevision = after;
					peer.catchup = true;
					Writer resume;
					resume.u64(after);
					send(peer, Packet::Resume, resume);
					finishCatchup(peer);
				}
			} else {
				const auto revision = in.u64();
				in.finish();
				if (receivingSnapshot || acknowledgedRevision != revision) {
					throw Error("Map synchronization revision mismatch.");
				}
				ready = true;
				peer.live = true;
				reconnectDelay = 1000;
				connectionStatus = "Connected";
				log("Map synchronized at revision " + std::to_string(revision) + ".");
				if (pending) {
					if (lastAcceptedTransaction >= pending->transaction.id) {
						completePending(pending->transaction);
					} else {
						pending->transaction.author = localId;
						sendTransaction(peer, pending->transaction);
					}
				}
				needsRefresh = true;
			}
			break;
		}
		case Packet::TransactionBegin: {
			if (peer.receivingSize || (hosting && !peer.live) || (!hosting && receivingSnapshot)) {
				throw Error("Unexpected transaction.");
			}
			peer.receivingSize = in.u32();
			peer.receivingDigest = in.fixed<32>();
			in.finish();
			if (!peer.receivingSize || peer.receivingSize > MaxTransaction) {
				throw Error("Transaction exceeds its size limit.");
			}
			peer.receiving.clear();
			peer.receiving.reserve(peer.receivingSize);
			peer.receivingSince = now;
			break;
		}
		case Packet::TransactionChunk: {
			if (!peer.receivingSize || in.u32() != peer.receiving.size()) {
				throw Error("Transaction chunk is out of sequence.");
			}
			auto data = in.raw(in.remaining());
			if (data.empty() || data.size() > peer.receivingSize - peer.receiving.size()) {
				throw Error("Invalid transaction chunk.");
			}
			peer.receiving.insert(peer.receiving.end(), data.begin(), data.end());
			break;
		}
		case Packet::TransactionEnd: {
			in.finish();
			if (!peer.receivingSize || peer.receiving.size() != peer.receivingSize || !secureEqual(sha256(peer.receiving), peer.receivingDigest)) {
				throw Error("Incomplete/corrupt transaction.");
			}
			auto tx = decodeTransaction(peer.receiving);
			peer.receivingSize = 0;
			peer.receiving.clear();
			receiveTransaction(peer, std::move(tx));
			break;
		}
		case Packet::Rejected: {
			if (hosting) {
				throw Error("Unexpected transaction rejection.");
			}
			auto id = in.u64();
			auto message = in.string(MaxChat);
			in.finish();
			if (pending && pending->transaction.id == id) {
				pending.reset();
			}
			log(message);
			needsRefresh = true;
			if (id) {
				requestResync();
			}
			break;
		}
		case Packet::Approval: {
			if (hosting) {
				throw Error("Unexpected approval notification.");
			}
			const auto id = in.u64();
			const auto message = in.string(MaxChat);
			in.finish();
			if (!pending || pending->transaction.id != id) {
				throw Error("Unknown approval transaction.");
			}
			log(message);
			connectionStatus = "Waiting for host approval";
			break;
		}
		case Packet::Chat: {
			if (hosting) {
				auto text = in.string(MaxChat);
				in.finish();
				if (!validText(text, MaxChat) || peer.role == Role::Viewer) {
					throw Error("Invalid chat message or role.");
				}
				Writer out;
				out.string(peer.name, MaxName);
				out.string(text, MaxChat);
				broadcast(Packet::Chat, out);
				log(peer.name + ": " + text);
			} else {
				auto name = in.string(MaxName);
				auto text = in.string(MaxChat);
				in.finish();
				if (!validText(name, MaxName) || !validText(text, MaxChat)) {
					throw Error("Invalid chat message.");
				}
				log(name + ": " + text);
			}
			break;
		}
		case Packet::Cursor: {
			const auto id = hosting ? peer.id : in.u32();
			const auto pos = posOf(in.u64());
			const bool afk = in.boolean();
			in.finish();
			auto p = participants.find(id);
			if (p != participants.end()) {
				p->second.cursor = pos;
				p->second.afk = afk;
			}
			if (hosting) {
				Writer out;
				out.u32(id);
				out.u64(keyOf(pos));
				out.u8(afk);
				broadcast(Packet::Cursor, out, id);
			}
			needsRefresh = true;
			break;
		}
		case Packet::Players: {
			if (hosting) {
				throw Error("Unexpected participant list.");
			}
			auto count = in.u8();
			if (count > MaxPlayers) {
				throw Error("Too many participants.");
			}
			std::map<uint32_t, Participant> updated;
			while (count--) {
				Participant p;
				p.id = in.u32();
				p.name = in.string(MaxName);
				auto role = in.u8();
				if (role > 3 || !validText(p.name, MaxName)) {
					throw Error("Invalid participant.");
				}
				p.role = static_cast<Role>(role);
				p.color = playerColor(p.id);
				p.cursor = posOf(in.u64());
				p.afk = in.boolean();
				p.latency = in.u32();
				if (!updated.emplace(p.id, p).second) {
					throw Error("Duplicate participant.");
				}
			}
			in.finish();
			participants = std::move(updated);
			needsRefresh = true;
			break;
		}
		case Packet::Lock: {
			if (!hosting) {
				throw Error("Unexpected lock request.");
			}
			auto id = in.u64();
			auto region = readRegion(in);
			in.finish();
			if (peer.role != Role::Editor || !regionLocks.acquire(peer.id, id, region, now)) {
				reject(&peer, 0, "Region is locked by another participant or your role cannot lock it.");
			} else {
				sendLocks();
			}
			break;
		}
		case Packet::Unlock: {
			if (!hosting) {
				throw Error("Unexpected unlock request.");
			}
			auto id = in.u64();
			in.finish();
			regionLocks.release(peer.id, id);
			sendLocks();
			break;
		}
		case Packet::Locks: {
			if (hosting) {
				throw Error("Unexpected lock list.");
			}
			auto count = in.u16();
			if (count > MaxLocks) {
				throw Error("Too many locks.");
			}
			std::vector<Lease> list;
			while (count--) {
				Lease l;
				l.id = in.u64();
				l.owner = in.u32();
				l.region = readRegion(in);
				auto remaining = in.u32();
				if (remaining > 30000) {
					throw Error("Invalid lease.");
				}
				l.expires = now + remaining;
				list.push_back(l);
			}
			in.finish();
			visibleLocks = std::move(list);
			needsRefresh = true;
			break;
		}
		case Packet::LocationPing: {
			const auto id = hosting ? peer.id : in.u32();
			auto pos = posOf(in.u64());
			in.finish();
			auto participant = participants.find(id);
			if (participant == participants.end()) {
				break;
			}
			if (locationPings.size() >= 64) {
				locationPings.erase(locationPings.begin());
			}
			locationPings.push_back({ pos, playerColor(id), participant->second.name, now + 5000 });
			if (hosting) {
				Writer out;
				out.u32(id);
				out.u64(keyOf(pos));
				broadcast(Packet::LocationPing, out);
			}
			needsRefresh = true;
			break;
		}
		default:
			throw Error("Unknown or disallowed multiplayer packet.");
	}
}

void MultiplayerSession::startSnapshot(Peer& peer) {
	peer.live = false;
	peer.snapshot = true;
	peer.catchup = false;
	peer.syncRevision = revisions.current();
	peer.iterator = editor.map.begin();
	peer.sequence = 0;
	peer.metadata = encodeMetadata(editor.map);
	peer.metadataOffset = 0;
	Writer out;
	out.raw(session);
	out.u64(peer.syncRevision);
	out.u16(editor.map.getWidth());
	out.u16(editor.map.getHeight());
	out.u32(editor.map.getVersion().otbm);
	out.u32(editor.map.getVersion().client);
	out.u8(static_cast<uint8_t>(editor.map.getStorageFormat()));
	out.u8(static_cast<uint8_t>(editor.map.getItemIdSpace()));
	out.u8(static_cast<uint8_t>(editor.map.getSpawnFormat()));
	out.string(editor.map.getName(), 1024);
	Position focus(editor.map.getWidth() / 2, editor.map.getHeight() / 2, 7);
	if (g_gui.GetCurrentEditor() == &editor && g_gui.GetCurrentMapTab()) {
		focus = g_gui.GetCurrentMapTab()->GetScreenCenterPosition();
	}
	out.u64(keyOf(focus));
	out.u32(static_cast<uint32_t>(peer.metadata.size()));
	send(peer, Packet::SnapshotBegin, out);
	log("Sending map to " + peer.name + ".");
}
void MultiplayerSession::pumpSnapshot(Peer& peer) {
	if (!peer.snapshot || peer.dead || peer.pendingBytes >= 512 * 1024) {
		return;
	}
	Writer out;
	out.u32(peer.sequence++);
	if (peer.metadataOffset < peer.metadata.size()) {
		auto size = std::min(TransferChunk, peer.metadata.size() - peer.metadataOffset);
		out.u8(0);
		out.u32(static_cast<uint32_t>(peer.metadataOffset));
		out.raw(std::span(peer.metadata).subspan(peer.metadataOffset, size));
		peer.metadataOffset += size;
		send(peer, Packet::SnapshotChunk, out);
		return;
	}
	Writer tiles;
	uint16_t count = 0;
	while (peer.iterator != editor.map.end() && count < 256 && tiles.data.size() < TransferChunk) {
		auto* tile = (*peer.iterator)->get();
		++peer.iterator;
		if (!tile) {
			continue;
		}
		auto bytes = encodeTile(tile);
		tiles.u64(keyOf(tile->getPosition()));
		tiles.bytes(bytes, MaxTile);
		++count;
	}
	if (count) {
		out.u8(1);
		out.u16(count);
		out.raw(tiles.data);
		send(peer, Packet::SnapshotChunk, out);
	} else {
		--peer.sequence;
	}
	if (peer.iterator == editor.map.end()) {
		peer.snapshot = false;
		peer.catchup = true;
		Writer end;
		end.u32(peer.sequence);
		send(peer, Packet::SnapshotEnd, end);
		peer.metadata.clear();
		finishCatchup(peer);
	}
}
void MultiplayerSession::finishCatchup(Peer& peer) {
	if (!peer.catchup || peer.dead || !peer.transfers.empty()) {
		return;
	}
	if (!journal.covers(peer.syncRevision, revisions.current())) {
		startSnapshot(peer);
		return;
	}
	if (peer.syncRevision < revisions.current()) {
		for (const auto& entry : journal.entries()) {
			if (entry.revision > peer.syncRevision) {
				sendTransaction(peer, decodeTransaction(entry.bytes));
			}
		}
		peer.syncRevision = revisions.current();
		return;
	}
	// All preceding transaction frames are already in the socket's ordered queue.
	Writer readyMessage;
	readyMessage.u64(revisions.current());
	send(peer, Packet::Ready, readyMessage);
	peer.catchup = false;
	peer.live = true;
}
void MultiplayerSession::resetClientMap() {
	BoolScope scope(applying);
	editor.selection.clear();
	editor.actionQueue->clear();
	while (editor.map.houses.begin() != editor.map.houses.end()) {
		editor.map.houses.removeHouse(editor.map.houses.begin()->second);
	}
	while (editor.map.waypoints.begin() != editor.map.waypoints.end()) {
		editor.map.waypoints.removeWaypoint(editor.map.waypoints.begin()->first);
	}
	editor.map.towns.clear();
	std::vector<std::string> zones;
	for (const auto& [name, id] : editor.map.zones) {
		zones.push_back(name);
	}
	for (const auto& name : zones) {
		editor.map.zones.removeZone(name);
	}
	while (editor.map.spawns.begin() != editor.map.spawns.end()) {
		editor.map.removeSpawn(*editor.map.spawns.begin());
	}
	editor.map.clear();
	editor.map.uidRefCount.clear();
	editor.map.filename.clear();
	editor.map.unnamed = true;
}
bool MultiplayerSession::canEdit() const {
	if (applying) {
		return true;
	}
	if (!running || !ready || pending) {
		return false;
	}
	const auto p = participants.find(localId);
	return p != participants.end() && (p->second.role == Role::Host || p->second.role == Role::Editor);
}
bool MultiplayerSession::canUndo() const {
	return canEdit() && historyPosition > 0;
}
bool MultiplayerSession::canRedo() const {
	return canEdit() && historyPosition < history.size();
}
uint64_t MultiplayerSession::revision() const {
	return hosting ? revisions.current() : acknowledgedRevision;
}
std::string MultiplayerSession::sessionId() const {
	return hex(session);
}
std::string MultiplayerSession::status() const {
	return connectionStatus;
}
size_t MultiplayerSession::pendingBytes() const {
	size_t bytes = 0;
	for (const auto& [socket, peer] : peers) {
		bytes += peer->pendingBytes + peer->transferBytes;
	}
	return bytes;
}
void MultiplayerSession::consumeBatch(BatchAction* rawBatch) {
	std::unique_ptr<BatchAction> batch(rawBatch);
	if (batch->getType() == ACTION_SELECT) {
		return;
	}
	Transaction tx;
	tx.session = session;
	tx.author = localId;
	tx.base = revision();
	History entry;
	std::map<uint64_t, Bytes> before;
	bool rolledBack = false;
	try {
		// Committed Change entries hold the OLD tile. The first occurrence is
		// the preimage even when a brush touches the same tile several times.
		for (const auto* action : batch->batch) {
			for (const auto* change : action->changes) {
				if (change->getType() == CHANGE_TILE) {
					const auto* tile = static_cast<const Tile*>(change->getData());
					auto key = keyOf(tile->getPosition());
					if (!before.contains(key)) {
						before.emplace(key, encodeTile(tile));
					}
				}
			}
		}
		const auto afterMetadata = encodeMetadata(editor.map);
		if (afterMetadata != metadata) {
			tx.metadataBefore = sha256(metadata);
			tx.metadata = afterMetadata;
			entry.inverse.metadata = metadata;
			entry.inverse.metadataBefore = sha256(afterMetadata);
		}
		for (const auto& [key, data] : before) {
			auto after = encodeTile(editor.map.getTile(posOf(key)));
			if (after == data) {
				continue;
			}
			tx.tiles.push_back({ key, sha256(data), after });
			entry.inverse.tiles.push_back({ key, sha256(after), data });
		}
		{
			BoolScope scope(applying);
			if (!batch->undo()) {
				throw Error("Cannot roll back this operation for multiplayer submission.");
			}
		}
		rolledBack = true;
		if (tx.tiles.empty() && tx.metadata.empty()) {
			return;
		}
		entry.forward = tx;
		entry.bytes = encodeTransaction(entry.forward).size() + encodeTransaction(entry.inverse).size();
		submit(std::move(tx), PendingKind::Edit, std::move(entry));
	} catch (const std::exception& e) {
		if (!rolledBack) {
			BoolScope scope(applying);
			batch->rollback();
		}
		log(std::string("Edit cancelled: ") + e.what());
	}
	needsRefresh = true;
}
void MultiplayerSession::submit(Transaction tx, PendingKind kind, std::optional<History> entry) {
	if (!canEdit()) {
		log("Wait for synchronization/approval, or ask the host for edit permission.");
		return;
	}
	tx.id = nextTransaction++;
	tx.session = session;
	tx.author = localId;
	tx.revision = 0;
	encodeTransaction(tx); // Check the complete transaction budget before sending anything.
	pending = Pending { tx, kind, std::move(entry) };
	if (hosting) {
		accept(nullptr, std::move(tx), true);
	} else {
		for (auto& [socket, peer] : peers) {
			if (peer->authenticated && !peer->dead) {
				sendTransaction(*peer, tx);
				break;
			}
		}
	}
	needsRefresh = true;
}
bool MultiplayerSession::undo() {
	if (!canUndo()) {
		return false;
	}
	try {
		auto tx = history[historyPosition - 1].inverse;
		tx.base = revision();
		submit(std::move(tx), PendingKind::Undo);
		return true;
	} catch (const std::exception& e) {
		log(e.what());
		return false;
	}
}
bool MultiplayerSession::redo() {
	if (!canRedo()) {
		return false;
	}
	try {
		auto tx = history[historyPosition].forward;
		tx.base = revision();
		submit(std::move(tx), PendingKind::Redo);
		return true;
	} catch (const std::exception& e) {
		log(e.what());
		return false;
	}
}
bool MultiplayerSession::hasSensitiveChanges(const Transaction& tx) const {
	if (!tx.metadata.empty()) {
		return true;
	}
	std::vector<std::unique_ptr<Tile>> decoded;
	for (const auto& edit : tx.tiles) {
		decoded.push_back(decodeTile(editor.map, edit.key, edit.after));
	}
	return !validateTiles(editor.map, decoded, false).empty();
}
void MultiplayerSession::accept(Peer* peer, Transaction tx, bool approved) {
	const auto author = peer ? peer->id : 0;
	const auto role = peer ? peer->role : Role::Host;
	try {
		auto reason = revisions.validate(tx, session, role, author, regionLocks, nowMs());
		if (!reason.empty()) {
			reject(peer, tx.id, reason);
			return;
		}
		if (!tx.metadata.empty() && !secureEqual(tx.metadataBefore, sha256(encodeMetadata(editor.map)))) {
			reject(peer, tx.id, "Metadata changed while this request was open.");
			return;
		}
		for (const auto& edit : tx.tiles) {
			if (!secureEqual(edit.before, sha256(encodeTile(editor.map.getTile(posOf(edit.key)))))) {
				reject(peer, tx.id, "Conflict: tile content changed. Refresh and retry.");
				return;
			}
		}
		if (peer && !approved && options.approvals && hasSensitiveChanges(tx)) {
			size_t bytes = 0;
			for (const auto& [id, a] : approvals) {
				bytes += encodeTransaction(a.transaction).size();
			}
			if (approvals.size() >= 64 || bytes + encodeTransaction(tx).size() > MaxJournalBytes) {
				reject(peer, tx.id, "Host approval queue is full.");
				return;
			}
			for (const auto& [id, a] : approvals) {
				if (a.transaction.author == author && a.transaction.id == tx.id) {
					return;
				}
			}
			auto id = nextApproval++;
			const auto description = tx.metadata.empty() ? "Sensitive item / Unique ID edit" : "Town / house / waypoint / zone edit";
			approvals.emplace(id, Approval { id, tx, peer->name, description, nowMs() });
			Writer waiting;
			waiting.u64(tx.id);
			waiting.string("Edit is waiting for host approval.");
			send(*peer, Packet::Approval, waiting);
			log(peer->name + " requested approval: " + description);
			return;
		}
		apply(tx);
		tx.revision = revisions.commit(tx);
		journal.push(tx.revision, encodeTransaction(tx));
		for (auto& [socket, recipient] : peers) {
			if (recipient->live && !recipient->dead) {
				sendTransaction(*recipient, tx);
			}
		}
		if (!peer) {
			completePending(tx);
		}
		needsRefresh = true;
	} catch (const std::exception& e) {
		reject(peer, tx.id, e.what());
	}
}
void MultiplayerSession::apply(const Transaction& tx) {
	if (!tx.metadata.empty()) {
		validateMetadata(tx.metadata);
	}
	std::vector<std::unique_ptr<Tile>> tiles;
	tiles.reserve(tx.tiles.size());
	for (const auto& edit : tx.tiles) {
		tiles.push_back(decodeTile(editor.map, edit.key, edit.after));
	}
	const auto validation = validateTiles(editor.map, tiles, true, tx.metadata);
	if (!validation.empty()) {
		throw Error(validation);
	}
	BoolScope scope(applying);
	if (!tx.metadata.empty()) {
		applyMetadata(editor.map, tx.metadata);
		metadata = tx.metadata;
	}
	std::unique_ptr<Action> action(editor.actionQueue->createAction(ACTION_REMOTE));
	for (auto& tile : tiles) {
		action->addChange(new Change(tile.release()));
	}
	if (!action->commit()) {
		throw Error("Cannot apply authoritative map change.");
	}
	editor.map.doChange();
	needsRefresh = true;
}
void MultiplayerSession::receiveTransaction(Peer& peer, Transaction tx) {
	if (hosting) {
		accept(&peer, std::move(tx));
		return;
	}
	if (tx.session != session || tx.revision != acknowledgedRevision + 1) {
		throw Error("Authoritative revision gap; reconnect to synchronize.");
	}
	apply(tx);
	acknowledgedRevision = tx.revision;
	if (tx.author == localId) {
		lastAcceptedTransaction = std::max(lastAcceptedTransaction, tx.id);
		completePending(tx);
	}
}
void MultiplayerSession::completePending(const Transaction& tx) {
	if (!pending || pending->transaction.id != tx.id) {
		return;
	}
	if (pending->kind == PendingKind::Undo) {
		if (historyPosition) {
			--historyPosition;
		}
	} else if (pending->kind == PendingKind::Redo) {
		if (historyPosition < history.size()) {
			++historyPosition;
		}
	} else if (pending->history) {
		while (history.size() > historyPosition) {
			historyBytes -= history.back().bytes;
			history.pop_back();
		}
		auto entry = std::move(*pending->history);
		entry.forward = tx;
		// Use the accepted image when the host assigns a replacement Unique ID.
		for (auto& inverse : entry.inverse.tiles) {
			for (const auto& accepted : tx.tiles) {
				if (inverse.key == accepted.key) {
					inverse.before = sha256(accepted.after);
				}
			}
		}
		entry.bytes = encodeTransaction(entry.forward).size() + encodeTransaction(entry.inverse).size();
		historyBytes += entry.bytes;
		history.push_back(std::move(entry));
		historyPosition = history.size();
		while (!history.empty() && (historyBytes > MaxJournalBytes || history.size() > 500)) {
			historyBytes -= history.front().bytes;
			history.pop_front();
			--historyPosition;
		}
	}
	pending.reset();
	connectionStatus = hosting ? "Hosting" : "Connected";
	needsRefresh = true;
}
void MultiplayerSession::reject(Peer* peer, uint64_t id, const std::string& reason) {
	const auto text = reason.substr(0, MaxChat);
	if (peer) {
		Writer out;
		out.u64(id);
		out.string(text);
		send(*peer, Packet::Rejected, out);
	} else if (pending && pending->transaction.id == id) {
		pending.reset();
	}
	log("Edit rejected: " + text);
	needsRefresh = true;
}

void MultiplayerSession::onTimer(wxTimerEvent&) {
	if (!running) {
		return;
	}
	const auto now = nowMs();
	std::erase_if(peers, [](const auto& pair) { return pair.second->dead; });
	if (!hosting && peers.empty() && reconnectAt && now >= reconnectAt) {
		try {
			connectClient();
		} catch (const std::exception& e) {
			log(e.what());
			reconnectAt = now + reconnectDelay;
		}
	}
	const bool heartbeat = now - lastHeartbeat >= 5000;
	if (heartbeat) {
		lastHeartbeat = now;
	}
	for (auto& [socket, peer] : peers) {
		if (peer->dead) {
			continue;
		}
		try {
			if (!editDepth) {
				unsigned budget = 16;
				while (!peer->dead && !peer->deferred.empty() && budget--) {
					auto bytes = std::move(peer->deferred.front());
					peer->deferred.pop_front();
					peer->deferredBytes -= bytes.size();
					process(*peer, bytes);
				}
				if (peer->dead) {
					continue;
				}
				if (hosting) {
					pumpSnapshot(*peer);
					finishCatchup(*peer);
				}
			}
			pumpTransactions(*peer);
			write(*peer);
			if (heartbeat && peer->authenticated && !peer->pingSent) {
				Writer ping;
				peer->pingSent = now;
				ping.u64(now);
				send(*peer, Packet::Ping, ping);
			}
			if (now - peer->lastReceived > (peer->authenticated ? IdleTimeout : 15000)) {
				drop(*peer, "Connection timed out.");
			}
			if (peer->receivingSize && now - peer->receivingSince > 60000 && !editDepth) {
				drop(*peer, "Transaction transfer timed out.");
			}
		} catch (const std::exception& e) {
			try {
				Writer failure;
				failure.string(std::string(e.what()).substr(0, MaxChat));
				send(*peer, Packet::Disconnect, failure);
				write(*peer);
			} catch (...) { }
			drop(*peer, e.what(), !hosting);
		}
	}
	if (hosting && regionLocks.expire(now)) {
		sendLocks();
	}
	if (heartbeat) {
		for (const auto& [id, region] : heldLocks) {
			if (hosting) {
				regionLocks.acquire(localId, id, region, now);
			} else {
				for (auto& [socket, peer] : peers) {
					if (peer->authenticated && !peer->dead) {
						Writer out;
						out.u64(id);
						writeRegion(out, region);
						send(*peer, Packet::Lock, out);
					}
				}
			}
		}
		if (hosting) {
			sendPlayers();
			sendLocks();
		}
		if (!editDepth && hosting && options.autosaveMinutes && now - lastBackup >= uint64_t(options.autosaveMinutes) * 60000) {
			saveBackup();
		}
	}
	if (ready && (cursorDirty || heartbeat) && now - lastCursorSent >= 50) {
		lastCursorSent = now;
		cursorDirty = false;
		auto p = participants.find(localId);
		const bool afk = now - lastActivity >= 120000;
		if (p != participants.end()) {
			p->second.cursor = cursor;
			p->second.afk = afk;
		}
		Writer out;
		if (hosting) {
			out.u32(localId);
		}
		out.u64(keyOf(cursor));
		out.u8(afk);
		if (hosting) {
			broadcast(Packet::Cursor, out);
		} else {
			for (auto& [socket, peer] : peers) {
				if (peer->authenticated && !peer->dead) {
					send(*peer, Packet::Cursor, out);
				}
			}
		}
	}
	if (std::erase_if(locationPings, [now](const auto& ping) { return ping.expires <= now; })) {
		needsRefresh = true;
	}
	if (!editDepth && needsRefresh && now - lastRefresh >= 100) {
		lastRefresh = now;
		needsRefresh = false;
		refresh();
	}
	if (window && now - lastRefresh < 60) {
		window->update();
	}
}
void MultiplayerSession::log(const std::string& message) {
	logLines.push_back(message);
	while (logLines.size() > 500) {
		logLines.pop_front();
	}
	if (g_gui.root) {
		g_gui.SetStatusText(wxstr("[Multiplayer] " + message));
	}
	needsRefresh = true;
}
void MultiplayerSession::refresh() {
	g_gui.RefreshView();
	g_gui.RefreshPalettes();
	g_gui.UpdateMenus();
	g_gui.UpdateTitle();
	if (window) {
		window->update();
	}
}
void MultiplayerSession::showWindow() {
	if (!window) {
		window = new MultiplayerWindow(g_gui.root, *this);
	}
	window->Show();
	window->Raise();
	window->update();
}
void MultiplayerSession::updateCursor(const Position& position) {
	if (!position.isValid()) {
		return;
	}
	lastActivity = nowMs();
	if (position == cursor) {
		return;
	}
	cursor = position;
	cursorDirty = true;
}
void MultiplayerSession::sendChat(const std::string& text) {
	if (!running || !validText(text, MaxChat)) {
		log("Chat messages must contain 1 to 1024 bytes of valid text.");
		return;
	}
	if (hosting) {
		Writer out;
		out.string(options.name, MaxName);
		out.string(text, MaxChat);
		broadcast(Packet::Chat, out);
		log(options.name + ": " + text);
	} else {
		auto p = participants.find(localId);
		if (p == participants.end() || p->second.role == Role::Viewer) {
			return;
		}
		Writer out;
		out.string(text, MaxChat);
		for (auto& [socket, peer] : peers) {
			if (peer->authenticated && !peer->dead) {
				send(*peer, Packet::Chat, out);
			}
		}
	}
	lastActivity = nowMs();
}
void MultiplayerSession::pingLocation(const Position& position) {
	if (!ready || !position.isValid()) {
		return;
	}
	Writer out;
	if (hosting) {
		out.u32(localId);
		locationPings.push_back({ position, playerColor(localId), options.name, nowMs() + 5000 });
	}
	out.u64(keyOf(position));
	if (hosting) {
		broadcast(Packet::LocationPing, out);
	} else {
		for (auto& [socket, peer] : peers) {
			if (peer->authenticated && !peer->dead) {
				send(*peer, Packet::LocationPing, out);
			}
		}
	}
	needsRefresh = true;
}
uint64_t MultiplayerSession::lockRegion(const Region& region) {
	if (!canEdit() || !region.valid()) {
		return 0;
	}
	const auto id = nextLock++;
	if (hosting) {
		if (!regionLocks.acquire(localId, id, region, nowMs())) {
			return 0;
		}
		sendLocks();
	} else {
		for (auto& [socket, peer] : peers) {
			if (peer->authenticated && !peer->dead) {
				Writer out;
				out.u64(id);
				writeRegion(out, region);
				send(*peer, Packet::Lock, out);
			}
		}
	}
	heldLocks[id] = region;
	return id;
}
void MultiplayerSession::unlockRegion(uint64_t id) {
	heldLocks.erase(id);
	if (hosting) {
		regionLocks.release(localId, id);
		sendLocks();
	} else {
		for (auto& [socket, peer] : peers) {
			if (peer->authenticated && !peer->dead) {
				Writer out;
				out.u64(id);
				send(*peer, Packet::Unlock, out);
			}
		}
	}
}
bool MultiplayerSession::ownsLock(uint64_t id) const {
	for (const auto& lock : visibleLocks) {
		if (lock.id == id && lock.owner == localId && lock.expires > nowMs()) {
			return true;
		}
	}
	return false;
}
void MultiplayerSession::sendPlayers() {
	if (!hosting) {
		return;
	}
	Writer out;
	out.u8(static_cast<uint8_t>(participants.size()));
	for (const auto& [id, p] : participants) {
		out.u32(id);
		out.string(p.name, MaxName);
		out.u8(static_cast<uint8_t>(p.role));
		out.u64(keyOf(p.cursor));
		out.u8(p.afk);
		out.u32(p.latency);
	}
	broadcast(Packet::Players, out);
	needsRefresh = true;
}
void MultiplayerSession::sendLocks() {
	if (!hosting) {
		return;
	}
	visibleLocks = regionLocks.all();
	Writer out;
	out.u16(static_cast<uint16_t>(visibleLocks.size()));
	auto now = nowMs();
	for (const auto& l : visibleLocks) {
		out.u64(l.id);
		out.u32(l.owner);
		writeRegion(out, l.region);
		out.u32(static_cast<uint32_t>(l.expires > now ? l.expires - now : 0));
	}
	broadcast(Packet::Locks, out);
	needsRefresh = true;
}
void MultiplayerSession::changeRole(uint32_t id, Role role) {
	if (!hosting || id == 0 || role == Role::Host) {
		return;
	}
	for (auto& [socket, peer] : peers) {
		if (peer->id == id && !peer->dead) {
			peer->role = role;
			participants[id].role = role;
			if (role != Role::Editor) {
				regionLocks.releaseOwner(id);
			}
			log(peer->name + " is now " + roleName(role) + ".");
			sendPlayers();
			sendLocks();
			return;
		}
	}
}
void MultiplayerSession::kick(uint32_t id) {
	if (!hosting || id == 0) {
		return;
	}
	for (auto& [socket, peer] : peers) {
		if (peer->id == id && !peer->dead) {
			Writer out;
			out.string("Disconnected by the host.");
			send(*peer, Packet::Disconnect, out);
			write(*peer);
			drop(*peer, "Disconnected by the host.", false);
			return;
		}
	}
}
void MultiplayerSession::approve(uint64_t id, bool accepted, uint16_t replacementUniqueId) {
	if (!hosting) {
		return;
	}
	auto found = approvals.find(id);
	if (found == approvals.end()) {
		return;
	}
	auto tx = std::move(found->second.transaction);
	approvals.erase(found);
	Peer* sender = nullptr;
	for (auto& [socket, peer] : peers) {
		if (peer->id == tx.author && !peer->dead) {
			sender = peer.get();
			break;
		}
	}
	if (!sender) {
		log("Requester disconnected; approval expired.");
		return;
	}
	if (!accepted) {
		reject(sender, tx.id, "Host rejected the requested change.");
		return;
	}
	try {
		if (replacementUniqueId) {
			if (editor.map.hasUniqueId(replacementUniqueId)) {
				throw Error("Replacement Unique ID is already in use.");
			}
			unsigned replaced = 0;
			for (auto& edit : tx.tiles) {
				auto tile = decodeTile(editor.map, edit.key, edit.after);
				auto replace = [&](auto&& self, Item* item) -> void {
					if (!item) {
						return;
					}
					if (item->getUniqueID()) {
						item->setUniqueID(replacementUniqueId);
						++replaced;
					}
					if (auto* container = dynamic_cast<Container*>(item)) {
						for (auto* child : container->getVector()) {
							self(self, child);
						}
					}
				};
				replace(replace, tile->ground);
				for (auto* item : tile->items) {
					replace(replace, item);
				}
				edit.after = encodeTile(tile.get());
			}
			if (replaced != 1) {
				throw Error("Approve with a new Unique ID requires exactly one unique item in this request.");
			}
		}
		accept(sender, std::move(tx), true);
	} catch (const std::exception& e) {
		reject(sender, tx.id, e.what());
	}
}
void MultiplayerSession::requestResync() {
	if (!running || hosting || receivingSnapshot) {
		return;
	}
	ready = false;
	connectionStatus = "Resynchronizing";
	for (auto& [socket, peer] : peers) {
		if (peer->authenticated && !peer->dead) {
			Writer out;
			out.u64(acknowledgedRevision);
			out.u8(0);
			send(*peer, Packet::Ready, out);
		}
	}
}
void MultiplayerSession::saveBackup() {
	lastBackup = nowMs();
	if (!hosting || !editor.map.hasFile() || !editor.map.hasChanged()) {
		return;
	}
	try {
		auto path = std::filesystem::path(editor.map.getFilename());
		auto directory = path.parent_path() / "multiplayer-backups";
		std::filesystem::create_directories(directory);
		auto stamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		auto base = path.stem().string() + "-r" + std::to_string(revisions.current()) + "-" + std::to_string(stamp);
		auto backup = directory / (base + ".otbm");
		// Backups have independent sidecars and do not change the open document's path.
		const auto oldSpawn = editor.map.spawnfile, oldNpc = editor.map.spawnNpcFile, oldHouse = editor.map.housefile, oldWaypoint = editor.map.waypointfile, oldZone = editor.map.zonefile;
		editor.map.spawnfile = base + "-spawn.xml";
		editor.map.spawnNpcFile = base + "-npc.xml";
		editor.map.housefile = base + "-house.xml";
		editor.map.waypointfile = base + "-waypoint.xml";
		editor.map.zonefile = base + "-zones.xml";
		bool saved = false;
		try {
			IOMapOTBM writer(editor.map.getVersion());
			saved = writer.saveMap(editor.map, wxstr(backup.string()));
		} catch (...) {
			editor.map.spawnfile = oldSpawn;
			editor.map.spawnNpcFile = oldNpc;
			editor.map.housefile = oldHouse;
			editor.map.waypointfile = oldWaypoint;
			editor.map.zonefile = oldZone;
			throw;
		}
		editor.map.spawnfile = oldSpawn;
		editor.map.spawnNpcFile = oldNpc;
		editor.map.housefile = oldHouse;
		editor.map.waypointfile = oldWaypoint;
		editor.map.zonefile = oldZone;
		log(saved ? "Multiplayer backup saved: " + backup.string() : "Multiplayer backup could not be saved.");
	} catch (const std::exception& e) {
		log(std::string("Backup failed: ") + e.what());
	}
}
MultiplayerSession::MetadataEdit::MetadataEdit(Map* map, House* affectedHouse) {
	auto* live = MultiplayerSession::current();
	if (!live || &live->editor.map != map || live->applying) {
		return;
	}
	if (!live->canEdit()) {
		permitted = false;
		live->log("Wait for synchronization or ask the host for edit permission.");
		return;
	}
	try {
		before = encodeMetadata(*map);
		base = live->revision();
		if (affectedHouse) {
			for (const auto& pos : affectedHouse->getTilePositions()) {
				tiles.emplace(keyOf(pos), encodeTile(map->getTile(pos)));
			}
		}
		session = live;
		++session->editDepth;
	} catch (const std::exception& e) {
		permitted = false;
		live->log(e.what());
	}
}
MultiplayerSession::MetadataEdit::~MetadataEdit() {
	if (!session) {
		return;
	}
	session->finishMetadataEdit(before, tiles, base);
	if (session->editDepth) {
		--session->editDepth;
	}
}
void MultiplayerSession::finishMetadataEdit(const Bytes& before, const std::map<uint64_t, Bytes>& oldTiles, uint64_t base) {
	try {
		auto after = encodeMetadata(editor.map);
		Transaction tx;
		tx.base = base;
		History entry;
		if (after != before) {
			tx.metadata = after;
			tx.metadataBefore = sha256(before);
			entry.inverse.metadata = before;
			entry.inverse.metadataBefore = sha256(after);
		}
		for (const auto& [key, bytes] : oldTiles) {
			auto value = encodeTile(editor.map.getTile(posOf(key)));
			if (value != bytes) {
				tx.tiles.push_back({ key, sha256(bytes), value });
				entry.inverse.tiles.push_back({ key, sha256(value), bytes });
			}
		}
		if (tx.tiles.empty() && tx.metadata.empty()) {
			return;
		}
		Transaction restore = entry.inverse;
		apply(restore);
		entry.forward = tx;
		entry.bytes = encodeTransaction(tx).size() + encodeTransaction(entry.inverse).size();
		submit(std::move(tx), PendingKind::Edit, std::move(entry));
	} catch (const std::exception& e) {
		log(std::string("Metadata edit failed: ") + e.what());
	}
}
MultiplayerSession::PropertyEdit::PropertyEdit(Map* map, const Position& position) {
	auto* live = MultiplayerSession::current();
	if (!live || &live->editor.map != map || live->applying) {
		return;
	}
	if (!live->canEdit()) {
		permitted = false;
		live->log("Properties are unavailable until synchronization/approval finishes.");
		return;
	}
	Region region { static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), static_cast<uint16_t>(position.x), static_cast<uint16_t>(position.y), static_cast<uint8_t>(position.z) };
	lock = live->lockRegion(region);
	const auto deadline = nowMs() + 5000;
	while (lock && live->running && !live->ownsLock(lock) && nowMs() < deadline) {
		wxYieldIfNeeded();
		wxMilliSleep(10);
	}
	if (!lock || !live->ownsLock(lock)) {
		if (lock) {
			live->unlockRegion(lock);
		}
		permitted = false;
		live->log("Could not acquire this tile's property lock.");
		return;
	}
	session = live;
	++session->editDepth;
}
MultiplayerSession::PropertyEdit::~PropertyEdit() {
	if (!session) {
		return;
	}
	if (session->pending) {
		session->unlockAfterAck.push_back(lock);
	} else {
		session->unlockRegion(lock);
	}
	if (session->editDepth) {
		--session->editDepth;
	}
}
