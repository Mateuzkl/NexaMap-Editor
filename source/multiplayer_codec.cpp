// NexaMap multiplayer. SPDX-License-Identifier: GPL-3.0-or-later
#include "main.h"
#include "multiplayer_codec.h"
#include "multiplayer_crypto.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "complexitem.h"
#include "iomap_otbm.h"
#include "creature.h"
#include "spawn.h"
#include "settings.h"
#include <unordered_map>

namespace Multiplayer {
	namespace {
		constexpr uint32_t MaxObjects = 4096;
		constexpr uint32_t MaxEntities = 65535;
		class NetworkMapFormat final : public IOMap {
		public:
			NetworkMapFormat() {
				version.otbm = MAP_OTBM_4;
			}
			bool loadMap(Map&, const FileName&) override {
				return false;
			}
			bool saveMap(Map&, const FileName&) override {
				return false;
			}
		};
		void position(Writer& out, const Position& pos) {
			if (!pos.isValid()) {
				throw Error("Invalid map position.");
			}
			out.u64(tileKey(static_cast<uint16_t>(pos.x), static_cast<uint16_t>(pos.y), static_cast<uint8_t>(pos.z)));
		}
		Position position(Reader& in) {
			auto key = in.u64();
			if (!validTileKey(key)) {
				throw Error("Invalid map position.");
			}
			return { tileX(key), tileY(key), tileZ(key) };
		}
		void attributes(Writer& out, const SpawnAttributeMap& values) {
			if (values.size() > 256) {
				throw Error("Too many spawn attributes.");
			}
			out.u16(static_cast<uint16_t>(values.size()));
			for (const auto& [key, value] : values) {
				out.string(key, 256);
				out.string(value, 16384);
			}
		}
		SpawnAttributeMap attributes(Reader& in) {
			auto count = in.u16();
			if (count > 256) {
				throw Error("Too many spawn attributes.");
			}
			SpawnAttributeMap result;
			while (count--) {
				auto name = in.string(256);
				auto value = in.string(16384);
				if (!result.emplace(name, value).second) {
					throw Error("Duplicate spawn attribute.");
				}
			}
			return result;
		}
		void validateItemTree(std::span<const uint8_t> bytes, uint32_t& objects) {
			// Validate the escaped node structure before the legacy OTBM parser sees it.
			struct Node {
				Bytes header;
				bool children = false;
			};
			std::vector<Node> stack;
			bool ended = false;
			for (size_t i = 0; i < bytes.size(); ++i) {
				auto b = bytes[i];
				if (ended) {
					throw Error("Trailing item node data.");
				}
				if (b == NODE_START) {
					if (++objects > MaxObjects || stack.size() >= 32) {
						throw Error("Item nesting/object limit exceeded.");
					}
					if (!stack.empty()) {
						stack.back().children = true;
					}
					stack.push_back({});
					continue;
				}
				if (stack.empty()) {
					throw Error("Invalid item node.");
				}
				if (b == NODE_END) {
					auto& h = stack.back().header;
					if (h.size() != 3 || h[0] != OTBM_ITEM || !g_items.typeExists(h[1] | (uint16_t(h[2]) << 8))) {
						throw Error("Unknown item ID or node type.");
					}
					stack.pop_back();
					if (stack.empty()) {
						ended = true;
					}
					continue;
				}
				if (b == ESCAPE_CHAR) {
					if (++i == bytes.size()) {
						throw Error("Truncated item escape.");
					}
					b = bytes[i];
					if (b != NODE_START && b != NODE_END && b != ESCAPE_CHAR) {
						throw Error("Invalid item escape.");
					}
				}
				if (stack.back().children) {
					throw Error("Item attributes after child nodes.");
				}
				if (stack.back().header.size() < 3) {
					stack.back().header.push_back(b);
				}
			}
			if (!ended || !stack.empty()) {
				throw Error("Unterminated item node.");
			}
		}
		void writeItem(Writer& out, const Item* item) {
			NetworkMapFormat format;
			MemoryNodeFileWriteHandle writer;
			if (!item || !item->serializeItemNode_OTBM(format, writer) || writer.error_code != FILE_NO_ERROR) {
				throw Error("Cannot serialize this item.");
			}
			out.bytes({ writer.getData(), writer.getSize() }, MaxTile);
		}
		std::unique_ptr<Item> readItem(Reader& in, uint32_t& objects) {
			auto bytes = in.bytes(MaxTile);
			validateItemTree(bytes, objects);
			MemoryNodeFileReadHandle file(bytes.data(), bytes.size());
			auto* root = file.getRootNode();
			uint8_t type;
			if (!root || !root->getU8(type) || type != OTBM_ITEM) {
				throw Error("Invalid item node.");
			}
			NetworkMapFormat format;
			std::unique_ptr<Item> item(Item::Create_OTBM(format, root, nullptr, true));
			if (!item || !item->unserializeItemNode_OTBM(format, root)) {
				throw Error("Invalid item attributes.");
			}
			if (file.error_code != FILE_NO_ERROR) {
				throw Error("Malformed item tree.");
			}
			return item;
		}
		struct TownData {
			uint32_t id;
			std::string name;
			Position temple;
		};
		struct Metadata {
			std::vector<TownData> towns;
			std::vector<HouseSnapshot> houses;
			std::map<std::string, Position> waypoints;
			std::map<std::string, uint32_t> zones;
		};
		Metadata readMetadata(std::span<const uint8_t> bytes) {
			if (bytes.size() > MaxMetadata) {
				throw Error("Metadata size limit exceeded.");
			}
			Reader in(bytes);
			if (in.u8() != 1) {
				throw Error("Unknown metadata format.");
			}
			Metadata data;
			std::set<uint32_t> townIds, houseIds, zoneIds;
			auto count = in.u32();
			if (count > MaxEntities || count > in.remaining() / 16) {
				throw Error("Invalid town count.");
			}
			while (count--) {
				TownData t;
				t.id = in.u32();
				t.name = in.string(1024);
				t.temple = position(in);
				if (!t.id || !townIds.insert(t.id).second) {
					throw Error("Duplicate/invalid town ID.");
				}
				data.towns.push_back(std::move(t));
			}
			count = in.u32();
			if (count > MaxEntities || count > in.remaining() / 37) {
				throw Error("Invalid house count.");
			}
			while (count--) {
				HouseSnapshot h;
				h.id = in.u32();
				h.name = in.string(1024);
				h.townid = in.u32();
				h.rent = static_cast<int32_t>(in.u32());
				h.requiredReset = in.u32();
				h.clientid = in.u32();
				h.beds = static_cast<int32_t>(in.u32());
				h.guildhall = in.boolean();
				h.exit = position(in);
				if (!h.id || !houseIds.insert(h.id).second || (h.townid && !townIds.contains(h.townid))) {
					throw Error("Invalid house/town reference.");
				}
				data.houses.push_back(std::move(h));
			}
			count = in.u32();
			if (count > MaxEntities || count > in.remaining() / 12) {
				throw Error("Invalid waypoint count.");
			}
			while (count--) {
				auto name = in.string(1024);
				auto pos = position(in);
				if (name.empty() || !data.waypoints.emplace(name, pos).second) {
					throw Error("Duplicate waypoint.");
				}
			}
			count = in.u32();
			if (count > MaxEntities || count > in.remaining() / 8) {
				throw Error("Invalid zone count.");
			}
			while (count--) {
				auto name = in.string(1024);
				auto id = in.u32();
				if (!Zones::isValidName(name) || !Zones::isValidID(id) || !zoneIds.insert(id).second || !data.zones.emplace(name, id).second) {
					throw Error("Invalid zone.");
				}
			}
			in.finish();
			return data;
		}
		void collectItems(const Item* item, std::map<uint16_t, uint32_t>& uids, std::vector<const Item*>& all) {
			if (!item) {
				return;
			}
			all.push_back(item);
			if (all.size() > MaxTiles * 16) {
				throw Error("Too many objects in transaction.");
			}
			if (auto uid = item->getUniqueID()) {
				++uids[uid];
			}
			if (const auto* container = dynamic_cast<const Container*>(item)) {
				for (size_t i = 0; i < container->getItemCount(); ++i) {
					collectItems(container->getItem(i), uids, all);
				}
			}
		}
	}
	Bytes encodeTile(const Tile* tile) {
		Writer out(MaxTile);
		out.u8(1);
		out.u32(tile ? tile->getMapFlags() : 0);
		out.u32(tile ? tile->getHouseID() : 0);
		const auto zoneCount = tile ? tile->zones.size() : 0;
		if (zoneCount > 1024) {
			throw Error("Too many zones on one tile.");
		}
		out.u16(static_cast<uint16_t>(zoneCount));
		if (tile) {
			for (auto id : tile->zones) {
				out.u32(id);
			}
		}
		out.u8(tile && tile->ground ? 1 : 0);
		if (tile && tile->ground) {
			writeItem(out, tile->ground);
		}
		const auto itemCount = tile ? tile->items.size() : 0;
		if (itemCount > MaxObjects) {
			throw Error("Too many items on one tile.");
		}
		out.u16(static_cast<uint16_t>(itemCount));
		if (tile) {
			for (const auto* item : tile->items) {
				writeItem(out, item);
			}
		}
		out.u8(tile && tile->spawn ? 1 : 0);
		if (tile && tile->spawn) {
			out.u8(static_cast<uint8_t>(tile->spawn->getSize()));
			for (auto kind : { SpawnAreaKind::Mixed, SpawnAreaKind::Monsters, SpawnAreaKind::Npcs }) {
				out.u8(tile->spawn->hasSourceKind(kind));
				if (tile->spawn->hasSourceKind(kind)) {
					attributes(out, tile->spawn->getSourceAttributes(kind));
				}
			}
		}
		out.u8(tile && tile->creature ? 1 : 0);
		if (tile && tile->creature) {
			const auto& c = *tile->creature;
			out.string(c.getName(), 1024);
			out.u8(c.isNpc());
			out.u32(c.getSpawnTime());
			out.u8(c.getDirection());
			out.u8(c.hasSpawnDirection());
			out.u32(c.getWeight());
			out.u8(c.hasSpawnWeight());
			out.u8(c.hasSpawnSource());
			if (c.hasSpawnSource()) {
				position(out, c.getSpawnSource());
			}
			attributes(out, c.getSpawnAttributes());
			out.u8(static_cast<uint8_t>(c.getAlternativeKind()));
			if (c.getSpawnAlternatives().size() > 256) {
				throw Error("Too many spawn alternatives.");
			}
			out.u16(static_cast<uint16_t>(c.getSpawnAlternatives().size()));
			for (const auto& a : c.getSpawnAlternatives()) {
				out.string(a.name, 1024);
				out.u8(a.isNpc);
				out.u32(a.weight);
				out.u8(a.hasWeight);
				attributes(out, a.attributes);
			}
		}
		return std::move(out.data);
	}
	std::unique_ptr<Tile> decodeTile(Map& map, uint64_t key, std::span<const uint8_t> data) {
		if (!validTileKey(key) || data.size() > MaxTile) {
			throw Error("Invalid tile.");
		}
		Reader in(data);
		if (in.u8() != 1) {
			throw Error("Unknown tile format.");
		}
		auto* loc = map.createTileL(tileX(key), tileY(key), tileZ(key));
		std::unique_ptr<Tile> tile(map.allocator(loc));
		tile->setMapFlags(in.u32());
		tile->house_id = in.u32();
		auto zones = in.u16();
		if (zones > 1024) {
			throw Error("Too many zones on tile.");
		}
		while (zones--) {
			auto id = in.u32();
			if (!id || !tile->zones.insert(id).second) {
				throw Error("Invalid zone ID.");
			}
		}
		uint32_t objects = 0;
		if (in.boolean()) {
			tile->ground = readItem(in, objects).release();
		}
		auto items = in.u16();
		if (items > MaxObjects) {
			throw Error("Too many items.");
		}
		while (items--) {
			tile->items.push_back(readItem(in, objects).release());
		}
		if (in.boolean()) {
			tile->spawn = new Spawn(in.u8());
			for (auto kind : { SpawnAreaKind::Mixed, SpawnAreaKind::Monsters, SpawnAreaKind::Npcs }) {
				if (in.boolean()) {
					tile->spawn->setSourceAttributes(kind, attributes(in));
				}
			}
		}
		if (in.boolean()) {
			auto name = in.string(1024);
			if (!g_creatures[name]) {
				throw Error("Unknown creature: " + name);
			}
			tile->creature = new Creature(name);
			auto& c = *tile->creature;
			c.setSpawnType(in.boolean());
			auto time = in.u32();
			if (time > INT32_MAX) {
				throw Error("Invalid spawn time.");
			}
			c.setSpawnTime(time);
			auto direction = in.u8();
			if (direction > 3) {
				throw Error("Invalid creature direction.");
			}
			c.setSpawnDirection(static_cast<Direction>(direction), in.boolean());
			auto weight = in.u32();
			c.setSpawnWeight(weight, in.boolean());
			if (in.boolean()) {
				c.setSpawnSource(position(in));
			}
			c.setSpawnAttributes(attributes(in));
			auto kind = in.u8();
			if (kind > 2) {
				throw Error("Invalid spawn alternative kind.");
			}
			c.setAlternativeKind(static_cast<SpawnAlternativeKind>(kind));
			auto count = in.u16();
			if (count > 256) {
				throw Error("Too many spawn alternatives.");
			}
			while (count--) {
				SpawnVariantData a;
				a.name = in.string(1024);
				a.isNpc = in.boolean();
				a.weight = in.u32();
				a.hasWeight = in.boolean();
				a.attributes = attributes(in);
				c.addSpawnAlternative(a);
			}
		}
		in.finish();
		// A lossy or noncanonical legacy item parse must never be applied to a map.
		if (encodeTile(tile.get()) != Bytes(data.begin(), data.end())) {
			throw Error("Tile data cannot be represented losslessly with these assets/settings.");
		}
		return tile;
	}
	Bytes encodeMetadata(Map& map) {
		Writer out(MaxMetadata);
		out.u8(1);
		out.u32(map.towns.count());
		for (const auto& [id, town] : map.towns) {
			out.u32(id);
			out.string(town->getName(), 1024);
			position(out, town->getTemplePosition());
		}
		out.u32(map.houses.count());
		for (const auto& [id, house] : map.houses) {
			const auto h = house->getSnapshot();
			out.u32(id);
			out.string(h.name, 1024);
			out.u32(h.townid);
			out.u32(h.rent);
			out.u32(h.requiredReset);
			out.u32(h.clientid);
			out.u32(h.beds);
			out.u8(h.guildhall);
			position(out, h.exit);
		}
		out.u32(static_cast<uint32_t>(map.waypoints.waypoints.size()));
		for (const auto& [name, wp] : map.waypoints) {
			out.string(name, 1024);
			position(out, wp->pos);
		}
		out.u32(static_cast<uint32_t>(map.zones.size()));
		for (const auto& [name, id] : map.zones) {
			out.string(name, 1024);
			out.u32(id);
		}
		return std::move(out.data);
	}
	void validateMetadata(std::span<const uint8_t> bytes) {
		readMetadata(bytes);
	}
	void applyMetadata(Map& map, std::span<const uint8_t> bytes) {
		const auto data = readMetadata(bytes);
		std::set<uint32_t> towns, houses;
		std::set<std::string> waypoints, zones;
		for (const auto& t : data.towns) {
			towns.insert(t.id);
			auto* town = map.towns.getTown(t.id);
			if (!town) {
				town = new Town(t.id);
				map.towns.addTown(town);
			}
			town->setName(t.name);
			town->setTemplePosition(t.temple);
		}
		for (auto it = map.towns.begin(); it != map.towns.end();) {
			if (!towns.contains(it->first)) {
				delete it->second;
				it = map.towns.erase(it);
			} else {
				++it;
			}
		}
		for (const auto& h : data.houses) {
			houses.insert(h.id);
			auto* house = map.houses.getHouse(h.id);
			if (!house) {
				house = new House(map);
				house->applySnapshot(h);
				map.houses.addHouse(house);
			}
			// Keep the local object/session identity and tile index when updating a house.
			house->setExit(Position());
			house->applySnapshot(h);
			house->setExit(h.exit);
		}
		for (auto it = map.houses.begin(); it != map.houses.end();) {
			auto* house = it->second;
			++it;
			if (!houses.contains(house->getID())) {
				map.houses.removeHouse(house);
			}
		}
		for (const auto& [name, pos] : data.waypoints) {
			waypoints.insert(name);
			auto* wp = map.waypoints.getWaypoint(name);
			if (wp && wp->pos == pos) {
				continue;
			}
			if (wp) {
				map.waypoints.removeWaypoint(name);
			}
			wp = new Waypoint;
			wp->name = name;
			wp->pos = pos;
			map.waypoints.addWaypoint(wp);
		}
		for (auto it = map.waypoints.begin(); it != map.waypoints.end();) {
			auto name = it->first;
			++it;
			if (!waypoints.contains(name)) {
				map.waypoints.removeWaypoint(name);
			}
		}
		// Replace the small registry; tile memberships are carried by tile changes.
		std::vector<std::string> oldZones;
		for (const auto& [name, id] : map.zones) {
			oldZones.push_back(name);
		}
		for (const auto& name : oldZones) {
			map.zones.removeZone(name);
		}
		for (const auto& [name, id] : data.zones) {
			map.zones.addZone(name, id);
		}
	}
	Digest assetSignature() {
		Writer out(16 * 1024 * 1024);
		out.u32(g_items.MajorVersion);
		out.u32(g_items.MinorVersion);
		out.u32(g_items.BuildNumber);
		out.u8(g_settings.getBoolean(Config::DRAGON_SOULS_OTBM_COUNT_UINT16));
		for (uint32_t id = 1; id <= g_items.getMaxID(); ++id) {
			if (!g_items.typeExists(id)) {
				continue;
			}
			const auto& t = g_items[id];
			out.u16(id);
			out.u16(t.clientID);
			out.u8(t.group);
			out.u8(t.type);
			out.u16(t.volume);
			out.u32(t.charges);
			out.u8(t.stackable);
			out.u8(t.client_chargeable);
			out.u8(t.extra_chargeable);
			out.u8(t.alwaysOnBottom);
			out.u8(t.unpassable);
			out.string(t.name, 4096);
		}
		return sha256(out.data);
	}
	std::string validateTiles(Map& map, const std::vector<std::unique_ptr<Tile>>& tiles, bool allowSensitive, std::span<const uint8_t> metadata) {
		std::set<uint32_t> houses, zones;
		if (!metadata.empty()) {
			const auto proposed = readMetadata(metadata);
			for (const auto& h : proposed.houses) {
				houses.insert(h.id);
			}
			for (const auto& [name, id] : proposed.zones) {
				zones.insert(id);
			}
		}
		std::map<uint16_t, uint32_t> oldIds, newIds;
		std::vector<const Item*> oldItems, newItems;
		for (const auto& tile : tiles) {
			if (tile->house_id && (metadata.empty() ? !map.houses.getHouse(tile->house_id) : !houses.contains(tile->house_id))) {
				return "Unknown house ID.";
			}
			for (auto id : tile->zones) {
				if (metadata.empty() ? !map.zones.hasZone(id) : !zones.contains(id)) {
					return "Unknown zone ID.";
				}
			}
			if (auto* old = map.getTile(tile->getPosition())) {
				collectItems(old->ground, oldIds, oldItems);
				for (const auto* item : old->items) {
					collectItems(item, oldIds, oldItems);
				}
			}
			collectItems(tile->ground, newIds, newItems);
			for (const auto* item : tile->items) {
				collectItems(item, newIds, newItems);
			}
		}
		for (const auto& [uid, count] : newIds) {
			if (count > 1 || map.getUniqueIdCount(uid) > oldIds[uid]) {
				return "Unique ID already exists on another tile.";
			}
			if (!allowSensitive && !oldIds.contains(uid)) {
				return "Creating a Unique ID requires host approval.";
			}
		}
		return {};
	}
} // namespace Multiplayer
