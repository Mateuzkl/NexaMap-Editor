// SPDX-License-Identifier: GPL-3.0-or-later
#include "mcp_tools_import_export.h"

#include "mcp_gui_call.h"
#include "mcp_registry.h"
#include "mcp_tool_utils.h"

#include "../action.h"
#include "../brush.h"
#include "../editor.h"
#include "../gui.h"
#include "../ground_brush.h"
#include "../iominimap.h"
#include "../iomap.h"
#include "../map.h"
#include "../tile.h"

#include <map>
#include <mutex>
#include <set>

#include <wx/filename.h>
#include <wx/image.h>

namespace mcp {
	namespace {
		void RequireConfirmation(const Json& arguments) {
			if (!arguments.value("confirm", false)) {
				throw Error("this non-undoable operation requires confirm=true");
			}
		}

		wxFileName RequiredExistingFile(const Json& arguments) {
			if (!arguments.contains("path") || !arguments["path"].is_string()) {
				throw Error("path is required");
			}
			wxFileName path(wxString::FromUTF8(arguments["path"].get<std::string>()));
			if (!path.IsAbsolute() || !path.FileExists()) {
				throw Error("path must be an existing absolute file");
			}
			return path;
		}

		ImportType ParseImportType(const std::string& value) {
			if (value == "none") {
				return IMPORT_DONT;
			}
			if (value == "merge") {
				return IMPORT_MERGE;
			}
			if (value == "smart-merge") {
				return IMPORT_SMART_MERGE;
			}
			if (value == "insert") {
				return IMPORT_INSERT;
			}
			throw Error("invalid import policy");
		}

		Json MapProperties(const Map& map) {
			return {
				{ "name", map.getName() },
				{ "description", map.getMapDescription() },
				{ "width", map.getWidth() },
				{ "height", map.getHeight() },
				{ "filename", map.hasFile() ? Json(map.getFilename()) : Json(nullptr) },
				{ "houseFilename", map.getHouseFilename() },
				{ "spawnFilename", map.getSpawnFilename() },
				{ "spawnNpcFilename", map.getSpawnNpcFilename() },
				{ "waypointFilename", map.getWaypointFilename() },
				{ "zoneFilename", map.getZoneFilename() },
			};
		}

		uint32_t ParseRgb(const std::string& text) {
			if (text.size() != 7 || text.front() != '#') {
				throw Error("bitmap mapping keys must use #rrggbb");
			}
			uint32_t value = 0;
			for (size_t index = 1; index < text.size(); ++index) {
				value <<= 4;
				const char c = text[index];
				if (c >= '0' && c <= '9') {
					value |= static_cast<uint32_t>(c - '0');
				} else if (c >= 'a' && c <= 'f') {
					value |= static_cast<uint32_t>(c - 'a' + 10);
				} else if (c >= 'A' && c <= 'F') {
					value |= static_cast<uint32_t>(c - 'A' + 10);
				} else {
					throw Error("bitmap mapping keys must use #rrggbb");
				}
			}
			return value;
		}

	}

	void RegisterImportExportTools() {
		static std::once_flag once;
		std::call_once(once, [] {
			ToolRegistry& registry = ToolRegistry::Instance();

			registry.add({
				"map_new",
				"Create and activate a new blank map tab. This is non-undoable and requires confirmation.",
				{ { "type", "object" }, { "properties", { { "width", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 }, { "default", 2048 } } }, { "height", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 }, { "default", 2048 } } }, { "name", { { "type", "string" }, { "maxLength", 255 } } }, { "description", { { "type", "string" }, { "maxLength", 4096 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } } } }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					RequireConfirmation(arguments);
					const int width = arguments.value("width", 2048);
					const int height = arguments.value("height", 2048);
					return StructuredResult(CallJsonOnGui([arguments, width, height] {
						if (!g_gui.NewMap()) {
							throw Error("NexaMap could not create a new map tab");
						}
						Editor* editor = g_gui.GetCurrentEditor();
						if (!editor) {
							throw Error("new map tab did not become active");
						}
						editor->map.setWidth(width);
						editor->map.setHeight(height);
						if (arguments.contains("name")) {
							editor->map.setName(arguments["name"].get<std::string>());
						}
						if (arguments.contains("description")) {
							editor->map.setMapDescription(arguments["description"].get<std::string>());
						}
						g_gui.RefreshView();
						Json result = MapProperties(editor->map);
						result["created"] = true;
						result["mapSessionId"] = editor->map.getSessionId();
						return result;
					}));
				},
			});

