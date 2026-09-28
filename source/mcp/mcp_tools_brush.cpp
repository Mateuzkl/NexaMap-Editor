// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_brush.h"

#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../brush.h"
#include "../ground_brush.h"
#include "../materials.h"
#include "../tileset.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>

namespace mcp {
	namespace {
		const char* CategoryName(TilesetCategoryType type) {
			switch (type) {
				case TILESET_TERRAIN:
					return "terrain";
				case TILESET_CREATURE:
					return "creature";
				case TILESET_DOODAD:
					return "doodad";
				case TILESET_COLLECTION:
					return "collection";
				case TILESET_ITEM:
					return "item";
				case TILESET_RAW:
					return "raw";
				case TILESET_HOUSE:
					return "house";
				case TILESET_WAYPOINT:
					return "waypoint";
				case TILESET_ZONES:
					return "zone";
				case TILESET_SAVED_TERRAIN:
					return "saved-terrain";
				case TILESET_FAVORITES:
					return "favorites";
				case TILESET_UNKNOWN:
				default:
					return "unknown";
			}
		}

		Json BrushJson(const Brush& brush) {
			Json value {
				{ "name", brush.getName() },
				{ "kind", BrushKind(brush) },
				{ "lookId", brush.getLookID() > 0 ? Json(brush.getLookID()) : Json(nullptr) },
				{ "needBorders", brush.needBorders() },
				{ "canDrag", brush.canDrag() },
				{ "canSmear", brush.canSmear() },
				{ "variations", std::max<int32_t>(1, brush.getMaxVariation()) },
				{ "visibleInPalette", brush.visibleInPalette() },
			};
			if (const auto* ground = dynamic_cast<const GroundBrush*>(&brush)) {
				value["zOrder"] = ground->getZ();
				value["outerBorder"] = ground->hasOuterBorder();
				value["innerBorder"] = ground->hasInnerBorder();
				value["optionalBorder"] = ground->hasOptionalBorder();
			}
			Json tilesets = Json::array();
			for (const auto& [tilesetName, tileset] : g_materials.tilesets) {
				if (tileset && tileset->containsBrush(&brush)) {
					tilesets.push_back(tilesetName);
				}
			}
			value["tilesets"] = std::move(tilesets);
			return value;
		}
	}

	void RegisterBrushTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();
			registry.add({
				"tileset_list",
				"List the loaded NexaMap tilesets and their real palette categories.",
				EmptyObjectSchema(),
				false,
				[](const Json&) {
					return StructuredResult(OnGui([](const EditorContext& context) {
						Json values = Json::array();
						for (const auto& [name, tileset] : g_materials.tilesets) {
							if (!tileset) {
								continue;
							}
							Json categories = Json::array();
							for (const TilesetCategory* category : tileset->categories) {
								if (category && !category->isTrivial()) {
									categories.push_back({ { "name", CategoryName(category->getType()) }, { "brushCount", category->size() } });
								}
							}
							values.push_back({ { "name", name }, { "categories", std::move(categories) } });
						}
						return Json { { "tilesets", std::move(values) }, { "assetMode", AssetModeName(context.assetMode) } };
					}));
				},
			});

			registry.add({
				"brush_list",
				"List brushes from an optional loaded tileset and palette category with bounded pagination.",
				{
					{ "type", "object" },
					{ "properties", {
										{ "tileset", { { "type", "string" }, { "maxLength", 128 } } },
										{ "category", { { "type", "string" }, { "maxLength", 32 } } },
										{ "kind", { { "type", "string" }, { "maxLength", 32 } } },
										{ "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 200 }, { "default", 50 } } },
										{ "offset", { { "type", "integer" }, { "minimum", 0 }, { "default", 0 } } },
									} },
					{ "additionalProperties", false },
				},
				false,
				[](const Json& arguments) {
					const std::string tilesetFilter = LowerText(arguments.value("tileset", std::string()));
					const std::string categoryFilter = LowerText(arguments.value("category", std::string()));
					const std::string kindFilter = LowerText(arguments.value("kind", std::string()));
					const size_t limit = std::min<size_t>(arguments.value("limit", 50), 200);
					const size_t offset = arguments.value("offset", 0);
					return StructuredResult(OnGui([=](const EditorContext& context) {
						std::vector<Brush*> brushes;
						std::unordered_set<Brush*> seen;
						for (const auto& [tilesetName, tileset] : g_materials.tilesets) {
							if (!tileset || (!tilesetFilter.empty() && LowerText(tilesetName) != tilesetFilter)) {
								continue;
							}
							for (const TilesetCategory* category : tileset->categories) {
								if (!category || (!categoryFilter.empty() && categoryFilter != CategoryName(category->getType()))) {
									continue;
								}
								for (Brush* brush : category->brushlist) {
									if (brush && (kindFilter.empty() || BrushKind(*brush) == kindFilter) && seen.insert(brush).second) {
										brushes.push_back(brush);
									}
								}
							}
						}
						std::sort(brushes.begin(), brushes.end(), [](const Brush* left, const Brush* right) { return LowerText(left->getName()) < LowerText(right->getName()); });
						Json values = Json::array();
						for (size_t index = offset; index < brushes.size() && values.size() < limit; ++index) {
							values.push_back(BrushJson(*brushes[index]));
						}
						return Json { { "brushes", std::move(values) }, { "total", brushes.size() }, { "offset", offset }, { "nextOffset", offset + limit < brushes.size() ? Json(offset + limit) : Json(nullptr) }, { "assetMode", AssetModeName(context.assetMode) } };
					}));
				},
			});

			registry.add({
				"brush_info",
				"Inspect one exact loaded brush and its border, variation and tileset metadata.",
				{ { "type", "object" }, { "properties", { { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } } } }, { "required", Json::array({ "name" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("name") || !arguments["name"].is_string()) {
						throw Error("name must be a string");
					}
					const std::string name = arguments["name"].get<std::string>();
					return StructuredResult(OnGui([name](const EditorContext& context) {
						Json value = BrushJson(*FindBrush(name));
						value["assetMode"] = AssetModeName(context.assetMode);
						value["itemIdMode"] = McpItemIdModeName(context.itemIdMode);
						return value;
					}));
				},
			});

			registry.add({
				"terrain_pairing",
				"Report how two loaded ground brushes relate in NexaMap's real autoborder model.",
				{ { "type", "object" }, { "properties", { { "first", { { "type", "string" } } }, { "second", { { "type", "string" } } } } }, { "required", Json::array({ "first", "second" }) }, { "additionalProperties", false } },
				false,
				[](const Json& arguments) {
					if (!arguments.contains("first") || !arguments["first"].is_string() || !arguments.contains("second") || !arguments["second"].is_string()) {
						throw Error("first and second must be brush names");
					}
					const std::string firstName = arguments["first"].get<std::string>();
					const std::string secondName = arguments["second"].get<std::string>();
					return StructuredResult(OnGui([firstName, secondName](const EditorContext&) {
						auto* first = dynamic_cast<GroundBrush*>(FindBrush(firstName));
						auto* second = dynamic_cast<GroundBrush*>(FindBrush(secondName));
						if (!first || !second) {
							throw Error("terrain_pairing requires two ground brushes");
						}
						return Json { { "first", firstName }, { "second", secondName }, { "friends", first->friendOf(second) }, { "firstZOrder", first->getZ() }, { "secondZOrder", second->getZ() }, { "firstHasBorderToSecond", GroundBrush::getBrushTo(first, second) != nullptr }, { "secondHasBorderToFirst", GroundBrush::getBrushTo(second, first) != nullptr } };
					}));
				},
			});
		});
	}
}
