// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_reference.h"

#include "mcp_context.h"
#include "mcp_gui_call.h"
#include "mcp_item_identity.h"
#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../doodad_brush.h"
#include "../editor.h"
#include "../graphics.h"
#include "../ground_brush.h"
#include "../gui.h"
#include "../item.h"
#include "../map.h"
#include "../reference_style.h"
#include "../reference_style_store.h"
#include "../tile.h"
#include "../wall_brush.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <unordered_set>

#include <wx/base64.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/mstream.h>

namespace mcp {
	namespace {

		ReferenceAssetMode ToReferenceAssetMode(AssetMode mode) {
			switch (mode) {
				case AssetMode::ClassicDatSpr:
					return ReferenceAssetMode::ClassicDatSpr;
				case AssetMode::Appearances:
					return ReferenceAssetMode::Appearances;
				case AssetMode::Unknown:
				default:
					return ReferenceAssetMode::Unknown;
			}
		}

		std::string EncodePng(const wxBitmap& bitmap) {
			wxMemoryOutputStream output;
			if (!bitmap.ConvertToImage().SaveFile(output, wxBITMAP_TYPE_PNG)) {
				throw Error("could not encode the rendered image as PNG");
			}
			const wxStreamBuffer* buffer = output.GetOutputStreamBuffer();
			return wxBase64Encode(buffer->GetBufferStart(), output.GetSize()).ToStdString();
		}

		void DrawItem(wxMemoryDC& dc, const Item* item, int x, int y, int tileSize) {
			if (!item || g_gui.gfx.isUnloaded()) {
				return;
			}
			Sprite* sprite = g_gui.gfx.getSprite(item->getClientID());
			if (sprite) {
				sprite->DrawTo(&dc, tileSize <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, x, y, tileSize, tileSize);
			}
		}

		wxBitmap RenderReferenceMaskBitmap(
			const ReferenceStyleSnapshot& snapshot,
			const Map& map,
			int tileSize,
			bool dimUnselected
		) {
			const int width = snapshot.sourceMax.x - snapshot.sourceMin.x + 1;
			const int height = snapshot.sourceMax.y - snapshot.sourceMin.y + 1;

			// Build set of local coordinates that belong to the reference mask.
			std::set<std::pair<int16_t, int16_t>> selectedMask;
			for (const Position& position : snapshot.sourceMask) {
				selectedMask.insert({
					static_cast<int16_t>(position.x - snapshot.sourceMin.x),
					static_cast<int16_t>(position.y - snapshot.sourceMin.y),
				});
			}

			wxBitmap bitmap(width * tileSize, height * tileSize, 32);
			wxMemoryDC dc(bitmap);
			dc.SetBackground(wxBrush(wxColour(8, 10, 12)));
			dc.Clear();

			for (int y = snapshot.sourceMin.y; y <= snapshot.sourceMax.y; ++y) {
				for (int x = snapshot.sourceMin.x; x <= snapshot.sourceMax.x; ++x) {
					const int localX = x - snapshot.sourceMin.x;
					const int localY = y - snapshot.sourceMin.y;
					const int drawX = localX * tileSize;
					const int drawY = localY * tileSize;

					const bool isSelected = selectedMask.contains({ static_cast<int16_t>(localX), static_cast<int16_t>(localY) });
					if (!isSelected) {
						if (dimUnselected) {
							dc.SetPen(*wxTRANSPARENT_PEN);
							dc.SetBrush(wxBrush(wxColour(12, 14, 18)));
							dc.DrawRectangle(drawX, drawY, tileSize, tileSize);
						}
						continue;
					}

					const Tile* tile = map.getTile(x, y, snapshot.floor);
					if (!tile) {
						continue;
					}

					DrawItem(dc, tile->ground, drawX, drawY, tileSize);
					for (const Item* item : tile->items) {
						DrawItem(dc, item, drawX, drawY, tileSize);
					}
				}
			}

			dc.SelectObject(wxNullBitmap);
			return bitmap;
		}

