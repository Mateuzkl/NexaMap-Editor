// SPDX-License-Identifier: GPL-3.0-or-later
// Full production Session/codec/ActionQueue over real nonblocking wxSockets.
// Only resource loading and the single-session UI policy are replaced by fixtures.
#include "main.h"
#include "editor.h"
#include "copybuffer.h"
#include "multiplayer_session.h"
#include "multiplayer_codec.h"
#include "multiplayer_crypto.h"
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
		Fixture(size_t tiles = 1) {
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
	};
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
		for (const std::string failure : { "password", "assets", "client", "identity" }) {
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
			if (failure == "identity") {
				f.client().identity = {};
			}
			f.join();
			pumpUntil([&] { return f.client().status().starts_with("Disconnected:"); });
			const auto expected = failure == "password" ? "Incorrect session password" : failure == "assets" ? "Item definitions"
				: failure == "client"																		 ? "Tibia client"
																											 : "Invalid participant";
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
		// Scenario 8: Retention logic and safe isolation
		{
			auto tempDir = std::filesystem::temp_directory_path() / ("nexamap_retention_test_" + std::to_string(Clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(tempDir);
			const std::string stem = "world";

			// Create 12 backup sets (stamp 1000..1011) with various sidecars
			for (uint64_t i = 0; i < 12; ++i) {
				std::string base = stem + "-r1-" + std::to_string(1000 + i);
				std::ofstream(tempDir / (base + ".otbm")) << "test";
				std::ofstream(tempDir / (base + "-spawn.xml")) << "test";
				std::ofstream(tempDir / (base + "-house.xml")) << "test";
				std::ofstream(tempDir / (base + "-zones.xml")) << "test";
			}

			// Create unrelated files in the same directory that MUST NOT be touched
			auto unrelated1 = tempDir / "world.otbm";
			auto unrelated2 = tempDir / "world-r1-notabackup.txt";
			auto unrelated3 = tempDir / "othermap-r1-1005.otbm";
			std::ofstream(unrelated1) << "keep";
			std::ofstream(unrelated2) << "keep";
			std::ofstream(unrelated3) << "keep";

			Fixture f;
			f.host().pruneOldBackups(tempDir, stem, 10);

			// Unrelated files must still exist
			check(std::filesystem::exists(unrelated1), "Retention deleted unrelated file world.otbm");
			check(std::filesystem::exists(unrelated2), "Retention deleted unrelated file world-r1-notabackup.txt");
			check(std::filesystem::exists(unrelated3), "Retention deleted other map backup");

			// Oldest 2 sets (1000 and 1001) should be removed completely
			for (uint64_t i = 0; i < 2; ++i) {
				std::string base = stem + "-r1-" + std::to_string(1000 + i);
				check(!std::filesystem::exists(tempDir / (base + ".otbm")), "Oldest otbm was not pruned");
				check(!std::filesystem::exists(tempDir / (base + "-spawn.xml")), "Oldest spawn was not pruned");
				check(!std::filesystem::exists(tempDir / (base + "-house.xml")), "Oldest house was not pruned");
				check(!std::filesystem::exists(tempDir / (base + "-zones.xml")), "Oldest zones was not pruned");
			}

			// Newer 10 sets (1002..1011) must remain
			for (uint64_t i = 2; i < 12; ++i) {
				std::string base = stem + "-r1-" + std::to_string(1000 + i);
				check(std::filesystem::exists(tempDir / (base + ".otbm")), "Retained otbm was erroneously pruned");
				check(std::filesystem::exists(tempDir / (base + "-spawn.xml")), "Retained spawn was erroneously pruned");
			}

			std::filesystem::remove_all(tempDir);
			std::cout << "PASS multiplayer backup retention and safe isolation\n";
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

			// Scenario 7: Verify sidecar paths are preserved exactly
			check(f.hostEditor.map.getSpawnFilename() == "testworld-spawn.xml", "Spawn filename was corrupted by backup");
			check(f.hostEditor.map.getHouseFilename() == "testworld-house.xml", "House filename was corrupted by backup");
			check(!f.host().backupInProgress, "backupInProgress stuck at true");

			// Scenario 4: Same revision afterward skips automatic backup
			f.hostEditor.map.doChange();
			check(!f.host().saveBackup(MultiplayerSession::BackupReason::Automatic), "Automatic backup should skip same revision");

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

			std::filesystem::remove_all(tempDir);
			std::cout << "PASS multiplayer manual and automatic backup control, deduplication, and session reset\n";
		}
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

int RunMultiplayerSessionTests(int argc, char** argv) {
	const bool startupValidation = argc > 1 && std::string(argv[1]) == "--startup-validation";
	const bool playtestValidation = argc > 1 && std::string(argv[1]) == "--playtest-validation";
	const bool collectionsValidation = argc > 1 && std::string(argv[1]) == "--collections-validation";
	const bool rendererLifecycle = argc > 1 && std::string(argv[1]) == "--renderer-lifecycle-validation";
	const bool resourceSessionTabs = argc > 1 && std::string(argv[1]) == "--resource-session-tab-validation";
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
			if (resourceSessionTabs) {
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
