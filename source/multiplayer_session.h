// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_SESSION_H
#define NEXAMAP_MULTIPLAYER_SESSION_H
#include "multiplayer_protocol.h"
#include "position.h"
#include <wx/event.h>
#include <wx/timer.h>
#include <memory>
#include <optional>

class Editor;
class Map;
class Tile;
class BatchAction;
class House;
class wxSocketBase;
class wxSocketServer;
class wxSocketEvent;
class MultiplayerWindow;
class EditorResourceSession;

class MultiplayerSession final : public wxEvtHandler {
public:
    struct Options {
        std::string name = "Mapper";
        std::string address = "127.0.0.1";
        std::string password;
        uint16_t port = 7171;
        uint32_t maxPlayers = 8;
        Multiplayer::Role defaultRole = Multiplayer::Role::Editor;
        bool approvals = true;
        uint32_t autosaveMinutes = 5;
    };
    struct Participant {
        uint32_t id = 0;
        std::string name;
        Multiplayer::Role role = Multiplayer::Role::Viewer;
        Position cursor;
        uint32_t color = 0x45c8ff;
        uint32_t latency = 0;
        bool afk = false;
    };
    struct Ping { Position position; uint32_t color; std::string author; uint64_t expires; };
    struct Approval {
        uint64_t id;
        Multiplayer::Transaction transaction;
        std::string requester;
        std::string description;
        uint64_t created;
    };
    explicit MultiplayerSession(Editor& editor);
    ~MultiplayerSession() override;
    bool host(const Options& options, std::string& error);
    bool join(const Options& options, std::string& error);
    void disconnect(const std::string& reason = "Session closed.");
    bool isHost() const { return hosting; }
    bool active() const { return running; }
    bool canEdit() const;
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();
    bool internalChange() const { return applying; }
    void beginActionGroup() { ++editDepth; }
    void endActionGroup() { if (editDepth) --editDepth; }
    void consumeBatch(BatchAction* batch);
    void updateCursor(const Position& position);
    void sendChat(const std::string& text);
    void pingLocation(const Position& position);
    uint64_t lockRegion(const Multiplayer::Region& region);
    void unlockRegion(uint64_t id);
    bool ownsLock(uint64_t id) const;
    void changeRole(uint32_t id, Multiplayer::Role role);
    void kick(uint32_t id);
    void approve(uint64_t id, bool accept, uint16_t replacementUniqueId = 0);
    void requestResync();
    void showWindow();
    void log(const std::string& message);
    const auto& players() const { return participants; }
    const auto& pings() const { return locationPings; }
    const auto& approvalRequests() const { return approvals; }
    const auto& messages() const { return logLines; }
    const auto& locks() const { return visibleLocks; }
    const Options& settings() const { return options; }
    std::string status() const;
    std::string sessionId() const;
    uint64_t revision() const;
    uint32_t clientId() const { return localId; }
    size_t pendingBytes() const;
    Editor& getEditor() const { return editor; }
    static MultiplayerSession* current();
    static bool permitsResourceSession(const std::shared_ptr<EditorResourceSession>& resources);

