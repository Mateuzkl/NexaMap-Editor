// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_protocol.h"
#include <algorithm>
#include <limits>
#include <set>

namespace Multiplayer {
	const char* roleName(Role role) {
		switch (role) {
			case Role::Host:
				return "Host";
			case Role::Editor:
				return "Editor";
			case Role::Reviewer:
				return "Reviewer";
			default:
				return "Viewer";
		}
	}
	void Writer::raw(std::span<const uint8_t> value) {
		if (value.size() > limit - data.size()) {
			throw Error("Packet exceeds its size limit.");
		}
		data.insert(data.end(), value.begin(), value.end());
	}
	void Writer::u8(uint8_t v) {
		raw({ &v, 1 });
	}
	void Writer::u16(uint16_t v) {
		u8(v & 255);
		u8(v >> 8);
	}
	void Writer::u32(uint32_t v) {
		u16(v & 65535);
		u16(v >> 16);
	}
	void Writer::u64(uint64_t v) {
		u32(static_cast<uint32_t>(v));
		u32(static_cast<uint32_t>(v >> 32));
	}
	void Writer::bytes(std::span<const uint8_t> value, size_t max) {
		if (value.size() > max || value.size() > UINT32_MAX) {
			throw Error("Field exceeds its size limit.");
		}
		u32(static_cast<uint32_t>(value.size()));
		raw(value);
	}
	void Writer::string(const std::string& value, size_t max) {
		bytes({ reinterpret_cast<const uint8_t*>(value.data()), value.size() }, max);
	}
	std::span<const uint8_t> Reader::raw(size_t size) {
		if (size > remaining()) {
			throw Error("Truncated packet.");
		}
		auto part = data.subspan(offset, size);
		offset += size;
		return part;
	}
	uint8_t Reader::u8() {
		return raw(1)[0];
	}
	uint16_t Reader::u16() {
		auto a = u8();
		auto b = u8();
		return a | (uint16_t(b) << 8);
	}
	uint32_t Reader::u32() {
		auto a = u16();
		auto b = u16();
		return a | (uint32_t(b) << 16);
	}
	uint64_t Reader::u64() {
		auto a = u32();
		auto b = u32();
		return a | (uint64_t(b) << 32);
	}
	bool Reader::boolean() {
		auto v = u8();
		if (v > 1) {
			throw Error("Invalid boolean.");
		}
		return v != 0;
	}
	Bytes Reader::bytes(size_t max) {
		auto n = u32();
		if (n > max) {
			throw Error("Field exceeds its size limit.");
		}
		auto value = raw(n);
		return { value.begin(), value.end() };
	}
	std::string Reader::string(size_t max) {
		auto value = bytes(max);
		return { value.begin(), value.end() };
	}
	void Reader::finish() const {
		if (remaining()) {
			throw Error("Unexpected trailing packet data.");
		}
	}
	size_t FrameReader::needed() const {
		return length ? length - pending.size() : 4 - pending.size();
	}
	void FrameReader::append(std::span<const uint8_t> bytes) {
		if (bytes.size() > needed()) {
			throw Error("Frame boundary exceeded.");
		}
		pending.insert(pending.end(), bytes.begin(), bytes.end());
		if (!length && pending.size() == 4) {
			Reader in(pending);
			auto n = in.u32();
			if (n == 0 || n > MaxPacket) {
				throw Error("Invalid network packet length.");
			}
			length = n;
			pending.clear();
			pending.reserve(n);
		}
	}
	bool FrameReader::ready() const {
		return length && pending.size() == length;
	}
	Bytes FrameReader::take() {
		if (!ready()) {
			throw Error("Incomplete network frame.");
		}
		length = 0;
		return std::exchange(pending, {});
	}
	Bytes frame(Packet type, std::span<const uint8_t> payload) {
		if (payload.size() >= MaxPacket) {
			throw Error("Network packet too large.");
		}
		Writer out(MaxPacket + 4);
		out.u32(static_cast<uint32_t>(payload.size() + 1));
		out.u8(static_cast<uint8_t>(type));
		out.raw(payload);
		return std::move(out.data);
	}
	bool validText(const std::string& value, size_t max, bool allowEmpty) {
		if ((!allowEmpty && value.empty()) || value.size() > max) {
			return false;
		}
		// Validate UTF-8 and reject control characters, overlong encodings and surrogates.
		for (size_t i = 0; i < value.size();) {
			uint32_t c = static_cast<uint8_t>(value[i++]);
			if (c < 32 || c == 127) {
				return false;
			}
			if (c < 128) {
				continue;
			}
			size_t n;
			uint32_t min;
			if (c >= 0xc2 && c <= 0xdf) {
				n = 1;
				min = 0x80;
				c &= 31;
			} else if (c >= 0xe0 && c <= 0xef) {
				n = 2;
				min = 0x800;
				c &= 15;
			} else if (c >= 0xf0 && c <= 0xf4) {
				n = 3;
				min = 0x10000;
				c &= 7;
			} else {
				return false;
			}
			if (n > value.size() - i) {
				return false;
			}
			while (n--) {
				auto b = static_cast<uint8_t>(value[i++]);
				if ((b & 0xc0) != 0x80) {
					return false;
				}
				c = (c << 6) | (b & 63);
			}
			if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
				return false;
			}
		}
		return true;
	}
	uint64_t tileKey(uint16_t x, uint16_t y, uint8_t z) {
		return uint64_t(x) | (uint64_t(y) << 16) | (uint64_t(z) << 32);
	}
	bool validTileKey(uint64_t key) {
		return key >> 36 == 0;
	}
	uint16_t tileX(uint64_t key) {
		return key & 65535;
	}
	uint16_t tileY(uint64_t key) {
		return (key >> 16) & 65535;
	}
	uint8_t tileZ(uint64_t key) {
		return static_cast<uint8_t>(key >> 32);
	}
	bool Region::valid() const {
		return x1 <= x2 && y1 <= y2 && z <= 15;
	}
	bool Region::contains(uint64_t key) const {
		return tileZ(key) == z && tileX(key) >= x1 && tileX(key) <= x2 && tileY(key) >= y1 && tileY(key) <= y2;
	}
	bool Region::overlaps(const Region& o) const {
		return z == o.z && x1 <= o.x2 && o.x1 <= x2 && y1 <= o.y2 && o.y1 <= y2;
	}
	void writeRegion(Writer& out, const Region& r) {
		out.u16(r.x1);
		out.u16(r.y1);
		out.u16(r.x2);
		out.u16(r.y2);
		out.u8(r.z);
	}
	Region readRegion(Reader& in) {
		Region r;
		r.x1 = in.u16();
		r.y1 = in.u16();
		r.x2 = in.u16();
		r.y2 = in.u16();
		r.z = in.u8();
		if (!r.valid()) {
			throw Error("Invalid lock region.");
		}
		return r;
	}
	bool Locks::acquire(uint32_t owner, uint64_t id, Region region, uint64_t now) {
		expire(now);
		if (!id || !region.valid()) {
			return false;
		}
		for (const auto& l : leases) {
			if (l.owner != owner && l.region.overlaps(region)) {
				return false;
			}
		}
		for (auto& l : leases) {
			if (l.owner == owner && l.id == id) {
				l.region = region;
				l.expires = now + 30000;
				return true;
			}
		}
		if (leases.size() >= MaxLocks) {
			return false;
		}
		leases.push_back({ id, owner, region, now + 30000 });
		return true;
	}
	void Locks::release(uint32_t owner, uint64_t id) {
		std::erase_if(leases, [&](const auto& l) { return l.owner == owner && l.id == id; });
	}
	void Locks::releaseOwner(uint32_t owner) {
		std::erase_if(leases, [&](const auto& l) { return l.owner == owner; });
	}
	bool Locks::expire(uint64_t now) {
		return std::erase_if(leases, [&](const auto& l) { return l.expires <= now; }) != 0;
	}
	bool Locks::permits(uint32_t owner, uint64_t key, uint64_t now) const {
		return std::none_of(leases.begin(), leases.end(), [&](const auto& l) { return l.owner != owner && l.expires > now && l.region.contains(key); });
	}
	Bytes encodeTransaction(const Transaction& tx) {
		if (tx.tiles.size() > MaxTiles) {
			throw Error("Too many tiles in one edit. Split the selection.");
		}
		Writer out(MaxTransaction);
		out.raw(tx.session);
		out.u64(tx.id);
		out.u64(tx.base);
		out.u64(tx.revision);
		out.u32(tx.author);
		out.raw(tx.metadataBefore);
		out.bytes(tx.metadata, MaxMetadata);
		out.u32(static_cast<uint32_t>(tx.tiles.size()));
		for (const auto& tile : tx.tiles) {
			out.u64(tile.key);
			out.raw(tile.before);
			out.bytes(tile.after, MaxTile);
		}
		return std::move(out.data);
	}
	Transaction decodeTransaction(std::span<const uint8_t> bytes) {
		if (bytes.size() > MaxTransaction) {
			throw Error("Transaction exceeds its size limit.");
		}
		Reader in(bytes);
		Transaction tx;
		tx.session = in.fixed<16>();
		tx.id = in.u64();
		tx.base = in.u64();
		tx.revision = in.u64();
		tx.author = in.u32();
		tx.metadataBefore = in.fixed<32>();
		tx.metadata = in.bytes(MaxMetadata);
		auto n = in.u32();
		if (n > MaxTiles || n > in.remaining() / 44) {
			throw Error("Invalid tile count.");
		}
		std::set<uint64_t> keys;
		tx.tiles.reserve(n);
		for (uint32_t i = 0; i < n; ++i) {
			TileEdit tile;
			tile.key = in.u64();
			tile.before = in.fixed<32>();
			tile.after = in.bytes(MaxTile);
			if (!validTileKey(tile.key) || !keys.insert(tile.key).second) {
				throw Error("Invalid or duplicate tile position.");
			}
			tx.tiles.push_back(std::move(tile));
		}
		in.finish();
		if (!tx.id || (tx.tiles.empty() && tx.metadata.empty())) {
			throw Error("Empty transaction.");
		}
		return tx;
	}
	uint64_t Revisions::lastTransaction(uint32_t author) const {
		auto it = transactions.find(author);
		return it == transactions.end() ? 0 : it->second;
	}
	std::string Revisions::validate(const Transaction& tx, const Identity& session, Role role, uint32_t author, const Locks& locks, uint64_t now) const {
		if (tx.session != session || tx.author != author) {
			return "Session or author mismatch.";
		}
		if (role != Role::Host && role != Role::Editor) {
			return "Your role cannot edit the map.";
		}
		if (!tx.id || tx.id <= lastTransaction(author)) {
			return "Duplicate or expired transaction.";
		}
		if (tx.base > revision || tx.revision != 0) {
			return "Invalid base revision.";
		}
		if (!tx.metadata.empty() && tx.base < metadataRevision) {
			return "Metadata changed. Refresh and retry.";
		}
		std::set<uint64_t> keys;
		for (const auto& tile : tx.tiles) {
			if (!validTileKey(tile.key) || !keys.insert(tile.key).second) {
				return "Invalid or duplicate tile.";
			}
			auto it = tiles.find(tile.key);
			if (it != tiles.end() && it->second > tx.base) {
				return "Conflict: another accepted edit changed this tile.";
			}
			if (!locks.permits(author, tile.key, now)) {
				return "A tile in this edit is locked by another participant.";
			}
		}
		return {};
	}
	uint64_t Revisions::commit(const Transaction& tx) {
		if (revision == UINT64_MAX) {
			throw Error("Session revision exhausted.");
		}
		++revision;
		transactions[tx.author] = tx.id;
		if (!tx.metadata.empty()) {
			metadataRevision = revision;
		}
		for (const auto& tile : tx.tiles) {
			tiles[tile.key] = revision;
		}
		return revision;
	}
	void Journal::push(uint64_t revision, Bytes data) {
		bytes += data.size();
		records.push_back({ revision, std::move(data) });
		while (!records.empty() && (records.size() > 5000 || bytes > MaxJournalBytes)) {
			bytes -= records.front().bytes.size();
			records.pop_front();
		}
	}
	bool Journal::covers(uint64_t after, uint64_t current) const {
		if (after > current) {
			return false;
		}
		if (after == current) {
			return true;
		}
		return !records.empty() && records.front().revision <= after + 1 && records.back().revision == current;
	}
} // namespace Multiplayer
