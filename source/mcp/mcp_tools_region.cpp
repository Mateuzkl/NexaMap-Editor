// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_region.h"

#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../action.h"
#include "../complexitem.h"
#include "../copybuffer.h"
#include "../editor.h"
#include "../gui.h"
#include "../item.h"
#include "../map.h"
#include "../multiplayer_session.h"
#include "../tile.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>

namespace mcp {
	namespace {
		struct ClipboardState {
			SessionId sourceMapSessionId = InvalidSessionId;
			uint64_t workspaceGeneration = 0;
			Region region;
			bool valid = false;
		};

		ClipboardState clipboardState;

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

		bool IsProtected(const Item& item) {
			if (item.getActionID() || item.getUniqueID() || dynamic_cast<const Teleport*>(&item)) {
				return true;
			}
			const auto* container = dynamic_cast<const Container*>(&item);
			return container && container->getItemCount() != 0;
		}

		void EnforceSelectionBoundary(const EditorContext& context, const Region& region, bool allowExpansion) {
			if (allowExpansion || context.editor.selection.size() == 0) {
				return;
			}
			std::unordered_set<const Tile*> selected;
			for (const Tile* tile : context.editor.selection) {
				selected.insert(tile);
			}
			for (int y = region.from.y; y <= region.to.y; ++y) {
				for (int x = region.from.x; x <= region.to.x; ++x) {
					const Tile* tile = context.map.getTile(x, y, region.from.z);
					if (!tile || !selected.contains(tile)) {
						throw Error("write would leave the current selection; pass allowExpansion=true only when intended");
					}
				}
			}
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

		bool SameCategory(const ItemType& first, const ItemType& second) {
			return first.isGroundTile() == second.isGroundTile() && first.isBorder == second.isBorder && first.isWall == second.isWall && first.isTable == second.isTable && first.isCarpet == second.isCarpet && first.isContainer() == second.isContainer() && first.isTeleport() == second.isTeleport();
		}

		void CommitTiles(const EditorContext& context, std::vector<std::unique_ptr<Tile>> tiles) {
			if (tiles.empty()) {
				throw Error("operation did not change any tiles");
			}
			std::unique_ptr<BatchAction> batch(context.editor.actionQueue->createBatch(ACTION_MCP));
			std::unique_ptr<Action> action(context.editor.actionQueue->createAction(batch.get()));
			for (auto& tile : tiles) {
				tile->update();
				action->addChange(newd Change(tile.release()));
			}
			batch->addAndCommitAction(action.release());
			context.editor.addBatch(batch.release(), 0);
			context.editor.actionQueue->resetTimer();
			g_gui.RefreshView();
		}

		Position TransformPosition(const Position& source, const Region& region, const Position& destination, const std::string& operation) {
			const int localX = source.x - region.from.x;
			const int localY = source.y - region.from.y;
			const int width = region.width();
			const int height = region.height();
			if (operation == "mirror-horizontal") {
				return Position(destination.x + width - 1 - localX, destination.y + localY, destination.z);
			}
			if (operation == "mirror-vertical") {
				return Position(destination.x + localX, destination.y + height - 1 - localY, destination.z);
			}
			if (operation == "rotate-90") {
				return Position(destination.x + height - 1 - localY, destination.y + localX, destination.z);
			}
			if (operation == "rotate-180") {
				return Position(destination.x + width - 1 - localX, destination.y + height - 1 - localY, destination.z);
			}
			if (operation == "rotate-270") {
				return Position(destination.x + localY, destination.y + width - 1 - localX, destination.z);
			}
			if (operation == "translate") {
				return Position(destination.x + localX, destination.y + localY, destination.z);
			}
			throw Error("unsupported transform operation");
		}

		void RotateItem(Item* item, int steps) {
			for (int step = 0; item && step < steps; ++step) {
				item->doRotate();
			}
		}

		Json MutationResult(const EditorContext& context, const std::string& operation, size_t count) {
			return { { "operation", operation }, { "changed", true }, { "tiles", count }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", context.editor.actionQueue->canUndo() } };
		}
	}

	void RegisterRegionTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();

			registry.add({
				"region_replace_items",
				"Replace or remove one loaded item ID throughout a bounded explicit region as one undoable action. Protected metadata is preserved by default.",
				{ { "type", "object" }, { "properties", { { "region", { { "type", "object" } } }, { "sourceId", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } }, { "targetId", { { "type", Json::array({ "integer", "null" }) }, { "minimum", 1 }, { "maximum", 65535 } } }, { "sameCategory", { { "type", "boolean" }, { "default", true } } }, { "preserveProtected", { { "type", "boolean" }, { "default", true } } }, { "allowExpansion", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "region", "sourceId", "targetId" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { if (!arguments.contains("region")){ throw Error("region is required");
} const Region region = ParseRegion(arguments["region"], 262144); const uint16_t sourceId = RequiredItemId(arguments["sourceId"]); const bool remove = arguments["targetId"].is_null(); const uint16_t targetId = remove ? 0 : RequiredItemId(arguments["targetId"]); const bool sameCategory = arguments.value("sameCategory", true); const bool preserveProtected = arguments.value("preserveProtected", true); const bool allowExpansion = arguments.value("allowExpansion", false); return StructuredResult(OnGui([=](const EditorContext& context) { VerifyWritableContext(context, arguments); EnforceSelectionBoundary(context, region, allowExpansion); if (!remove && sameCategory && !SameCategory(g_items.getItemType(sourceId), g_items.getItemType(targetId))){ throw Error("target item is outside the source category");
} std::vector<std::unique_ptr<Tile>> changes; size_t replacements = 0; for (int y = region.from.y; y <= region.to.y; ++y){ for (int x = region.from.x; x <= region.to.x; ++x) { Tile* existing = context.map.getTile(x, y, region.from.z); if (!existing){ continue;
} std::unique_ptr<Tile> tile(existing->deepCopy(context.map)); bool changed = false; if (tile->ground && tile->ground->getID() == sourceId && !(preserveProtected && IsProtected(*tile->ground))) { if (remove) { delete tile->ground; tile->ground = nullptr; } else{ tile->ground->setID(targetId);
} changed = true; ++replacements; } for (auto iterator = tile->items.begin(); iterator != tile->items.end();) { Item* item = *iterator; if (!item || item->getID() != sourceId || (preserveProtected && IsProtected(*item))) { ++iterator; continue; } if (remove) { delete item; iterator = tile->items.erase(iterator); } else { item->setID(targetId); ++iterator; } changed = true; ++replacements; } if (changed){ changes.push_back(std::move(tile));
} }
} CommitTiles(context, std::move(changes)); Json result = MutationResult(context, "replace-items", replacements); result["sourceId"] = sourceId; result["targetId"] = remove ? Json(nullptr) : Json(targetId); return result; })); },
			});

			registry.add({
				"region_copy",
				"Capture all mapped tiles in an explicit region into NexaMap's native copy buffer.",
				{ { "type", "object" }, { "properties", { { "region", { { "type", "object" } } } } }, { "required", Json::array({ "region" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) { const Region region = ParseRegion(arguments["region"], 262144); return StructuredResult(OnGui([region](const EditorContext& context) { context.editor.copybuffer.copyRegion(context.editor, region.from, region.to); clipboardState = { context.mapSessionId, context.workspaceGeneration, region, context.editor.copybuffer.canPaste() }; return Json { { "copied", clipboardState.valid }, { "tiles", context.editor.copybuffer.getTileCount() }, { "from", PositionJson(region.from) }, { "to", PositionJson(region.to) }, { "sourceMapSessionId", context.mapSessionId }, { "workspaceGeneration", context.workspaceGeneration } }; })); },
			});

			registry.add({
				"region_paste",
				"Paste the last region_copy at an explicit origin using NexaMap's native undoable paste pipeline. Cross-resource raw paste is rejected.",
				{ { "type", "object" }, { "properties", { { "origin", PositionSchema() }, { "allowExpansion", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "origin" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { const Position origin = ParsePosition(arguments["origin"]); const bool allowExpansion = arguments.value("allowExpansion", false); return StructuredResult(OnGui([arguments, origin, allowExpansion](const EditorContext& context) { VerifyWritableContext(context, arguments); if (!clipboardState.valid || !context.editor.copybuffer.canPaste()){ throw Error("region clipboard is empty");
} if (clipboardState.workspaceGeneration != context.workspaceGeneration){ throw Error("source and destination resource sessions differ; raw cross-resource paste is blocked");
} const int width = clipboardState.region.width(); const int height = clipboardState.region.height(); const Region destination { origin, Position(origin.x + width - 1, origin.y + height - 1, origin.z) }; EnforceSelectionBoundary(context, destination, allowExpansion); const uint64_t tiles = context.editor.copybuffer.getTileCount(); context.editor.copybuffer.paste(context.editor, origin); context.editor.actionQueue->resetTimer(); g_gui.RefreshView(); return MutationResult(context, "paste", static_cast<size_t>(tiles)); })); },
			});

			registry.add({
				"region_transform",
				"Transform visual terrain/items in a bounded region while leaving protected gameplay metadata in place.",
				{ { "type", "object" }, { "properties", { { "region", { { "type", "object" } } }, { "operation", { { "type", "string" }, { "enum", Json::array({ "mirror-horizontal", "mirror-vertical", "rotate-90", "rotate-180", "rotate-270", "translate" }) } } }, { "destination", PositionSchema() }, { "allowExpansion", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "region", "operation" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { const Region region = ParseRegion(arguments["region"], 65536); const std::string operation = arguments.value("operation", ""); const Position destination = arguments.contains("destination") ? ParsePosition(arguments["destination"]) : region.from; const bool allowExpansion = arguments.value("allowExpansion", false); return StructuredResult(OnGui([arguments, region, operation, destination, allowExpansion](const EditorContext& context) { VerifyWritableContext(context, arguments); const int destinationWidth = (operation == "rotate-90" || operation == "rotate-270") ? region.height() : region.width(); const int destinationHeight = (operation == "rotate-90" || operation == "rotate-270") ? region.width() : region.height(); const Region target { destination, Position(destination.x + destinationWidth - 1, destination.y + destinationHeight - 1, destination.z) }; if (!target.to.isValid()){ throw Error("transformed region leaves valid map coordinates");
} EnforceSelectionBoundary(context, region, allowExpansion); EnforceSelectionBoundary(context, target, allowExpansion); struct Visual { std::unique_ptr<Item> ground; std::vector<std::unique_ptr<Item>> items; }; std::map<Position, Visual> visuals; for (int y = region.from.y; y <= region.to.y; ++y){ for (int x = region.from.x; x <= region.to.x; ++x) { const Position sourcePosition(x, y, region.from.z); const Tile* source = context.map.getTile(sourcePosition); if (!source){ continue;
} Visual visual; if (source->ground && !IsProtected(*source->ground)){ visual.ground.reset(source->ground->deepCopy());
} for (const Item* item : source->items){ if (item && !IsProtected(*item)){ visual.items.emplace_back(item->deepCopy());
}} visuals.emplace(sourcePosition, std::move(visual)); }
} std::set<Position> affected; for (const auto& [sourcePosition, visual] : visuals) { affected.insert(sourcePosition); affected.insert(TransformPosition(sourcePosition, region, destination, operation)); } std::map<Position, std::unique_ptr<Tile>> outputs; for (const Position& position : affected) { TileLocation* location = context.map.createTileL(position); Tile* existing = location->get(); std::unique_ptr<Tile> output(existing ? existing->deepCopy(context.map) : context.map.allocator(location)); if (!output->ground || !IsProtected(*output->ground)) { delete output->ground; output->ground = nullptr; } for (auto iterator = output->items.begin(); iterator != output->items.end();) { if (*iterator && IsProtected(**iterator)) { ++iterator; continue; } delete *iterator; iterator = output->items.erase(iterator); } outputs.emplace(position, std::move(output)); } const int rotations = operation == "rotate-90" ? 1 : operation == "rotate-180" ? 2 : operation == "rotate-270" ? 3 : 0; for (auto& [sourcePosition, visual] : visuals) { const Position targetPosition = TransformPosition(sourcePosition, region, destination, operation); Tile* output = outputs.at(targetPosition).get(); if (visual.ground && !output->ground) { RotateItem(visual.ground.get(), rotations); output->ground = visual.ground.release(); } for (auto& item : visual.items) { RotateItem(item.get(), rotations); output->addItem(item.release()); } } std::vector<std::unique_ptr<Tile>> changes; changes.reserve(outputs.size()); for (auto& [position, output] : outputs){ changes.push_back(std::move(output));
} const size_t changed = changes.size(); CommitTiles(context, std::move(changes)); return MutationResult(context, operation, changed); })); },
			});
		});
	}
}
