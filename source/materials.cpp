//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <wx/dir.h>

#include "items.h"
#include "creatures.h"

#include "gui.h"
#include "materials.h"
#include "brush.h"
#include "creature_brush.h"
#include "raw_brush.h"
#include "client_assets.h"
#include "item_id_mapping.h"

#include <limits>

namespace {
	struct MaterialIdTranslationStats {
		uint64_t translated = 0;
		uint64_t missing = 0;
	};

	void TranslateItemAttribute(pugi::xml_node node, const char* attributeName, MaterialIdTranslationStats& stats) {
		pugi::xml_attribute attribute = node.attribute(attributeName);
		if (!attribute) {
			return;
		}
		const uint32_t raw = attribute.as_uint();
		if (raw == 0 || raw > std::numeric_limits<uint16_t>::max()) {
			return;
		}
		const ItemIdMapping::Result mapping = ItemIdMapping::serverToClient(static_cast<uint16_t>(raw));
		if (!mapping.found) {
			++stats.missing;
			return;
		}
		attribute.set_value(mapping.converted);
		stats.translated += mapping.converted != raw ? 1 : 0;
	}

	void TranslateMaterialNode(pugi::xml_node node, MaterialIdTranslationStats& stats) {
		const std::string nodeName = as_lower_str(node.name());
		if (nodeName != "metaitem") {
			if (nodeName == "item" || nodeName == "door" || nodeName == "match_item" || nodeName == "replace_item") {
				TranslateItemAttribute(node, "id", stats);
			}
			if (nodeName == "replace_item" || nodeName == "replace_border") {
				TranslateItemAttribute(node, "with", stats);
			}
		}
		TranslateItemAttribute(node, "item", stats);
		TranslateItemAttribute(node, "server_lookid", stats);
		TranslateItemAttribute(node, "afteritem", stats);
		TranslateItemAttribute(node, "ground_equivalent", stats);

		for (pugi::xml_node child = node.first_child(); child;) {
			pugi::xml_node next = child.next_sibling();
			if (as_lower_str(child.name()) == "item" && child.attribute("fromid")) {
				const uint32_t fromId = child.attribute("fromid").as_uint();
				const uint32_t toId = std::max(fromId, child.attribute("toid").as_uint(fromId));
				for (uint32_t sourceId = fromId; sourceId <= toId && sourceId <= std::numeric_limits<uint16_t>::max(); ++sourceId) {
					pugi::xml_node expanded = node.insert_copy_before(child, child);
					expanded.remove_attribute("fromid");
					expanded.remove_attribute("toid");
					pugi::xml_attribute id = expanded.attribute("id");
					if (!id) {
						id = expanded.append_attribute("id");
					}
					id.set_value(sourceId);
					TranslateMaterialNode(expanded, stats);
				}
				node.remove_child(child);
			} else {
				TranslateMaterialNode(child, stats);
			}
			child = next;
		}
	}
}

Materials g_materials;

Materials::Materials() {
	////
}

Materials::~Materials() {
	clear();
}

void Materials::clear() {
	for (auto iter = tilesets.begin(); iter != tilesets.end(); ++iter) {
		delete iter->second;
	}

	for (auto iter = extensions.begin(); iter != extensions.end(); ++iter) {
		delete *iter;
	}

	tilesets.clear();
	extensions.clear();
}

const MaterialsExtensionList& Materials::getExtensions() {
	return extensions;
}

bool Materials::loadMaterials(const FileName& identifier, wxString& error, wxArrayString& warnings, bool serverIdsToClientIds) {
	std::set<wxString> visited;
	return loadMaterialsInternal(identifier, error, warnings, serverIdsToClientIds, visited);
}

