// SPDX-License-Identifier: GPL-3.0-or-later
#include "lua_extension_editor.h"

#include "action.h"
#include "brush.h"
#include "common.h"
#include "doodad_brush.h"
#include "editor.h"
#include "gui.h"
#include "item.h"
#include "item_id_mapping.h"
#include "items.h"
#include "map.h"
#include "multiplayer_session.h"
#include "reference_style_store.h"
#include "tile.h"
#include "workspace_session.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace LuaExtension {
	namespace {

		::Position EditorPosition(const Position& position) {
			return { position.x, position.y, position.z };
		}

		Position ValuePosition(const ::Position& position) {
			return { position.x, position.y, position.z };
		}

		std::string BrushKind(const Brush& brush) {
			if (brush.isGround()) {
				return "ground";
			}
			if (brush.isWall()) {
				return "wall";
			}
			if (brush.isDoodad()) {
				return "doodad";
			}
			if (brush.isCarpet()) {
				return "carpet";
			}
			if (brush.isTable()) {
				return "table";
			}
			if (brush.isDoor()) {
				return "door";
			}
			if (brush.isRaw()) {
				return "raw";
			}
			return "other";
		}

		bool SupportedBrush(Brush& brush) {
			return brush.isGround() || (brush.isWall() && !brush.isWallDecoration()) || (brush.isDoodad() && brush.asDoodad() && brush.asDoodad()->hasSingleObjects(0));
		}

		bool ProtectedTile(const Tile* tile) {
			return tile && (tile->creature || tile->spawn || tile->getHouseID() != 0 || tile->hasUniqueItem());
		}

		std::vector<::Position> NativePositions(const std::vector<Position>& positions) {
			std::vector<::Position> native;
			native.reserve(positions.size());
			for (const Position& position : positions) {
				native.push_back(EditorPosition(position));
			}
			return native;
		}

	} // namespace

	Context CaptureContext(Editor& editor, std::string& error) {
		error.clear();
		Context context;
		context.mapSessionId = editor.map.getSessionId();
		context.workspaceGeneration = g_workspace.getGeneration();
		context.mapChangeGeneration = editor.map.getChangeGeneration();
		context.activeIdModeKnown = g_workspace.getEffectiveItemIdMode() != ItemIdMode::Unknown;
		context.floor = g_gui.GetCurrentFloor();
		const auto reference = ReferenceStyleStore::Instance().getSnapshot();
		const auto target = ReferenceStyleStore::Instance().getTargetSnapshot();
		if (reference) {
			if (reference->sourceMapSessionId != context.mapSessionId || reference->workspaceGeneration != context.workspaceGeneration) {
				error = "Captured reference belongs to a stale map or resource session.";
				return context;
			}
			if (!target) {
				error = "No target area is selected. Select a different area and capture the target first.";
				return context;
			}
			if (target->mapSessionId != context.mapSessionId || target->workspaceGeneration != context.workspaceGeneration) {
				error = "Target area belongs to a stale map or resource session. Capture the target again.";
				return context;
			}
			context.floor = target->floor;
			for (const ::Position& position : target->positions) {
				context.selection.push_back(ValuePosition(position));
			}
			for (const ::Position& position : reference->sourceMask) {
				context.sourceMask.push_back(ValuePosition(position));
			}
			ReferenceInfo info;
			for (const auto& ground : reference->groundFamilies) {
				info.groundFamilies.push_back(ground.name);
				if (ground.knownBrush && !ground.brush.empty()) {
					info.brushes.push_back(ground.brush);
				}
			}
			for (const auto& brush : reference->brushes) {
				info.brushes.push_back(brush.name);
			}
			for (const auto& item : reference->items) {
				if (item.sourceVerified) {
					info.sourceVerifiedIds.push_back(item.activeId);
				}
			}
			context.reference = std::move(info);
		} else {
			for (const Tile* tile : editor.selection.getTiles()) {
				if (tile && tile->getZ() == context.floor) {
					context.selection.push_back(ValuePosition(tile->getPosition()));
				}
			}
		}
		std::sort(context.selection.begin(), context.selection.end());
		context.selection.erase(std::unique(context.selection.begin(), context.selection.end()), context.selection.end());
		std::sort(context.sourceMask.begin(), context.sourceMask.end());
		context.sourceMask.erase(std::unique(context.sourceMask.begin(), context.sourceMask.end()), context.sourceMask.end());
		if (context.selection.empty()) {
			error = "No target area selected.";
			return context;
		}
		if (context.selection.size() > Limits {}.maxSelectionTiles) {
			error = "Target area exceeds Lua tile limit.";
			return context;
		}
		if (reference && target) {
			if (auto rejection = ValidateReferenceTargetWrite(&*reference, &*target, editor.map.getSessionId(), g_workspace.getGeneration(), target->positions)) {
				error = *rejection;
				return context;
			}
		}
		context.readTile = [&editor](const Position& position) -> std::optional<TileInfo> {
			const Tile* tile = editor.map.getTile(position.x, position.y, position.z);
			TileInfo info;
			if (!tile) {
				return info;
			}
			info.mapped = true;
			info.groundId = tile->ground ? tile->ground->getID() : 0;
			info.blocking = tile->isBlocking();
			info.walkable = tile->ground && !tile->isBlocking();
			info.creature = tile->creature != nullptr;
			info.spawn = tile->spawn != nullptr;
			info.houseId = tile->getHouseID();
			for (const Item* item : tile->items) {
				if (item) {
					info.itemIds.push_back(item->getID());
				}
			}
			info.zones.assign(tile->zones.begin(), tile->zones.end());
			return info;
		};
		context.findItem = [](uint16_t id) -> std::optional<ItemInfo> {
			if (!g_items.typeExists(id)) {
				return std::nullopt;
			}
			const ItemType& type = g_items.getItemType(id);
			ItemInfo info;
			info.activeId = id;
			info.name = type.name;
			if (type.isGroundTile()) {
				info.category = "ground";
			} else if (type.isBorder) {
				info.category = "border";
			} else if (type.isWall) {
				info.category = "wall";
			} else {
				info.category = "item";
			}
			if (g_workspace.getEffectiveItemIdMode() == ItemIdMode::ServerId) {
				info.serverId = id;
				info.clientId = type.clientID;
			} else if (g_workspace.getEffectiveItemIdMode() == ItemIdMode::ClientId) {
				info.clientId = id;
				auto mapping = ItemIdMapping::clientToServer(id);
				info.ambiguousMapping = mapping.ambiguous;
				if (mapping.found && !mapping.ambiguous) {
					info.serverId = mapping.converted;
				}
			} else {
				info.ambiguousMapping = true;
			}
			return info;
		};
		context.findBrush = [](const std::string& name) -> std::optional<BrushInfo> {
			Brush* brush = g_brushes.getBrush(name);
			if (!brush || !SupportedBrush(*brush)) {
				return std::nullopt;
			}
			return BrushInfo { brush->getName(), BrushKind(*brush), static_cast<uint16_t>(std::clamp(brush->getLookID(), 0, 65535)) };
		};
		std::set<Brush*> seen;
		for (const auto& [name, brush] : g_brushes.getMap()) {
			if (brush && !name.empty() && SupportedBrush(*brush) && seen.insert(brush).second && context.brushes.size() < 10000) {
				context.brushes.push_back({ brush->getName(), BrushKind(*brush), static_cast<uint16_t>(std::clamp(brush->getLookID(), 0, 65535)) });
			}
		}
		return context;
	}

	bool ValidatePlan(Editor& editor, const Plan& plan, std::string& error) {
		error.clear();
		if (editor.multiplayer && editor.multiplayer->active()) {
			error = "Lua extensions are unavailable during multiplayer sessions.";
			return false;
		}
		if (!plan.valid()) {
			error = plan.error;
			return false;
		}
		if (plan.operations.empty()) {
			error = "Preview did not generate any operations.";
			return false;
		}
		if (plan.operations.size() > Limits {}.maxOperations) {
			error = "Lua operation limit exceeded.";
			return false;
		}
		if (plan.mapSessionId != editor.map.getSessionId() || plan.workspaceGeneration != g_workspace.getGeneration()) {
			error = "Preview belongs to a stale map or resource session. Preview again.";
			return false;
		}
		if (plan.mapChangeGeneration != editor.map.getChangeGeneration()) {
			error = "Map changed since preview. Preview again before applying.";
			return false;
		}
		std::string contextError;
		Context current = CaptureContext(editor, contextError);
		if (!contextError.empty()) {
			error = contextError;
			return false;
		}
		if (current.selection != plan.allowedPositions || current.sourceMask != plan.sourceMask) {
			error = "Target or reference changed since preview. Preview again.";
			return false;
		}
		const auto reference = ReferenceStyleStore::Instance().getSnapshot();
		const auto target = ReferenceStyleStore::Instance().getTargetSnapshot();
		std::vector<::Position> writePositions;
		writePositions.reserve(plan.operations.size());
		for (const Operation& operation : plan.operations) {
			if (!std::binary_search(plan.allowedPositions.begin(), plan.allowedPositions.end(), operation.position) || std::binary_search(plan.sourceMask.begin(), plan.sourceMask.end(), operation.position)) {
				error = "Operation is outside the exact target or overlaps the source.";
				return false;
			}
			const ::Position native = EditorPosition(operation.position);
			if (!native.isValid() || operation.position.z != current.floor) {
				error = "Operation has an invalid position or floor.";
				return false;
			}
			if (ProtectedTile(editor.map.getTile(native))) {
				error = "Target includes a tile with protected creature, spawn, house, or unique item.";
				return false;
			}
			if (operation.kind == OperationKind::Brush) {
				Brush* brush = g_brushes.getBrush(operation.brush);
				if (!brush || !SupportedBrush(*brush) || brush->getName() != operation.brush) {
					error = "A planned brush no longer exists in active resources.";
					return false;
				}
				if (current.reference && std::find(current.reference->brushes.begin(), current.reference->brushes.end(), operation.brush) == current.reference->brushes.end()) {
					error = "A planned brush was not verified in the captured reference source.";
					return false;
				}
			} else if (operation.kind == OperationKind::RawItem) {
				if (!g_items.typeExists(operation.itemId) || g_workspace.getEffectiveItemIdMode() == ItemIdMode::Unknown) {
					error = "A planned raw item ID is unavailable or its ID mode is unknown.";
					return false;
				}
				if (current.reference && std::find(current.reference->sourceVerifiedIds.begin(), current.reference->sourceVerifiedIds.end(), operation.itemId) == current.reference->sourceVerifiedIds.end()) {
					error = "A planned raw item ID was not verified in the captured reference source.";
					return false;
				}
			} else {
				error = "Unknown Lua operation.";
				return false;
			}
			writePositions.push_back(native);
		}
		if (auto rejection = ValidateReferenceTargetWrite(reference ? &*reference : nullptr, target ? &*target : nullptr, editor.map.getSessionId(), g_workspace.getGeneration(), writePositions)) {
			error = *rejection;
			return false;
		}
		return true;
	}

	bool ApplyPlan(Editor& editor, const Plan& plan, std::string& error) {
		if (!ValidatePlan(editor, plan, error)) {
			return false;
		}
		std::unique_ptr<BatchAction> batch;
		try {
			std::map<Position, std::vector<const Operation*>> grouped;
			std::vector<Position> order;
			for (const Operation& operation : plan.operations) {
				auto [found, inserted] = grouped.try_emplace(operation.position);
				if (inserted) {
					order.push_back(operation.position);
				}
				found->second.push_back(&operation);
			}
			batch.reset(editor.actionQueue->createBatch(ACTION_GENERATE_AREA));
			auto action = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
			ScopedRandomSeed deterministicBrushRandom(plan.seed);
			std::set<Position> borderTiles;
			std::set<Position> wallTiles;
			for (const Position& position : order) {
				const auto& operations = grouped.at(position);
				TileLocation* location = editor.map.createTileL(EditorPosition(position));
				Tile* original = location->get();
				auto tile = std::unique_ptr<Tile>(original ? original->deepCopy(editor.map) : editor.map.allocator(location));
				for (const Operation* operation : operations) {
					if (operation->kind == OperationKind::Brush) {
						Brush* brush = g_brushes.getBrush(operation->brush);
						if (!brush) {
							throw std::runtime_error("Brush disappeared during apply.");
						}
						const int beforeSize = tile->size();
						const uint16_t beforeGround = tile->ground ? tile->ground->getID() : 0;
						brush->draw(&editor.map, tile.get(), nullptr);
						if (brush->isGround()) {
							if (!tile->ground) {
								throw std::runtime_error("Ground brush did not create ground.");
							}
							borderTiles.insert(position);
						} else if (brush->isWall()) {
							wallTiles.insert(position);
						} else if (beforeSize == tile->size() && beforeGround == (tile->ground ? tile->ground->getID() : 0)) {
							throw std::runtime_error("Brush did not produce an item on the target tile.");
						}
					} else {
						std::unique_ptr<Item> item(Item::Create(operation->itemId));
						if (!item) {
							throw std::runtime_error("Could not create active raw item.");
						}
						tile->addItem(item.release());
					}
				}
				tile->update();
				action->addChange(newd Change(tile.release()));
			}
			if (!batch->addAndCommitAction(action.release())) {
				throw std::runtime_error("Could not commit Lua tile operations.");
			}
			if (!borderTiles.empty() || !wallTiles.empty()) {
				auto borders = std::unique_ptr<Action>(editor.actionQueue->createAction(batch.get()));
				borderTiles.insert(wallTiles.begin(), wallTiles.end());
				for (const Position& position : borderTiles) {
					Tile* current = editor.map.getTile(EditorPosition(position));
					if (!current) {
						continue;
					}
					auto tile = std::unique_ptr<Tile>(current->deepCopy(editor.map));
					if (wallTiles.contains(position)) {
						tile->wallize(&editor.map);
					} else {
						tile->cleanBorders();
						tile->borderize(&editor.map);
					}
					tile->update();
					borders->addChange(newd Change(tile.release()));
				}
				if (!batch->addAndCommitAction(borders.release())) {
					throw std::runtime_error("Could not commit Lua brush borders.");
				}
			}
			editor.actionQueue->resetTimer();
			editor.addBatch(batch.release(), 0);
			editor.actionQueue->resetTimer();
			g_gui.RefreshView();
			return true;
		} catch (const std::exception& exception) {
			if (batch) {
				batch->rollback();
			}
			error = std::string(exception.what()) + " All committed changes were rolled back.";
		} catch (...) {
			if (batch) {
				batch->rollback();
			}
			error = "Unexpected Lua apply failure. All committed changes were rolled back.";
		}
		return false;
	}

} // namespace LuaExtension
