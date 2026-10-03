#include "main.h"
#include "favorites_resources.h"
#include "brush.h"
#include "raw_brush.h"
#include "creature_brush.h"
#include "creatures.h"
#include "editor_resource_session.h"
#include "gui.h"
#include "items.h"
#include "materials.h"
#include "palette_window.h"
#include "workspace_session.h"
#include "terrain_stamp/terrain_stamp_library.h"

#include <algorithm>
#include <wx/weakref.h>

namespace {
	std::string FavoritePath(const std::filesystem::path& path) {
		if (path.empty()) {
			return {};
		}
		std::error_code error;
		auto normalized = std::filesystem::weakly_canonical(path, error);
		if (error) {
			normalized = path.lexically_normal();
		}
		const auto utf8 = normalized.generic_u8string();
		std::string result(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#ifdef _WIN32
		result = wxString::FromUTF8(result).Lower().ToStdString(wxConvUTF8);
#endif
		return result;
	}

	nlohmann::json FavoriteFile(const std::filesystem::path& path) {
		const auto fingerprint = ResourceFingerprint::Read(path);
		return { { "path", FavoritePath(path) }, { "exists", fingerprint.exists }, { "size", fingerprint.size }, { "modified", fingerprint.modifiedAt.time_since_epoch().count() } };
	}

	std::optional<FavoriteKind> FavoriteBrushKind(const Brush* brush) {
		if (!brush) {
			return std::nullopt;
		}
		if (brush->isRaw()) {
			return FavoriteKind::Item;
		}
		if (brush->isCreature()) {
			const auto* creature = static_cast<const CreatureBrush*>(brush)->getType();
			return creature && creature->isNpc ? FavoriteKind::Npc : FavoriteKind::Creature;
		}
		if (brush->isGround()) {
			return FavoriteKind::Ground;
		}
		if (brush->isWall()) {
			return FavoriteKind::Wall;
		}
		if (brush->isWallDecoration()) {
			return FavoriteKind::WallDecoration;
		}
		if (brush->isDoodad()) {
			return FavoriteKind::Doodad;
		}
		if (brush->isTable()) {
			return FavoriteKind::Table;
		}
		if (brush->isCarpet()) {
			return FavoriteKind::Carpet;
		}
		// House IDs, spawn mixtures and editing tools aren't resource definitions.
		return std::nullopt;
	}

	std::string FavoriteBrushDefinition(Brush* brush) {
		const auto kind = FavoriteBrushKind(brush);
		if (!kind) {
			return {};
		}
		nlohmann::json value = { { "kind", static_cast<int>(*kind) }, { "name", brush->getName() }, { "look", brush->getLookID() } };
		if (const auto* raw = brush->asRaw()) {
			const ItemType* item = raw->getItemType();
			if (!item || !item->id) {
				return {};
			}
			value["item"] = { item->id, item->clientID, item->name, static_cast<int>(item->group), static_cast<int>(item->type) };
		} else if (const auto* creature = brush->asCreature()) {
			const auto* type = creature->getType();
			if (!type || type->missing) {
				return {};
			}
			const Outfit& outfit = type->outfit;
			value["outfit"] = { outfit.lookType, outfit.lookItem, outfit.lookMount, outfit.lookAddon, outfit.lookHead, outfit.lookBody, outfit.lookLegs, outfit.lookFeet, outfit.lookMountHead, outfit.lookMountBody, outfit.lookMountLegs, outfit.lookMountFeet };
		}
		return value.dump();
	}

	std::string FavoriteStampDefinition(const std::string& name) {
		if (!TerrainStampLibrary::IsValidName(name)) {
			return {};
		}
		const auto file = TerrainStampLibrary::GetDirectory() / (TerrainStampLibrary::SanitizeFileStem(name) + ".json");
		if (!ResourceFingerprint::Read(file).exists) {
			return {};
		}
		TerrainStamp stamp;
		std::string error;
		if (!TerrainStampLibrary::Load(name, stamp, error) || stamp.empty() || (stamp.clientVersion != -1 && stamp.clientVersion != g_gui.GetCurrentVersionID())) {
			return {};
		}
		return FavoriteFile(file).dump();
	}
}

std::string FavoriteResources::CaptureContext() {
	try {
		const auto& server = g_workspace.getServer();
		const auto& client = g_workspace.getClient();
		nlohmann::json context = {
			{ "version", 1 }, { "clientRoot", FavoritePath(std::filesystem::u8path(client.rootPath.ToStdString(wxConvUTF8))) }, { "serverRoot", FavoritePath(server.rootPath) }, { "clientVersion", g_gui.GetCurrentVersionID() }, { "appearances", g_gui.IsCanaryCrystalAssetsLoaded() }, { "idMode", static_cast<int>(g_workspace.getEffectiveItemIdMode()) }, { "serverType", static_cast<int>(server.serverType) }, { "itemsOtb", FavoriteFile(server.itemsOtbPath) }, { "itemsXml", FavoriteFile(server.itemsXmlPath) }
		};
		if (g_gui.IsCanaryCrystalAssetsLoaded()) {
			const auto assets = ClientAssetsManifestLoader::Validate(std::filesystem::u8path(ClientAssets::getPath().ToStdString(wxConvUTF8)));
			if (!assets.valid) {
				return {};
			}
			context["assetsRoot"] = FavoritePath(assets.manifest.root);
			context["catalog"] = FavoriteFile(assets.manifest.catalogFile);
			context["appearanceData"] = FavoriteFile(assets.manifest.appearancesFile);
		} else {
			context["metadata"] = FavoriteFile(std::filesystem::u8path(g_gui.gfx.getMetadataFileName().GetFullPath().ToStdString(wxConvUTF8)));
			context["sprites"] = FavoriteFile(std::filesystem::u8path(g_gui.gfx.getSpritesFileName().GetFullPath().ToStdString(wxConvUTF8)));
		}
		return context.dump();
	} catch (const std::exception& error) {
		wxLogWarning("Favorites: could not identify loaded resources: %s", wxString::FromUTF8(error.what()));
		return {};
	}
}

std::string FavoriteResources::ActiveContext() {
	const auto session = GetActiveEditorResourceSession();
	return g_gui.IsVersionLoaded() && session ? session->getFavoritesContext() : std::string();
}

std::optional<FavoriteEntry> FavoriteResources::FromBrush(Brush* brush) {
	const auto kind = FavoriteBrushKind(brush);
	const auto context = ActiveContext();
	if (!kind || context.empty()) {
		return std::nullopt;
	}
	FavoriteEntry entry;
	entry.context = context;
	entry.kind = *kind;
	entry.displayName = brush->getName();
	entry.stableId = entry.displayName;
	entry.definition = FavoriteBrushDefinition(brush);
	entry.category = FavoritesManager::defaultCategory(*kind);
	if (const auto* raw = brush->asRaw()) {
		if (!raw->getItemType()) {
			return std::nullopt;
		}
		entry.itemId = raw->getItemID();
		entry.clientId = raw->getItemType()->clientID;
		entry.stableId = std::to_string(entry.itemId);
	}
	if (brush->hasCollection()) {
		entry.category = FavoriteCategory::Collections;
	}
	for (const auto& [name, tileset] : g_materials.tilesets) {
		if (tileset->containsBrush(brush)) {
			entry.tileset = name;
			break;
		}
	}
	if (!entry.valid() || ResolveBrush(entry) != brush) {
		return std::nullopt;
	}
	return entry;
}

std::optional<FavoriteEntry> FavoriteResources::FromStamp(const std::string& name) {
	FavoriteEntry entry { ActiveContext(), FavoriteKind::TerrainStamp, name, name, FavoriteStampDefinition(name), FavoriteCategory::SavedTerrain };
	return entry.valid() ? std::optional<FavoriteEntry>(entry) : std::nullopt;
}

Brush* FavoriteResources::ResolveBrush(const FavoriteEntry& entry) {
	const std::string context = ActiveContext();
	if (!entry.valid() || context.empty() || context != entry.context) {
		return nullptr;
	}
	Brush* candidate = nullptr;
	if (entry.kind == FavoriteKind::Item) {
		const auto& item = g_items[entry.itemId];
		if (item.id != entry.itemId || item.clientID != entry.clientId) {
			return nullptr;
		}
		candidate = item.raw_brush;
	} else if (entry.kind == FavoriteKind::Creature || entry.kind == FavoriteKind::Npc) {
		CreatureType* type = g_creatures[entry.stableId];
		if (type && !type->missing && type->isNpc == (entry.kind == FavoriteKind::Npc)) {
			candidate = type->brush;
		}
	} else if (entry.kind != FavoriteKind::TerrainStamp) {
		const auto range = g_brushes.getMap().equal_range(entry.stableId);
		for (auto it = range.first; it != range.second; ++it) {
			if (FavoriteBrushKind(it->second) == entry.kind) {
				if (candidate) {
					return nullptr;
				} // Ambiguous names never choose an arbitrary brush.
				candidate = it->second;
			}
		}
	}
	return candidate && entry.matchesDefinition(context, FavoriteBrushDefinition(candidate)) ? candidate : nullptr;
}

bool FavoriteResources::IsAvailable(const FavoriteEntry& entry) {
	if (entry.kind == FavoriteKind::TerrainStamp) {
		return entry.matchesDefinition(ActiveContext(), FavoriteStampDefinition(entry.stableId));
	}
	return ResolveBrush(entry) != nullptr;
}

wxBitmap FavoriteResources::Preview(const FavoriteEntry& entry, int pixels) {
	if (!IsAvailable(entry)) {
		return {};
	}
	GameSprite* sprite = nullptr;
	const Outfit* outfit = nullptr;
	if (Brush* brush = ResolveBrush(entry)) {
		if (auto* creature = brush->asCreature()) {
			outfit = &creature->getType()->outfit;
			sprite = outfit->lookItem ? dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(outfit->lookItem)) : g_gui.gfx.getCreatureSprite(outfit->lookType);
			if (outfit->lookItem) {
				outfit = nullptr;
			}
		} else {
			sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(brush->getLookID()));
		}
	} else if (entry.kind == FavoriteKind::TerrainStamp) {
		TerrainStamp stamp;
		std::string error;
		if (TerrainStampLibrary::Load(entry.stableId, stamp, error)) {
			for (const auto& tile : stamp.tiles) {
				if (!tile.items.empty()) {
					sprite = g_items[tile.items.front().id].sprite;
					break;
				}
			}
		}
	}
	if (!sprite) {
		return {};
	}
	std::vector<uint8_t> rgba;
	int width = 0, height = 0;
	bool pending = false;
	if (!sprite->getVisualPreviewRGBA(rgba, width, height, pending, false, outfit)) {
		return {};
	}
	wxImage image(width, height);
	image.InitAlpha();
	for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
		std::copy_n(rgba.data() + i * 4, 3, image.GetData() + i * 3);
		image.GetAlpha()[i] = rgba[i * 4 + 3];
	}
	const double scale = static_cast<double>(pixels) / std::max(width, height);
	return wxBitmap(image.Scale(std::max(1, static_cast<int>(width * scale)), std::max(1, static_cast<int>(height * scale)), wxIMAGE_QUALITY_NEAREST));
}

