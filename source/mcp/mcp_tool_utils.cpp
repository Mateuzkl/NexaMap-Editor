// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tool_utils.h"

#include "mcp_gui_call.h"

#include "../brush.h"
#include "../carpet_brush.h"
#include "../complexitem.h"
#include "../editor.h"
#include "../ground_brush.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../multiplayer_session.h"
#include "../reference_style_store.h"
#include "../table_brush.h"
#include "../tile.h"
#include "../wall_brush.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>

namespace mcp {
	size_t Region::width() const {
		return static_cast<size_t>(to.x - from.x + 1);
	}

	size_t Region::height() const {
		return static_cast<size_t>(to.y - from.y + 1);
	}

	size_t Region::area() const {
		return width() * height();
	}

	Json EmptyObjectSchema() {
		return { { "type", "object" }, { "properties", Json::object() }, { "additionalProperties", false } };
	}

	Json PositionSchema() {
		return {
			{ "type", "object" },
			{ "properties", {
								{ "x", { { "type", "integer" }, { "minimum", 0 }, { "maximum", MAP_MAX_WIDTH } } },
								{ "y", { { "type", "integer" }, { "minimum", 0 }, { "maximum", MAP_MAX_HEIGHT } } },
								{ "z", { { "type", "integer" }, { "minimum", 0 }, { "maximum", MAP_MAX_LAYER } } },
							} },
			{ "required", Json::array({ "x", "y", "z" }) },
			{ "additionalProperties", false },
		};
	}

	Json PositionJson(const Position& position) {
		return { { "x", position.x }, { "y", position.y }, { "z", position.z } };
	}

	Position ParsePosition(const Json& value, const char* fieldName) {
		if (!value.is_object() || !value.contains("x") || !value.contains("y") || !value.contains("z") || !value["x"].is_number_integer() || !value["y"].is_number_integer() || !value["z"].is_number_integer()) {
			throw Error(std::string(fieldName) + " must contain integer x, y and z values");
		}
		const Position position(value["x"].get<int>(), value["y"].get<int>(), value["z"].get<int>());
		if (!position.isValid()) {
			throw Error(std::string(fieldName) + " is outside valid map coordinates");
		}
		return position;
	}

	Region ParseRegion(const Json& arguments, size_t maximumArea) {
		Region region;
		if (arguments.contains("from") || arguments.contains("to")) {
			if (!arguments.contains("from") || !arguments.contains("to")) {
				throw Error("from and to must be provided together");
			}
			region.from = ParsePosition(arguments["from"], "from");
			region.to = ParsePosition(arguments["to"], "to");
		} else if (arguments.contains("origin") || arguments.contains("width") || arguments.contains("height")) {
			if (!arguments.contains("origin") || !arguments.contains("width") || !arguments.contains("height") || !arguments["width"].is_number_integer() || !arguments["height"].is_number_integer()) {
				throw Error("origin, width and height must be provided together");
			}
			region.from = ParsePosition(arguments["origin"], "origin");
			const int width = arguments["width"].get<int>();
			const int height = arguments["height"].get<int>();
			if (width < 1 || height < 1 || width > 512 || height > 512) {
				throw Error("width and height must be between 1 and 512");
			}
			region.to = Position(region.from.x + width - 1, region.from.y + height - 1, region.from.z);
			if (!region.to.isValid()) {
				throw Error("region extends beyond valid map coordinates");
			}
		} else {
			throw Error("provide either from/to or origin/width/height");
		}
		if (region.from.z != region.to.z) {
			throw Error("a region request must target exactly one floor");
		}
		if (region.from.x > region.to.x || region.from.y > region.to.y) {
			throw Error("region from must not be greater than to");
		}
		if (region.area() > maximumArea) {
			throw Error("requested region is too large; maximum area is " + std::to_string(maximumArea) + " tiles");
		}
		return region;
	}

	void VerifyWritableContext(const EditorContext& context, const Json& arguments) {
		if (context.editor.multiplayer && context.editor.multiplayer->active() && !context.editor.multiplayer->canEdit()) {
			throw Error("the active multiplayer role cannot edit this map");
		}
		if (arguments.contains("mapSessionId") && (!arguments["mapSessionId"].is_number_unsigned() || arguments["mapSessionId"].get<SessionId>() != context.mapSessionId)) {
			throw Error("stale map session; inspect map_info again before writing");
		}
		if (arguments.contains("workspaceGeneration") && (!arguments["workspaceGeneration"].is_number_unsigned() || arguments["workspaceGeneration"].get<uint64_t>() != context.workspaceGeneration)) {
			throw Error("stale resource workspace; inspect workspace_info again before writing");
		}
	}

