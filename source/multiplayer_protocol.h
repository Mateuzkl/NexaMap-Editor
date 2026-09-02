// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MULTIPLAYER_PROTOCOL_H
#define NEXAMAP_MULTIPLAYER_PROTOCOL_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace Multiplayer {
	using Bytes = std::vector<uint8_t>;
	using Identity = std::array<uint8_t, 16>;
	using Digest = std::array<uint8_t, 32>;
	constexpr uint32_t ProtocolVersion = 1;
	constexpr size_t MaxPacket = 1024 * 1024;
	constexpr size_t MaxTile = 256 * 1024;
	constexpr size_t MaxTransaction = 32 * 1024 * 1024;
	constexpr size_t MaxTiles = 100000;
	constexpr size_t MaxPendingBytes = 8 * 1024 * 1024;
	constexpr size_t MaxPendingPackets = 1024;
	constexpr size_t MaxJournalBytes = 64 * 1024 * 1024;
	constexpr size_t MaxName = 64;
	constexpr size_t MaxChat = 1024;
	constexpr size_t MaxMetadata = 4 * 1024 * 1024;
	constexpr size_t MaxPlayers = 16;
	constexpr size_t MaxLocks = 256;

	enum class Packet : uint8_t {
		Challenge = 1,
		Hello,
		Welcome,
		SnapshotBegin,
		SnapshotChunk,
		SnapshotEnd,
		TransactionBegin,
		TransactionChunk,
		TransactionEnd,
		Accepted,
		Rejected,
		Resume,
		Ready,
		Chat,
		Cursor,
		Ping,
		Pong,
		Players,
		Lock,
		Unlock,
		Locks,
		LocationPing,
		Approval,
		ApprovalResult,
		Disconnect
	};
	enum class Role : uint8_t { Host,
								Editor,
								Reviewer,
								Viewer };
	const char* roleName(Role role);

	struct Error : std::runtime_error {
		using std::runtime_error::runtime_error;
	};

	class Writer {
	public:
		explicit Writer(size_t limit = MaxPacket) :
			limit(limit) { }
		void u8(uint8_t v);
		void u16(uint16_t v);
		void u32(uint32_t v);
		void u64(uint64_t v);
		void raw(std::span<const uint8_t> value);
		void bytes(std::span<const uint8_t> value, size_t max = MaxPacket);
		void string(const std::string& value, size_t max = MaxChat);
		Bytes data;

	private:
		size_t limit;
	};

	class Reader {
	public:
		explicit Reader(std::span<const uint8_t> data) :
			data(data) { }
		uint8_t u8();
		uint16_t u16();
		uint32_t u32();
		uint64_t u64();
		bool boolean();
		std::span<const uint8_t> raw(size_t size);
		Bytes bytes(size_t max = MaxPacket);
		std::string string(size_t max = MaxChat);
		template <size_t N>
		std::array<uint8_t, N> fixed() {
			std::array<uint8_t, N> result;
			auto part = raw(N);
			std::copy(part.begin(), part.end(), result.begin());
			return result;
		}
		void finish() const;
		size_t remaining() const {
			return data.size() - offset;
		}

	private:
		std::span<const uint8_t> data;
		size_t offset = 0;
	};

	// One incomplete frame at most. Reject the length before allocating its body.
	class FrameReader {
	public:
		size_t needed() const;
		void append(std::span<const uint8_t> bytes);
		bool ready() const;
		Bytes take();

	private:
		Bytes pending;
		uint32_t length = 0;
	};
	Bytes frame(Packet type, std::span<const uint8_t> payload);
	bool validText(const std::string& value, size_t max, bool allowEmpty = false);

	uint64_t tileKey(uint16_t x, uint16_t y, uint8_t z);
	bool validTileKey(uint64_t key);
	uint16_t tileX(uint64_t key);
	uint16_t tileY(uint64_t key);
	uint8_t tileZ(uint64_t key);

	struct Region {
		uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
		uint8_t z = 7;
		bool valid() const;
		bool contains(uint64_t key) const;
		bool overlaps(const Region& other) const;
	};
	void writeRegion(Writer& out, const Region& region);
	Region readRegion(Reader& in);
	struct Lease {
		uint64_t id = 0;
		uint32_t owner = 0;
		Region region;
		uint64_t expires = 0;
	};
	class Locks {
	public:
		bool acquire(uint32_t owner, uint64_t id, Region region, uint64_t now);
		void release(uint32_t owner, uint64_t id);
		void releaseOwner(uint32_t owner);
		bool expire(uint64_t now);
		bool permits(uint32_t owner, uint64_t key, uint64_t now) const;
		const std::vector<Lease>& all() const {
			return leases;
		}

	private:
		std::vector<Lease> leases;
	};

	struct TileEdit {
		uint64_t key = 0;
		Digest before {};
		Bytes after;
	};
	struct Transaction {
		Identity session {};
		uint64_t id = 0;
		uint64_t base = 0;
		uint64_t revision = 0;
		uint32_t author = 0;
		Digest metadataBefore {};
		Bytes metadata;
		std::vector<TileEdit> tiles;
	};
	Bytes encodeTransaction(const Transaction& tx);
	Transaction decodeTransaction(std::span<const uint8_t> bytes);

	// Stores only accepted revisions. A rejected/replayed packet cannot advance state.
	class Revisions {
	public:
		std::string validate(const Transaction& tx, const Identity& session, Role role, uint32_t author, const Locks& locks, uint64_t now) const;
		uint64_t commit(const Transaction& tx);
		uint64_t current() const {
			return revision;
		}
		uint64_t lastTransaction(uint32_t author) const;

	private:
		uint64_t revision = 0;
		uint64_t metadataRevision = 0;
		std::map<uint64_t, uint64_t> tiles;
		std::map<uint32_t, uint64_t> transactions;
	};
	struct JournalEntry {
		uint64_t revision;
		Bytes bytes;
	};
	class Journal {
	public:
		void push(uint64_t revision, Bytes bytes);
		bool covers(uint64_t after, uint64_t current) const;
		const std::deque<JournalEntry>& entries() const {
			return records;
		}
		size_t sizeBytes() const {
			return bytes;
		}

	private:
		size_t bytes = 0;
		std::deque<JournalEntry> records;
	};
} // namespace Multiplayer
#endif