			registry.add({
				"map_open",
				"Open an existing absolute map file in a new tab. Requires confirmation and does not modify the current map.",
				{ { "type", "object" }, { "properties", { { "path", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 4096 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } } } }, { "required", Json::array({ "path" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					RequireConfirmation(arguments);
					const wxFileName path = RequiredExistingFile(arguments);
					return StructuredResult(CallJsonOnGui([path] { if (!g_gui.LoadMap(FileName(path.GetFullPath()))){ throw Error("NexaMap could not open the requested map");
} Editor* editor = g_gui.GetCurrentEditor(); if (!editor){ throw Error("opened map did not become active");
} Json result = MapProperties(editor->map); result["opened"] = true; result["mapSessionId"] = editor->map.getSessionId(); return result; }));
				},
			});

			registry.add({
				"map_properties",
				"Read basic metadata and auxiliary filenames from the active map.",
				{ { "type", "object" }, { "properties", Json::object() }, { "additionalProperties", false } },
				false,
				[](const Json&) { return StructuredResult(OnGui([](const EditorContext& context) { Json result = MapProperties(context.map); result["mapSessionId"] = context.mapSessionId; return result; })); },
			});

			registry.add({
				"map_properties_update",
				"Explicitly update basic active-map metadata. This is non-undoable and requires confirmation.",
				{ { "type", "object" }, { "properties", { { "width", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } }, { "height", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } }, { "name", { { "type", "string" }, { "maxLength", 255 } } }, { "description", { { "type", "string" }, { "maxLength", 4096 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { RequireConfirmation(arguments); return StructuredResult(OnGui([arguments](const EditorContext& context) { VerifyWritableContext(context, arguments); if (arguments.contains("width")){ context.map.setWidth(arguments["width"].get<int>());
} if (arguments.contains("height")){ context.map.setHeight(arguments["height"].get<int>());
} if (arguments.contains("name")){ context.map.setName(arguments["name"].get<std::string>());
} if (arguments.contains("description")){ context.map.setMapDescription(arguments["description"].get<std::string>());
} context.map.doChange(); g_gui.RefreshView(); Json result = MapProperties(context.map); result["updated"] = true; result["mapSessionId"] = context.mapSessionId; return result; })); },
			});

			registry.add({
				"map_import",
				"Merge an existing map file into the active map. This clears undo history and requires explicit confirmation.",
				{ { "type", "object" }, { "properties", { { "path", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 4096 } } }, { "offset", PositionSchema() }, { "housePolicy", { { "type", "string" }, { "enum", Json::array({ "none", "merge", "smart-merge", "insert" }) }, { "default", "smart-merge" } } }, { "spawnPolicy", { { "type", "string" }, { "enum", Json::array({ "none", "merge", "smart-merge", "insert" }) }, { "default", "smart-merge" } } }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "path" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					RequireConfirmation(arguments);
					const wxFileName path = RequiredExistingFile(arguments);
					const Position offset = arguments.contains("offset") ? ParsePosition(arguments["offset"], "offset") : Position(0, 0, 0);
					const ImportType housePolicy = ParseImportType(arguments.value("housePolicy", "smart-merge"));
					const ImportType spawnPolicy = ParseImportType(arguments.value("spawnPolicy", "smart-merge"));
					return StructuredResult(OnGui([arguments, path, offset, housePolicy, spawnPolicy](const EditorContext& context) { VerifyWritableContext(context, arguments); if (!context.editor.importMap(FileName(path.GetFullPath()), offset.x, offset.y, offset.z, housePolicy, spawnPolicy)){ throw Error("map import failed");
} g_gui.RefreshView(); return Json { { "imported", true }, { "path", path.GetFullPath().ToStdString() }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", false } }; }));
				},
			});

			registry.add({
				"map_from_bitmap",
				"Paint a bounded bitmap into the active map by mapping #rrggbb colors to real loaded ground brushes, with native autoborders and one undo step.",
				{ { "type", "object" }, { "properties", { { "path", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 4096 } } }, { "origin", PositionSchema() }, { "mappings", { { "type", "object" }, { "minProperties", 1 }, { "maxProperties", 256 }, { "additionalProperties", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } } } }, { "autoborder", { { "type", "boolean" }, { "default", true } } }, { "allowExpansion", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "path", "origin", "mappings" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					const wxFileName path = RequiredExistingFile(arguments);
					const Position origin = ParsePosition(arguments["origin"]);
					if (!arguments.contains("mappings") || !arguments["mappings"].is_object() || arguments["mappings"].empty()) {
						throw Error("mappings must be a non-empty color-to-brush object");
					}
					const bool autoborder = arguments.value("autoborder", true);
					const bool allowExpansion = arguments.value("allowExpansion", false);
					return StructuredResult(OnGui([arguments, path, origin, autoborder, allowExpansion](const EditorContext& context) {
						VerifyWritableContext(context, arguments);
						wxImage image;
						if (!image.LoadFile(path.GetFullPath()) || !image.IsOk()) {
							throw Error("could not decode bitmap image");
						}
						if (image.GetWidth() <= 0 || image.GetHeight() <= 0 || image.GetWidth() > 512 || image.GetHeight() > 512 || static_cast<uint64_t>(image.GetWidth()) * image.GetHeight() > 262144) {
							throw Error("bitmap dimensions must fit within 512x512 and 262144 pixels");
						}
						std::map<uint32_t, GroundBrush*> mappings;
						for (auto iterator = arguments["mappings"].begin(); iterator != arguments["mappings"].end(); ++iterator) {
							Brush* brush = FindBrush(iterator.value().get<std::string>());
							if (!brush->isGround()) {
								throw Error("map_from_bitmap mappings must reference ground brushes");
							}
							mappings.emplace(ParseRgb(iterator.key()), brush->asGround());
						}
						std::map<Position, GroundBrush*> paints;
						std::vector<Position> positions;
						for (int y = 0; y < image.GetHeight(); ++y) {
							for (int x = 0; x < image.GetWidth(); ++x) {
								if (image.HasAlpha() && image.GetAlpha(x, y) == 0) {
									continue;
								}
								const uint32_t rgb = (static_cast<uint32_t>(image.GetRed(x, y)) << 16) | (static_cast<uint32_t>(image.GetGreen(x, y)) << 8) | image.GetBlue(x, y);
								auto mapping = mappings.find(rgb);
								if (mapping == mappings.end()) {
									continue;
								}
								const Position position(origin.x + x, origin.y + y, origin.z);
								if (!position.isValid()) {
									throw Error("bitmap destination leaves valid map coordinates");
								}
								paints.emplace(position, mapping->second);
								positions.push_back(position);
							}
						}
						if (paints.empty()) {
							throw Error("no bitmap pixels matched the supplied color mappings");
						}
						EnforceSelectionBoundary(context, positions, allowExpansion);
						std::unique_ptr<BatchAction> batch(context.editor.actionQueue->createBatch(ACTION_MCP));
						std::unique_ptr<Action> draw(context.editor.actionQueue->createAction(batch.get()));
						std::set<Position> borders;
						for (const auto& [position, brush] : paints) {
							TileLocation* location = context.map.createTileL(position);
							Tile* existing = location->get();
							std::unique_ptr<Tile> tile(existing ? existing->deepCopy(context.map) : context.map.allocator(location));
							tile->cleanBorders();
							brush->draw(&context.map, tile.get(), nullptr);
							draw->addChange(newd Change(tile.release()));
							if (autoborder) {
								for (int dy = -1; dy <= 1; ++dy) {
									for (int dx = -1; dx <= 1; ++dx) {
										const Position border(position.x + dx, position.y + dy, position.z);
										if (border.isValid() && (allowExpansion || paints.contains(border))) {
											borders.insert(border);
										}
									}
								}
							}
						}
						if (!batch->addAndCommitAction(draw.release())) {
							throw Error("could not commit bitmap terrain action");
						}
						if (autoborder) {
							std::unique_ptr<Action> borderAction(context.editor.actionQueue->createAction(batch.get()));
							for (const Position& position : borders) {
								TileLocation* location = context.map.createTileL(position);
								Tile* existing = location->get();
								std::unique_ptr<Tile> tile(existing ? existing->deepCopy(context.map) : context.map.allocator(location));
								tile->borderize(&context.map);
								if (tile->size() > 0) {
									borderAction->addChange(newd Change(tile.release()));
								}
							}
							if (borderAction->size() > 0 && !batch->addAndCommitAction(borderAction.release())) {
								batch->rollback();
								throw Error("could not commit bitmap autoborders");
							}
						}
						context.editor.addBatch(batch.release(), 0);
						context.editor.actionQueue->resetTimer();
						g_gui.RefreshView();
						return Json { { "painted", true }, { "tiles", paints.size() }, { "width", image.GetWidth() }, { "height", image.GetHeight() }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", context.editor.actionQueue->canUndo() } };
					}));
				},
			});

			registry.add({
				"export_minimap",
				"Export the active map minimap to an absolute directory. External file creation requires confirmation.",
				{ { "type", "object" }, { "properties", { { "directory", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 4096 } } }, { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 255 } } }, { "format", { { "type", "string" }, { "enum", Json::array({ "png", "bmp", "otmm" }) }, { "default", "png" } } }, { "mode", { { "type", "string" }, { "enum", Json::array({ "all-floors", "ground-floor", "specific-floor", "selected-area" }) }, { "default", "all-floors" } } }, { "floor", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 15 }, { "default", 7 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } } } }, { "required", Json::array({ "directory", "name" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					RequireConfirmation(arguments);
					wxFileName directory(wxString::FromUTF8(arguments.value("directory", "")), "");
					if (!directory.IsAbsolute() || !directory.DirExists()) {
						throw Error("directory must be an existing absolute directory");
					}
					const std::string name = arguments.value("name", "");
					wxFileName safeName(wxString::FromUTF8(name));
					if (safeName.GetFullName().ToStdString() != name || !safeName.GetPath().empty()) {
						throw Error("name must be a plain file name without directories");
					}
					const std::string formatName = arguments.value("format", "png");
					const std::string modeName = arguments.value("mode", "all-floors");
					const MinimapExportFormat format = formatName == "png" ? MinimapExportFormat::Png : formatName == "bmp" ? MinimapExportFormat::Bmp
																															: MinimapExportFormat::Otmm;
					const MinimapExportMode mode = modeName == "ground-floor" ? MinimapExportMode::GroundFloor : modeName == "specific-floor" ? MinimapExportMode::SpecificFloor
						: modeName == "selected-area"																						  ? MinimapExportMode::SelectedArea
																																			  : MinimapExportMode::AllFloors;
					const int floor = arguments.value("floor", 7);
					return StructuredResult(OnGui([directory, name, format, mode, floor](const EditorContext& context) { IOMinimap exporter(&context.editor, format, mode, false); if (!exporter.saveMinimap(directory.GetFullPath().ToStdString(), name, floor)){ throw Error("minimap export failed: " + exporter.getError());
} return Json { { "exported", true }, { "directory", directory.GetFullPath().ToStdString() }, { "name", name } }; }));
				},
			});
		});
	}
}
