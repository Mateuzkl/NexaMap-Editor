// SPDX-License-Identifier: GPL-3.0-or-later
// Sandboxed, value-only Lua extension runtime. No editor-owned pointer crosses
// into Lua; scripts produce a bounded plan that the editor validates later.
#ifndef NEXAMAP_LUA_EXTENSION_RUNTIME_H_
#define NEXAMAP_LUA_EXTENSION_RUNTIME_H_

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace LuaExtension {

	struct Position {
		int x = 0;
		int y = 0;
		int z = 7;
		bool operator==(const Position&) const = default;
		bool operator<(const Position& other) const {
			return z != other.z ? z < other.z : y != other.y ? y < other.y
															 : x < other.x;
		}
	};

	struct ItemInfo {
		uint16_t activeId = 0;
		uint16_t serverId = 0;
		uint16_t clientId = 0;
		std::string name;
		std::string category;
		bool ambiguousMapping = false;
	};

	struct BrushInfo {
		std::string name;
		std::string kind;
		uint16_t lookId = 0;
	};

	struct TileInfo {
		bool mapped = false;
		uint16_t groundId = 0;
		std::vector<uint16_t> itemIds;
		bool blocking = false;
		bool walkable = false;
		bool creature = false;
		bool spawn = false;
		uint32_t houseId = 0;
		std::vector<unsigned int> zones;
	};

	struct ReferenceInfo {
		std::vector<std::string> groundFamilies;
		std::vector<std::string> brushes;
		std::vector<uint16_t> sourceVerifiedIds;
	};

	struct Context {
		std::vector<Position> selection;
		std::vector<Position> sourceMask;
		std::optional<ReferenceInfo> reference;
		bool activeIdModeKnown = true;
		std::vector<BrushInfo> brushes;
		uint64_t mapSessionId = 0;
		uint64_t workspaceGeneration = 0;
		uint64_t mapChangeGeneration = 0;
		int floor = 7;
		std::function<std::optional<TileInfo>(const Position&)> readTile;
		std::function<std::optional<ItemInfo>(uint16_t)> findItem;
		std::function<std::optional<BrushInfo>(const std::string&)> findBrush;
	};

	enum class OperationKind : uint8_t {
		Brush,
		RawItem,
	};

	struct Operation {
		OperationKind kind = OperationKind::Brush;
		Position position;
		std::string brush;
		uint16_t itemId = 0;
	};

	struct Metadata {
		std::string name;
		std::string version;
		std::string description;
		std::string author;
		std::string category;
		nlohmann::json parameters = nlohmann::json::array();
	};

	struct Limits {
		size_t maxSourceBytes = 256 * 1024;
		size_t maxMemoryBytes = 32 * 1024 * 1024;
		size_t maxOperations = 65536;
		size_t maxSelectionTiles = 65536;
		uint64_t instructionBudget = 3'000'000;
		std::chrono::milliseconds timeBudget { 2000 };
	};

	struct Plan {
		Metadata metadata;
		std::vector<Operation> operations;
		uint64_t seed = 0;
		uint64_t mapSessionId = 0;
		uint64_t workspaceGeneration = 0;
		uint64_t mapChangeGeneration = 0;
		std::vector<Position> allowedPositions;
		std::vector<Position> sourceMask;
		std::string error;
		bool valid() const noexcept {
			return error.empty();
		}
	};

	class Runtime final {
	public:
		static Metadata Inspect(const std::string& source, const std::string& sourceName, std::string& error, const Limits& limits = {});
		static Plan Preview(
			const std::string& source,
			const std::string& sourceName,
			const Context& context,
			const nlohmann::json& parameters = nlohmann::json::object(),
			const Limits& limits = {},
			const std::atomic_bool* cancelled = nullptr
		);
	};

} // namespace LuaExtension

#endif // NEXAMAP_LUA_EXTENSION_RUNTIME_H_
