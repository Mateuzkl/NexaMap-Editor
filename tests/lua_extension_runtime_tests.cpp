// SPDX-License-Identifier: GPL-3.0-or-later
#include "lua_extension_runtime.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace LuaExtension;

namespace {

	int failures = 0;

	void Check(bool condition, const char* description) {
		if (!condition) {
			std::cerr << "FAIL: " << description << '\n';
			++failures;
		}
	}

	Context TestContext() {
		Context context;
		context.floor = 7;
		context.mapSessionId = 42;
		context.workspaceGeneration = 3;
		context.selection = { { 10, 10, 7 }, { 11, 10, 7 } };
		context.sourceMask = { { 1, 1, 7 } };
		context.findBrush = [](const std::string& name) -> std::optional<BrushInfo> {
			if (name == "forest floor") {
				return BrushInfo { name, "ground", 123 };
			}
			return std::nullopt;
		};
		context.findItem = [](uint16_t id) -> std::optional<ItemInfo> {
			if (id == 100) {
				return ItemInfo { 100, 100, 200, "Tree", "doodad", false };
			}
			if (id == 101) {
				return ItemInfo { 101, 101, 201, "Rock", "doodad", false };
			}
			return std::nullopt;
		};
		context.readTile = [](const Position&) -> std::optional<TileInfo> { return TileInfo {}; };
		return context;
	}

} // namespace

