// SPDX-License-Identifier: GPL-3.0-or-later
// Full production Session/codec/ActionQueue over real nonblocking wxSockets.
// Only resource loading and the single-session UI policy are replaced by fixtures.
#include "main.h"
#include "action.h"
#include "complexitem.h"
#include "editor.h"
#include "copybuffer.h"
#include "settings.h"
#include "spawn.h"
#include "multiplayer_session.h"
#include "multiplayer_peer.h"
#include "multiplayer_codec.h"
#include "multiplayer_crypto.h"
#include "welcome_dialog.h"
#include <wx/evtloop.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>

Editor::Editor(CopyBuffer& buffer, std::nullptr_t) :
	actionQueue(new ActionQueue(*this)), selection(*this), copybuffer(buffer), replace_brush(nullptr) {
	map.mapVersion = { MAP_OTBM_4, static_cast<ClientVersionID>(17) };
	map.setWidth(512);
	map.setHeight(512);
	map.setName("Socket test fixture");
	map.unnamed = true;
}

class MultiplayerSessionTests {
	using Clock = std::chrono::steady_clock;
	static void check(bool value, const char* text) {
		if (!value) {
			throw std::runtime_error(text);
		}
	}
	static void pumpUntil(const std::function<bool()>& done, int timeout = 8000) {
		const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
		while (!done()) {
			check(Clock::now() < deadline, "Timed out waiting for session test condition");
			wxTheApp->ProcessPendingEvents();
			wxEventLoopBase::GetActive()->DispatchTimeout(10);
			wxTheApp->ProcessIdle();
		}
	}
	static bool hasMessage(const MultiplayerSession& session, const std::string& text) {
		for (const auto& line : session.messages()) {
			if (line.find(text) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
	static uint16_t unusedPort() {
		wxIPV4address address;
		address.LocalHost();
		address.Service(0);
		wxSocketServer probe(address, wxSOCKET_NOWAIT);
		check(probe.IsOk() && probe.GetLocal(address), "Cannot reserve test port");
		return address.Service();
	}
	struct Fixture {
		CopyBuffer buffer;
		Editor hostEditor { buffer, nullptr }, clientEditor { buffer, nullptr };
		MultiplayerSession::Options options;
		explicit Fixture(size_t tiles = 1, const std::function<void(Fixture&)>& setup = {}) {
			for (size_t index = 0; index < tiles; ++index) {
				const Position p(100 + index % 256, 100 + index / 256, 7);
				auto* tile = hostEditor.map.allocator(hostEditor.map.createTileL(p));
				tile->setMapFlags(TILESTATE_PROTECTIONZONE);
				hostEditor.map.setTile(p, tile);
			}
			options.address = "127.0.0.1";
			options.port = unusedPort();
			options.password = "socket-fixture-password";
			options.name = "Host fixture";
			options.maxPlayers = 2;
			options.autosaveMinutes = 0;
			options.approvals = false;
			hostEditor.multiplayer = std::make_unique<MultiplayerSession>(hostEditor);
			clientEditor.multiplayer = std::make_unique<MultiplayerSession>(clientEditor);
			MultiplayerSession::activateForTests(nullptr);
			if (setup) {
				setup(*this);
			}
			std::string error;
			check(host().host(options, error), error.c_str());
		}
		MultiplayerSession& host() {
			return *hostEditor.multiplayer;
		}
		MultiplayerSession& client() {
			return *clientEditor.multiplayer;
		}
		void join() {
			MultiplayerSession::activateForTests(nullptr);
			std::string error;
			auto joinOptions = options;
			joinOptions.name = "Client fixture";
			check(client().join(joinOptions, error), error.c_str());
		}
		void connected() {
			join();
			pumpUntil([&] { return client().status() == "Connected"; });
			check(host().players().size() == 2, "Host is missing the authenticated participant");
			check(clientEditor.map.getTileCount() == hostEditor.map.getTileCount(), "Incomplete snapshot");
			check(hasMessage(client(), "Authenticating") && hasMessage(client(), "Synchronizing") && hasMessage(client(), "Map synchronization started."), "Missing handshake transitions");
		}
		void refused(const std::string& reason) {
			pumpUntil([&] { return client().status().starts_with("Disconnected:"); });
			check(client().status().find(reason) != std::string::npos, "Wrong connection refusal reason");
		}
	};
	using Peer = MultiplayerSession::Peer;
	static constexpr uint16_t PlainItem = 100, ContainerItem = 101, TeleportItem = 103;
	static void defineItems() {
		const auto define = [](uint16_t id, ItemTypes_t kind) {
			auto* type = new ItemType();
			type->id = type->clientID = id;
			type->name = "Test item";
			type->type = kind;
			g_items.items.set(id, type);
		};
		define(PlainItem, ITEM_TYPE_NONE);
		define(ContainerItem, ITEM_TYPE_CONTAINER);
		define(TeleportItem, ITEM_TYPE_TELEPORT);
	}
	static Peer& onlyPeer(MultiplayerSession& session) {
		check(session.peers.size() == 1, "Expected exactly one peer");
		return *session.peers.begin()->second;
	}
	static bool noLivePeers(const MultiplayerSession& session) {
		return std::all_of(session.peers.begin(), session.peers.end(), [](const auto& entry) { return entry.second->dead; });
	}
	static Item* uniqueItem(uint16_t uid) {
		auto* item = Item::Create(PlainItem);
		item->setUniqueID(uid);
		return item;
	}
	static void placeTile(Map& map, const Position& position, const std::function<void(Tile&)>& edit) {
		auto* tile = map.getTile(position);
		if (!tile) {
			tile = map.allocator(map.createTileL(position));
			edit(*tile);
			map.setTile(position, tile);
		} else {
			edit(*tile);
		}
	}
	static Multiplayer::Transaction tileEdit(MultiplayerSession& session, const Position& position, const std::function<void(Tile&)>& edit) {
		auto& map = session.editor.map;
		const auto* tile = map.getTile(position);
		std::unique_ptr<Tile> changed(tile ? map.getTile(position)->deepCopy(map) : new Tile(position.x, position.y, position.z));
		edit(*changed);
		Multiplayer::Transaction tx;
		tx.base = session.revision();
		tx.tiles.push_back({ Multiplayer::tileKey(position.x, position.y, position.z), Multiplayer::sha256(Multiplayer::encodeTile(tile)), Multiplayer::encodeTile(changed.get()) });
		return tx;
	}
	static void editTile(MultiplayerSession& session, const Position& position, const std::function<void(Tile&)>& edit) {
		session.submit(tileEdit(session, position, edit));
	}
	static void writeRaw(wxSocketBase& socket, const Multiplayer::Bytes& bytes) {
		socket.Write(bytes.data(), static_cast<wxUint32>(bytes.size()));
		check(socket.LastWriteCount() == bytes.size(), "Raw test write was truncated");
	}
	static Multiplayer::Bytes readRaw(wxSocketBase& socket, Multiplayer::FrameReader& frames) {
		Multiplayer::Bytes result;
		pumpUntil([&] {
			std::array<uint8_t, 4096> buffer;
			while (true) {
				socket.Read(buffer.data(), static_cast<wxUint32>(std::min(buffer.size(), frames.needed())));
				const auto got = socket.LastReadCount();
				if (!got) {
					return false;
				}
				frames.append(std::span(buffer).first(got));
				if (frames.ready()) {
					result = frames.take();
					return true;
				}
			}
		});
		return result;
	}
	static wxSocketClient* rawConnect(uint16_t port) {
		wxIPV4address address;
		address.Hostname("127.0.0.1");
		address.Service(port);
		auto* socket = new wxSocketClient(wxSOCKET_NOWAIT);
		socket->Notify(false);
		check(socket->Connect(address, true), "Raw test connection failed");
		return socket;
	}
	// Stands in for a host that does not know the session password.
	struct RogueHost {
		wxSocketServer server;
		wxSocketBase* socket = nullptr;
		Multiplayer::FrameReader frames;
		Multiplayer::Identity session = Multiplayer::randomIdentity();
		Multiplayer::Bytes nonce;
		static wxIPV4address local(uint16_t port) {
			wxIPV4address address;
			address.Hostname("127.0.0.1");
			address.Service(port);
			return address;
		}
		explicit RogueHost(uint16_t port) :
			server(local(port), wxSOCKET_NOWAIT | wxSOCKET_REUSEADDR) {
			check(server.IsOk(), "Rogue host cannot listen");
			const auto a = Multiplayer::randomIdentity(), b = Multiplayer::randomIdentity();
			nonce.assign(a.begin(), a.end());
			nonce.insert(nonce.end(), b.begin(), b.end());
		}
		~RogueHost() {
			if (socket) {
				socket->Destroy();
			}
		}
		void acceptClient() {
			pumpUntil([&] { socket = server.Accept(false); return socket != nullptr; });
			socket->Notify(false);
			socket->SetFlags(wxSOCKET_NOWAIT);
		}
		void sendChallenge() {
			Multiplayer::Writer out;
			out.u32(Multiplayer::ProtocolVersion);
			out.raw(session);
			out.raw(nonce);
			writeRaw(*socket, Multiplayer::frame(Multiplayer::Packet::Challenge, out.data));
		}
		Multiplayer::Bytes readHelloBody(Multiplayer::Digest* proof = nullptr) {
			const auto hello = readRaw(*socket, frames);
			check(hello[0] == uint8_t(Multiplayer::Packet::Hello), "Client did not answer with Hello");
			Multiplayer::Reader in(hello);
			in.u8();
			auto body = in.bytes(4096);
			const auto received = in.fixed<32>();
			if (proof) {
				*proof = received;
			}
			return body;
		}
	};
	static Multiplayer::Writer forgedWelcome(uint64_t generation) {
		Multiplayer::Writer out;
		out.u32(7);
		out.u8(static_cast<uint8_t>(Multiplayer::Role::Editor));
		out.u64(generation);
		out.u64(0);
		out.string("Rogue", Multiplayer::MaxName);
		out.raw(Multiplayer::randomIdentity());
		out.raw(Multiplayer::Digest {});
		return out;
	}
	static void loseConnection(MultiplayerSession& session) {
		for (auto& [socket, peer] : session.peers) {
			session.drop(*peer, "Test network interruption", true);
		}
	}
	static void changeTile(MultiplayerSession& session, const Position& pos, uint32_t flags) {
		Multiplayer::Transaction tx;
		tx.base = session.revision();
		auto* tile = session.editor.map.getTile(pos);
		auto changed = std::unique_ptr<Tile>(tile->deepCopy(session.editor.map));
		changed->setMapFlags(flags);
		tx.tiles.push_back({ Multiplayer::tileKey(pos.x, pos.y, pos.z), Multiplayer::sha256(Multiplayer::encodeTile(tile)), Multiplayer::encodeTile(changed.get()) });
		session.submit(std::move(tx));
	}

	static void hostingPolicyTests() {
		CopyBuffer buffer;
		Editor editor(buffer, nullptr);
		editor.multiplayer = std::make_unique<MultiplayerSession>(editor);
		MultiplayerSession::activateForTests(nullptr);
		MultiplayerSession::Options options;
		options.port = unusedPort();
		for (const std::string password : { "", "1234567" }) {
			options.password = password;
			std::string error;
			check(!editor.multiplayer->host(options, error) && error.find("at least 8") != std::string::npos, "Host accepted a missing or short password");
			check(!editor.multiplayer->active() && !editor.multiplayer->listener, "Refused host left a listener behind");
		}
		{
			Fixture f(1, [](Fixture& f) {
				f.options.password.clear();
				f.options.loopbackOnly = true;
			});
			wxIPV4address local;
			check(f.host().listener->GetLocal(local) && local.IPAddress() == "127.0.0.1", "Loopback session bound to a public interface");
			f.connected();
		}
		{
			Fixture f;
			wxIPV4address local;
			check(f.host().listener->GetLocal(local) && local.IPAddress() != "127.0.0.1", "Normal session is not reachable from the LAN");
			check(f.host().options.password.empty(), "Host kept the password after deriving the key");
		}
		std::cout << "PASS empty and short password refusal, loopback-only session, no stored host password\n";
	}
	static void hostProofTests() {
		for (const std::string attack : { "plain welcome", "unknown key", "right key, wrong proof" }) {
			Fixture f;
			RogueHost rogue(f.options.port = unusedPort());
			f.join();
			rogue.acceptClient();
			rogue.sendChallenge();
			Multiplayer::Digest proof;
			const auto body = rogue.readHelloBody(&proof);
			const auto forged = forgedWelcome(f.client().generation);
			if (attack == "plain welcome") {
				writeRaw(*rogue.socket, Multiplayer::frame(Multiplayer::Packet::Welcome, forged.data));
			} else if (attack == "unknown key") {
				const auto key = Multiplayer::deriveSessionKey("not the password", rogue.session);
				writeRaw(*rogue.socket, Multiplayer::sealFrame(Multiplayer::deriveHandshake(key, rogue.session, rogue.nonce, body).hostToClient, 0, Multiplayer::Packet::Welcome, forged.data));
			} else {
				const auto keys = Multiplayer::deriveHandshake(Multiplayer::deriveSessionKey(f.options.password, rogue.session), rogue.session, rogue.nonce, body);
				check(Multiplayer::secureEqual(proof, keys.clientProof), "Client proof does not match the shared derivation");
				writeRaw(*rogue.socket, Multiplayer::sealFrame(keys.hostToClient, 0, Multiplayer::Packet::Welcome, forged.data));
			}
			f.refused("could not prove");
			check(!f.client().localId && f.client().session == Multiplayer::Identity {} && !f.client().peers.empty(), "Client trusted an unproven host");
			std::cout << "PASS host proof: " << attack << '\n';
		}
		{
			// A failed proof must leave the session the client already synchronized with untouched.
			Fixture f;
			f.connected();
			const auto synchronized = f.client().session;
			f.client().pending = MultiplayerSession::Pending { {}, MultiplayerSession::PendingKind::Edit, {} };
			RogueHost rogue(f.options.port = unusedPort());
			f.client().options.port = f.options.port;
			loseConnection(f.client());
			rogue.acceptClient();
			rogue.sendChallenge();
			rogue.readHelloBody();
			writeRaw(*rogue.socket, Multiplayer::frame(Multiplayer::Packet::Welcome, forgedWelcome(f.client().generation).data));
			f.refused("could not prove");
			check(f.client().session == synchronized && f.client().pending.has_value(), "Unproven host reset client state");
			f.client().pending.reset();
			std::cout << "PASS unproven host cannot reset a synchronized client\n";
		}
		{
			// Relay: the attacker passes the real Challenge to a victim and its Hello back.
			Fixture f;
			const auto realPort = f.options.port;
			RogueHost rogue(f.options.port = unusedPort());
			auto* attacker = rawConnect(realPort);
			Multiplayer::FrameReader frames;
			const auto challenge = readRaw(*attacker, frames);
			f.join();
			rogue.acceptClient();
			Multiplayer::Writer relayed;
			relayed.raw(std::span(challenge).subspan(1));
			writeRaw(*rogue.socket, Multiplayer::frame(Multiplayer::Packet::Challenge, relayed.data));
			const auto hello = readRaw(*rogue.socket, rogue.frames);
			Multiplayer::Writer forwarded;
			forwarded.raw(std::span(hello).subspan(1));
			writeRaw(*attacker, Multiplayer::frame(Multiplayer::Packet::Hello, forwarded.data));
			const auto welcome = readRaw(*attacker, frames);
			check(welcome[0] == uint8_t(Multiplayer::Packet::Welcome) && f.host().players().size() == 2, "Relay fixture did not reach authentication");
			Multiplayer::Writer injected;
			injected.string("relayed chat");
			writeRaw(*attacker, Multiplayer::frame(Multiplayer::Packet::Chat, injected.data));
			pumpUntil([&] { return f.host().players().size() == 1; });
			check(hasMessage(f.host(), "failed authentication") && !hasMessage(f.host(), "relayed chat"), "Relayed login could inject a frame");
			attacker->Destroy();
			std::cout << "PASS relayed login holds no usable session\n";
		}
	}
	static void frameAuthenticationTests() {
		const auto waitForDrop = [](Fixture& f) {
			pumpUntil([&] { return f.host().players().size() == 1; });
			check(hasMessage(f.host(), "failed authentication"), "Host did not report the frame failure");
			pumpUntil([&] { return f.client().status().starts_with("Disconnected:"); });
		};
		{
			Fixture f;
			f.connected();
			auto& peer = onlyPeer(f.client());
			auto& hostSide = onlyPeer(f.host());
			pumpUntil([&] { return peer.writes.empty(); });
			Multiplayer::Writer chat;
			chat.string("sent once");
			f.client().send(peer, Multiplayer::Packet::Chat, chat);
			const auto recorded = peer.writes.back();
			pumpUntil([&] { return hasMessage(f.host(), "sent once"); });
			check(hostSide.receiveSequence > 0, "Host did not authenticate frames");
			writeRaw(*peer.socket, recorded);
			waitForDrop(f);
			std::cout << "PASS replayed frame rejected\n";
		}
		{
			Fixture f;
			f.connected();
			auto& peer = onlyPeer(f.client());
			pumpUntil([&] { return peer.writes.empty(); });
			Multiplayer::Writer chat;
			chat.string("tampered chat");
			f.client().send(peer, Multiplayer::Packet::Chat, chat);
			auto forged = std::move(peer.writes.back());
			peer.writes.pop_back();
			peer.pendingBytes -= forged.size();
			--peer.sendSequence;
			forged[10] ^= 1;
			writeRaw(*peer.socket, forged);
			waitForDrop(f);
			check(!hasMessage(f.host(), "tampered chat"), "Tampered frame reached the session");
			std::cout << "PASS tampered frame rejected before processing\n";
		}
		{
			Fixture f;
			f.connected();
			auto& peer = onlyPeer(f.client());
			pumpUntil([&] { return peer.writes.empty(); });
			auto tx = tileEdit(f.client(), { 100, 100, 7 }, [](Tile& tile) { tile.setMapFlags(TILESTATE_NOPVP); });
			tx.session = f.client().session;
			tx.id = 1;
			tx.author = f.client().localId;
			const auto bytes = Multiplayer::encodeTransaction(tx);
			Multiplayer::Writer begin;
			begin.u32(static_cast<uint32_t>(bytes.size()));
			begin.raw(Multiplayer::sha256(bytes));
			writeRaw(*peer.socket, Multiplayer::frame(Multiplayer::Packet::TransactionBegin, begin.data));
			waitForDrop(f);
			check(f.host().revision() == 0, "Injected transaction frame was processed");
			std::cout << "PASS injected unsealed transaction frame rejected\n";
		}
		for (const bool partial : { false, true }) {
			// closeSocket discards queued frames, so the Disconnect must still verify.
			Fixture f;
			f.connected();
			auto& hostSide = onlyPeer(f.host());
			pumpUntil([&] { return hostSide.writes.empty(); });
			for (int i = 0; i < 3; ++i) {
				Multiplayer::Writer chat;
				chat.string("Host");
				chat.string("queued");
				f.host().send(hostSide, Multiplayer::Packet::Chat, chat);
			}
			if (partial) {
				auto& front = hostSide.writes.front();
				hostSide.socket->Write(front.data(), 3);
				hostSide.writeOffset = 3;
				hostSide.pendingBytes -= 3;
			}
			f.host().kick(f.client().clientId());
			f.refused("Disconnected by the host.");
			std::cout << "PASS sealed Disconnect after discarding queued frames" << (partial ? " (partly written frame)" : "") << '\n';
		}
	}
	static void admissionTests() {
		{
			Fixture f;
			std::vector<wxSocketClient*> idle;
			for (size_t i = 0; i < 4; ++i) {
				idle.push_back(rawConnect(f.options.port));
			}
			pumpUntil([&] { return f.host().peers.size() == 4; });
			auto* extra = rawConnect(f.options.port);
			Multiplayer::FrameReader frames;
			const auto refusal = readRaw(*extra, frames);
			check(refusal[0] == uint8_t(Multiplayer::Packet::Disconnect) && f.host().peers.size() == 4, "Pending limit per source was not enforced");
			// The peers keep "talking", so only an absolute deadline can remove them.
			for (auto& [socket, peer] : f.host().peers) {
				peer->created -= 16000;
				peer->lastReceived = nowMs();
			}
			pumpUntil([&] { return noLivePeers(f.host()); });
			extra->Destroy();
			for (auto* socket : idle) {
				socket->Destroy();
			}
			f.connected();
			pumpUntil([&] { return hasMessage(f.host(), "before authentication"); });
			std::cout << "PASS pending handshake limit, absolute deadline, player slot unaffected\n";
		}
		{
			Fixture f;
			auto joinWith = [&](const std::string& password) {
				f.client().disconnect();
				f.options.password = password;
				f.join();
			};
			joinWith("wrong password");
			f.refused("Incorrect session password");
			check(f.host().loginBackoff.size() == 1, "Failed login left no backoff entry");
			joinWith("socket-fixture-password");
			f.refused("Too many login attempts");
			f.host().loginBackoff.begin()->second.blockedUntil = 0;
			joinWith("socket-fixture-password");
			pumpUntil([&] { return f.client().status() == "Connected"; });
			check(f.host().loginBackoff.empty(), "Successful login kept its backoff");
			std::cout << "PASS per-source login backoff\n";
		}
		{
			Fixture f;
			f.host().loginWindowStart = nowMs();
			f.host().loginWindowCount = 1000;
			f.join();
			f.refused("Too many login attempts");
			check(f.host().players().size() == 1, "Global login limit let a client in");
			std::cout << "PASS global login rate limit\n";
		}
		{
			Fixture f;
			auto* stranger = rawConnect(f.options.port);
			Multiplayer::FrameReader frames;
			readRaw(*stranger, frames);
			Multiplayer::Writer reason;
			reason.string("forged line\nHost: everyone is banned");
			writeRaw(*stranger, Multiplayer::frame(Multiplayer::Packet::Disconnect, reason.data));
			pumpUntil([&] { return noLivePeers(f.host()); });
			check(!hasMessage(f.host(), "forged line"), "Host logged a pre-auth reason verbatim");
			stranger->Destroy();
			RogueHost rogue(f.options.port = unusedPort());
			f.join();
			rogue.acceptClient();
			reason = Multiplayer::Writer();
			reason.string("Update at evil.example\nSecond line");
			writeRaw(*rogue.socket, Multiplayer::frame(Multiplayer::Packet::Disconnect, reason.data));
			f.refused("The host refused the connection.");
			check(!hasMessage(f.client(), "evil.example"), "Client showed an unauthenticated reason verbatim");
			std::cout << "PASS pre-auth Disconnect reasons are not shown verbatim\n";
		}
	}
	static void identityTests() {
		{
			Fixture f;
			f.connected();
			const auto token = f.client().reconnectToken;
			check(token != Multiplayer::Identity {} && f.host().identities.contains(token), "Host did not mint a reconnect token");
			const auto id = f.client().clientId();
			f.host().changeRole(id, Multiplayer::Role::Viewer);
			pumpUntil([&] { return !f.client().canEdit(); });
			// A hostile client throws its token away and comes back as a stranger.
			f.client().reconnectToken = {};
			loseConnection(f.client());
			pumpUntil([&] { return f.client().status() == "Connected"; }, 15000);
			check(f.client().clientId() != id && !f.client().canEdit(), "Dropping the token undid a demotion");
			check(f.host().identities.contains(f.client().reconnectToken) && f.client().reconnectToken != token, "New identity got no fresh token");
			std::cout << "PASS demotion survives a discarded token\n";
		}
		{
			Fixture f;
			f.connected();
			f.host().kick(f.client().clientId());
			f.refused("Disconnected by the host.");
			pumpUntil([&] { return f.host().players().size() == 1; });
			Editor returning(f.buffer, nullptr);
			returning.multiplayer = std::make_unique<MultiplayerSession>(returning);
			const auto joinAgain = [&] {
				MultiplayerSession::activateForTests(nullptr);
				std::string error;
				check(returning.multiplayer->join(f.options, error), error.c_str());
			};
			joinAgain();
			pumpUntil([&] { return returning.multiplayer->status().starts_with("Disconnected:"); });
			check(returning.multiplayer->status().find("removed from this session") != std::string::npos, "Kicked user reconnected with a fresh identity");
			returning.multiplayer->disconnect();
			f.host().clearBans();
			joinAgain();
			pumpUntil([&] { return returning.multiplayer->status() == "Connected"; });
			returning.multiplayer.reset();
			pumpUntil([&] { return f.host().players().size() == 1; });
			std::cout << "PASS kick bans a fresh identity until the host clears bans\n";
		}
		{
			Fixture f;
			for (uint32_t i = 0; i < 1024; ++i) {
				f.host().identities.emplace(Multiplayer::randomIdentity(), MultiplayerSession::IdentityState { 1000 + i, 1, Multiplayer::Role::Editor, i });
			}
			f.connected();
			check(f.host().identities.size() <= 1024 && f.host().identities.contains(f.client().reconnectToken), "Identity table was not pruned");
			std::cout << "PASS full identity table no longer refuses new users\n";
		}
	}
	static void spawnTests() {
		check(g_settings.getInteger(Config::MAX_SPAWN_RADIUS) <= Multiplayer::MaxSpawnRadius, "The spawn UI allows a radius multiplayer rejects");
		const Position center(250, 250, 7);
		{
			Fixture f;
			f.connected();
			auto addSpawn = [](int radius) {
				return [radius](Tile& tile) {
					delete tile.spawn;
					tile.spawn = new Spawn(radius);
				};
			};
			editTile(f.client(), center, addSpawn(Multiplayer::MaxSpawnRadius + 1));
			pumpUntil([&] { return hasMessage(f.client(), "Spawn radius exceeds"); });
			pumpUntil([&] { return f.client().status() == "Connected"; });
			check(!f.hostEditor.map.getTile(center) && !f.hostEditor.map.getTileL(center), "Oversized spawn changed the host map");
			editTile(f.client(), center, addSpawn(Multiplayer::MaxSpawnRadius));
			pumpUntil([&] { return f.client().revision() == 1; });
			check(f.hostEditor.map.getTile(center)->spawn->getSize() == Multiplayer::MaxSpawnRadius && f.clientEditor.map.getTile(center)->spawn->getSize() == Multiplayer::MaxSpawnRadius, "The largest palette spawn did not sync");
			Multiplayer::Transaction wide;
			wide.base = f.client().revision();
			for (uint16_t i = 0; i < 47; ++i) {
				const Position position(10 + i, 400, 7);
				wide.tiles.push_back(tileEdit(f.client(), position, addSpawn(Multiplayer::MaxSpawnRadius)).tiles.front());
			}
			f.client().submit(std::move(wide));
			pumpUntil([&] { return hasMessage(f.client(), "too much spawn area"); });
			check(f.host().revision() == 1 && !f.hostEditor.map.getTileL(10, 400, 7), "Spawn area bomb reached the map");
			std::cout << "PASS spawn radius and per-transaction spawn area limits\n";
		}
		{
			// XML may load radius 255. The host's real map must still reach every client.
			Fixture f(1, [&](Fixture& f) {
				placeTile(f.hostEditor.map, center, [](Tile& tile) { tile.spawn = new Spawn(Multiplayer::MaxStoredSpawnRadius); });
				f.hostEditor.map.addSpawn(f.hostEditor.map.getTile(center));
			});
			f.connected();
			check(f.clientEditor.map.getTile(center)->spawn->getSize() == Multiplayer::MaxStoredSpawnRadius, "Large stored spawn did not sync");
			editTile(f.client(), center, [](Tile& tile) { tile.setMapFlags(TILESTATE_NOPVP); });
			pumpUntil([&] { return f.host().revision() == 1 && f.client().revision() == 1; });
			std::cout << "PASS spawns loaded with radius 255 still sync and stay editable\n";
		}
		{
			Fixture f(0);
			auto& map = f.hostEditor.map;
			const Position edge(2, 2, 7);
			placeTile(map, edge, [](Tile& tile) { tile.spawn = new Spawn(5); });
			auto* tile = map.getTile(edge);
			check(map.addSpawn(tile), "Spawn near the edge was not added");
			check(map.getTileL(0, 0, 7)->getSpawnCount() == 1 && map.getTileL(7, 7, 7)->getSpawnCount() == 1, "Edge spawn missing inside the map");
			check(!map.getTileL(65533, 2, 7) && !map.getTileL(2, 65533, 7), "Spawn wrapped around the map edge");
			map.removeSpawn(tile);
			check(map.getTileL(0, 0, 7)->getSpawnCount() == 0 && map.getTileL(7, 7, 7)->getSpawnCount() == 0, "Edge spawn removal left counts behind");
			std::cout << "PASS addSpawn skips out-of-range coordinates\n";
		}
	}
	static void uniqueIdTests() {
		const Position first(100, 100, 7), second(101, 100, 7);
		{
			CopyBuffer buffer;
			Editor editor(buffer, nullptr);
			placeTile(editor.map, first, [](Tile& tile) {
				auto* container = dynamic_cast<Container*>(Item::Create(ContainerItem));
				check(container, "Container fixture");
				container->getVector().push_back(uniqueItem(2000));
				tile.items.push_back(container);
			});
			check(editor.map.getUniqueIdCount(2000) == 1, "Load path missed a Unique ID inside a container");
			auto* replacement = editor.map.allocator(editor.map.createTileL(first));
			replacement->items.push_back(uniqueItem(2001));
			auto* batch = editor.actionQueue->createBatch(ACTION_DRAW);
			auto* action = editor.actionQueue->createAction(batch);
			action->addChange(new Change(replacement));
			batch->addAndCommitAction(action);
			editor.actionQueue->addBatch(batch);
			check(editor.map.getUniqueIdCount(2000) == 0 && editor.map.getUniqueIdCount(2001) == 1, "Commit left Unique ID counts stale");
			editor.actionQueue->undo();
			check(editor.map.getUniqueIdCount(2000) == 1 && editor.map.getUniqueIdCount(2001) == 0, "Undo left Unique ID counts stale");
			editor.actionQueue->redo();
			check(editor.map.getUniqueIdCount(2000) == 0 && editor.map.getUniqueIdCount(2001) == 1, "Redo left Unique ID counts stale");
			std::cout << "PASS Unique ID counts across commit, undo and redo, including containers\n";
		}
		{
			Fixture f(2, [](Fixture& f) { f.options.approvals = true; });
			f.connected();
			auto approveNext = [&](bool accept) {
				pumpUntil([&] { return f.host().approvalRequests().size() == 1; });
				f.host().approve(f.host().approvalRequests().begin()->first, accept);
			};
			editTile(f.client(), first, [](Tile& tile) { tile.items.push_back(uniqueItem(1234)); });
			approveNext(true);
			pumpUntil([&] { return f.client().revision() == 1; });
			check(f.hostEditor.map.getUniqueIdCount(1234) == 1 && f.clientEditor.map.getUniqueIdCount(1234) == 1, "Approved Unique ID was not counted");
			// Same ID on a second tile needs the host too, and approving it still cannot duplicate it.
			editTile(f.client(), second, [](Tile& tile) { tile.items.push_back(uniqueItem(1234)); });
			approveNext(true);
			pumpUntil([&] { return hasMessage(f.client(), "already exists on another tile"); });
			pumpUntil([&] { return f.client().status() == "Connected"; });
			check(f.host().revision() == 1 && f.hostEditor.map.getUniqueIdCount(1234) == 1, "Duplicate Unique ID was committed");
			// Removing needs no approval, and the freed ID is usable again.
			editTile(f.client(), first, [](Tile& tile) {
				delete tile.items.back();
				tile.items.pop_back();
			});
			pumpUntil([&] { return f.client().revision() == 2; });
			check(f.hostEditor.map.getUniqueIdCount(1234) == 0, "Remote removal left the Unique ID counted");
			check(!f.host().hasSensitiveChanges(tileEdit(f.host(), first, [](Tile& tile) { tile.setMapFlags(TILESTATE_NOPVP); })), "A plain edit asked for approval");
			check(f.host().hasSensitiveChanges(tileEdit(f.host(), first, [](Tile& tile) { tile.items.push_back(uniqueItem(1235)); })), "The Unique ID approval gate is open");
			std::cout << "PASS Unique ID approval gate and duplicate refusal over the network\n";
		}
		{
			// Correct counts must not turn duplicates in a real map into a failed join.
			Fixture f(0, [&](Fixture& f) {
				for (const auto& position : { first, second }) {
					placeTile(f.hostEditor.map, position, [](Tile& tile) { tile.items.push_back(uniqueItem(777)); });
				}
			});
			check(f.hostEditor.map.getUniqueIdCount(777) == 2, "Duplicate fixture was not counted");
			f.connected();
			check(f.clientEditor.map.getUniqueIdCount(777) == 2, "Snapshot duplicates were not counted");
			editTile(f.client(), first, [](Tile& tile) { tile.setMapFlags(TILESTATE_NOPVP); });
			pumpUntil([&] { return f.host().revision() == 1 && f.client().revision() == 1; });
			std::cout << "PASS a map with duplicate Unique IDs still syncs and stays editable\n";
		}
	}
	static void lockTests() {
		Fixture f;
		f.connected();
		const auto expectHostLeases = [&](size_t count) {
			pumpUntil([&] { return f.host().regionLocks.all().size() == count; });
		};
		const auto denied = f.client().lockRegion({ 0, 0, 65535, 65535, 7 });
		pumpUntil([&] { return hasMessage(f.client(), "too large"); });
		check(f.host().regionLocks.all().empty(), "Whole-floor lock was granted");
		f.client().unlockRegion(denied);
		std::vector<uint64_t> held;
		for (uint16_t i = 0; i <= Multiplayer::MaxLocksPerOwner; ++i) {
			held.push_back(f.client().lockRegion({ static_cast<uint16_t>(200 + i), 200, static_cast<uint16_t>(200 + i), 200, 7 }));
		}
		expectHostLeases(Multiplayer::MaxLocksPerOwner);
		pumpUntil([&] { return f.client().locks().size() == Multiplayer::MaxLocksPerOwner; });
		for (auto id : held) {
			f.client().unlockRegion(id);
		}
		expectHostLeases(0);
		const Position position(100, 100, 7);
		const auto lease = f.client().lockRegion({ 100, 100, 100, 100, 7 });
		expectHostLeases(1);
		changeTile(f.host(), position, TILESTATE_NOPVP);
		pumpUntil([&] { return f.client().revision() == 1; });
		check(f.host().revision() == 1, "A client lock blocked the host's edit");
		check(f.host().lockRegion({ 100, 100, 100, 100, 7 }) != 0, "Host could not take over a client's lock");
		check(f.host().regionLocks.all().size() == 1 && f.host().regionLocks.all().front().owner == 0, "Host did not break the client's lock");
		f.client().unlockRegion(lease);
		std::cout << "PASS lock area cap, per-owner lease cap, host exempt and able to break locks\n";
	}
	static void metadataTests() {
		{
			Fixture f(1, [](Fixture& f) {
				auto* town = new Town(1);
				town->setName("Thais");
				town->setTemplePosition({ 100, 100, 7 });
				f.hostEditor.map.towns.addTown(town);
			});
			auto& map = f.hostEditor.map;
			const auto before = Multiplayer::encodeMetadata(map);
			auto* town = map.towns.getTown(1);
			town->setTemplePosition({ 65000, 100, 7 });
			const auto moved = Multiplayer::encodeMetadata(map);
			const auto change = Multiplayer::describeMetadataChange(before, moved);
			check(change.find("Thais") != std::string::npos && change.find("65000,100,7") != std::string::npos, "Approval text does not show the change");
			// Only the x coordinate differs from an accepted image: 65001 used to decode and then brick encoding.
			auto invalid = moved;
			Multiplayer::Writer valid, outOfRange;
			valid.u64(Multiplayer::tileKey(65000, 100, 7));
			outOfRange.u64(Multiplayer::tileKey(65001, 100, 7));
			const auto found = std::search(invalid.begin(), invalid.end(), valid.data.begin(), valid.data.end());
			check(found != invalid.end(), "Temple key not found in metadata");
			std::copy(outOfRange.data.begin(), outOfRange.data.end(), found);
			bool rejected = false;
			try {
				Multiplayer::validateMetadata(invalid);
			} catch (const Multiplayer::Error&) {
				rejected = true;
			}
			check(rejected, "Metadata the encoder refuses was accepted");
			std::cout << "PASS metadata decode matches encode, approval text shows the change\n";
		}
		{
			Fixture f(1);
			auto& map = f.hostEditor.map;
			auto waypoint = std::make_unique<Waypoint>();
			waypoint->name = "Temple Exit";
			waypoint->pos = { 100, 100, 7 };
			check(map.waypoints.addWaypoint(std::move(waypoint)), "Waypoint fixture");
			Multiplayer::applyMetadata(map, Multiplayer::encodeMetadata(map));
			check(map.waypoints.getWaypoint("Temple Exit") != nullptr, "Applying metadata deleted a mixed-case waypoint");
			std::cout << "PASS mixed-case waypoints survive metadata apply\n";
		}
	}
	static void boundsTests() {
		{
			Fixture f;
			f.connected();
			const Position outside(600, 600, 7), untouched(300, 300, 7);
			editTile(f.client(), outside, [](Tile& tile) { tile.setMapFlags(TILESTATE_NOPVP); });
			pumpUntil([&] { return hasMessage(f.client(), "outside the map"); });
			pumpUntil([&] { return f.client().status() == "Connected"; });
			check(!f.hostEditor.map.getTileL(outside), "Rejected edit outside the map allocated host memory");
			auto tx = tileEdit(f.client(), untouched, [](Tile&) {});
			tx.tiles.front().after = { 9, 9, 9 };
			f.client().submit(std::move(tx));
			pumpUntil([&] { return hasMessage(f.client(), "Unknown tile format"); });
			pumpUntil([&] { return f.client().status() == "Connected"; });
			check(!f.hostEditor.map.getTileL(untouched) && f.host().revision() == 0, "Rejected edit left an allocation behind");
			f.host().changeRole(f.client().clientId(), Multiplayer::Role::Viewer);
			pumpUntil([&] { return !f.client().canEdit(); });
			auto refused = tileEdit(f.client(), { 100, 100, 7 }, [](Tile& tile) { tile.setMapFlags(TILESTATE_NOPVP); });
			refused.session = f.client().session;
			refused.id = 77;
			refused.author = f.client().localId;
			f.client().sendTransaction(onlyPeer(f.client()), refused);
			pumpUntil([&] { return hasMessage(f.client(), "Your role cannot edit"); });
			check(f.host().revision() == 0 && hasMessage(f.host(), "Your role cannot edit"), "Viewer transaction was processed");
			std::cout << "PASS map bounds, no allocation for rejected edits, viewer transactions refused early\n";
		}
		{
			Fixture f;
			const Position position(100, 100, 7);
			auto* tile = f.hostEditor.map.getTile(position);
			auto bad = std::unique_ptr<Tile>(tile->deepCopy(f.hostEditor.map));
			auto* teleport = dynamic_cast<Teleport*>(Item::Create(TeleportItem));
			check(teleport, "Teleport fixture");
			teleport->setDestination({ 100, 100, 200 });
			auto* container = dynamic_cast<Container*>(Item::Create(ContainerItem));
			container->getVector().push_back(teleport);
			bad->items.push_back(container);
			bool rejected = false;
			try {
				Multiplayer::decodeTile(f.hostEditor.map, Multiplayer::tileKey(100, 100, 7), Multiplayer::encodeTile(bad.get()));
			} catch (const Multiplayer::Error&) {
				rejected = true;
			}
			check(rejected, "Teleport destination z above 15 was accepted");
			std::cout << "PASS teleport destination z above 15 rejected, also inside containers\n";
		}
	}
	static void orderingTests() {
		const Position position(100, 100, 7);
		{
			Fixture f;
			f.connected();
			auto& peer = onlyPeer(f.client());
			f.client().beginActionGroup();
			changeTile(f.host(), position, TILESTATE_NOPVP);
			changeTile(f.host(), position, TILESTATE_PROTECTIONZONE);
			pumpUntil([&] { return peer.deferred.size() >= 6; });
			f.client().endActionGroup();
			changeTile(f.host(), position, TILESTATE_NOPVP);
			pumpUntil([&] { return f.client().revision() == 3; });
			check(f.client().status() == "Connected" && Multiplayer::encodeTile(f.clientEditor.map.getTile(position)) == Multiplayer::encodeTile(f.hostEditor.map.getTile(position)), "Frames lost their order after a deferred batch");
			std::cout << "PASS new frames queue behind deferred frames\n";
		}
		{
			// Longer than the old limit of 1024 queued transactions, and than MaxPendingPackets in frames.
			Fixture f;
			f.connected();
			f.client().reconnectDelay = 100;
			loseConnection(f.client());
			constexpr uint64_t gap = 1100;
			for (uint64_t i = 0; i < gap; ++i) {
				changeTile(f.host(), position, i % 2 ? TILESTATE_PROTECTIONZONE : TILESTATE_NOPVP);
			}
			check(f.host().revision() == gap, "Host edits did not commit");
			pumpUntil([&] { return f.client().status() == "Connected" && f.client().revision() == gap; }, 15000);
			check(Multiplayer::encodeTile(f.clientEditor.map.getTile(position)) == Multiplayer::encodeTile(f.hostEditor.map.getTile(position)), "Resume produced a different tile");
			std::cout << "PASS resume over a gap of 1100 transactions\n";
		}
	}

public:
	static void run() {
		{
			Fixture f;
			// The production policy must still reject a second UI session.
			std::string error;
			check(!f.client().join(f.options, error) && f.host().active(), "Second join damaged the existing session");
			f.connected();
			const Position position(100, 100, 7);
			changeTile(f.client(), position, TILESTATE_NOPVP);
			pumpUntil([&] { return f.host().revision() == 1 && f.client().revision() == 1; });
			check(Multiplayer::encodeTile(f.hostEditor.map.getTile(position)) == Multiplayer::encodeTile(f.clientEditor.map.getTile(position)), "Client edit differs on host");
			changeTile(f.host(), position, TILESTATE_PROTECTIONZONE);
			pumpUntil([&] { return f.client().revision() == 2; });
			check(Multiplayer::encodeTile(f.hostEditor.map.getTile(position)) == Multiplayer::encodeTile(f.clientEditor.map.getTile(position)), "Host edit differs on client");
			f.host().changeRole(f.client().clientId(), Multiplayer::Role::Viewer);
			pumpUntil([&] { return !f.client().canEdit(); });
			loseConnection(f.client());
			pumpUntil([&] { return f.client().status() == "Connected"; });
			check(!f.client().canEdit() && f.client().revision() == 2, "Reconnect lost role or revision");
			f.host().disconnect("Test shutdown");
			pumpUntil([&] { return f.client().status().find("Host disconnected: Test shutdown") != std::string::npos; });
			std::cout << "PASS Host/Join Challenge/Hello/Welcome/Snapshot/Ready, edits both ways, role/revision reconnect, host shutdown\n";
		}
		for (const std::string failure : { "password", "assets", "client" }) {
			Fixture f;
			if (failure == "password") {
				f.options.password = "wrong";
			}
			if (failure == "assets") {
				++g_items.BuildNumber;
			}
			if (failure == "client") {
				f.clientEditor.map.convert({ MAP_OTBM_4, static_cast<ClientVersionID>(18) });
			}
			f.join();
			pumpUntil([&] { return f.client().status().starts_with("Disconnected:"); });
			const auto expected = failure == "password" ? "Incorrect session password" : failure == "assets" ? "Item definitions"
																											 : "Tibia client";
			check(f.client().status().find(expected) != std::string::npos, "Handshake rejection reason was lost");
			if (failure == "assets") {
				--g_items.BuildNumber;
			}
			std::cout << "PASS rejection: " << failure << '\n';
		}
		{
			Fixture f;
			f.connected();
			Editor third(f.buffer, nullptr);
			third.multiplayer = std::make_unique<MultiplayerSession>(third);
			MultiplayerSession::activateForTests(nullptr);
			std::string error;
			check(third.multiplayer->join(f.options, error), error.c_str());
			pumpUntil([&] { return third.multiplayer->status().starts_with("Disconnected:"); });
			check(third.multiplayer->status().find("full") != std::string::npos, "Full session rejection was lost");
			check(f.client().status() == "Connected" && f.host().players().size() == 2, "Full rejection disrupted existing players");
			std::cout << "PASS session full\n";
		}
		{
			Fixture f;
			f.options.port = unusedPort();
			MultiplayerSession::activateForTests(nullptr);
			std::string error;
			const bool started = f.client().join(f.options, error);
			if (started) {
				pumpUntil([&] { return f.client().status().starts_with("Disconnected:"); });
			}
			check(!started || f.client().status().find("refused") != std::string::npos, "Closed port did not report connection failure");
			std::cout << "PASS closed port\n";
		}
		{
			Fixture f(6000);
			f.join();
			pumpUntil([&] { return f.client().receivingSnapshot; });
			loseConnection(f.client());
			check(!f.client().synchronizedBaseline, "Partial map can incorrectly resume");
			pumpUntil([&] { return f.client().status() == "Connected"; }, 15000);
			check(f.clientEditor.map.getTileCount() == 6000, "Reconnect retained partial snapshot");
			f.client().requestResync();
			pumpUntil([&] { return f.client().status() == "Connected"; });
			std::cout << "PASS partial snapshot reconnect and journal resync\n";
		}
		{
			Fixture f(6000);
			f.join();
			pumpUntil([&] { return f.client().receivingSnapshot; });
			f.clientEditor.multiplayer.reset();
			pumpUntil([&] { return f.host().players().size() == 1; });
			std::cout << "PASS client destruction during snapshot\n";
		}
		{
			Fixture f;
			f.connected();
			auto* owner = new wxFrame(nullptr, wxID_ANY, "Hidden property lock fixture");
			bool callback = false;
			MultiplayerSession::activateForTests(&f.client());
			check(f.client().deferPropertyEdit(owner, { 100, 100, 7 }, [&] {
				MultiplayerSession::PropertyEdit guard(&f.clientEditor.map, { 100, 100, 7 });
				check(guard.allowed(), "Asynchronous property lock missing");
				callback = true;
			}),
				  "Lock should be asynchronous");
			pumpUntil([&] { return callback; });
			check(f.client().heldLocks.empty(), "Property lock leaked after dialog");
			check(f.client().deferPropertyEdit(owner, { 100, 100, 7 }, [] { throw std::runtime_error("Destroyed window callback ran"); }), "Second lock should be asynchronous");
			owner->Destroy();
			pumpUntil([&] { return f.client().propertyRequests.empty() && f.client().heldLocks.empty(); });
			std::cout << "PASS asynchronous property lock, release and cancelled window\n";
		}
		{
			Fixture f;
			MultiplayerSession::activateForTests(nullptr);
			f.options.address = "invalid hostname with spaces";
			std::string error;
			check(f.client().join(f.options, error), error.c_str());
			pumpUntil([&] { return f.client().status().starts_with("Disconnected:"); });
			check(f.client().status().find("Invalid hostname") != std::string::npos, "Invalid hostname cause missing");
			std::cout << "PASS asynchronous invalid hostname\n";
		}
		{
			Fixture f;
			f.options.address = "localhost";
			f.connected();
			f.client().exerciseQueueLimitForTests();
			check(!f.client().canEdit(), "Queue overflow left a live editor");
			std::cout << "PASS hostname resolution and transfer queue overflow lifetime\n";
		}
		{
			Fixture f;
			f.connected();
			MultiplayerSession::activateForTests(&f.client());
			auto guard = std::make_unique<MultiplayerSession::MetadataEdit>(&f.clientEditor.map);
			check(guard->allowed(), "Metadata fixture guard denied");
			auto* batch = f.clientEditor.actionQueue->createBatch(ACTION_SELECT);
			f.clientEditor.multiplayer.reset();
			guard.reset();
			delete batch;
			pumpUntil([&] { return f.host().players().size() == 1; });
			std::cout << "PASS session destruction before metadata guard/action batch\n";
		}
		// Scenario 1: Options defaults
		{
			MultiplayerSession::Options opts;
			check(opts.autosaveMinutes == 0, "Options::autosaveMinutes must default to 0");
			check(opts.maxBackupSets == 10, "Options::maxBackupSets must default to 10");
			std::cout << "PASS multiplayer backup Options defaults (0 min, 10 sets)\n";
		}
		// Scenario 8: Retention logic, remnant cleanup, and safe isolation
		{
			auto tempDir = std::filesystem::temp_directory_path() / ("nexamap_retention_test_" + std::to_string(Clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(tempDir);
			const std::string stem = "world";

			// Create 12 backup sets (stamp 1000..1011) with various sidecars and .complete markers
			for (uint64_t i = 0; i < 12; ++i) {
				std::string base = stem + "-r1-" + std::to_string(1000 + i);
				std::ofstream(tempDir / (base + ".otbm")) << "test";
				std::ofstream(tempDir / (base + "-spawn.xml")) << "test";
				std::ofstream(tempDir / (base + "-house.xml")) << "test";
				std::ofstream(tempDir / (base + "-zones.xml")) << "test";
				std::ofstream(tempDir / (base + ".complete")) << "done";
			}

			// Create an incomplete remnant lacking .otbm (must be purged without consuming a retention slot)
			auto remnantSpawn = tempDir / (stem + "-r1-999-spawn.xml");
			std::ofstream(remnantSpawn) << "remnant";

			// Create an uncommitted crash-partial set having .otbm and sidecars but missing .complete
			auto uncommittedOtbm = tempDir / (stem + "-r1-1002_1.otbm");
			auto uncommittedSpawn = tempDir / (stem + "-r1-1002_1-spawn.xml");
			std::ofstream(uncommittedOtbm) << "crash";
			std::ofstream(uncommittedSpawn) << "crash";

			// Create unrelated files in the same directory that MUST NOT be touched
			auto unrelated1 = tempDir / "world.otbm";
			auto unrelated2 = tempDir / "world-r1-notabackup.txt";
			auto unrelated3 = tempDir / "othermap-r1-1005.otbm";
			std::ofstream(unrelated1) << "keep";
			std::ofstream(unrelated2) << "keep";
			std::ofstream(unrelated3) << "keep";

			Fixture f;
			f.host().pruneOldBackups(tempDir, stem, 10);

			// Incomplete remnant must be removed
			check(!std::filesystem::exists(remnantSpawn), "Retention failed to purge incomplete remnant lacking .otbm");

			// Uncommitted crash-partial set must be removed
			check(!std::filesystem::exists(uncommittedOtbm), "Retention failed to purge uncommitted crash-partial .otbm lacking .complete");
			check(!std::filesystem::exists(uncommittedSpawn), "Retention failed to purge uncommitted crash-partial sidecar");

			// Unrelated files must still exist
			check(std::filesystem::exists(unrelated1), "Retention deleted unrelated file world.otbm");
			check(std::filesystem::exists(unrelated2), "Retention deleted unrelated file world-r1-notabackup.txt");
			check(std::filesystem::exists(unrelated3), "Retention deleted other map backup");

			// Oldest 2 sets (1000 and 1001) should be removed completely, including .complete
			for (uint64_t i = 0; i < 2; ++i) {
				std::string base = stem + "-r1-" + std::to_string(1000 + i);
				check(!std::filesystem::exists(tempDir / (base + ".otbm")), "Oldest otbm was not pruned");
				check(!std::filesystem::exists(tempDir / (base + "-spawn.xml")), "Oldest spawn was not pruned");
				check(!std::filesystem::exists(tempDir / (base + "-house.xml")), "Oldest house was not pruned");
				check(!std::filesystem::exists(tempDir / (base + "-zones.xml")), "Oldest zones was not pruned");
				check(!std::filesystem::exists(tempDir / (base + ".complete")), "Oldest .complete was not pruned");
			}

			// Newer 10 sets (1002..1011) must remain, including .complete
			for (uint64_t i = 2; i < 12; ++i) {
				std::string base = stem + "-r1-" + std::to_string(1000 + i);
				check(std::filesystem::exists(tempDir / (base + ".otbm")), "Retained otbm was erroneously pruned");
				check(std::filesystem::exists(tempDir / (base + "-spawn.xml")), "Retained spawn was erroneously pruned");
				check(std::filesystem::exists(tempDir / (base + ".complete")), "Retained .complete was erroneously pruned");
			}

			std::filesystem::remove_all(tempDir);
			std::cout << "PASS multiplayer backup retention, remnant cleanup, and safe isolation\n";
		}
		// Scenarios 2, 3, 4, 5, 6, 7, 9, 10: Full Session & Backup Lifecycle
		{
			auto tempDir = std::filesystem::temp_directory_path() / ("nexamap_session_backup_test_" + std::to_string(Clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(tempDir);
			auto mapPath = tempDir / "testworld.otbm";

			Fixture f;
			f.connected();

			// Scenario 9: Client cannot invoke host backup action
			check(!f.client().saveBackup(MultiplayerSession::BackupReason::Manual), "Client must not execute manual backup");
			check(hasMessage(f.client(), "Manual backup is only available to the host"), "Missing client rejection log");

			// Map has no file yet
			check(!f.host().saveBackup(MultiplayerSession::BackupReason::Manual), "Manual backup on unnamed map must fail");
			check(hasMessage(f.host(), "map must be saved to a file first"), "Missing no-file log");

			// Set a file on host map
			f.hostEditor.map.setFilename(mapPath.string());
			f.hostEditor.map.setSpawnFilename("testworld-spawn.xml");
			f.hostEditor.map.setHouseFilename("testworld-house.xml");

			// Scenario 6: Manual backup works even when autosaveMinutes == 0
			check(f.host().settings().autosaveMinutes == 0, "Fixture should have autosaveMinutes == 0");
			check(f.host().saveBackup(MultiplayerSession::BackupReason::Manual), "Manual backup failed");
			check(f.host().lastBackedUpRevision.has_value() && *f.host().lastBackedUpRevision == 0, "Revision 0 not marked as backed up");
			check(hasMessage(f.host(), "Manual multiplayer backup saved"), "Missing manual backup success log");

			auto backupDir = tempDir / "multiplayer-backups";
			check(std::filesystem::exists(backupDir), "multiplayer-backups dir was not created");

			// Rapid manual backup within same revision creates unique collision-resistant file without overwriting
			check(f.host().saveBackup(MultiplayerSession::BackupReason::Manual), "Second rapid manual backup failed");
			size_t otbmCount = 0;
			size_t completeCount = 0;
			for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
				if (entry.path().extension() == ".otbm") {
					++otbmCount;
				} else if (entry.path().extension() == ".complete") {
					++completeCount;
				}
			}
			check(otbmCount == 2, "Second manual backup must not overwrite earlier backup set");
			check(completeCount == 2, "Each completed backup set must produce a .complete marker");
			check(WELCOME_DIALOG_MULTIPLAYER_JOIN == wxID_HIGHEST + 7003, "WELCOME_DIALOG_MULTIPLAYER_JOIN action ID mismatch");

			// Scenario 7: Verify sidecar paths are preserved exactly
			check(f.hostEditor.map.getSpawnFilename() == "testworld-spawn.xml", "Spawn filename was corrupted by backup");
			check(f.hostEditor.map.getHouseFilename() == "testworld-house.xml", "House filename was corrupted by backup");
			check(!f.host().backupInProgress, "backupInProgress stuck at true");

			// Automatic backup skips when neither revision nor generation changed
			check(!f.host().saveBackup(MultiplayerSession::BackupReason::Automatic), "Automatic backup should skip same revision and generation");

			// Host mutation via doChange() advances generation and allows automatic backup
			f.hostEditor.map.doChange();
			check(f.host().saveBackup(MultiplayerSession::BackupReason::Automatic), "Automatic backup failed after host doChange mutation");
			check(!f.host().saveBackup(MultiplayerSession::BackupReason::Automatic), "Automatic backup should skip when generation already backed up");

			// Scenario 5: New accepted revision allows next automatic backup
			const Position position(100, 100, 7);
			changeTile(f.client(), position, TILESTATE_NOPVP);
			pumpUntil([&] { return f.host().revision() == 1; });
			check(f.host().saveBackup(MultiplayerSession::BackupReason::Automatic), "Automatic backup failed for new revision");
			check(*f.host().lastBackedUpRevision == 1, "lastBackedUpRevision not updated to 1");
			check(hasMessage(f.host(), "Automatic multiplayer backup saved"), "Missing automatic backup log");

			// Scenario 4 again: unchanged revision 1 now skipped
			check(!f.host().saveBackup(MultiplayerSession::BackupReason::Automatic), "Automatic backup should skip unchanged revision 1");

			// Scenario 10: Disconnect / session reset
			f.host().disconnect("Test finished");
			check(!f.host().lastBackedUpRevision.has_value(), "lastBackedUpRevision leaked after disconnect");
			check(!f.host().backupInProgress, "backupInProgress leaked after disconnect");
			check(!f.host().saveBackup(MultiplayerSession::BackupReason::Manual), "Disconnected host must not be allowed to backup");
			check(hasMessage(f.host(), "Manual backup is only available to the host"), "Missing disconnected host rejection log");

			std::filesystem::remove_all(tempDir);
			std::cout << "PASS multiplayer manual and automatic backup control, deduplication, and session reset\n";
		}
		defineItems();
		hostingPolicyTests();
		hostProofTests();
		frameAuthenticationTests();
		admissionTests();
		identityTests();
		spawnTests();
		uniqueIdTests();
		lockTests();
		metadataTests();
		boundsTests();
		orderingTests();
		// Drain independent closing handlers and delayed socket destruction.
		const auto end = Clock::now() + std::chrono::milliseconds(2200);
		pumpUntil([&] { return Clock::now() >= end; }, 3000);
	}
};

void RunStartupValidationTests();
void RunPlaytestIntegrationTests();
void RunCollectionsPaletteTests();
void RunRendererLifecycleTests();
void RunResourceSessionTabTests();
void RunCrossClientClipboardTests();
void RunMapMigrationWindowTests();

int RunMultiplayerSessionTests(int argc, char** argv) {
	const bool startupValidation = argc > 1 && std::string(argv[1]) == "--startup-validation";
	const bool playtestValidation = argc > 1 && std::string(argv[1]) == "--playtest-validation";
	const bool collectionsValidation = argc > 1 && std::string(argv[1]) == "--collections-validation";
	const bool rendererLifecycle = argc > 1 && std::string(argv[1]) == "--renderer-lifecycle-validation";
	const bool resourceSessionTabs = argc > 1 && std::string(argv[1]) == "--resource-session-tab-validation";
	const bool crossClientPaste = argc > 1 && std::string(argv[1]) == "--cross-client-paste-validation";
	const bool migrationWindow = argc > 1 && std::string(argv[1]) == "--migration-window-validation";
	if (!wxEntryStart(argc, argv)) {
		return 1;
	}
	wxTheApp->SetExitOnFrameDelete(false);
	wxLog::SetActiveTarget(new wxLogStderr());
	wxSetAssertHandler([](const wxString& file, int line, const wxString&, const wxString& condition, const wxString& message) {
		std::cerr << "wx assertion: " << file.ToStdString() << ':' << line << ' ' << condition.ToStdString() << ' ' << message.ToStdString() << std::endl;
		std::abort();
	});
	int result = 0;
	{
		wxEventLoop loop;
		wxEventLoopActivator activate(&loop);
		try {
			if (migrationWindow) {
				RunMapMigrationWindowTests();
			} else if (crossClientPaste) {
				RunCrossClientClipboardTests();
			} else if (resourceSessionTabs) {
				RunResourceSessionTabTests();
			} else if (rendererLifecycle) {
				RunRendererLifecycleTests();
			} else if (collectionsValidation) {
				RunCollectionsPaletteTests();
			} else if (playtestValidation) {
				RunPlaytestIntegrationTests();
			} else if (startupValidation) {
				RunStartupValidationTests();
			} else {
				MultiplayerSessionTests::run();
			}
		} catch (const std::exception& error) {
			std::cerr << "FAIL " << error.what() << std::endl;
			result = 1;
		}
	}
	wxEntryCleanup();
	return result;
}