bool Materials::loadMaterialsInternal(const FileName& identifier, wxString& error, wxArrayString& warnings, bool serverIdsToClientIds, std::set<wxString>& visited) {
	FileName normalizedIdentifier(identifier);
	normalizedIdentifier.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
	const wxString normalizedPath = normalizedIdentifier.GetFullPath();
	if (visited.count(normalizedPath) != 0) {
		warnings.push_back("Skipped repeated or cyclic materials include: " + normalizedPath);
		return true;
	}
	visited.insert(normalizedPath);

	if (ClientAssets::isLoaded()) {
		wxLogMessage("Canary/Crystal materials: loading " + normalizedPath);
	}
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(normalizedPath.mb_str());
	if (!result) {
		warnings.push_back("Could not open " + normalizedIdentifier.GetFullName() + " (file not found or syntax error)");
		return false;
	}

	pugi::xml_node node = doc.child("materials");
	if (!node) {
		warnings.push_back(normalizedIdentifier.GetFullName() + ": Invalid rootheader.");
		return false;
	}

	if (serverIdsToClientIds) {
		MaterialIdTranslationStats translation;
		TranslateMaterialNode(node, translation);
		if (translation.missing > 0) {
			warnings.push_back(wxString::Format(
				"%s: %llu material item reference(s) have no Server ID to ClientID mapping and were left unchanged.",
				normalizedIdentifier.GetFullName(),
				static_cast<unsigned long long>(translation.missing)
			));
		}
	}
	unserializeMaterials(normalizedIdentifier, node, error, warnings, serverIdsToClientIds, visited);
	return true;
}

bool Materials::loadExtensions(const FileName& directoryName, wxString& error, wxArrayString& warnings) {
	directoryName.Mkdir(0755, wxPATH_MKDIR_FULL); // Create if it doesn't exist

	wxDir ext_dir(directoryName.GetPath());
	if (!ext_dir.IsOpened()) {
		error = "Could not open extensions directory.";
		return false;
	}

	wxString filename;
	if (!ext_dir.GetFirst(&filename)) {
		// No extensions found
		return true;
	}

	StringVector clientVersions;
	do {
		FileName fn;
		fn.SetPath(directoryName.GetPath());
		fn.SetFullName(filename);
		if (fn.GetExt() != "xml") {
			continue;
		}

		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file(fn.GetFullPath().mb_str());
		if (!result) {
			warnings.push_back("Could not open " + filename + " (file not found or syntax error)");
			continue;
		}

		pugi::xml_node extensionNode = doc.child("materialsextension");
		if (!extensionNode) {
			warnings.push_back(filename + ": Invalid rootheader.");
			continue;
		}

		pugi::xml_attribute attribute;
		if (!(attribute = extensionNode.attribute("name"))) {
			warnings.push_back(filename + ": Couldn't read extension name.");
			continue;
		}

		const std::string& extensionName = attribute.as_string();
		if (!(attribute = extensionNode.attribute("author"))) {
			warnings.push_back(filename + ": Couldn't read extension name.");
			continue;
		}

		const std::string& extensionAuthor = attribute.as_string();
		if (!(attribute = extensionNode.attribute("description"))) {
			warnings.push_back(filename + ": Couldn't read extension name.");
			continue;
		}

		const std::string& extensionDescription = attribute.as_string();
		if (extensionName.empty() || extensionAuthor.empty() || extensionDescription.empty()) {
			warnings.push_back(filename + ": Couldn't read extension attributes (name, author, description).");
			continue;
		}

		std::string extensionUrl = extensionNode.attribute("url").as_string();
		extensionUrl.erase(std::remove(extensionUrl.begin(), extensionUrl.end(), '\''), extensionUrl.end());

		std::string extensionAuthorLink = extensionNode.attribute("authorurl").as_string();
		extensionAuthorLink.erase(std::remove(extensionAuthorLink.begin(), extensionAuthorLink.end(), '\''), extensionAuthorLink.end());

		auto* materialExtension = newd MaterialsExtension(extensionName, extensionAuthor, extensionDescription);
		materialExtension->url = extensionUrl;
		materialExtension->author_url = extensionAuthorLink;

		if ((attribute = extensionNode.attribute("client"))) {
			clientVersions.clear();
			const std::string& extensionClientString = attribute.as_string();

			size_t lastPosition = 0;
			size_t position = extensionClientString.find(';');
			while (position != std::string::npos) {
				clientVersions.push_back(extensionClientString.substr(lastPosition, position - lastPosition));
				lastPosition = position + 1;
				position = extensionClientString.find(';', lastPosition);
			}

			clientVersions.push_back(extensionClientString.substr(lastPosition));
			for (const std::string& version : clientVersions) {
				materialExtension->addVersion(version);
			}

			std::sort(materialExtension->version_list.begin(), materialExtension->version_list.end(), VersionComparisonPredicate);

			auto duplicate = std::unique(materialExtension->version_list.begin(), materialExtension->version_list.end());
			while (duplicate != materialExtension->version_list.end()) {
				materialExtension->version_list.erase(duplicate);
				duplicate = std::unique(materialExtension->version_list.begin(), materialExtension->version_list.end());
			}
		} else {
			warnings.push_back(filename + ": Extension is not available for any version.");
		}

		extensions.push_back(materialExtension);
		if (materialExtension->isForVersion(g_gui.GetCurrentVersionID())) {
			if (ClientAssets::isLoaded()) {
				MaterialIdTranslationStats translation;
				TranslateMaterialNode(extensionNode, translation);
			}
			std::set<wxString> visited;
			unserializeMaterials(fn, extensionNode, error, warnings, false, visited);
		}
	} while (ext_dir.GetNext(&filename));

	return true;
}