int main() {
	std::string error;
	const std::string basic = R"(return {
		name = 'Forest', version = '1', category = 'Procedural',
		run = function(ctx, params)
			local brush = Brushes.find('forest floor')
			for pos in ctx.selection:positions() do brush:apply(pos) end
		end
	})";
	auto info = Runtime::Inspect(basic, "forest.lua", error);
	Check(error.empty() && info.name == "Forest", "metadata inspection");
	auto context = TestContext();
	auto plan = Runtime::Preview(basic, "forest.lua", context);
	Check(plan.valid() && plan.operations.size() == 2, "preview creates bounded brush operations");
	Check(plan.mapSessionId == 42 && plan.workspaceGeneration == 3, "plan records resource session");
	Check(context.sourceMask.size() == 1 && context.sourceMask[0] == Position { 1, 1, 7 }, "preview leaves source mask unchanged");
	auto irregular = TestContext();
	irregular.selection = { { 10, 10, 7 }, { 12, 10, 7 } };
	plan = Runtime::Preview(basic, "forest.lua", irregular);
	Check(plan.valid() && plan.operations.size() == 2, "irregular selection is preserved exactly");
	const std::string hole = "return { name = 'Hole', run = function() Map.placeItem(Position.new(11,10,7), 100) end }";
	plan = Runtime::Preview(hole, "hole.lua", irregular);
	Check(!plan.valid() && plan.operations.empty(), "unselected hole inside bounds cannot be written");

	const std::string outside = R"(return { name = 'Bad', run = function()
		Map.placeItem(Position.new(1, 1, 7), 100)
	end })";
	plan = Runtime::Preview(outside, "bad.lua", context);
	Check(!plan.valid() && plan.operations.empty(), "outside-target write is rejected");

	context.selection = context.sourceMask;
	plan = Runtime::Preview(basic, "forest.lua", context);
	Check(!plan.valid() && plan.error.find("overlaps") != std::string::npos, "source overlap is rejected");
	context = TestContext();
	context.selection.clear();
	plan = Runtime::Preview(basic, "forest.lua", context);
	Check(!plan.valid() && plan.error == "No target area selected.", "empty target is rejected");

	context = TestContext();
	const std::string sandbox = R"(return { name = 'Sandbox', run = function()
		assert(os == nil and io == nil and package == nil and debug == nil and require == nil and load == nil)
	end })";
	plan = Runtime::Preview(sandbox, "sandbox.lua", context);
	Check(plan.valid(), "unsafe standard libraries are unavailable");

	const std::string loop = "return { name = 'Loop', run = function() while true do end end }";
	Limits limits;
	limits.instructionBudget = 10000;
	plan = Runtime::Preview(loop, "loop.lua", context, {}, limits);
	Check(!plan.valid() && plan.error.find("budget") != std::string::npos, "infinite loop is interrupted");

	std::atomic_bool cancelled { true };
	plan = Runtime::Preview(loop, "cancel.lua", context, {}, {}, &cancelled);
	Check(!plan.valid(), "cancelled script is interrupted");

	const std::string badScript = "return { name = 'Error', run = function() error('boom') end }";
	plan = Runtime::Preview(badScript, "error.lua", context);
	Check(!plan.valid() && plan.error.find("error.lua") != std::string::npos, "script error includes source name");

	const std::string raw = "return { name = 'Raw', run = function() Map.placeItem(Position.new(10,10,7), 100) end }";
	plan = Runtime::Preview(raw, "raw.lua", context);
	Check(plan.valid() && plan.operations.size() == 1 && plan.operations[0].kind == OperationKind::RawItem, "known active raw item is staged");
	context.reference = ReferenceInfo { {}, { "forest floor" }, { 100 } };
	plan = Runtime::Preview(raw, "raw.lua", context);
	Check(plan.valid(), "reference-verified raw ID is accepted");
	const std::string referenceRead = R"(return { name='Ref', run=function()
		local ref = Reference.current()
		assert(ref.brushes[1] == 'forest floor' and #ref:brushes() == 1)
		assert(ref:materials()[1] == 100)
	end })";
	plan = Runtime::Preview(referenceRead, "reference.lua", context);
	Check(plan.valid(), "SOURCE reference values and methods are read-only copies");
	const std::string unverified = "return { name = 'Unverified', run = function() Map.placeItem(Position.new(10,10,7), 101) end }";
	plan = Runtime::Preview(unverified, "unverified.lua", context);
	Check(!plan.valid() && plan.operations.empty(), "raw ID absent from SOURCE reference is rejected");
	context.reference.reset();
	const std::string unknownRaw = "return { name = 'Unknown', run = function() Map.placeItem(Position.new(10,10,7), 999) end }";
	plan = Runtime::Preview(unknownRaw, "unknown.lua", context);
	Check(!plan.valid() && plan.operations.empty(), "unknown active raw item is rejected");
	const std::string forgedBrush = "return { name = 'Forged', run = function() local b = Brushes.find('forest floor'); b._name = 'missing'; b:apply(Position.new(10,10,7)) end }";
	plan = Runtime::Preview(forgedBrush, "forged.lua", context);
	Check(!plan.valid() && plan.operations.empty(), "forged brush handle is rejected");
	const std::string geometry = R"(return { name = 'Geometry', run = function()
		local area = Geo.rectangle(10, 10, 2, 1, 7)
		local line = Geo.line(Position.new(10,10,7), Position.new(11,10,7))
		assert(#Geo.intersection(area, line) == 2)
		local brush = Brushes.find('forest floor')
		brush:applyMany(area)
	end })";
	plan = Runtime::Preview(geometry, "geometry.lua", context);
	Check(plan.valid() && plan.operations.size() == 2, "geometry masks and applyMany produce exact-target operations");
	const std::string noise = R"(return { name = 'Noise', run = function()
		local a = Noise.perlin({width=32,height=32,seed=123,scale=0.08,octaves=4,persistence=0.5})
		local b = Noise.perlin({width=32,height=32,seed=123,scale=0.08,octaves=4,persistence=0.5})
		assert(a:get(10,10) == b:get(10,10))
		assert(a:get(10,10) >= 0 and a:get(10,10) <= 1)
		assert(a:normalize():threshold(0.5):get(10,10) == 0 or a:get(10,10) == 1)
	end })";
	plan = Runtime::Preview(noise, "noise.lua", context);
	Check(plan.valid(), "seeded Perlin noise is bounded and deterministic");
	const std::string cave = R"(return { name = 'Cave', run = function(ctx)
		local a = Algo.cellularAutomata({area=ctx.selection,seed=44,fillChance=0.5,iterations=2})
		local b = Algo.cellularAutomata({area=ctx.selection,seed=44,fillChance=0.5,iterations=2})
		for pos in ctx.selection:positions() do assert(a:isFloor(pos) == b:isFloor(pos)) end
	end })";
	plan = Runtime::Preview(cave, "cave.lua", context);
	Check(plan.valid(), "cellular automata returns deterministic floor lookup");

	const std::string random = R"(return { name = 'Random', run = function()
		local rng = Random.new(123)
		local same = Random.new(123)
		assert(rng:float() == same:float())
		assert(rng:int(1, 10) == same:integer(1, 10))
		assert(rng:choice({'a','b'}) == same:choice({'a','b'}))
		if rng:integer(1, 10) > 5 then Map.placeItem(Position.new(10,10,7), 100) end
	end })";
	auto first = Runtime::Preview(random, "random.lua", context);
	auto second = Runtime::Preview(random, "random.lua", context);
	Check(first.valid() && second.valid() && first.operations.size() == second.operations.size(), "seeded random is deterministic");

	for (const char* fileName : { "forest_generator.lua", "cave_generator.lua", "reference_room.lua" }) {
		const auto path = std::filesystem::path(NEXAMAP_TEST_SOURCE_DIR) / "extensions" / "lua" / fileName;
		std::ifstream file(path, std::ios::binary);
		Check(static_cast<bool>(file), "bundled Lua example exists");
		if (file) {
			const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			std::string exampleError;
			const auto metadata = Runtime::Inspect(source, fileName, exampleError);
			Check(exampleError.empty() && !metadata.name.empty(), "bundled Lua example metadata and syntax are valid");
		}
	}

	return failures == 0 ? 0 : 1;
}