		Json FormatSummary(const ReferenceStyleSnapshot& snapshot, const EditorContext& context) {
			Json topGrounds = Json::array();
			for (size_t i = 0; i < std::min<size_t>(5, snapshot.groundFamilies.size()); ++i) {
				const auto& gf = snapshot.groundFamilies[i];
				topGrounds.push_back({
					{ "name", gf.name },
					{ "brush", gf.brush },
					{ "tiles", gf.tiles },
					{ "ratio", gf.ratio },
				});
			}

			Json topBrushes = Json::array();
			for (size_t i = 0; i < std::min<size_t>(8, snapshot.brushes.size()); ++i) {
				const auto& bu = snapshot.brushes[i];
				topBrushes.push_back({
					{ "name", bu.name },
					{ "kind", bu.kind },
					{ "count", bu.count },
					{ "ratio", bu.ratio },
				});
			}

			return {
				{ "role", "read-only-source-style-reference" },
				{ "version", snapshot.version },
				{ "floor", snapshot.floor },
				{ "bounds", {
								{ "from", PositionJson(snapshot.sourceMin) },
								{ "to", PositionJson(snapshot.sourceMax) },
							} },
				{ "selectedTileCount", snapshot.selectedTileCount },
				{ "mappedTileCount", snapshot.mappedTileCount },
				{ "ignoredOtherFloorTiles", snapshot.ignoredOtherFloorTiles },
				{ "assetMode", AssetModeName(context.assetMode) },
				{ "itemIdMode", McpItemIdModeName(context.itemIdMode) },
				{ "uniqueVisibleItems", snapshot.items.size() },
				{ "groundFamiliesCount", snapshot.groundFamilies.size() },
				{ "brushesCount", snapshot.brushes.size() },
				{ "topGroundFamilies", std::move(topGrounds) },
				{ "topBrushes", std::move(topBrushes) },
				{ "layout", {
								{ "width", snapshot.layout.width },
								{ "height", snapshot.layout.height },
								{ "mappedRatio", snapshot.layout.mappedRatio },
								{ "walkableRatio", snapshot.layout.walkableRatio },
								{ "blockingRatio", snapshot.layout.blockingRatio },
								{ "groundCoverage", snapshot.layout.groundCoverage },
								{ "wallsPerTile", snapshot.layout.wallsPerTile },
								{ "doodadsPerTile", snapshot.layout.doodadsPerTile },
								{ "decorativeItemsPerTile", snapshot.layout.decorativeItemsPerTile },
								{ "perimeterWallRatio", snapshot.layout.perimeterWallRatio },
								{ "interiorWallRatio", snapshot.layout.interiorWallRatio },
							} },
				{ "gameplay", {
								  { "teleports", snapshot.gameplay.teleports },
								  { "doors", snapshot.gameplay.doors },
								  { "houses", snapshot.gameplay.houses },
								  { "spawns", snapshot.gameplay.spawns },
								  { "creatures", snapshot.gameplay.creatures },
								  { "waypoints", snapshot.gameplay.waypoints },
								  { "zones", snapshot.gameplay.zones },
								  { "containers", snapshot.gameplay.containers },
							  } },
			};
		}

		Json FormatTarget(const TargetAreaSnapshot& target) {
			Json positions = Json::array();
			for (const Position& position : target.positions) {
				positions.push_back(PositionJson(position));
			}
			return {
				{ "role", "only-writable-generation-destination" },
				{ "mapSessionId", target.mapSessionId },
				{ "workspaceGeneration", target.workspaceGeneration },
				{ "floor", target.floor },
				{ "bounds", {
								{ "from", PositionJson(target.targetMin) },
								{ "to", PositionJson(target.targetMax) },
							} },
				{ "selectedTileCount", target.positions.size() },
				{ "exactPositions", std::move(positions) },
				{ "allowReferenceSourceOverwrite", target.allowReferenceSourceOverwrite },
			};
		}