	void EnforceSelectionBoundary(const EditorContext& context, const std::vector<Position>& positions, bool allowExpansion) {
		if (allowExpansion || context.editor.selection.size() == 0) {
			return;
		}
		std::unordered_set<const Tile*> selected;
		for (const Tile* tile : context.editor.selection) {
			selected.insert(tile);
		}
		for (const Position& position : positions) {
			const Tile* tile = context.map.getTile(position);
			if (!tile || selected.find(tile) == selected.end()) {
				throw Error("write would leave the current selection; pass allowExpansion=true only when intended");
			}
		}
	}

	void EnforceSelectionBoundary(const EditorContext& context, const Region& region, bool allowExpansion) {
		if (allowExpansion || context.editor.selection.size() == 0) {
			return;
		}
		std::vector<Position> positions;
		positions.reserve(region.area());
		for (int y = region.from.y; y <= region.to.y; ++y) {
			for (int x = region.from.x; x <= region.to.x; ++x) {
				positions.emplace_back(x, y, region.from.z);
			}
		}
		EnforceSelectionBoundary(context, positions, false);
	}

	void EnforceReferenceTargetBoundary(const EditorContext& context, const std::vector<Position>& positions, bool allowReferenceSourceOverwrite) {
		const std::optional<ReferenceStyleSnapshot> reference = ReferenceStyleStore::Instance().getSnapshot();
		if (!reference) {
			return;
		}
		const std::optional<TargetAreaSnapshot> target = ReferenceStyleStore::Instance().getTargetSnapshot();
		if (const std::optional<std::string> rejection = ValidateReferenceTargetWrite(
				&*reference,
				target ? &*target : nullptr,
				context.mapSessionId,
				context.workspaceGeneration,
				positions,
				allowReferenceSourceOverwrite
			)) {
			throw Error(*rejection);
		}
	}

	bool IsProtectedItem(const Item& item) {
		if (item.getActionID() != 0 || item.getUniqueID() != 0 || dynamic_cast<const Teleport*>(&item)) {
			return true;
		}
		const auto* container = dynamic_cast<const Container*>(&item);
		return container && container->getItemCount() != 0;
	}

	uint16_t RequiredItemId(const Json& value) {
		if (!value.is_number_unsigned()) {
			throw Error("item id must be an unsigned integer");
		}
		const uint64_t id = value.get<uint64_t>();
		if (id == 0 || id > 65535 || !g_items.typeExists(static_cast<int>(id))) {
			throw Error("item id is not loaded in the active resource session");
		}
		return static_cast<uint16_t>(id);
	}

	Json OnGui(std::function<Json(const EditorContext&)> function) {
		return CallJsonOnGui([function = std::move(function)] {
			const EditorContext context = ResolveEditorContext();
			return function(context);
		});
	}

	std::string LowerText(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return value;
	}

	std::string BrushKind(const Brush& brush) {
		if (dynamic_cast<const GroundBrush*>(&brush)) {
			return "ground";
		}
		if (dynamic_cast<const WallBrush*>(&brush)) {
			return "wall";
		}
		if (dynamic_cast<const CarpetBrush*>(&brush)) {
			return "carpet";
		}
		if (dynamic_cast<const TableBrush*>(&brush)) {
			return "table";
		}
		if (brush.isDoodad()) {
			return "doodad";
		}
		if (brush.isRaw()) {
			return "raw";
		}
		if (brush.isCreature()) {
			return "creature";
		}
		if (brush.isSpawn()) {
			return "spawn";
		}
		return "other";
	}

	Brush* FindBrush(const std::string& name) {
		const std::string wanted = LowerText(name);
		Brush* match = nullptr;
		std::unordered_set<Brush*> visited;
		for (const auto& [key, brush] : g_brushes.getMap()) {
			if (!brush || !visited.insert(brush).second) {
				continue;
			}
			const std::string brushName = brush->getName().empty() ? key : brush->getName();
			if (LowerText(brushName) != wanted) {
				continue;
			}
			if (match && match != brush) {
				throw Error("brush name is ambiguous: " + name);
			}
			match = brush;
		}
		if (!match) {
			throw Error("brush is not loaded in the active resource session: " + name);
		}
		return match;
	}
}