bool Materials::unserializeMaterials(const FileName& filename, pugi::xml_node node, wxString& error, wxArrayString& warnings, bool serverIdsToClientIds, std::set<wxString>& visited) {
	wxString warning;
	pugi::xml_attribute attribute;
	for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string& childName = as_lower_str(childNode.name());
		if (childName == "include") {
			if (!(attribute = childNode.attribute("file"))) {
				continue;
			}

			const wxString includeFile(attribute.as_string(), wxConvUTF8);
			if (includeFile.empty()) {
				warnings.push_back(filename.GetFullName() + ": Ignored an empty materials include.");
				continue;
			}
			FileName includeName(filename.GetPath(), includeFile);

			wxString subError;
			if (!loadMaterialsInternal(includeName, subError, warnings, serverIdsToClientIds, visited)) {
				warnings.push_back("Error while loading file \"" + includeName.GetFullName() + "\": " + subError);
			}
		} else if (childName == "metaitem") {
			g_items.loadMetaItem(childNode);
		} else if (childName == "border") {
			g_brushes.unserializeBorder(childNode, warnings);
			if (warning.size()) {
				warnings.push_back("materials.xml: " + warning);
			}
		} else if (childName == "brush") {
			g_brushes.unserializeBrush(childNode, warnings);
			if (warning.size()) {
				warnings.push_back("materials.xml: " + warning);
			}
		} else if (childName == "tileset") {
			unserializeTileset(childNode, warnings);
		}
	}
	return true;
}