    // Dialogs that edit registries outside ActionQueue use the same transaction path.
    class MetadataEdit {
    public:
        explicit MetadataEdit(Map* map, House* affectedHouse = nullptr);
        ~MetadataEdit();
        bool allowed() const { return permitted; }
    private:
        MultiplayerSession* session = nullptr;
        Multiplayer::Bytes before;
        std::map<uint64_t, Multiplayer::Bytes> tiles;
        uint64_t base = 0;
        bool permitted = true;
    };
    class PropertyEdit {
    public:
        PropertyEdit(Map* map, const Position& position);
        ~PropertyEdit();
        bool allowed() const { return permitted; }
    private:
        MultiplayerSession* session = nullptr;
        uint64_t lock = 0;
        bool permitted = true;
    };
private:
    struct Peer;
    struct History {
        Multiplayer::Transaction forward;
        Multiplayer::Transaction inverse;
        size_t bytes = 0;
    };
    enum class PendingKind { Edit, Undo, Redo };
    struct Pending { Multiplayer::Transaction transaction; PendingKind kind; std::optional<History> history; };
    struct IdentityState { uint32_t id; uint64_t generation; };
    void onSocket(wxSocketEvent& event);
    void onTimer(wxTimerEvent& event);
    void connectClient();
    void configureSocket(Peer& peer);
    void read(Peer& peer);
    void write(Peer& peer);
    void send(Peer& peer, Multiplayer::Packet type, const Multiplayer::Writer& payload);
    void broadcast(Multiplayer::Packet type, const Multiplayer::Writer& payload, uint32_t except = UINT32_MAX);
    void sendTransaction(Peer& peer, const Multiplayer::Transaction& tx);
    void pumpTransactions(Peer& peer);
    void process(Peer& peer, const Multiplayer::Bytes& bytes);
    void challenge(Peer& peer);
    void login(Peer& peer, Multiplayer::Reader& in);
    void drop(Peer& peer, const std::string& reason, bool reconnect = true);
    void startSnapshot(Peer& peer);
    void pumpSnapshot(Peer& peer);
    void finishCatchup(Peer& peer);
    void resetClientMap();
    void submit(Multiplayer::Transaction tx, PendingKind kind = PendingKind::Edit, std::optional<History> history = {});
    void accept(Peer* peer, Multiplayer::Transaction tx, bool approved = false);
    void apply(const Multiplayer::Transaction& tx);
    void receiveTransaction(Peer& peer, Multiplayer::Transaction tx);
    void completePending(const Multiplayer::Transaction& tx);
    void reject(Peer* peer, uint64_t id, const std::string& reason);
    void sendPlayers();
    void sendLocks();
    void refresh();
    void saveBackup();
    bool hasSensitiveChanges(const Multiplayer::Transaction& tx) const;
    void finishMetadataEdit(const Multiplayer::Bytes& before, const std::map<uint64_t, Multiplayer::Bytes>& tiles, uint64_t base);

    Editor& editor;
    Options options;
    wxTimer timer;
    wxSocketServer* listener = nullptr;
    std::map<wxSocketBase*, std::unique_ptr<Peer>> peers;
    std::map<Multiplayer::Identity, IdentityState> identities;
    std::map<uint32_t, Participant> participants;
    std::map<uint64_t, Approval> approvals;
    std::vector<Multiplayer::Lease> visibleLocks;
    std::map<uint64_t, Multiplayer::Region> heldLocks;
    std::vector<uint64_t> unlockAfterAck;
    std::vector<Ping> locationPings;
    std::deque<std::string> logLines;
    std::shared_ptr<EditorResourceSession> resources;
    MultiplayerWindow* window = nullptr;
    Multiplayer::Identity identity {}, session {};
    Multiplayer::Digest signature {};
    Multiplayer::Revisions revisions;
    Multiplayer::Locks regionLocks;
    Multiplayer::Journal journal;
    Multiplayer::Bytes metadata;
    Multiplayer::Bytes incomingMetadata;
    std::optional<Pending> pending;
    std::deque<History> history;
    size_t historyPosition = 0, historyBytes = 0;
    uint32_t localId = 0, nextClientId = 1, snapshotSequence = 0;
    uint64_t acknowledgedRevision = 0, generation = 0, nextTransaction = 1, nextApproval = 1, nextLock = 1;
    uint64_t reconnectAt = 0, reconnectDelay = 1000, lastHeartbeat = 0, lastCursorSent = 0, lastActivity = 0, lastBackup = 0, lastRefresh = 0;
    uint64_t snapshotRevision = 0;
    uint64_t lastAcceptedTransaction = 0;
    uint32_t metadataSize = 0;
    Position cursor;
    bool running = false, hosting = false, ready = false, applying = false, cursorDirty = false, needsRefresh = false, receivingSnapshot = false;
    unsigned editDepth = 0;
    std::string connectionStatus = "Disconnected";
    friend class MultiplayerWindow;
};
#endif