		Json FormatMaterials(const ReferenceStyleSnapshot& snapshot) {
			Json itemsJson = Json::array();
			for (const auto& item : snapshot.items) {
				itemsJson.push_back({
					{ "activeId", item.activeId },
					{ "serverId", item.serverId },
					{ "clientId", item.clientId },
					{ "name", item.name },
					{ "category", item.category },
					{ "count", item.count },
					{ "ratio", item.ratio },
					{ "brush", item.brush },
					{ "brushKind", item.brushKind },
					{ "sourceVerified", item.sourceVerified },
				});
			}

			Json brushesJson = Json::array();
			for (const auto& brush : snapshot.brushes) {
				brushesJson.push_back({
					{ "name", brush.name },
					{ "kind", brush.kind },
					{ "count", brush.count },
					{ "ratio", brush.ratio },
					{ "lookId", brush.lookId },
					{ "needBorders", brush.needBorders },
					{ "hasVariations", brush.hasVariations },
					{ "tilesets", brush.tilesets },
				});
			}

			return {
				{ "items", std::move(itemsJson) },
				{ "brushes", std::move(brushesJson) },
			};
		}

		Json FormatLayout(const ReferenceStyleSnapshot& snapshot) {
			return {
				{ "metrics", {
								 { "width", snapshot.layout.width },
								 { "height", snapshot.layout.height },
								 { "selectedTileCount", snapshot.layout.selectedTileCount },
								 { "mappedRatio", snapshot.layout.mappedRatio },
								 { "walkableRatio", snapshot.layout.walkableRatio },
								 { "blockingRatio", snapshot.layout.blockingRatio },
								 { "groundCoverage", snapshot.layout.groundCoverage },
								 { "borderItemsPerTile", snapshot.layout.borderItemsPerTile },
								 { "wallsPerTile", snapshot.layout.wallsPerTile },
								 { "doodadsPerTile", snapshot.layout.doodadsPerTile },
								 { "decorativeItemsPerTile", snapshot.layout.decorativeItemsPerTile },
								 { "totalVisibleItemsPerTile", snapshot.layout.totalVisibleItemsPerTile },
								 { "perimeterWallRatio", snapshot.layout.perimeterWallRatio },
								 { "interiorWallRatio", snapshot.layout.interiorWallRatio },
								 { "uniqueGroundCount", snapshot.layout.uniqueGroundCount },
								 { "uniqueWallBrushCount", snapshot.layout.uniqueWallBrushCount },
								 { "uniqueDoodadBrushCount", snapshot.layout.uniqueDoodadBrushCount },
								 { "uniqueVisibleItemCount", snapshot.layout.uniqueVisibleItemCount },
							 } },
				{ "topologyGrid", snapshot.topologyGrid },
				{ "gameplay", {
								  { "teleports", snapshot.gameplay.teleports },
								  { "doors", snapshot.gameplay.doors },
								  { "houses", snapshot.gameplay.houses },
								  { "spawns", snapshot.gameplay.spawns },
								  { "creatures", snapshot.gameplay.creatures },
								  { "waypoints", snapshot.gameplay.waypoints },
								  { "zones", snapshot.gameplay.zones },
								  { "containers", snapshot.gameplay.containers },
							  } },
			};
		}

		Json FormatBorders(const ReferenceStyleSnapshot& snapshot) {
			Json familiesJson = Json::array();
			for (const auto& gf : snapshot.groundFamilies) {
				familiesJson.push_back({
					{ "name", gf.name },
					{ "brush", gf.brush },
					{ "knownBrush", gf.knownBrush },
					{ "brushId", gf.brushId },
					{ "representativeItemId", gf.representativeItemId },
					{ "itemIds", gf.itemIds },
					{ "tiles", gf.tiles },
					{ "ratio", gf.ratio },
				});
			}

			Json transitionsJson = Json::array();
			for (const auto& tr : snapshot.transitions) {
				transitionsJson.push_back({
					{ "groundA", tr.groundA },
					{ "groundB", tr.groundB },
					{ "contacts", tr.contacts },
					{ "borderItemIds", tr.borderItemIds },
					{ "borderAlignments", tr.borderAlignments },
				});
			}

			return {
				{ "groundFamilies", std::move(familiesJson) },
				{ "transitions", std::move(transitionsJson) },
			};
		}

