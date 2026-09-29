// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_render.h"

#include "mcp_item_identity.h"
#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../brush.h"
#include "../graphics.h"
#include "../gui.h"
#include "../item.h"
#include "../map.h"
#include "../tile.h"

#include <algorithm>
#include <mutex>

#include <wx/base64.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/mstream.h>

namespace mcp {
	namespace {
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

		wxColour MinimapColour(const Tile* tile) {
			if (!tile) {
				return wxColour(8, 10, 12);
			}
			const uint8_t color = tile->getMiniMapColor();
			return wxColour(static_cast<int>(color / 36) % 6 * 51, static_cast<int>(color / 6) % 6 * 51, color % 6 * 51);
		}
	}

	void RegisterRenderTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"map_render_region",
				"Render a bounded map region as a PNG using active classic or appearances resources. Returns MCP image content.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "from", PositionSchema() },
										{ "to", PositionSchema() },
										{ "origin", PositionSchema() },
										{ "width", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 128 } } },
										{ "height", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 128 } } },
										{ "mode", { { "type", "string" }, { "enum", Json::array({ "sprites", "minimap" }) }, { "default", "sprites" } } },
										{ "tileSize", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 32 } } },
									} },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments, 16384);
					const std::string mode = arguments.value("mode", std::string("sprites"));
					if (mode != "sprites" && mode != "minimap") {
						throw Error("mode must be sprites or minimap");
					}
					const int tileSize = arguments.value("tileSize", mode == "sprites" ? 32 : 4);
					if (tileSize < 1 || tileSize > 32) {
						throw Error("tileSize must be between 1 and 32");
					}
					if (region.width() * static_cast<size_t>(tileSize) > 2048 || region.height() * static_cast<size_t>(tileSize) > 2048) {
						throw Error("rendered image dimensions must not exceed 2048x2048");
					}
					return OnGui([region, mode, tileSize](const EditorContext& context) {
						wxBitmap bitmap(static_cast<int>(region.width()) * tileSize, static_cast<int>(region.height()) * tileSize, 32);
						wxMemoryDC dc(bitmap);
						dc.SetBackground(wxBrush(wxColour(8, 10, 12)));
						dc.Clear();
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Tile* tile = context.map.getTile(x, y, region.from.z);
								const int drawX = (x - region.from.x) * tileSize;
								const int drawY = (y - region.from.y) * tileSize;
								if (mode == "minimap") {
									dc.SetPen(*wxTRANSPARENT_PEN);
									dc.SetBrush(wxBrush(MinimapColour(tile)));
									dc.DrawRectangle(drawX, drawY, tileSize, tileSize);
									continue;
								}
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
						Json metadata { { "mode", mode }, { "from", PositionJson(region.from) }, { "to", PositionJson(region.to) }, { "pixelWidth", bitmap.GetWidth() }, { "pixelHeight", bitmap.GetHeight() }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "assetMode", AssetModeName(context.assetMode) } };
						return ImageResult("image/png", EncodePng(bitmap), std::move(metadata));
					});
				},
			});

			registry.add({
				"brush_preview",
				"Render the representative sprite of an exact loaded brush as MCP PNG image content.",
				{ { "type", "object" }, { "properties", { { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "size", { { "type", "integer" }, { "minimum", 16 }, { "maximum", 256 }, { "default", 96 } } } } }, { "required", Json::array({ "name" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("name") || !arguments["name"].is_string()) {
						throw Error("name must be a string");
					}
					const std::string name = arguments["name"].get<std::string>();
					const int size = arguments.value("size", 96);
					if (size < 16 || size > 256) {
						throw Error("size must be between 16 and 256");
					}
					return OnGui([name, size](const EditorContext& context) {
						Brush* brush = FindBrush(name);
						if (brush->getLookID() <= 0 || g_gui.gfx.isUnloaded()) {
							throw Error("brush does not have a renderable representative sprite");
						}
						Sprite* sprite = g_gui.gfx.getSprite(brush->getLookID());
						if (!sprite) {
							throw Error("brush representative sprite is not loaded");
						}
						wxBitmap bitmap(size, size, 32);
						wxMemoryDC dc(bitmap);
						dc.SetBackground(wxBrush(wxColour(8, 10, 12)));
						dc.Clear();
						sprite->DrawTo(&dc, size <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, 0, 0, size, size);
						dc.SelectObject(wxNullBitmap);
						return ImageResult("image/png", EncodePng(bitmap), { { "name", name }, { "kind", BrushKind(*brush) }, { "lookId", brush->getLookID() }, { "assetMode", AssetModeName(context.assetMode) } });
					});
				},
			});

			registry.add({
				"sprite_render",
				"Render one loaded item sprite by active item ID using the current classic or appearances resource session.",
				{ { "type", "object" }, { "properties", { { "id", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } }, { "size", { { "type", "integer" }, { "minimum", 16 }, { "maximum", 256 }, { "default", 96 } } } } }, { "required", Json::array({ "id" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("id") || !arguments["id"].is_number_unsigned()) {
						throw Error("id is required");
					}
					const uint16_t id = arguments["id"].get<uint16_t>();
					const int size = arguments.value("size", 96);
					return OnGui([id, size](const EditorContext& context) {
						if (!g_items.typeExists(id)) {
							throw Error("item id is not loaded in the active resource session");
						}
						const ItemType& type = g_items.getItemType(id);
						if (type.clientID == 0 || g_gui.gfx.isUnloaded()) {
							throw Error("item has no loaded sprite identity");
						}
						Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
						if (!sprite) {
							throw Error("item sprite is not loaded");
						}
						wxBitmap bitmap(size, size, 32);
						wxMemoryDC dc(bitmap);
						dc.SetBackground(wxBrush(wxColour(8, 10, 12)));
						dc.Clear();
						sprite->DrawTo(&dc, size <= 16 ? SPRITE_SIZE_16x16 : SPRITE_SIZE_32x32, 0, 0, size, size);
						dc.SelectObject(wxNullBitmap);
						Json metadata = ItemIdentity(id, context.itemIdMode, type);
						metadata["assetMode"] = AssetModeName(context.assetMode);
						metadata["pixelWidth"] = size;
						metadata["pixelHeight"] = size;
						return ImageResult("image/png", EncodePng(bitmap), std::move(metadata));
					});
				},
			});

			registry.add({
				"item_sprite_info",
				"Return loaded sprite identity and dimensions for one active item ID.",
				{ { "type", "object" }, { "properties", { { "id", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } } } }, { "required", Json::array({ "id" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("id") || !arguments["id"].is_number_unsigned()) {
						throw Error("id is required");
					}
					const uint16_t id = arguments["id"].get<uint16_t>();
					return StructuredResult(OnGui([id](const EditorContext& context) {
						if (!g_items.typeExists(id)) {
							throw Error("item id is not loaded in the active resource session");
						}
						const ItemType& type = g_items.getItemType(id);
						Json result = ItemIdentity(id, context.itemIdMode, type);
						result["spriteId"] = type.clientID == 0 ? Json(nullptr) : Json(type.clientID);
						result["hasLoadedSprite"] = type.clientID != 0 && !g_gui.gfx.isUnloaded() && g_gui.gfx.getSprite(type.clientID) != nullptr;
						result["assetMode"] = AssetModeName(context.assetMode);
						return result;
					}));
				},
			});
			const Tool itemSpriteInfo = *registry.find("item_sprite_info");
			registry.add({ "spr_sprite_info", "Inspect classic SPR or active-backend sprite identity for one item.", itemSpriteInfo.inputSchema, false, itemSpriteInfo.handler });

			const Tool regionRender = *registry.find("map_render_region");
			registry.add({ "client_view", "Render an explicit region with active client sprites for AI visual inspection.", regionRender.inputSchema, false, regionRender.handler });
			registry.add({ "map_screenshot", "Render an explicit map region as a screenshot-like MCP image.", regionRender.inputSchema, false, regionRender.handler });

			registry.add({
				"client_view_scan",
				"Scan a bounded client-view region for ground holes, unmapped leaks and blocked tiles.",
				{ { "type", "object" }, { "properties", { { "region", { { "type", "object" } } }, { "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 1000 }, { "default", 100 } } } } }, { "required", Json::array({ "region" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					const Region region = ParseRegion(arguments.at("region"), 65536);
					const size_t limit = std::min<size_t>(arguments.value("limit", 100), 1000);
					return StructuredResult(OnGui([region, limit](const EditorContext& context) {
						Json issues = Json::array();
						size_t total = 0;
						size_t mapped = 0;
						size_t blocked = 0;
						auto add = [&](const char* code, const Position& position) { ++total; if (issues.size() < limit){ issues.push_back({ { "code", code }, { "position", PositionJson(position) } });
} };
						for (int y = region.from.y; y <= region.to.y; ++y) {
							for (int x = region.from.x; x <= region.to.x; ++x) {
								const Position position(x, y, region.from.z);
								const Tile* tile = context.map.getTile(position);
								if (!tile) {
									bool adjacent = false;
									for (int dy = -1; dy <= 1 && !adjacent; ++dy) {
										for (int dx = -1; dx <= 1; ++dx) {
											if ((dx || dy) && context.map.getTile(x + dx, y + dy, position.z)) {
												adjacent = true;
												break;
											}
										}
									}
									if (adjacent) {
										add("unmapped-visible-hole", position);
									}
									continue;
								}
								++mapped;
								blocked += tile->isBlocking() ? 1 : 0;
								if (!tile->ground && (!tile->items.empty() || tile->creature || tile->spawn)) {
									add("content-without-ground", position);
								}
							}
						}
						return Json { { "valid", total == 0 }, { "issueCount", total }, { "issues", std::move(issues) }, { "truncated", total > limit }, { "mappedTiles", mapped }, { "blockedTiles", blocked } };
					}));
				},
			});
		});
	}
}
