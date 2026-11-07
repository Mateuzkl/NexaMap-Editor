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
		// Both directions share this check: anything accepted here must stay encodable.
		const Position& checkedPosition(const Position& pos) {
			if (!pos.isValid()) {
				throw Error("Invalid map position.");
			}
			return pos;
		}
		void position(Writer& out, const Position& value) {
			const auto& pos = checkedPosition(value);
			out.u64(tileKey(static_cast<uint16_t>(pos.x), static_cast<uint16_t>(pos.y), static_cast<uint8_t>(pos.z)));
		}
		Position position(Reader& in) {
			auto key = in.u64();
			if (!validTileKey(key)) {
				throw Error("Invalid map position.");
			}
			const Position pos(tileX(key), tileY(key), tileZ(key));
			return checkedPosition(pos);
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
			uint8_t format_version = 1;
			std::vector<TownData> towns;
			std::vector<HouseSnapshot> houses;
			std::map<std::string, Position> waypoints;
			std::map<std::string, std::string> waypoint_category;
			std::vector<std::string> waypoint_categories;
			std::vector<std::string> uncategorized_order;
			std::map<std::string, std::vector<std::string>> category_waypoint_order;
			std::map<std::string, uint32_t> zones;
		};
		void readWaypointGroups(Reader& in, Metadata& data, const std::set<std::string>& waypointKeys) {
			auto catCount = in.u32();
			if (catCount > MaxEntities) {
				throw Error("Invalid waypoint category count.");
			}
			std::set<std::string> categoryNames;
			while (catCount--) {
				const auto category = in.string(1024);
				if (category.empty() || !categoryNames.insert(as_lower_str(category)).second) {
					throw Error("Duplicate/invalid waypoint category.");
				}
				data.waypoint_categories.push_back(category);
			}
			auto uncatCount = in.u32();
			if (uncatCount > MaxEntities) {
				throw Error("Invalid uncategorized waypoint count.");
			}
			while (uncatCount--) {
				const auto name = in.string(1024);
				if (name.empty() || !waypointKeys.contains(as_lower_str(name))) {
					throw Error("Invalid uncategorized waypoint reference.");
				}
				data.uncategorized_order.push_back(name);
			}
			for (const auto& category : data.waypoint_categories) {
				auto orderCount = in.u32();
				if (orderCount > MaxEntities) {
					throw Error("Invalid category waypoint count.");
				}
				std::vector<std::string> order;
				std::set<std::string> seenInCategory;
				while (orderCount--) {
					const auto name = in.string(1024);
					if (name.empty() || !waypointKeys.contains(as_lower_str(name)) || !seenInCategory.insert(as_lower_str(name)).second) {
						throw Error("Invalid category waypoint reference.");
					}
					order.push_back(name);
				}
				data.category_waypoint_order[category] = std::move(order);
			}
		}
		Metadata readMetadata(std::span<const uint8_t> bytes) {
			if (bytes.size() > MaxMetadata) {
				throw Error("Metadata size limit exceeded.");
			}
			Reader in(bytes);
			const uint8_t formatVersion = in.u8();
			if (formatVersion != 1 && formatVersion != 2) {
				throw Error("Unknown metadata format.");
			}
			Metadata data;
			data.format_version = formatVersion;
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
			std::set<std::string> waypointKeys;
			while (count--) {
				auto name = in.string(1024);
				auto pos = position(in);
				if (name.empty() || !waypointKeys.insert(as_lower_str(name)).second || !data.waypoints.emplace(name, pos).second) {
					throw Error("Duplicate waypoint.");
				}
				if (formatVersion >= 2) {
					const auto category = in.string(1024);
					data.waypoint_category[name] = category;
				}
			}
			if (formatVersion >= 2) {
				readWaypointGroups(in, data, waypointKeys);
				const std::set<std::string> categoryNames(data.waypoint_categories.begin(), data.waypoint_categories.end());
				std::set<std::string> ordered;
				std::map<std::string, std::string> categoryByKey;
				for (const auto& [name, category] : data.waypoint_category) {
					categoryByKey[as_lower_str(name)] = category;
				}
				const auto expectCategory = [&](const std::string& name, const std::string& category) {
					// Waypoints::applyOrderingFromMetadata refuses these after the map already changed.
					const auto key = as_lower_str(name);
					const auto found = categoryByKey.find(key);
					if (!ordered.insert(key).second || found == categoryByKey.end() || found->second != category) {
						throw Error("Duplicate or mismatched waypoint ordering.");
					}
				};
				for (const auto& name : data.uncategorized_order) {
					expectCategory(name, {});
				}
				for (const auto& [category, order] : data.category_waypoint_order) {
					for (const auto& name : order) {
						expectCategory(name, category);
					}
				}
				for (const auto& [name, category] : data.waypoint_category) {
					(void)name;
					if (!category.empty() && !categoryNames.contains(category)) {
						throw Error("Unknown waypoint category reference.");
					}
				}
				if (ordered.size() != waypointKeys.size()) {
					throw Error("Incomplete waypoint ordering.");
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
		void checkTeleports(const Item* item) {
			if (!item) {
				return;
			}
			if (const auto* teleport = dynamic_cast<const Teleport*>(item); teleport && (teleport->getZ() < 0 || teleport->getZ() > MAP_MAX_LAYER)) {
				throw Error("Invalid teleport destination.");
			}
			if (const auto* container = dynamic_cast<const Container*>(item)) {
				for (const auto* child : container->getVector()) {
					checkTeleports(child);
				}
			}
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
	std::unique_ptr<Tile> decodeTile(Map& map, uint64_t key, std::span<const uint8_t> data, int maxSpawnRadius) {
		if (!validTileKey(key) || data.size() > MaxTile) {
			throw Error("Invalid tile.");
		}
		Reader in(data);
		if (in.u8() != 1) {
			throw Error("Unknown tile format.");
		}
		std::unique_ptr<Tile> tile(newd Tile(tileX(key), tileY(key), tileZ(key)));
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
			checkTeleports(tile->ground);
		}
		auto items = in.u16();
		if (items > MaxObjects) {
			throw Error("Too many items.");
		}
		while (items--) {
			tile->items.push_back(readItem(in, objects).release());
			checkTeleports(tile->items.back());
		}
		if (in.boolean()) {
			const int radius = in.u8();
			if (radius > maxSpawnRadius) {
				// A large spawn loaded from XML stays editable, it just cannot be created.
				const auto* existing = map.getTile(tileX(key), tileY(key), tileZ(key));
				if (!existing || !existing->spawn || existing->spawn->getSize() != radius) {
					throw Error("Spawn radius exceeds the multiplayer limit.");
				}
			}
			tile->spawn = new Spawn(radius);
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
	Bytes encodeMetadata(const Map& map) {
		Writer out(MaxMetadata);
		out.u8(2);
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
		std::string waypointError;
		if (!map.waypoints.validateInvariants(&waypointError)) {
			throw Error("Invalid waypoint state: " + waypointError);
		}
		out.u32(static_cast<uint32_t>(map.waypoints.size()));
		for (const auto& [name, wp] : map.waypoints) {
			out.string(wp->name, 1024);
			position(out, wp->pos);
			out.string(wp->category, 1024);
		}
		out.u32(static_cast<uint32_t>(map.waypoints.categories().size()));
		for (const auto& category : map.waypoints.categories()) {
			out.string(category, 1024);
		}
		out.u32(static_cast<uint32_t>(map.waypoints.uncategorizedOrder().size()));
		for (const auto& waypointName : map.waypoints.uncategorizedOrder()) {
			out.string(waypointName, 1024);
		}
		for (const auto& category : map.waypoints.categories()) {
			const auto& categoryOrders = map.waypoints.categoryWaypointOrders();
			const auto found = categoryOrders.find(category);
			if (found == categoryOrders.end()) {
				out.u32(0);
			} else {
				out.u32(static_cast<uint32_t>(found->second.size()));
				for (const auto& waypointName : found->second) {
					out.string(waypointName, 1024);
				}
			}
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
		std::set<std::string> waypoints;
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
			waypoints.insert(as_lower_str(name)); // Registry keys are lowercase.
			const std::string category = data.format_version >= 2 && data.waypoint_category.count(name) != 0 ? data.waypoint_category.at(name) : std::string();
			if (Waypoint* waypoint = map.waypoints.getWaypoint(name)) {
				if (waypoint->pos != pos) {
					map.waypoints.moveWaypoint(name, pos);
				}
				continue;
			}
			auto waypoint = std::make_unique<Waypoint>();
			waypoint->name = name;
			waypoint->pos = pos;
			waypoint->category = category;
			map.waypoints.addWaypoint(std::move(waypoint));
		}
		for (auto it = map.waypoints.begin(); it != map.waypoints.end();) {
			auto name = it->first;
			++it;
			if (!waypoints.contains(name)) {
				map.waypoints.removeWaypoint(name);
			}
		}
		if (data.format_version >= 2) {
			if (!map.waypoints.applyOrderingFromMetadata(
					data.waypoint_categories,
					data.uncategorized_order,
					data.category_waypoint_order,
					data.waypoint_category
				)) {
				throw Error("Invalid waypoint group metadata.");
			}
		} else {
			map.waypoints.clearGroups();
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
	std::string describeMetadataChange(std::span<const uint8_t> before, std::span<const uint8_t> after) {
		using Summary = std::map<std::string, std::string>;
		const auto pos = [](const Position& p) {
			return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z);
		};
		struct Lists {
			Summary towns, houses, waypoints, zones;
		};
		const auto summarize = [&](const Metadata& data) {
			Lists lists;
			for (const auto& t : data.towns) {
				lists.towns["#" + std::to_string(t.id) + " " + t.name] = pos(t.temple);
			}
			for (const auto& h : data.houses) {
				lists.houses["#" + std::to_string(h.id) + " " + h.name] = pos(h.exit) + " town " + std::to_string(h.townid) + " rent " + std::to_string(h.rent) + " reset " + std::to_string(h.requiredReset) + " beds " + std::to_string(h.beds) + (h.guildhall ? " guildhall" : "");
			}
			for (const auto& [name, position] : data.waypoints) {
				const auto category = data.waypoint_category.find(name);
				lists.waypoints[name] = pos(position) + (category == data.waypoint_category.end() ? "" : " " + category->second);
			}
			for (const auto& [name, id] : data.zones) {
				lists.zones[name] = std::to_string(id);
			}
			return lists;
		};
		const auto oldLists = summarize(readMetadata(before));
		const auto newLists = summarize(readMetadata(after));
		std::string text;
		const auto compare = [&text](const char* label, const Summary& oldValues, const Summary& newValues) {
			constexpr size_t MaxListed = 4;
			size_t listed = 0, hidden = 0;
			std::string entries;
			const auto add = [&](char mark, const std::string& name, const std::string& value) {
				if (listed++ >= MaxListed) {
					++hidden;
					return;
				}
				entries += std::string(entries.empty() ? " " : ", ") + mark + name + (value.empty() ? "" : " (" + value + ")");
			};
			for (const auto& [name, value] : newValues) {
				const auto old = oldValues.find(name);
				if (old == oldValues.end()) {
					add('+', name, value);
				} else if (old->second != value) {
					add('~', name, value);
				}
			}
			for (const auto& [name, value] : oldValues) {
				if (!newValues.contains(name)) {
					add('-', name, {});
				}
			}
			if (listed) {
				text += (text.empty() ? "" : "; ") + std::string(label) + entries + (hidden ? ", and " + std::to_string(hidden) + " more" : "");
			}
		};
		compare("Towns", oldLists.towns, newLists.towns);
		compare("Houses", oldLists.houses, newLists.houses);
		compare("Waypoints", oldLists.waypoints, newLists.waypoints);
		compare("Zones", oldLists.zones, newLists.zones);
		return printableText(text.empty() ? "Metadata order or waypoint groups only" : text);
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
	uint64_t newSpawnArea(Map& map, const std::vector<DecodedTile>& tiles) {
		uint64_t area = 0;
		for (const auto& [key, tile] : tiles) {
			if (!tile->spawn) {
				continue;
			}
			const auto* old = map.getTile(tileX(key), tileY(key), tileZ(key));
			if (!old || !old->spawn || old->spawn->getSize() != tile->spawn->getSize()) {
				const uint64_t side = uint64_t(tile->spawn->getSize()) * 2 + 1;
				area += side * side;
			}
		}
		return area;
	}
	std::string validateTiles(Map& map, const std::vector<DecodedTile>& tiles, bool allowSensitive, std::span<const uint8_t> metadata, bool snapshot) {
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
		if (!snapshot && newSpawnArea(map, tiles) > MaxTransactionSpawnArea) {
			return "This edit creates too much spawn area at once. Split it into smaller edits.";
		}
		std::map<uint16_t, uint32_t> oldIds, newIds;
		std::vector<const Item*> oldItems, newItems;
		for (const auto& [key, tile] : tiles) {
			if (tile->house_id && (metadata.empty() ? !map.houses.getHouse(tile->house_id) : !houses.contains(tile->house_id))) {
				return "Unknown house ID.";
			}
			for (auto id : tile->zones) {
				if (metadata.empty() ? !map.zones.hasZone(id) : !zones.contains(id)) {
					return "Unknown zone ID.";
				}
			}
			if (auto* old = map.getTile(tileX(key), tileY(key), tileZ(key))) {
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
		if (snapshot) {
			return {};
		}
		for (const auto& [uid, count] : newIds) {
			const auto old = oldIds.find(uid);
			const uint32_t replaced = old == oldIds.end() ? 0 : old->second;
			// Duplicates a loaded map already has stay editable; only a new instance is refused.
			if (count > replaced && map.getUniqueIdCount(uid) + count > replaced + 1) {
				return "Unique ID already exists on another tile.";
			}
			if (!allowSensitive && old == oldIds.end()) {
				return "Creating a Unique ID requires host approval.";
			}
		}
		return {};
	}
} // namespace Multiplayer