void Materials::createOtherTileset() {
	// Skip the expensive full item-DB + creature rescan when nothing changed since the
	// last build. This runs on every palette refresh (CreaturePalettePanel::OnUpdate),
	// so rebuilding unconditionally re-scans every item id and every creature each time.
	const size_t cur_creature_count = g_creatures.size();
	const int32_t cur_item_maxid = g_items.getMaxID();
	if (tilesets.count("Others") && tilesets.count("NPCs")
		&& cur_creature_count == other_tileset_creature_count
		&& cur_item_maxid == other_tileset_item_maxid) {
		return;
	}

	Tileset* others;
	Tileset* npc_tileset;

	if (tilesets["Others"] != nullptr) {
		others = tilesets["Others"];
		others->clear();
	} else {
		others = newd Tileset(g_brushes, "Others");
		tilesets["Others"] = others;
	}

	if (tilesets["NPCs"] != nullptr) {
		npc_tileset = tilesets["NPCs"];
		npc_tileset->clear();
	} else {
		npc_tileset = newd Tileset(g_brushes, "NPCs");
		tilesets["NPCs"] = npc_tileset;
	}

	// There should really be an iterator to do this
	for (int32_t id = 0; id <= g_items.getMaxID(); ++id) {
		ItemType& it = g_items[id];
		if (it.id == 0) {
			continue;
		}

		if (!it.isMetaItem()) {
			Brush* brush;
			if (it.in_other_tileset) {
				others->getCategory(TILESET_RAW)->brushlist.push_back(it.raw_brush);
				continue;
			} else if (it.raw_brush == nullptr) {
				brush = it.raw_brush = newd RAWBrush(it.id);
				it.has_raw = true;
				g_brushes.addBrush(it.raw_brush);
			} else if (!it.has_raw) {
				brush = it.raw_brush;
			} else {
				continue;
			}

			brush->flagAsVisible();
			others->getCategory(TILESET_RAW)->brushlist.push_back(it.raw_brush);
			it.in_other_tileset = true;
		}
	}

	for (auto iter = g_creatures.begin(); iter != g_creatures.end(); ++iter) {
		CreatureType* type = iter->second;
		if (type->in_other_tileset) {
			if (type->isNpc) {
				npc_tileset->getCategory(TILESET_CREATURE)->brushlist.push_back(type->brush);
			} else {
				others->getCategory(TILESET_CREATURE)->brushlist.push_back(type->brush);
			}
		} else if (type->brush == nullptr) {
			type->brush = newd CreatureBrush(type);
			g_brushes.addBrush(type->brush);
			type->brush->flagAsVisible();
			type->in_other_tileset = true;
			if (type->isNpc) {
				npc_tileset->getCategory(TILESET_CREATURE)->brushlist.push_back(type->brush);
			} else {
				others->getCategory(TILESET_CREATURE)->brushlist.push_back(type->brush);
			}
		}
	}

	other_tileset_creature_count = cur_creature_count;
	other_tileset_item_maxid = cur_item_maxid;
}

bool Materials::unserializeTileset(pugi::xml_node node, wxArrayString& warnings) {
	pugi::xml_attribute attribute;
	if (!(attribute = node.attribute("name"))) {
		warnings.push_back("Couldn't read tileset name");
		return false;
	}

	const std::string& name = attribute.as_string();

	Tileset* tileset;
	auto it = tilesets.find(name);
	if (it != tilesets.end()) {
		tileset = it->second;
	} else {
		tileset = newd Tileset(g_brushes, name);
		tilesets.insert(std::make_pair(name, tileset));
	}

	for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		tileset->loadCategory(childNode, warnings);
	}
	return true;
}

void Materials::addToTileset(const std::string& tilesetName, int itemId, TilesetCategoryType categoryType) {
	ItemType& it = g_items[itemId];

	if (it.id == 0) {
		return;
	}

	Tileset* tileset;
	auto _it = tilesets.find(tilesetName);
	if (_it != tilesets.end()) {
		tileset = _it->second;
	} else {
		tileset = newd Tileset(g_brushes, tilesetName);
		tilesets.insert(std::make_pair(tilesetName, tileset));
	}

	TilesetCategory* category = tileset->getCategory(categoryType);

	if (!it.isMetaItem()) {
		Brush* brush;
		if (it.in_other_tileset) {
			category->brushlist.push_back(it.raw_brush);
			return;
		} else if (it.raw_brush == nullptr) {
			brush = it.raw_brush = newd RAWBrush(it.id);
			it.has_raw = true;
			g_brushes.addBrush(it.raw_brush);
		} else {
			brush = it.raw_brush;
		}

		brush->flagAsVisible();
		category->brushlist.push_back(it.raw_brush);
		it.in_other_tileset = true;
	}
}

bool Materials::isInTileset(Item* item, const std::string& tilesetName) const {
	const ItemType& it = g_items[item->getID()];

	return it.id != 0 && (isInTileset(it.brush, tilesetName) || isInTileset(it.doodad_brush, tilesetName) || isInTileset(it.raw_brush, tilesetName)) || isInTileset(it.collection_brush, tilesetName);
}

bool Materials::isInTileset(Brush* brush, const std::string& tilesetName) const {
	if (!brush) {
		return false;
	}

	auto tilesetiter = tilesets.find(tilesetName);
	if (tilesetiter == tilesets.end()) {
		return false;
	}
	Tileset* tileset = tilesetiter->second;

	return tileset->containsBrush(brush);
}