		void CheckReferenceCompatibility(const ReferenceStyleSnapshot& snapshot, const EditorContext& context) {
			if (snapshot.sourceMapSessionId != InvalidSessionId && snapshot.sourceMapSessionId != context.mapSessionId) {
				throw Error("Captured AI style reference belongs to a different map session. Capture a new reference for the active map.");
			}
			const ReferenceAssetMode activeMode = ToReferenceAssetMode(context.assetMode);
			if (snapshot.assetMode != activeMode) {
				throw Error("Captured AI style reference belongs to a different resource session. Capture a new reference for the active assets.");
			}
			if (snapshot.workspaceGeneration != context.workspaceGeneration) {
				throw Error("Active workspace has changed since reference was captured. Capture a new reference for the active assets.");
			}
		}

	} // namespace

	void RegisterReferenceTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();

			// ─── 1. reference_capture ──────────────────────────────────
			registry.add({
				"reference_capture",
				"Capture the currently selected map area as an immutable AI style reference. Analyzes floor, wall, border, doodad, item, and layout vocabulary.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "floor", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 15 } } },
										{ "includeImage", { { "type", "boolean" }, { "default", false } } },
									} },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const int requestedFloor = arguments.value("floor", -1);
					const bool includeImage = arguments.value("includeImage", false);

