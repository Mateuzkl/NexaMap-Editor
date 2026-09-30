// SPDX-License-Identifier: GPL-3.0-or-later
#include "lua_extension_runtime.h"

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace LuaExtension {
	namespace {

		struct MemoryLimit {
			size_t used = 0;
			size_t limit = 0;
		};

		void* Allocate(void* userdata, void* pointer, size_t oldSize, size_t newSize) {
			auto& memory = *static_cast<MemoryLimit*>(userdata);
			if (!pointer) {
				oldSize = 0; // Lua may pass a type tag rather than a size for new allocations.
			}
			if (newSize == 0) {
				std::free(pointer);
				memory.used = oldSize <= memory.used ? memory.used - oldSize : 0;
				return nullptr;
			}
			const size_t base = oldSize <= memory.used ? memory.used - oldSize : 0;
			if (base > memory.limit || newSize > memory.limit - base) {
				return nullptr;
			}
			void* next = std::realloc(pointer, newSize);
			if (next) {
				memory.used = base + newSize;
			}
			return next;
		}

		struct RunState {
			const Context* context = nullptr;
			Plan* plan = nullptr;
			const Limits* limits = nullptr;
			const std::atomic_bool* cancelled = nullptr;
			std::chrono::steady_clock::time_point deadline;
			uint64_t instructions = 0;
			uint64_t randomState = 1;
			std::string error;
		};

		RunState& State(lua_State* lua) {
			return **static_cast<RunState**>(lua_getextraspace(lua));
		}

		void BudgetHook(lua_State* lua, lua_Debug*) {
			RunState& state = State(lua);
			state.instructions += 1000;
			if (state.instructions > state.limits->instructionBudget || std::chrono::steady_clock::now() >= state.deadline || (state.cancelled && state.cancelled->load(std::memory_order_relaxed))) {
				luaL_error(lua, "Lua script exceeded execution budget and was cancelled.");
			}
		}

		int DiscardPrint(lua_State*) {
			return 0;
		}

		void OpenSafeLibraries(lua_State* lua) {
			const struct {
				const char* name;
				lua_CFunction open;
			} libraries[] = {
				{ LUA_GNAME, luaopen_base },
				{ LUA_TABLIBNAME, luaopen_table },
				{ LUA_STRLIBNAME, luaopen_string },
				{ LUA_MATHLIBNAME, luaopen_math },
				{ LUA_UTF8LIBNAME, luaopen_utf8 },
			};
			for (const auto& library : libraries) {
				luaL_requiref(lua, library.name, library.open, 1);
				lua_pop(lua, 1);
			}
			for (const char* blocked : { "dofile", "loadfile", "load", "collectgarbage", "require", "module" }) {
				lua_pushnil(lua);
				lua_setglobal(lua, blocked);
			}
			lua_pushcfunction(lua, DiscardPrint);
			lua_setglobal(lua, "print");
			lua_getglobal(lua, "math");
			lua_pushnil(lua);
			lua_setfield(lua, -2, "random");
			lua_pushnil(lua);
			lua_setfield(lua, -2, "randomseed");
			lua_pop(lua, 1);
		}

		class StateOwner final {
		public:
			StateOwner(MemoryLimit& memory, RunState& state) :
				lua_(CreateState(memory)) {
				if (lua_) {
					*static_cast<RunState**>(lua_getextraspace(lua_)) = &state;
					OpenSafeLibraries(lua_);
					lua_sethook(lua_, BudgetHook, LUA_MASKCOUNT, 1000);
				}
			}
			~StateOwner() {
				if (lua_) {
					lua_close(lua_);
				}
			}
			StateOwner(const StateOwner&) = delete;
			StateOwner& operator=(const StateOwner&) = delete;
			lua_State* get() const {
				return lua_;
			}

		private:
			static lua_State* CreateState(MemoryLimit& memory) {
#if LUA_VERSION_NUM >= 505
				return lua_newstate(Allocate, &memory, 0);
#else
				return lua_newstate(Allocate, &memory);
#endif
			}
			lua_State* lua_ = nullptr;
		};

		bool ReadInteger(lua_State* lua, int index, int& value) {
			if (!lua_isinteger(lua, index)) {
				return false;
			}
			const lua_Integer raw = lua_tointeger(lua, index);
			if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) {
				return false;
			}
			value = static_cast<int>(raw);
			return true;
		}

		bool ReadPosition(lua_State* lua, int index, Position& position) {
			if (!lua_istable(lua, index)) {
				return false;
			}
			index = lua_absindex(lua, index);
			lua_getfield(lua, index, "x");
			const bool xOk = ReadInteger(lua, -1, position.x);
			lua_pop(lua, 1);
			lua_getfield(lua, index, "y");
			const bool yOk = ReadInteger(lua, -1, position.y);
			lua_pop(lua, 1);
			lua_getfield(lua, index, "z");
			const bool zOk = ReadInteger(lua, -1, position.z);
			lua_pop(lua, 1);
			return xOk && yOk && zOk && position.x >= 0 && position.x <= 65000 && position.y >= 0 && position.y <= 65000 && position.z >= 0 && position.z <= 15;
		}

		int PositionOffset(lua_State* lua);
		int PositionDistance(lua_State* lua);

		void PushPosition(lua_State* lua, const Position& position) {
			lua_createtable(lua, 0, 5);
			lua_pushinteger(lua, position.x);
			lua_setfield(lua, -2, "x");
			lua_pushinteger(lua, position.y);
			lua_setfield(lua, -2, "y");
			lua_pushinteger(lua, position.z);
			lua_setfield(lua, -2, "z");
			lua_pushcfunction(lua, PositionOffset);
			lua_setfield(lua, -2, "offset");
			lua_pushcfunction(lua, PositionDistance);
			lua_setfield(lua, -2, "distanceTo");
		}

		int PositionNew(lua_State* lua) {
			Position position;
			if (!ReadInteger(lua, 1, position.x) || !ReadInteger(lua, 2, position.y) || !ReadInteger(lua, 3, position.z)) {
				lua_pushnil(lua);
				return 1;
			}
			PushPosition(lua, position);
			return 1;
		}

		int PositionOffset(lua_State* lua) {
			Position position;
			int dx = 0, dy = 0, dz = 0;
			if (!ReadPosition(lua, 1, position) || !ReadInteger(lua, 2, dx) || !ReadInteger(lua, 3, dy)) {
				lua_pushnil(lua);
				return 1;
			}
			if (lua_gettop(lua) >= 4 && !ReadInteger(lua, 4, dz)) {
				lua_pushnil(lua);
				return 1;
			}
			const int64_t x = static_cast<int64_t>(position.x) + dx;
			const int64_t y = static_cast<int64_t>(position.y) + dy;
			const int64_t z = static_cast<int64_t>(position.z) + dz;
			if (x < 0 || x > 65000 || y < 0 || y > 65000 || z < 0 || z > 15) {
				lua_pushnil(lua);
				return 1;
			}
			position = { static_cast<int>(x), static_cast<int>(y), static_cast<int>(z) };
			PushPosition(lua, position);
			return 1;
		}

		int PositionDistance(lua_State* lua) {
			Position left, right;
			if (!ReadPosition(lua, 1, left) || !ReadPosition(lua, 2, right)) {
				lua_pushnil(lua);
				return 1;
			}
			const double dx = static_cast<double>(left.x) - right.x;
			const double dy = static_cast<double>(left.y) - right.y;
			const double dz = static_cast<double>(left.z) - right.z;
			lua_pushnumber(lua, std::sqrt(dx * dx + dy * dy + dz * dz));
			return 1;
		}

		bool Allowed(const Context& context, const Position& position) {
			return std::binary_search(context.selection.begin(), context.selection.end(), position) && !std::binary_search(context.sourceMask.begin(), context.sourceMask.end(), position);
		}

		int SelectionCount(lua_State* lua) {
			const Context* context = State(lua).context;
			lua_pushinteger(lua, context ? static_cast<lua_Integer>(context->selection.size()) : 0);
			return 1;
		}

		int SelectionEmpty(lua_State* lua) {
			const Context* context = State(lua).context;
			lua_pushboolean(lua, !context || context->selection.empty());
			return 1;
		}

		int SelectionFloor(lua_State* lua) {
			const Context* context = State(lua).context;
			lua_pushinteger(lua, context ? context->floor : 7);
			return 1;
		}

		int SelectionBounds(lua_State* lua) {
			const Context* context = State(lua).context;
			if (!context || context->selection.empty()) {
				lua_pushnil(lua);
				return 1;
			}
			Position low = context->selection.front(), high = low;
			for (const Position& position : context->selection) {
				low.x = std::min(low.x, position.x);
				low.y = std::min(low.y, position.y);
				high.x = std::max(high.x, position.x);
				high.y = std::max(high.y, position.y);
			}
			lua_createtable(lua, 0, 2);
			PushPosition(lua, low);
			lua_setfield(lua, -2, "from");
			PushPosition(lua, high);
			lua_setfield(lua, -2, "to");
			return 1;
		}

		int SelectionContains(lua_State* lua) {
			const Context* context = State(lua).context;
			Position position;
			lua_pushboolean(lua, context && ReadPosition(lua, 2, position) && std::binary_search(context->selection.begin(), context->selection.end(), position));
			return 1;
		}

		int NextPosition(lua_State* lua) {
			const Context* context = State(lua).context;
			const lua_Integer index = lua_tointeger(lua, lua_upvalueindex(1));
			if (!context || index < 0 || static_cast<size_t>(index) >= context->selection.size()) {
				return 0;
			}
			PushPosition(lua, context->selection[static_cast<size_t>(index)]);
			lua_pushinteger(lua, index + 1);
			lua_replace(lua, lua_upvalueindex(1));
			return 1;
		}

		int SelectionPositions(lua_State* lua) {
			lua_pushinteger(lua, 0);
			lua_pushcclosure(lua, NextPosition, 1);
			return 1;
		}

		void PushSelection(lua_State* lua) {
			lua_createtable(lua, 0, 6);
			const luaL_Reg methods[] = {
				{ "count", SelectionCount },
				{ "isEmpty", SelectionEmpty },
				{ "floor", SelectionFloor },
				{ "bounds", SelectionBounds },
				{ "contains", SelectionContains },
				{ "positions", SelectionPositions },
				{ nullptr, nullptr },
			};
			luaL_setfuncs(lua, methods, 0);
		}

		int EditorSelection(lua_State* lua) {
			PushSelection(lua);
			return 1;
		}

		void RegisterPositionAndSelection(lua_State* lua) {
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, PositionNew);
			lua_setfield(lua, -2, "new");
			lua_setglobal(lua, "Position");
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, EditorSelection);
			lua_setfield(lua, -2, "selection");
			lua_setglobal(lua, "Editor");
		}

		std::string FieldString(lua_State* lua, int table, const char* key) {
			lua_getfield(lua, table, key);
			size_t length = 0;
			const char* value = lua_tolstring(lua, -1, &length);
			std::string result = value && length <= 4096 ? std::string(value, length) : std::string();
			lua_pop(lua, 1);
			return result;
		}

		int TileHasGround(lua_State* lua) {
			lua_getfield(lua, 1, "groundId");
			lua_pushboolean(lua, lua_tointeger(lua, -1) != 0);
			return 1;
		}

		int TileGetGround(lua_State* lua) {
			lua_getfield(lua, 1, "groundId");
			return 1;
		}

		int TileGetItems(lua_State* lua) {
			lua_getfield(lua, 1, "items");
			return 1;
		}

		int TileFlag(lua_State* lua) {
			const char* field = lua_tostring(lua, lua_upvalueindex(1));
			lua_getfield(lua, 1, field);
			return 1;
		}

		void AddFieldMethod(lua_State* lua, const char* method, const char* field) {
			lua_pushstring(lua, field);
			lua_pushcclosure(lua, TileFlag, 1);
			lua_setfield(lua, -2, method);
		}

		int MapGetTile(lua_State* lua) {
			const Context* context = State(lua).context;
			Position position;
			if (!context || !ReadPosition(lua, 1, position) || !context->readTile) {
				lua_pushnil(lua);
				return 1;
			}
			auto tile = context->readTile(position);
			if (!tile) {
				lua_pushnil(lua);
				return 1;
			}
			lua_createtable(lua, 0, 16);
			lua_pushboolean(lua, tile->mapped);
			lua_setfield(lua, -2, "mapped");
			lua_pushinteger(lua, tile->groundId);
			lua_setfield(lua, -2, "groundId");
			lua_createtable(lua, static_cast<int>(tile->itemIds.size()), 0);
			for (size_t index = 0; index < tile->itemIds.size(); ++index) {
				lua_pushinteger(lua, tile->itemIds[index]);
				lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
			}
			lua_setfield(lua, -2, "items");
			lua_pushboolean(lua, tile->blocking);
			lua_setfield(lua, -2, "blocking");
			lua_pushboolean(lua, tile->walkable);
			lua_setfield(lua, -2, "walkable");
			lua_pushboolean(lua, tile->creature);
			lua_setfield(lua, -2, "creature");
			lua_pushboolean(lua, tile->spawn);
			lua_setfield(lua, -2, "spawn");
			lua_pushinteger(lua, tile->houseId);
			lua_setfield(lua, -2, "houseId");
			lua_createtable(lua, static_cast<int>(tile->zones.size()), 0);
			for (size_t index = 0; index < tile->zones.size(); ++index) {
				lua_pushinteger(lua, tile->zones[index]);
				lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
			}
			lua_setfield(lua, -2, "zones");
			lua_pushcfunction(lua, TileHasGround);
			lua_setfield(lua, -2, "hasGround");
			lua_pushcfunction(lua, TileGetGround);
			lua_setfield(lua, -2, "getGroundId");
			lua_pushcfunction(lua, TileGetItems);
			lua_setfield(lua, -2, "getItems");
			AddFieldMethod(lua, "isBlocking", "blocking");
			AddFieldMethod(lua, "isWalkable", "walkable");
			AddFieldMethod(lua, "hasCreature", "creature");
			AddFieldMethod(lua, "hasSpawn", "spawn");
			AddFieldMethod(lua, "getHouseId", "houseId");
			AddFieldMethod(lua, "getZones", "zones");
			return 1;
		}

		int ItemsGet(lua_State* lua) {
			const Context* context = State(lua).context;
			int id = 0;
			if (!context || !ReadInteger(lua, 1, id) || id <= 0 || id > 65535 || !context->findItem) {
				lua_pushnil(lua);
				return 1;
			}
			auto item = context->findItem(static_cast<uint16_t>(id));
			if (!item) {
				lua_pushnil(lua);
				return 1;
			}
			lua_createtable(lua, 0, 6);
			lua_pushinteger(lua, item->activeId);
			lua_setfield(lua, -2, "activeId");
			lua_pushinteger(lua, item->serverId);
			lua_setfield(lua, -2, "serverId");
			lua_pushinteger(lua, item->clientId);
			lua_setfield(lua, -2, "clientId");
			lua_pushlstring(lua, item->name.data(), item->name.size());
			lua_setfield(lua, -2, "name");
			lua_pushlstring(lua, item->category.data(), item->category.size());
			lua_setfield(lua, -2, "category");
			lua_pushboolean(lua, item->ambiguousMapping);
			lua_setfield(lua, -2, "ambiguousMapping");
			return 1;
		}

		bool StageOperation(lua_State* lua, Operation operation) {
			RunState& state = State(lua);
			if (std::chrono::steady_clock::now() >= state.deadline || (state.cancelled && state.cancelled->load(std::memory_order_relaxed))) {
				state.error = "Lua script exceeded execution budget and was cancelled.";
				return false;
			}
			if (!state.context || !state.plan || !Allowed(*state.context, operation.position)) {
				state.error = "Operation is outside the exact target selection or overlaps the reference source.";
				return false;
			}
			if (state.plan->operations.size() >= state.limits->maxOperations) {
				state.error = "Lua operation limit exceeded.";
				return false;
			}
			if (operation.kind == OperationKind::Brush) {
				const auto brush = state.context->findBrush ? state.context->findBrush(operation.brush) : std::nullopt;
				if (!brush || brush->name != operation.brush) {
					state.error = "Unknown brush in active resources.";
					return false;
				}
				if (state.context->reference && std::find(state.context->reference->brushes.begin(), state.context->reference->brushes.end(), operation.brush) == state.context->reference->brushes.end()) {
					state.error = "Brush was not verified in the captured reference source.";
					return false;
				}
			} else if (operation.kind == OperationKind::RawItem && state.context->reference) {
				const auto& verified = state.context->reference->sourceVerifiedIds;
				if (std::find(verified.begin(), verified.end(), operation.itemId) == verified.end()) {
					state.error = "Raw item ID was not verified in the captured reference source.";
					return false;
				}
			}
			state.plan->operations.push_back(std::move(operation));
			return true;
		}

		int BrushName(lua_State* lua) {
			lua_getfield(lua, 1, "_name");
			return 1;
		}
		int BrushKind(lua_State* lua) {
			lua_getfield(lua, 1, "_kind");
			return 1;
		}
		int BrushLook(lua_State* lua) {
			lua_getfield(lua, 1, "_lookId");
			return 1;
		}

		int BrushApply(lua_State* lua) {
			Position position;
			if (!ReadPosition(lua, 2, position)) {
				State(lua).error = "Brush.apply requires a valid position.";
				lua_pushboolean(lua, false);
				return 1;
			}
			const std::string name = FieldString(lua, 1, "_name");
			lua_pushboolean(lua, !name.empty() && StageOperation(lua, Operation { OperationKind::Brush, position, name, 0 }));
			return 1;
		}

		int BrushApplyMany(lua_State* lua) {
			if (!lua_istable(lua, 2)) {
				State(lua).error = "Brush.applyMany requires a position array.";
				lua_pushboolean(lua, false);
				return 1;
			}
			const std::string name = FieldString(lua, 1, "_name");
			const size_t count = lua_rawlen(lua, 2);
			if (count > State(lua).limits->maxOperations) {
				State(lua).error = "Lua operation limit exceeded.";
				lua_pushboolean(lua, false);
				return 1;
			}
			for (size_t index = 1; index <= count; ++index) {
				lua_rawgeti(lua, 2, static_cast<lua_Integer>(index));
				Position position;
				const bool valid = ReadPosition(lua, -1, position);
				lua_pop(lua, 1);
				if (!valid || !StageOperation(lua, Operation { OperationKind::Brush, position, name, 0 })) {
					if (State(lua).error.empty()) {
						State(lua).error = "Invalid position in Brush.applyMany.";
					}
					lua_pushboolean(lua, false);
					return 1;
				}
			}
			lua_pushboolean(lua, true);
			return 1;
		}

		void PushBrush(lua_State* lua, const BrushInfo& brush) {
			lua_createtable(lua, 0, 7);
			lua_pushlstring(lua, brush.name.data(), brush.name.size());
			lua_setfield(lua, -2, "_name");
			lua_pushlstring(lua, brush.kind.data(), brush.kind.size());
			lua_setfield(lua, -2, "_kind");
			lua_pushinteger(lua, brush.lookId);
			lua_setfield(lua, -2, "_lookId");
			const luaL_Reg methods[] = {
				{ "name", BrushName },
				{ "kind", BrushKind },
				{ "lookId", BrushLook },
				{ "apply", BrushApply },
				{ "applyMany", BrushApplyMany },
				{ nullptr, nullptr },
			};
			luaL_setfuncs(lua, methods, 0);
		}

		int BrushesFind(lua_State* lua) {
			const Context* context = State(lua).context;
			const char* name = lua_tostring(lua, 1);
			if (!context || !name || !context->findBrush) {
				lua_pushnil(lua);
				return 1;
			}
			auto brush = context->findBrush(name);
			if (brush) {
				PushBrush(lua, *brush);
			} else {
				lua_pushnil(lua);
			}
			return 1;
		}

		int BrushesList(lua_State* lua) {
			const Context* context = State(lua).context;
			const char* filter = lua_tostring(lua, 1);
			lua_createtable(lua, 0, 0);
			if (!context) {
				return 1;
			}
			size_t index = 1;
			for (const auto& brush : context->brushes) {
				if (filter && brush.kind != filter) {
					continue;
				}
				PushBrush(lua, brush);
				lua_rawseti(lua, -2, static_cast<lua_Integer>(index++));
			}
			return 1;
		}

		int BrushesSearch(lua_State* lua) {
			const Context* context = State(lua).context;
			const char* query = lua_tostring(lua, 1);
			lua_createtable(lua, 0, 0);
			if (!context || !query) {
				return 1;
			}
			size_t index = 1;
			for (const auto& brush : context->brushes) {
				if (brush.name.find(query) == std::string::npos) {
					continue;
				}
				PushBrush(lua, brush);
				lua_rawseti(lua, -2, static_cast<lua_Integer>(index++));
			}
			return 1;
		}

		int MapPlaceItem(lua_State* lua) {
			const Context* context = State(lua).context;
			Position position;
			int id = 0;
			if (!context || !context->activeIdModeKnown || !ReadPosition(lua, 1, position) || !ReadInteger(lua, 2, id) || id <= 0 || id > 65535 || !context->findItem) {
				State(lua).error = "Map.placeItem requires a position and active item ID.";
				lua_pushboolean(lua, false);
				return 1;
			}
			auto item = context->findItem(static_cast<uint16_t>(id));
			if (!item || item->activeId != id) {
				State(lua).error = "Unknown or ambiguous active item ID.";
				lua_pushboolean(lua, false);
				return 1;
			}
			lua_pushboolean(lua, StageOperation(lua, Operation { OperationKind::RawItem, position, {}, static_cast<uint16_t>(id) }));
			return 1;
		}

		void RegisterMapResources(lua_State* lua) {
			lua_createtable(lua, 0, 2);
			lua_pushcfunction(lua, MapGetTile);
			lua_setfield(lua, -2, "getTile");
			lua_pushcfunction(lua, MapPlaceItem);
			lua_setfield(lua, -2, "placeItem");
			lua_setglobal(lua, "Map");
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, ItemsGet);
			lua_setfield(lua, -2, "get");
			lua_setglobal(lua, "Items");
			lua_createtable(lua, 0, 3);
			lua_pushcfunction(lua, BrushesFind);
			lua_setfield(lua, -2, "find");
			lua_pushcfunction(lua, BrushesList);
			lua_setfield(lua, -2, "list");
			lua_pushcfunction(lua, BrushesSearch);
			lua_setfield(lua, -2, "search");
			lua_setglobal(lua, "Brushes");
		}

		uint64_t Mix(uint64_t value) {
			value += 0x9e3779b97f4a7c15ULL;
			value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
			value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
			return value ^ (value >> 31);
		}

		uint64_t AdvanceRandom(lua_State* lua) {
			lua_getfield(lua, 1, "_state");
			uint64_t value = static_cast<uint64_t>(lua_tointeger(lua, -1));
			lua_pop(lua, 1);
			value = Mix(value);
			lua_pushinteger(lua, static_cast<lua_Integer>(value));
			lua_setfield(lua, 1, "_state");
			return value;
		}

		int RandomNext(lua_State* lua) {
			const uint64_t value = AdvanceRandom(lua);
			lua_pushnumber(lua, static_cast<double>(value >> 11) / 9007199254740992.0);
			return 1;
		}

		int RandomInteger(lua_State* lua) {
			int low = 0, high = 0;
			if (!ReadInteger(lua, 2, low) || !ReadInteger(lua, 3, high) || low > high) {
				lua_pushnil(lua);
				return 1;
			}
			const uint64_t value = AdvanceRandom(lua);
			const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(high) - low + 1);
			lua_pushinteger(lua, low + static_cast<int64_t>(value % span));
			return 1;
		}

		int RandomChoice(lua_State* lua) {
			if (!lua_istable(lua, 2)) {
				lua_pushnil(lua);
				return 1;
			}
			const size_t length = lua_rawlen(lua, 2);
			if (length == 0 || length > State(lua).limits->maxSelectionTiles) {
				lua_pushnil(lua);
				return 1;
			}
			const uint64_t value = AdvanceRandom(lua);
			lua_rawgeti(lua, 2, static_cast<lua_Integer>(value % length + 1));
			return 1;
		}

		int RandomNew(lua_State* lua) {
			const lua_Integer seed = lua_isinteger(lua, 1) ? lua_tointeger(lua, 1) : static_cast<lua_Integer>(State(lua).randomState);
			lua_createtable(lua, 0, 6);
			lua_pushinteger(lua, seed);
			lua_setfield(lua, -2, "_state");
			lua_pushcfunction(lua, RandomNext);
			lua_setfield(lua, -2, "next");
			lua_pushcfunction(lua, RandomNext);
			lua_setfield(lua, -2, "float");
			lua_pushcfunction(lua, RandomInteger);
			lua_setfield(lua, -2, "integer");
			lua_pushcfunction(lua, RandomInteger);
			lua_setfield(lua, -2, "int");
			lua_pushcfunction(lua, RandomChoice);
			lua_setfield(lua, -2, "choice");
			return 1;
		}

		int NoiseValue(lua_State* lua) {
			int x = 0, y = 0, seed = 0;
			if (!ReadInteger(lua, 1, x) || !ReadInteger(lua, 2, y) || !ReadInteger(lua, 3, seed)) {
				lua_pushnil(lua);
				return 1;
			}
			const uint64_t value = Mix(static_cast<uint64_t>(seed) ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^ static_cast<uint32_t>(y));
			lua_pushnumber(lua, static_cast<double>(value >> 11) / 9007199254740992.0);
			return 1;
		}

		double NumberField(lua_State* lua, int table, const char* name, double fallback) {
			lua_getfield(lua, table, name);
			const double value = lua_isnumber(lua, -1) ? lua_tonumber(lua, -1) : fallback;
			lua_pop(lua, 1);
			return value;
		}

		uint64_t LatticeHash(int64_t x, int64_t y, uint64_t seed) {
			return Mix(seed ^ (static_cast<uint64_t>(x) * 0x9e3779b97f4a7c15ULL) ^ (static_cast<uint64_t>(y) * 0xbf58476d1ce4e5b9ULL));
		}

		double Gradient(uint64_t hash, double x, double y) {
			switch (hash & 7) {
				case 0:
					return x;
				case 1:
					return -x;
				case 2:
					return y;
				case 3:
					return -y;
				case 4:
					return (x + y) * 0.7071067811865476;
				case 5:
					return (x - y) * 0.7071067811865476;
				case 6:
					return (-x + y) * 0.7071067811865476;
				default:
					return (-x - y) * 0.7071067811865476;
			}
		}

		double Perlin(double x, double y, uint64_t seed) {
			const int64_t ix = static_cast<int64_t>(std::floor(x));
			const int64_t iy = static_cast<int64_t>(std::floor(y));
			const double fx = x - ix, fy = y - iy;
			const auto fade = [](double value) { return value * value * value * (value * (value * 6 - 15) + 10); };
			const double u = fade(fx), v = fade(fy);
			const double n00 = Gradient(LatticeHash(ix, iy, seed), fx, fy);
			const double n10 = Gradient(LatticeHash(ix + 1, iy, seed), fx - 1, fy);
			const double n01 = Gradient(LatticeHash(ix, iy + 1, seed), fx, fy - 1);
			const double n11 = Gradient(LatticeHash(ix + 1, iy + 1, seed), fx - 1, fy - 1);
			return std::lerp(std::lerp(n00, n10, u), std::lerp(n01, n11, u), v);
		}

		int NoiseGet(lua_State* lua) {
			if (!lua_istable(lua, 1) || !lua_isnumber(lua, 2) || !lua_isnumber(lua, 3)) {
				lua_pushnil(lua);
				return 1;
			}
			const double x = lua_tonumber(lua, 2), y = lua_tonumber(lua, 3);
			const double width = NumberField(lua, 1, "_width", 0), height = NumberField(lua, 1, "_height", 0);
			if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= width || y >= height || x > 1024 || y > 1024) {
				lua_pushnil(lua);
				return 1;
			}
			const double seedNumber = NumberField(lua, 1, "_seed", 0);
			const double scale = NumberField(lua, 1, "_scale", 0.08);
			const double octaveNumber = NumberField(lua, 1, "_octaves", 4);
			const double persistence = NumberField(lua, 1, "_persistence", 0.5);
			if (!std::isfinite(seedNumber) || std::abs(seedNumber) > 9007199254740992.0 || !std::isfinite(scale) || scale <= 0 || scale > 8 || !std::isfinite(octaveNumber) || octaveNumber < 1 || octaveNumber > 8 || std::floor(octaveNumber) != octaveNumber || !std::isfinite(persistence) || persistence <= 0 || persistence > 1) {
				State(lua).error = "Invalid or modified noise configuration.";
				lua_pushnil(lua);
				return 1;
			}
			const uint64_t seed = static_cast<uint64_t>(static_cast<int64_t>(seedNumber));
			const int octaves = static_cast<int>(octaveNumber);
			double frequency = scale, amplitude = 1, sum = 0, normalization = 0;
			for (int octave = 0; octave < octaves; ++octave) {
				sum += Perlin(x * frequency, y * frequency, seed + static_cast<uint64_t>(octave)) * amplitude;
				normalization += amplitude;
				frequency *= 2;
				amplitude *= persistence;
			}
			const double value = std::clamp(0.5 + sum / (2 * normalization), 0.0, 1.0);
			lua_getfield(lua, 1, "_threshold");
			if (lua_isnumber(lua, -1)) {
				lua_pushnumber(lua, value >= lua_tonumber(lua, -1) ? 1.0 : 0.0);
			} else {
				lua_pushnumber(lua, value);
			}
			return 1;
		}

		int NoiseNormalize(lua_State* lua) {
			lua_pushvalue(lua, 1); // Already returned in normalized [0,1] form.
			return 1;
		}

		int NoiseThreshold(lua_State* lua) {
			if (!lua_istable(lua, 1) || !lua_isnumber(lua, 2) || lua_tonumber(lua, 2) < 0 || lua_tonumber(lua, 2) > 1) {
				lua_pushnil(lua);
				return 1;
			}
			lua_pushvalue(lua, 2);
			lua_setfield(lua, 1, "_threshold");
			lua_pushvalue(lua, 1);
			return 1;
		}

		int NoisePerlin(lua_State* lua) {
			if (!lua_istable(lua, 1)) {
				lua_pushnil(lua);
				return 1;
			}
			const double width = NumberField(lua, 1, "width", 0), height = NumberField(lua, 1, "height", 0);
			const double scale = NumberField(lua, 1, "scale", 0.08);
			const double octaves = NumberField(lua, 1, "octaves", 4);
			const double persistence = NumberField(lua, 1, "persistence", 0.5);
			const double seed = NumberField(lua, 1, "seed", 0);
			if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(scale) || !std::isfinite(persistence) || width < 1 || width > 1024 || height < 1 || height > 1024 || scale <= 0 || scale > 8 || octaves < 1 || octaves > 8 || std::floor(octaves) != octaves || persistence <= 0 || persistence > 1 || !std::isfinite(seed) || std::abs(seed) > 9007199254740992.0) {
				lua_pushnil(lua);
				return 1;
			}
			lua_createtable(lua, 0, 9);
			for (const auto& [field, value] : { std::pair { "_width", width }, { "_height", height }, { "_scale", scale }, { "_octaves", octaves }, { "_persistence", persistence }, { "_seed", seed } }) {
				lua_pushnumber(lua, value);
				lua_setfield(lua, -2, field);
			}
			lua_pushcfunction(lua, NoiseGet);
			lua_setfield(lua, -2, "get");
			lua_pushcfunction(lua, NoiseNormalize);
			lua_setfield(lua, -2, "normalize");
			lua_pushcfunction(lua, NoiseThreshold);
			lua_setfield(lua, -2, "threshold");
			return 1;
		}

		int GeoRectangle(lua_State* lua) {
			int x = 0, y = 0, width = 0, height = 0, z = 7;
			if (!ReadInteger(lua, 1, x) || !ReadInteger(lua, 2, y) || !ReadInteger(lua, 3, width) || !ReadInteger(lua, 4, height) || (lua_gettop(lua) >= 5 && !ReadInteger(lua, 5, z)) || width <= 0 || height <= 0 || static_cast<int64_t>(width) * height > static_cast<int64_t>(State(lua).limits->maxSelectionTiles) || x < 0 || y < 0 || static_cast<int64_t>(x) + width > 65001 || static_cast<int64_t>(y) + height > 65001 || z < 0 || z > 15) {
				lua_pushnil(lua);
				return 1;
			}
			lua_createtable(lua, width * height, 0);
			int index = 1;
			for (int row = 0; row < height; ++row) {
				for (int column = 0; column < width; ++column) {
					PushPosition(lua, { x + column, y + row, z });
					lua_rawseti(lua, -2, index++);
				}
			}
			return 1;
		}

		int GeoCircle(lua_State* lua) {
			int x = 0, y = 0, radius = 0, z = 7;
			if (!ReadInteger(lua, 1, x) || !ReadInteger(lua, 2, y) || !ReadInteger(lua, 3, radius) || (lua_gettop(lua) >= 4 && !ReadInteger(lua, 4, z)) || radius < 0 || radius > 256 || z < 0 || z > 15) {
				lua_pushnil(lua);
				return 1;
			}
			lua_createtable(lua, 0, 0);
			int index = 1;
			for (int dy = -radius; dy <= radius; ++dy) {
				for (int dx = -radius; dx <= radius; ++dx) {
					if (dx * dx + dy * dy > radius * radius || x + dx < 0 || x + dx > 65000 || y + dy < 0 || y + dy > 65000) {
						continue;
					}
					if (static_cast<size_t>(index) > State(lua).limits->maxSelectionTiles) {
						State(lua).error = "Geometry mask limit exceeded.";
						return 1;
					}
					PushPosition(lua, { x + dx, y + dy, z });
					lua_rawseti(lua, -2, index++);
				}
			}
			return 1;
		}

		int GeoLine(lua_State* lua) {
			Position first, last;
			if (!ReadPosition(lua, 1, first) || !ReadPosition(lua, 2, last) || first.z != last.z) {
				lua_pushnil(lua);
				return 1;
			}
			const int count = std::max(std::abs(last.x - first.x), std::abs(last.y - first.y)) + 1;
			if (count > static_cast<int>(State(lua).limits->maxSelectionTiles)) {
				lua_pushnil(lua);
				return 1;
			}
			lua_createtable(lua, count, 0);
			int x = first.x, y = first.y;
			const int dx = std::abs(last.x - first.x), dy = -std::abs(last.y - first.y);
			const int sx = first.x < last.x ? 1 : -1, sy = first.y < last.y ? 1 : -1;
			int error = dx + dy, index = 1;
			while (true) {
				PushPosition(lua, { x, y, first.z });
				lua_rawseti(lua, -2, index++);
				if (x == last.x && y == last.y) {
					break;
				}
				const int twice = error * 2;
				if (twice >= dy) {
					error += dy;
					x += sx;
				}
				if (twice <= dx) {
					error += dx;
					y += sy;
				}
			}
			return 1;
		}

		std::set<Position> ReadMask(lua_State* lua, int index) {
			std::set<Position> positions;
			if (!lua_istable(lua, index)) {
				return positions;
			}
			index = lua_absindex(lua, index);
			const size_t length = std::min<size_t>(lua_rawlen(lua, index), State(lua).limits->maxSelectionTiles + 1);
			for (size_t item = 1; item <= length; ++item) {
				lua_rawgeti(lua, index, static_cast<lua_Integer>(item));
				Position position;
				if (ReadPosition(lua, -1, position)) {
					positions.insert(position);
				}
				lua_pop(lua, 1);
			}
			return positions;
		}

		int GeoSet(lua_State* lua) {
			const int operation = static_cast<int>(lua_tointeger(lua, lua_upvalueindex(1)));
			const auto left = ReadMask(lua, 1);
			const auto right = ReadMask(lua, 2);
			lua_createtable(lua, 0, 0);
			int index = 1;
			if (operation == 0) {
				std::set<Position> combined = left;
				combined.insert(right.begin(), right.end());
				if (combined.size() > State(lua).limits->maxSelectionTiles) {
					State(lua).error = "Geometry mask limit exceeded.";
					return 1;
				}
				for (const Position& position : combined) {
					PushPosition(lua, position);
					lua_rawseti(lua, -2, index++);
				}
			} else {
				for (const Position& position : left) {
					const bool inRight = right.contains(position);
					if ((operation == 1 && inRight) || (operation == 2 && !inRight)) {
						PushPosition(lua, position);
						lua_rawseti(lua, -2, index++);
					}
				}
			}
			return 1;
		}

		lua_Integer PositionKey(const Position& position) {
			return (static_cast<lua_Integer>(position.z) << 40) | (static_cast<lua_Integer>(position.y) << 20) | position.x;
		}

		int CellularIsFloor(lua_State* lua) {
			Position position;
			if (!ReadPosition(lua, 2, position)) {
				lua_pushboolean(lua, false);
				return 1;
			}
			lua_getfield(lua, 1, "_lookup");
			if (!lua_istable(lua, -1)) {
				lua_pushboolean(lua, false);
				return 1;
			}
			lua_rawgeti(lua, -1, PositionKey(position));
			lua_pushboolean(lua, lua_toboolean(lua, -1));
			return 1;
		}

		int CellularAutomata(lua_State* lua) {
			const Context* context = State(lua).context;
			if (!context || !lua_istable(lua, 1)) {
				lua_pushnil(lua);
				return 1;
			}
			int seed = static_cast<int>(State(lua).randomState), iterations = 4;
			double chance = 0.45;
			lua_getfield(lua, 1, "seed");
			if (lua_isinteger(lua, -1)) {
				ReadInteger(lua, -1, seed);
			}
			lua_pop(lua, 1);
			lua_getfield(lua, 1, "iterations");
			if (lua_isinteger(lua, -1)) {
				ReadInteger(lua, -1, iterations);
			}
			lua_pop(lua, 1);
			lua_getfield(lua, 1, "fillChance");
			if (lua_isnumber(lua, -1)) {
				chance = lua_tonumber(lua, -1);
			}
			lua_pop(lua, 1);
			if (iterations < 0 || iterations > 8 || !std::isfinite(chance) || chance < 0 || chance > 1 || context->selection.size() > State(lua).limits->maxSelectionTiles) {
				lua_pushnil(lua);
				return 1;
			}
			std::set<Position> filled;
			size_t processed = 0;
			for (const Position& position : context->selection) {
				if ((++processed & 1023) == 0 && (std::chrono::steady_clock::now() >= State(lua).deadline || (State(lua).cancelled && State(lua).cancelled->load(std::memory_order_relaxed)))) {
					State(lua).error = "Lua script exceeded execution budget and was cancelled.";
					lua_pushnil(lua);
					return 1;
				}
				const uint64_t hash = Mix(static_cast<uint64_t>(seed) ^ (static_cast<uint64_t>(position.x) << 32) ^ static_cast<uint32_t>(position.y));
				if (static_cast<double>(hash >> 11) / 9007199254740992.0 < chance) {
					filled.insert(position);
				}
			}
			for (int pass = 0; pass < iterations; ++pass) {
				std::set<Position> next;
				for (const Position& position : context->selection) {
					if ((++processed & 1023) == 0 && (std::chrono::steady_clock::now() >= State(lua).deadline || (State(lua).cancelled && State(lua).cancelled->load(std::memory_order_relaxed)))) {
						State(lua).error = "Lua script exceeded execution budget and was cancelled.";
						lua_pushnil(lua);
						return 1;
					}
					int neighbors = 0;
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							if ((dx || dy) && filled.contains({ position.x + dx, position.y + dy, position.z })) {
								++neighbors;
							}
						}
					}
					if (neighbors >= 5 || (neighbors == 4 && filled.contains(position))) {
						next.insert(position);
					}
				}
				filled.swap(next);
				if (std::chrono::steady_clock::now() >= State(lua).deadline || (State(lua).cancelled && State(lua).cancelled->load(std::memory_order_relaxed))) {
					State(lua).error = "Lua script exceeded execution budget and was cancelled.";
					break;
				}
			}
			lua_createtable(lua, static_cast<int>(filled.size()), 2);
			lua_createtable(lua, 0, static_cast<int>(filled.size()));
			int index = 1;
			for (const Position& position : filled) {
				PushPosition(lua, position);
				lua_rawseti(lua, -3, index++);
				lua_pushboolean(lua, true);
				lua_rawseti(lua, -2, PositionKey(position));
			}
			lua_setfield(lua, -2, "_lookup");
			lua_pushcfunction(lua, CellularIsFloor);
			lua_setfield(lua, -2, "isFloor");
			return 1;
		}

		int ReturnSelf(lua_State* lua) {
			lua_pushvalue(lua, 1);
			return 1;
		}

		void MakeCallableArray(lua_State* lua) {
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, ReturnSelf);
			lua_setfield(lua, -2, "__call");
			lua_setmetatable(lua, -2);
		}

		int ReferenceCurrent(lua_State* lua) {
			const Context* context = State(lua).context;
			if (!context || !context->reference) {
				lua_pushnil(lua);
				return 1;
			}
			const ReferenceInfo& reference = *context->reference;
			lua_createtable(lua, 0, 3);
			auto addStrings = [lua](const char* field, const std::vector<std::string>& values) {
				lua_createtable(lua, static_cast<int>(values.size()), 0);
				for (size_t index = 0; index < values.size(); ++index) {
					lua_pushlstring(lua, values[index].data(), values[index].size());
					lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
				}
				MakeCallableArray(lua);
				lua_setfield(lua, -2, field);
			};
			addStrings("groundFamilies", reference.groundFamilies);
			addStrings("brushes", reference.brushes);
			lua_createtable(lua, static_cast<int>(reference.sourceVerifiedIds.size()), 0);
			for (size_t index = 0; index < reference.sourceVerifiedIds.size(); ++index) {
				lua_pushinteger(lua, reference.sourceVerifiedIds[index]);
				lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
			}
			MakeCallableArray(lua);
			lua_setfield(lua, -2, "sourceVerifiedIds");
			lua_getfield(lua, -1, "sourceVerifiedIds");
			lua_setfield(lua, -2, "materials");
			return 1;
		}

		int TargetCurrent(lua_State* lua) {
			const Context* context = State(lua).context;
			if (!context || context->selection.empty()) {
				lua_pushnil(lua);
			} else {
				PushSelection(lua);
			}
			return 1;
		}

		void RegisterProcedural(lua_State* lua) {
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, RandomNew);
			lua_setfield(lua, -2, "new");
			lua_setglobal(lua, "Random");
			lua_createtable(lua, 0, 2);
			lua_pushcfunction(lua, NoiseValue);
			lua_setfield(lua, -2, "value");
			lua_pushcfunction(lua, NoisePerlin);
			lua_setfield(lua, -2, "perlin");
			lua_setglobal(lua, "Noise");
			lua_createtable(lua, 0, 2);
			lua_pushcfunction(lua, GeoRectangle);
			lua_setfield(lua, -2, "rectangle");
			lua_pushcfunction(lua, GeoCircle);
			lua_setfield(lua, -2, "circle");
			lua_pushcfunction(lua, GeoLine);
			lua_setfield(lua, -2, "line");
			for (int operation = 0; operation < 3; ++operation) {
				lua_pushinteger(lua, operation);
				lua_pushcclosure(lua, GeoSet, 1);
				lua_setfield(lua, -2, operation == 0 ? "union" : operation == 1 ? "intersection"
																				: "subtract");
			}
			lua_setglobal(lua, "Geo");
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, CellularAutomata);
			lua_setfield(lua, -2, "cellularAutomata");
			lua_setglobal(lua, "Algo");
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, ReferenceCurrent);
			lua_setfield(lua, -2, "current");
			lua_setglobal(lua, "Reference");
			lua_createtable(lua, 0, 1);
			lua_pushcfunction(lua, TargetCurrent);
			lua_setfield(lua, -2, "current");
			lua_setglobal(lua, "Target");
		}

		void PushJson(lua_State* lua, const nlohmann::json& value, int depth = 0) {
			if (depth > 8) {
				lua_pushnil(lua);
				return;
			}
			if (value.is_boolean()) {
				lua_pushboolean(lua, value.get<bool>());
			} else if (value.is_number_integer()) {
				lua_pushinteger(lua, value.get<lua_Integer>());
			} else if (value.is_number()) {
				lua_pushnumber(lua, value.get<double>());
			} else if (value.is_string()) {
				const std::string data = value.get<std::string>();
				lua_pushlstring(lua, data.data(), data.size());
			} else if (value.is_array()) {
				lua_createtable(lua, static_cast<int>(value.size()), 0);
				for (size_t index = 0; index < value.size() && index < 256; ++index) {
					PushJson(lua, value[index], depth + 1);
					lua_rawseti(lua, -2, static_cast<lua_Integer>(index + 1));
				}
			} else if (value.is_object()) {
				lua_createtable(lua, 0, static_cast<int>(std::min<size_t>(value.size(), 256)));
				size_t count = 0;
				for (auto entry = value.begin(); entry != value.end() && count++ < 256; ++entry) {
					PushJson(lua, entry.value(), depth + 1);
					lua_setfield(lua, -2, entry.key().c_str());
				}
			} else {
				lua_pushnil(lua);
			}
		}

		nlohmann::json ReadScalar(lua_State* lua, int index) {
			switch (lua_type(lua, index)) {
				case LUA_TBOOLEAN:
					return lua_toboolean(lua, index) != 0;
				case LUA_TNUMBER:
					if (lua_isinteger(lua, index)) {
						return lua_tointeger(lua, index);
					}
					return lua_tonumber(lua, index);
				case LUA_TSTRING:
					return std::string(lua_tostring(lua, index));
				default:
					return nullptr;
			}
		}

		Metadata ReadMetadata(lua_State* lua, int table) {
			Metadata metadata;
			table = lua_absindex(lua, table);
			metadata.name = FieldString(lua, table, "name");
			metadata.version = FieldString(lua, table, "version");
			metadata.description = FieldString(lua, table, "description");
			metadata.author = FieldString(lua, table, "author");
			metadata.category = FieldString(lua, table, "category");
			lua_getfield(lua, table, "parameters");
			if (lua_istable(lua, -1)) {
				const size_t count = std::min<size_t>(lua_rawlen(lua, -1), 64);
				for (size_t index = 1; index <= count; ++index) {
					lua_rawgeti(lua, -1, static_cast<lua_Integer>(index));
					if (lua_istable(lua, -1)) {
						nlohmann::json schema = nlohmann::json::object();
						for (const char* key : { "name", "type", "label", "kind" }) {
							const std::string value = FieldString(lua, -1, key);
							if (!value.empty()) {
								schema[key] = value;
							}
						}
						for (const char* key : { "default", "min", "max" }) {
							lua_getfield(lua, -1, key);
							if (!lua_isnil(lua, -1)) {
								schema[key] = ReadScalar(lua, -1);
							}
							lua_pop(lua, 1);
						}
						lua_getfield(lua, -1, "options");
						if (lua_istable(lua, -1)) {
							nlohmann::json options = nlohmann::json::array();
							for (size_t option = 1; option <= std::min<size_t>(lua_rawlen(lua, -1), 64); ++option) {
								lua_rawgeti(lua, -1, static_cast<lua_Integer>(option));
								if (lua_isstring(lua, -1)) {
									options.push_back(lua_tostring(lua, -1));
								}
								lua_pop(lua, 1);
							}
							schema["options"] = std::move(options);
						}
						lua_pop(lua, 1);
						if (schema.contains("name") && schema.contains("type")) {
							metadata.parameters.push_back(std::move(schema));
						}
					}
					lua_pop(lua, 1);
				}
			}
			lua_pop(lua, 1);
			return metadata;
		}

		bool LoadExtension(lua_State* lua, const std::string& source, const std::string& sourceName, Metadata& metadata, std::string& error) {
			if (luaL_loadbufferx(lua, source.data(), source.size(), sourceName.c_str(), "t") != LUA_OK || lua_pcall(lua, 0, 1, 0) != LUA_OK) {
				error = lua_tostring(lua, -1) ? lua_tostring(lua, -1) : "Lua could not load this extension.";
				return false;
			}
			if (!lua_istable(lua, -1)) {
				error = "Extension must return a metadata table.";
				return false;
			}
			metadata = ReadMetadata(lua, -1);
			if (metadata.name.empty() || metadata.name.size() > 128) {
				error = "Extension must declare a name (maximum 128 characters).";
				return false;
			}
			lua_getfield(lua, -1, "run");
			const bool hasRun = lua_isfunction(lua, -1);
			lua_pop(lua, 1);
			if (!hasRun) {
				error = "Extension must declare run = function(ctx, params).";
				return false;
			}
			return true;
		}

		std::string SourceError(const std::string& sourceName, const std::string& detail) {
			return sourceName + ": " + detail;
		}

		bool EffectiveParameters(const Metadata& metadata, const nlohmann::json& supplied, nlohmann::json& effective, std::string& error) {
			if (!supplied.is_object() && !supplied.is_null()) {
				error = "Extension parameters must be a JSON object.";
				return false;
			}
			effective = supplied.is_null() ? nlohmann::json::object() : supplied;
			for (const auto& schema : metadata.parameters) {
				const std::string name = schema.value("name", "");
				const std::string type = schema.value("type", "");
				if (name.empty()) {
					continue;
				}
				if (!effective.contains(name)) {
					if (schema.contains("default")) {
						effective[name] = schema["default"];
					} else {
						error = "Missing required parameter: " + name;
						return false;
					}
				}
				const auto& value = effective[name];
				bool valid = true;
				if (type == "integer" || type == "seed" || type == "item") {
					valid = value.is_number_integer();
				} else if (type == "number") {
					valid = value.is_number();
				} else if (type == "boolean") {
					valid = value.is_boolean();
				} else if (type == "string" || type == "enum" || type == "brush" || type == "color") {
					valid = value.is_string();
				} else {
					valid = false;
				}
				if (!valid) {
					error = "Invalid type for parameter: " + name;
					return false;
				}
				if (value.is_number() && schema.contains("min") && schema["min"].is_number() && value.get<double>() < schema["min"].get<double>()) {
					error = "Parameter below minimum: " + name;
					return false;
				}
				if (value.is_number() && schema.contains("max") && schema["max"].is_number() && value.get<double>() > schema["max"].get<double>()) {
					error = "Parameter above maximum: " + name;
					return false;
				}
				if (type == "enum" && schema.contains("options") && schema["options"].is_array() && std::find(schema["options"].begin(), schema["options"].end(), value) == schema["options"].end()) {
					error = "Invalid enum option: " + name;
					return false;
				}
			}
			return true;
		}

	} // namespace

	Metadata Runtime::Inspect(const std::string& source, const std::string& sourceName, std::string& error, const Limits& limits) {
		error.clear();
		Metadata metadata;
		if (source.size() > limits.maxSourceBytes) {
			error = SourceError(sourceName, "Extension source exceeds size limit.");
			return metadata;
		}
		if (limits.maxMemoryBytes < 1024 * 1024) {
			error = SourceError(sourceName, "Lua memory limit is too small to initialize safely.");
			return metadata;
		}
		MemoryLimit memory { 0, limits.maxMemoryBytes };
		RunState state;
		state.limits = &limits;
		state.deadline = std::chrono::steady_clock::now() + limits.timeBudget;
		StateOwner owner(memory, state);
		if (!owner.get()) {
			error = SourceError(sourceName, "Could not allocate Lua state.");
			return metadata;
		}
		RegisterPositionAndSelection(owner.get());
		RegisterMapResources(owner.get());
		RegisterProcedural(owner.get());
		if (!LoadExtension(owner.get(), source, sourceName, metadata, error)) {
			error = SourceError(sourceName, error);
		}
		return metadata;
	}

	Plan Runtime::Preview(const std::string& source, const std::string& sourceName, const Context& context, const nlohmann::json& parameters, const Limits& limits, const std::atomic_bool* cancelled) {
		Plan plan;
		plan.mapSessionId = context.mapSessionId;
		plan.workspaceGeneration = context.workspaceGeneration;
		plan.mapChangeGeneration = context.mapChangeGeneration;
		plan.allowedPositions = context.selection;
		plan.sourceMask = context.sourceMask;
		if (source.size() > limits.maxSourceBytes) {
			plan.error = "Extension source exceeds size limit.";
		} else if (limits.maxMemoryBytes < 1024 * 1024) {
			plan.error = "Lua memory limit is too small to initialize safely.";
		} else if (context.selection.empty()) {
			plan.error = "No target area selected.";
		} else if (context.selection.size() > limits.maxSelectionTiles) {
			plan.error = "Target area exceeds tile limit.";
		} else if (!std::is_sorted(context.selection.begin(), context.selection.end()) || !std::is_sorted(context.sourceMask.begin(), context.sourceMask.end())) {
			plan.error = "Target/source positions must be sorted.";
		} else if (std::adjacent_find(context.selection.begin(), context.selection.end()) != context.selection.end()) {
			plan.error = "Target selection contains duplicate positions.";
		} else {
			for (const Position& position : context.selection) {
				if (position.z != context.floor) {
					plan.error = "Target spans a different floor.";
					break;
				}
				if (std::binary_search(context.sourceMask.begin(), context.sourceMask.end(), position)) {
					plan.error = "Target area overlaps the captured reference source. Select a different target area.";
					break;
				}
			}
		}
		if (!plan.error.empty()) {
			return plan;
		}
		MemoryLimit memory { 0, limits.maxMemoryBytes };
		RunState state;
		state.context = &context;
		state.plan = &plan;
		state.limits = &limits;
		state.cancelled = cancelled;
		state.deadline = std::chrono::steady_clock::now() + limits.timeBudget;
		StateOwner owner(memory, state);
		if (!owner.get()) {
			plan.error = "Could not allocate Lua state.";
			return plan;
		}
		lua_State* lua = owner.get();
		RegisterPositionAndSelection(lua);
		RegisterMapResources(lua);
		RegisterProcedural(lua);
		if (!LoadExtension(lua, source, sourceName, plan.metadata, plan.error)) {
			plan.error = SourceError(sourceName, plan.error);
			return plan;
		}
		nlohmann::json effective;
		if (!EffectiveParameters(plan.metadata, parameters, effective, plan.error)) {
			plan.error = SourceError(sourceName, plan.error);
			return plan;
		}
		if (effective.contains("seed") && effective["seed"].is_number_integer()) {
			plan.seed = effective["seed"].get<uint64_t>();
		}
		state.randomState = plan.seed;
		lua_getfield(lua, -1, "run");
		lua_createtable(lua, 0, 4);
		PushSelection(lua);
		lua_setfield(lua, -2, "selection");
		lua_pushinteger(lua, static_cast<lua_Integer>(plan.seed));
		lua_setfield(lua, -2, "seed");
		PushJson(lua, effective);
		lua_setfield(lua, -2, "parameters");
		PushJson(lua, effective);
		if (lua_pcall(lua, 2, 0, 0) != LUA_OK) {
			plan.error = SourceError(sourceName, lua_tostring(lua, -1) ? lua_tostring(lua, -1) : "Lua run failed.");
			plan.operations.clear();
		} else if (!state.error.empty()) {
			plan.error = SourceError(sourceName, state.error);
			plan.operations.clear();
		}
		return plan;
	}

} // namespace LuaExtension