wxString FavoriteResources::Tooltip(const FavoriteEntry& entry) {
	wxString text = wxString::FromUTF8(entry.displayName) + "\nCategory: " + FavoritesManager::categoryName(entry.category);
	if (entry.kind != FavoriteKind::Item && entry.kind != FavoriteKind::TerrainStamp) {
		text += "\nBrush: " + wxString::FromUTF8(entry.stableId);
	}
	if (entry.clientId) {
		text += wxString::Format("\nClient ID: %u", entry.clientId);
	}
	if (entry.itemId && g_workspace.getEffectiveItemIdMode() == ItemIdMode::ServerId) {
		text += wxString::Format("\nServer ID: %u", entry.itemId);
	}
	if (!entry.tileset.empty()) {
		text += "\nTileset: " + wxString::FromUTF8(entry.tileset);
	}
	if (!IsAvailable(entry)) {
		text += "\nUnavailable: resource is missing or has changed.";
	}
	return text;
}

void FavoriteResources::AppendMenu(wxMenu& menu, wxWindow* parent, const FavoriteEntry& entry, const wxString& label) {
	const bool remove = g_gui.GetFavorites().contains(entry);
	wxString title = remove ? "Remove from Favorites" : "Add to Favorites";
	if (!label.empty()) {
		title = (remove ? "Remove: " : "Add: ") + label;
	}
	title.Replace("&", "&&");
	const int id = wxWindow::NewControlId();
	menu.Append(id, title);
	wxWeakRef<wxWindow> owner(parent);
	menu.Bind(
		wxEVT_MENU, [owner, entry, remove](wxCommandEvent&) {
			if (!owner || ActiveContext() != entry.context || (!remove && !IsAvailable(entry))) {
				return;
			}
			std::string error;
			const bool changed = remove ? g_gui.GetFavorites().remove(entry, error) : g_gui.GetFavorites().add(entry, error);
			if (!changed) {
				wxMessageBox(wxString::FromUTF8(error), "Favorites", wxOK | wxICON_ERROR, owner.get());
				return;
			}
			g_gui.RefreshFavorites();
		},
		id
	);
}

bool FavoriteResources::IsCurrentPalette(const wxWindow* window) {
	const auto* palette = GetParentPalette(window);
	return palette && palette->HasCurrentResources();
}

void FavoriteResources::Popup(wxWindow* parent, Brush* brush) {
	// Old palette controls may still be awaiting wxWindow::Destroy after a tab switch.
	if (!IsCurrentPalette(parent)) {
		return;
	}
	if (const auto entry = FromBrush(brush)) {
		wxMenu menu;
		AppendMenu(menu, parent, *entry);
		parent->PopupMenu(&menu);
	}
}