					return OnGui([requestedFloor, includeImage](const EditorContext& context) {
						if (!context.editor.hasSelection()) {
							throw Error("No map tiles are selected. Select an area in NexaMap to capture as an AI style reference.");
						}

						ReferenceStyleCaptureOptions options;
						options.floor = requestedFloor;
						options.maxTiles = 65536;

						ReferenceStyleSnapshot snapshot = ReferenceStyleAnalyzer::Capture(
							context.editor.selection,
							context.map,
							options
						);

						if (snapshot.selectedTileCount == 0) {
							throw Error("The selection has no tiles on the requested floor. Select tiles and try again.");
						}

						// Store the captured reference snapshot in the active session store.
						ReferenceStyleStore::Instance().set(snapshot);

						Json result {
							{ "captured", true },
							{ "referenceVersion", snapshot.version },
							{ "floor", snapshot.floor },
							{ "selectedTiles", snapshot.selectedTileCount },
							{ "bounds", {
											{ "from", PositionJson(snapshot.sourceMin) },
											{ "to", PositionJson(snapshot.sourceMax) },
										} },
							{ "uniqueVisibleItems", snapshot.items.size() },
							{ "groundFamilies", snapshot.groundFamilies.size() },
							{ "brushes", snapshot.brushes.size() },
							{ "assetMode", AssetModeName(context.assetMode) },
							{ "itemIdMode", McpItemIdModeName(context.itemIdMode) },
						};

						if (includeImage) {
							wxBitmap bitmap = RenderReferenceMaskBitmap(snapshot, context.map, 32, true);
							result["image"] = {
								{ "mimeType", "image/png" },
								{ "data", EncodePng(bitmap) },
								{ "pixelWidth", bitmap.GetWidth() },
								{ "pixelHeight", bitmap.GetHeight() },
							};
						}

						// The selection was the source. Clearing it prevents clients that still
						// inspect selection_get from mistaking it for a writable destination.
						context.editor.selection.clear();
						g_gui.RefreshView();
						result["selectionCleared"] = true;
						result["sourceRole"] = "read-only-style-reference";
						result["message"] = "Reference captured. Now select a different target area.";

						return result;
					});
				},
			});

			// ─── 2. target_capture ─────────────────────────────────────
			registry.add({
				"target_capture",
				"Capture the current editor selection as the explicit writable destination for the active AI style reference.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "floor", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 15 } } },
										{ "allowReferenceSourceOverwrite", { { "type", "boolean" }, { "default", false } } },
									} },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const bool allowReferenceSourceOverwrite = arguments.value("allowReferenceSourceOverwrite", false);
					return OnGui([arguments, allowReferenceSourceOverwrite](const EditorContext& context) {
						const std::optional<ReferenceStyleSnapshot> reference = ReferenceStyleStore::Instance().getSnapshot();
						if (!reference) {
							throw Error("No AI style reference has been captured. Call reference_capture before target_capture.");
						}
						CheckReferenceCompatibility(*reference, context);
						if (!context.editor.hasSelection()) {
							throw Error("No map tiles are selected. Select a different target area before calling target_capture.");
						}

						const int floor = arguments.value("floor", reference->floor);
						TargetAreaSnapshot target = CaptureTargetAreaSnapshot(
							context.editor.selection,
							floor,
							context.mapSessionId,
							context.workspaceGeneration
						);
						if (target.positions.empty()) {
							throw Error("The selection has no tiles on the requested floor. Select a target area and try again.");
						}
						target.allowReferenceSourceOverwrite = allowReferenceSourceOverwrite;
						if (!allowReferenceSourceOverwrite && ReferenceSourceOverlapsTarget(*reference, target)) {
							throw Error("Target area overlaps the captured reference source. Select a different target area.");
						}
						ReferenceStyleStore::Instance().setTarget(target);

						Json result = FormatTarget(target);
						result["captured"] = true;
						result["message"] = "Target area captured. AI writes are restricted to this exact area.";
						return result;
					});
				},
			});

			// ─── 3. target_get ─────────────────────────────────────────
			registry.add({
				"target_get",
				"Retrieve the exact captured writable generation target. This is the only destination reference-driven writes may modify.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					return OnGui([](const EditorContext& context) {
						const std::optional<TargetAreaSnapshot> target = ReferenceStyleStore::Instance().getTargetSnapshot();
						if (!target) {
							throw Error("No target area is selected. Select a target area and call target_capture first.");
						}
						if (target->mapSessionId != context.mapSessionId) {
							throw Error("Target area belongs to a stale map session. Capture the target again.");
						}
						if (target->workspaceGeneration != context.workspaceGeneration) {
							throw Error("Target area belongs to a stale resource workspace. Capture the target again.");
						}
						return FormatTarget(*target);
					});
				},
			});

			// ─── 4. target_clear ───────────────────────────────────────
			registry.add({
				"target_clear",
				"Clear the captured writable generation target without clearing the source reference.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					ReferenceStyleStore::Instance().clearTarget();
					return Json { { "cleared", true } };
				},
			});

			// ─── 5. reference_get ──────────────────────────────────────
			registry.add({
				"reference_get",
				"Retrieve the captured AI style reference. Detail modes: summary (default), materials, layout, borders, or full.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "detail", { { "type", "string" }, { "enum", Json::array({ "summary", "materials", "layout", "borders", "full" }) }, { "default", "summary" } } },
									} },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const std::string detail = arguments.value("detail", std::string("summary"));

					return OnGui([detail](const EditorContext& context) {
						const ReferenceStyleSnapshot* snapshotPtr = ReferenceStyleStore::Instance().get();
						if (!snapshotPtr) {
							throw Error("No AI style reference has been captured. Select a reference room in NexaMap and call reference_capture first.");
						}

						CheckReferenceCompatibility(*snapshotPtr, context);
						const ReferenceStyleSnapshot& snapshot = *snapshotPtr;

						if (detail == "materials") {
							return FormatMaterials(snapshot);
						}
						if (detail == "layout") {
							return FormatLayout(snapshot);
						}
						if (detail == "borders") {
							return FormatBorders(snapshot);
						}
						if (detail == "full") {
							Json fullResult = FormatSummary(snapshot, context);
							fullResult["materials"] = FormatMaterials(snapshot);
							fullResult["layout"] = FormatLayout(snapshot);
							fullResult["borders"] = FormatBorders(snapshot);

							Json cellsJson = Json::array();
							for (const auto& cell : snapshot.cells) {
								cellsJson.push_back({
									{ "x", cell.localX },
									{ "y", cell.localY },
									{ "mapped", cell.mapped },
									{ "walkable", cell.walkable },
									{ "blocking", cell.blocking },
									{ "wall", cell.wall },
									{ "door", cell.door },
									{ "teleport", cell.teleport },
								});
							}
							fullResult["cells"] = std::move(cellsJson);
							Json sourceMask = Json::array();
							for (const Position& position : snapshot.sourceMask) {
								sourceMask.push_back(PositionJson(position));
							}
							fullResult["sourceMask"] = std::move(sourceMask);
							return fullResult;
						}

						// Default: summary.
						return FormatSummary(snapshot, context);
					});
				},
			});

			// ─── 6. reference_render ───────────────────────────────────
			registry.add({
				"reference_render",
				"Render the captured AI style reference exact selection mask as a PNG image using active sprites.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "maxSize", { { "type", "integer" }, { "minimum", 64 }, { "maximum", 2048 }, { "default", 1024 } } },
										{ "tileSize", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 32 }, { "default", 32 } } },
										{ "dimUnselected", { { "type", "boolean" }, { "default", true } } },
									} },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const int maxSize = arguments.value("maxSize", 1024);
					int tileSize = arguments.value("tileSize", 32);
					const bool dimUnselected = arguments.value("dimUnselected", true);

					return OnGui([maxSize, tileSize, dimUnselected](const EditorContext& context) mutable {
						const ReferenceStyleSnapshot* snapshotPtr = ReferenceStyleStore::Instance().get();
						if (!snapshotPtr) {
							throw Error("No AI style reference has been captured. Call reference_capture first.");
						}

						CheckReferenceCompatibility(*snapshotPtr, context);
						const ReferenceStyleSnapshot& snapshot = *snapshotPtr;

						const int width = snapshot.sourceMax.x - snapshot.sourceMin.x + 1;
						const int height = snapshot.sourceMax.y - snapshot.sourceMin.y + 1;

						// Clamp tileSize so dimensions do not exceed maxSize or 2048.
						const int maxDim = std::max(width, height);
						if (maxDim * tileSize > maxSize) {
							tileSize = std::max(1, maxSize / maxDim);
						}

						wxBitmap bitmap = RenderReferenceMaskBitmap(snapshot, context.map, tileSize, dimUnselected);

						Json metadata {
							{ "referenceVersion", snapshot.version },
							{ "floor", snapshot.floor },
							{ "selectedTiles", snapshot.selectedTileCount },
							{ "bounds", {
											{ "from", PositionJson(snapshot.sourceMin) },
											{ "to", PositionJson(snapshot.sourceMax) },
										} },
							{ "pixelWidth", bitmap.GetWidth() },
							{ "pixelHeight", bitmap.GetHeight() },
							{ "assetMode", AssetModeName(context.assetMode) },
						};

						return ImageResult("image/png", EncodePng(bitmap), std::move(metadata));
					});
				},
			});

			// ─── 7. reference_clear ────────────────────────────────────
			registry.add({
				"reference_clear",
				"Clear the currently captured AI style reference snapshot.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					ReferenceStyleStore::Instance().clear();
					return Json { { "cleared", true } };
				},
			});

			// ─── 8. reference_generation_guide ─────────────────────────
			registry.add({
				"reference_generation_guide",
				"Get workflow guidelines and best practices for creating maps matching the captured reference style.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					return Json {
						{ "workflow", Json::array({
										  "1. Call reference_get and reference_render. They describe SOURCE STYLE ONLY and are never a writable destination.",
										  "2. Call target_get. Its exact positions are the ONLY writable generation destination; never use reference source bounds as a target.",
										  "3. Plan the target layout using the source style without copying protected gameplay metadata.",
										  "4. Use brush_apply or tile_edit ONLY inside target_get exactPositions. These tools reject a missing, stale, or overlapping target by default.",
										  "5. Render only the target with map_render_region.",
										  "6. Run border_check, path_check, and map_validate for the target, then fix the reported issues inside the same target.",
								  }) },
						{ "rules", Json::array({
									   "reference_get/reference_render are SOURCE STYLE ONLY; target_get is the ONLY writable destination.",
									   "Never use reference source bounds or sourceMask as the generation destination.",
									   "Prefer brush_apply using captured brush names over painting raw item IDs.",
									   "Let autoborder generate border transitions between ground types; do not guess border IDs manually.",
									   "Use tile_edit for source-verified raw items only when no semantic brush exists.",
									   "Preserve target walkable space and focal points requested by the user.",
									   "Do not guess IDs from outside the loaded resource session.",
								   }) },
						{ "exampleCodexPrompt", "Use reference_get/reference_render as SOURCE STYLE ONLY. Call target_get and create a polished teleport room only in its exact positions; never write in the reference source bounds. Match its floors, walls, borders, and decoration density without copying protected metadata. Render and validate the target, then fix any border or path problems inside the target." },
					};
				},
			});

			// ─── 9. reference_similarity ───────────────────────────────
			registry.add({
				"reference_similarity",
				"Compare a target map region against the captured reference style metrics.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "from", PositionSchema() },
										{ "to", PositionSchema() },
									} },
					{ "required", Json::array({ "from", "to" }) },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments, 16384);

					return OnGui([region](const EditorContext& context) {
						const ReferenceStyleSnapshot* snapshotPtr = ReferenceStyleStore::Instance().get();
						if (!snapshotPtr) {
							throw Error("No AI style reference has been captured. Call reference_capture first.");
						}

						CheckReferenceCompatibility(*snapshotPtr, context);
						const ReferenceStyleSnapshot& ref = *snapshotPtr;

						// Collect target region brushes.
						std::unordered_set<std::string> refGrounds;
						std::unordered_set<std::string> refWalls;
						std::unordered_set<std::string> refDoodads;
						for (const auto& b : ref.brushes) {
							if (b.kind == "ground") {
								refGrounds.insert(b.name);
							} else if (b.kind == "wall") {
								refWalls.insert(b.name);
							} else if (b.kind == "doodad") {
								refDoodads.insert(b.name);
							}
						}

						std::unordered_set<std::string> targetGrounds;
						std::unordered_set<std::string> targetWalls;
						std::unordered_set<std::string> targetDoodads;
						size_t targetVisibleItems = 0;
						size_t targetTileCount = 0;

						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Tile* tile = context.map.getTile(x, y, region.from.z);
								if (!tile) {
									continue;
								}
								++targetTileCount;

								if (tile->ground) {
									++targetVisibleItems;
									if (GroundBrush* gb = tile->ground->getGroundBrush()) {
										targetGrounds.insert(gb->getName());
									}
								}
								for (const Item* item : tile->items) {
									if (!item || item->isMetaItem()) {
										continue;
									}
									++targetVisibleItems;
									if (WallBrush* wb = item->getWallBrush()) {
										targetWalls.insert(wb->getName());
									}
									if (Brush* db = item->getDoodadBrush()) {
										targetDoodads.insert(db->getName());
									}
								}
							}
						}

						const auto overlap = [](const std::unordered_set<std::string>& refSet, const std::unordered_set<std::string>& tgtSet) {
							if (refSet.empty() || tgtSet.empty()) {
								return 0.0;
							}
							size_t shared = 0;
							for (const auto& item : tgtSet) {
								if (refSet.contains(item)) {
									++shared;
								}
							}
							return static_cast<double>(shared) / static_cast<double>(refSet.size());
						};

						const double groundOverlap = overlap(refGrounds, targetGrounds);
						const double wallOverlap = overlap(refWalls, targetWalls);
						const double doodadOverlap = overlap(refDoodads, targetDoodads);

						const double refDecorDensity = ref.layout.totalVisibleItemsPerTile;
						const double targetDecorDensity = targetTileCount > 0 ? static_cast<double>(targetVisibleItems) / static_cast<double>(targetTileCount) : 0.0;
						const double densityDelta = std::abs(targetDecorDensity - refDecorDensity);

						return Json {
							{ "groundBrushOverlap", std::round(groundOverlap * 1000.0) / 1000.0 },
							{ "wallBrushOverlap", std::round(wallOverlap * 1000.0) / 1000.0 },
							{ "doodadBrushOverlap", std::round(doodadOverlap * 1000.0) / 1000.0 },
							{ "targetDecorDensity", std::round(targetDecorDensity * 100.0) / 100.0 },
							{ "referenceDecorDensity", std::round(refDecorDensity * 100.0) / 100.0 },
							{ "decorationDensityDelta", std::round(densityDelta * 100.0) / 100.0 },
						};
					});
				},
			});
		});
	}

} // namespace mcp
