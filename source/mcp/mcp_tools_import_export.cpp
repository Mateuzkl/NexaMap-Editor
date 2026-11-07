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
#include "../path_encoding.h"
#include "../tile.h"

#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>

namespace mcp {
	namespace {
		// Decoding happens on the GUI thread, so the file's header decides whether it is decoded.
		constexpr size_t MaximumBitmapFileBytes = 64 * 1024 * 1024;
		constexpr uint32_t MaximumBitmapDimension = 512;
		constexpr uint64_t MaximumBitmapPixels = 262144;
		// Matches the limit of the selected-area export; each floor allocates width*height*3.
		constexpr int MaximumMinimapImageDimension = 2048;

		void RequireConfirmation(const Json& arguments) {
			if (!arguments.value("confirm", false)) {
				throw Error("this non-undoable operation requires confirm=true");
			}
		}

		wxFileName RequiredExistingFile(const Json& arguments) {
			if (!arguments.contains("path") || !arguments["path"].is_string()) {
				throw Error("path is required");
			}
#ifdef _WIN32
			// Merely probing \\\\host\\share sends the user's NTLM credentials to that host.
			if (IsUncPath(arguments["path"].get<std::string>())) {
				throw Error("network paths are not allowed");
			}
#endif
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

		std::string BaseName(const std::string& path) {
			return wxFileName(wxString::FromUTF8(path)).GetFullName().ToStdString(wxConvUTF8);
		}

		Json MapProperties(const Map& map) {
			return {
				{ "name", map.getName() },
				{ "description", map.getMapDescription() },
				{ "width", map.getWidth() },
				{ "height", map.getHeight() },
				{ "filename", map.hasFile() ? Json(BaseName(map.getFilename())) : Json(nullptr) },
				{ "houseFilename", BaseName(map.getHouseFilename()) },
				{ "spawnFilename", BaseName(map.getSpawnFilename()) },
				{ "spawnNpcFilename", BaseName(map.getSpawnNpcFilename()) },
				{ "waypointFilename", BaseName(map.getWaypointFilename()) },
				{ "zoneFilename", BaseName(map.getZoneFilename()) },
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

		// Read once into memory, so the bytes probed are exactly the bytes decoded.
		std::string ReadBitmapFile(const wxFileName& path) {
			wxFile file(path.GetFullPath());
			const wxFileOffset length = file.IsOpened() ? file.Length() : -1;
			if (length <= 0 || static_cast<uint64_t>(length) > MaximumBitmapFileBytes) {
				throw Error("bitmap file is unreadable or larger than 64 MiB");
			}
			std::string bytes(static_cast<size_t>(length), '\0');
			if (file.Read(bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) {
				throw Error("could not read bitmap file");
			}
			const std::optional<std::pair<uint32_t, uint32_t>> size = ProbeImageSize(bytes);
			if (!size) {
				throw Error("bitmap must be a PNG or BMP file");
			}
			if (size->first == 0 || size->second == 0 || size->first > MaximumBitmapDimension || size->second > MaximumBitmapDimension || static_cast<uint64_t>(size->first) * size->second > MaximumBitmapPixels) {
				throw Error("bitmap dimensions must fit within 512x512 and 262144 pixels");
			}
			return bytes;
		}

		std::filesystem::path MinimapExportRoot() {
			return PathEncoding::FromUtf8(GUI::GetLocalDataDirectory().ToStdString(wxConvUTF8)) / "mcp-exports";
		}

		void RequireFreeMinimapTargets(const std::filesystem::path& root, const std::string& name, const std::string& formatName, MinimapExportMode mode) {
			if (formatName == "otmm") {
				RequireFreeExportTarget(root / PathEncoding::FromUtf8(name + ".otmm"));
				return;
			}
			// IOMinimap names whole-floor images name_z and selected-area images name-z.
			const std::string separator = mode == MinimapExportMode::SelectedArea ? "-" : "_";
			for (int floor = 0; floor <= MAP_MAX_LAYER; ++floor) {
				RequireFreeExportTarget(root / PathEncoding::FromUtf8(name + separator + std::to_string(floor) + "." + formatName));
			}
		}

		// Mirrors the per-floor bounding box in IOMinimap, whose buffer is sized from it.
		void RequireBoundedMinimapImage(Map& map, int firstFloor, int lastFloor) {
			std::array<int, MAP_LAYERS> minX, minY, maxX, maxY;
			minX.fill(MAP_MAX_WIDTH + 1);
			minY.fill(MAP_MAX_HEIGHT + 1);
			maxX.fill(-1);
			maxY.fill(-1);
			for (auto iterator = map.begin(); iterator != map.end(); ++iterator) {
				const Tile* tile = (*iterator)->get();
				if (!tile || (!tile->ground && tile->items.empty())) {
					continue;
				}
				const Position& position = tile->getPosition();
				minX[position.z] = std::min(minX[position.z], position.x);
				minY[position.z] = std::min(minY[position.z], position.y);
				maxX[position.z] = std::max(maxX[position.z], position.x);
				maxY[position.z] = std::max(maxY[position.z], position.y);
			}
			for (int floor = firstFloor; floor <= lastFloor; ++floor) {
				if (maxX[floor] - minX[floor] >= MaximumMinimapImageDimension || maxY[floor] - minY[floor] >= MaximumMinimapImageDimension) {
					throw Error("the map spans more than 2048 tiles on a floor; use format otmm or mode selected-area");
				}
			}
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
					RequireUserConfirmation("Create and switch to a new blank map tab.");
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
					RequireUserConfirmation("Open this map file in NexaMap:\n" + SanitizeDialogText(arguments["path"].get<std::string>()));
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
				[](const Json&) { return UntrustedMapResult(OnGui([](const EditorContext& context) { Json result = MapProperties(context.map); result["mapSessionId"] = context.mapSessionId; return result; })); },
			});

			registry.add({
				"map_properties_update",
				"Explicitly update basic active-map metadata. This is non-undoable and requires confirmation.",
				{ { "type", "object" }, { "properties", { { "width", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } }, { "height", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 65535 } } }, { "name", { { "type", "string" }, { "maxLength", 255 } } }, { "description", { { "type", "string" }, { "maxLength", 4096 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) { RequireConfirmation(arguments); RequireMapConfirmation(arguments, "Change the active map's name, description or size. This cannot be undone."); return StructuredResult(OnGui([arguments](const EditorContext& context) { VerifyWritableContext(context, arguments); if (arguments.contains("width")){ context.map.setWidth(arguments["width"].get<int>());
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
					RequireMapConfirmation(arguments, "Merge this map file into the active map. This clears the undo history:\n" + arguments["path"].get<std::string>());
					return StructuredResult(OnGui([arguments, path, offset, housePolicy, spawnPolicy](const EditorContext& context) { VerifyWritableContext(context, arguments); if (!context.editor.importMap(FileName(path.GetFullPath()), offset.x, offset.y, offset.z, housePolicy, spawnPolicy)){ throw Error("map import failed");
} g_gui.RefreshView(); return Json { { "imported", true }, { "path", path.GetFullName().ToStdString(wxConvUTF8) }, { "mapSessionId", context.mapSessionId }, { "changeGeneration", context.map.getChangeGeneration() }, { "undoAvailable", false } }; }));
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
					const std::string bitmap = ReadBitmapFile(path);
					RequireSafetyOverrideConfirmation(arguments);
					return StructuredResult(OnGui([arguments, bitmap, origin, autoborder, allowExpansion](const EditorContext& context) {
						VerifyWritableContext(context, arguments);
						wxImage image;
						wxMemoryInputStream stream(bitmap.data(), bitmap.size());
						if (!image.LoadFile(stream) || !image.IsOk()) {
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
				"Export the active map minimap as new files in NexaMap's mcp-exports folder. Requires confirm=true and approval in NexaMap; existing files are never overwritten.",
				{ { "type", "object" }, { "properties", { { "name", { { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } } }, { "format", { { "type", "string" }, { "enum", Json::array({ "png", "bmp", "otmm" }) }, { "default", "png" } } }, { "mode", { { "type", "string" }, { "enum", Json::array({ "all-floors", "ground-floor", "specific-floor", "selected-area" }) }, { "default", "all-floors" } } }, { "floor", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 15 }, { "default", 7 } } }, { "confirm", { { "type", "boolean" }, { "default", false } } }, { "mapSessionId", { { "type", "integer" }, { "minimum", 1 } } }, { "workspaceGeneration", { { "type", "integer" }, { "minimum", 0 } } } } }, { "required", Json::array({ "name" }) }, { "additionalProperties", false } },
				true,
				[](const Json& arguments) {
					RequireConfirmation(arguments);
					const std::string name = arguments.value("name", "");
					if (!IsSafeExportName(name)) {
						throw Error("name may only use letters, digits, '.', '_' and '-', and must not start with '.' or '-'");
					}
					const std::string formatName = arguments.value("format", "png");
					const std::string modeName = arguments.value("mode", "all-floors");
					const MinimapExportFormat format = formatName == "png" ? MinimapExportFormat::Png : formatName == "bmp" ? MinimapExportFormat::Bmp
																															: MinimapExportFormat::Otmm;
					const MinimapExportMode mode = modeName == "ground-floor" ? MinimapExportMode::GroundFloor : modeName == "specific-floor" ? MinimapExportMode::SpecificFloor
						: modeName == "selected-area"																						  ? MinimapExportMode::SelectedArea
																																			  : MinimapExportMode::AllFloors;
					const int floor = arguments.value("floor", 7);
					RequireMapConfirmation(arguments, "Export the minimap of the active map to NexaMap's mcp-exports folder as: " + name);
					return StructuredResult(OnGui([arguments, name, formatName, format, mode, floor](const EditorContext& context) {
						VerifyMapSession(context, arguments);
						const std::filesystem::path root = MinimapExportRoot();
						PrepareExportRoot(root);
						RequireFreeMinimapTargets(root, name, formatName, mode);
						if (format != MinimapExportFormat::Otmm && mode != MinimapExportMode::SelectedArea) {
							const int singleFloor = mode == MinimapExportMode::GroundFloor ? GROUND_LAYER : floor;
							const bool allFloors = mode == MinimapExportMode::AllFloors;
							RequireBoundedMinimapImage(context.map, allFloors ? 0 : singleFloor, allFloors ? MAP_MAX_LAYER : singleFloor);
						}
						IOMinimap exporter(&context.editor, format, mode, false);
						const std::u8string rootText = root.u8string();
						if (!exporter.saveMinimap(std::string(rootText.begin(), rootText.end()), name, floor)) {
							throw Error("minimap export failed: " + exporter.getError());
						}
						return Json { { "exported", true }, { "directory", "mcp-exports" }, { "name", name } };
					}));
				},
			});
		});
	}
}
