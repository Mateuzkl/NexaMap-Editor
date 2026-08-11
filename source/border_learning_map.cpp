#include "main.h"

#include "border_learning.h"
#include "basemap.h"
#include "ground_brush.h"
#include "items.h"
#include "selection.h"
#include "tile.h"

#include <algorithm>
#include <array>
#include <map>

namespace {

	constexpr uint64_t unknownGroundKeyFlag = uint64_t { 1 } << 63;
	const std::array<Position, 8> snapshotNeighbourOffsets = {
		Position(-1, -1, 0),
		Position(0, -1, 0),
		Position(1, -1, 0),
		Position(-1, 0, 0),
		Position(1, 0, 0),
		Position(-1, 1, 0),
		Position(0, 1, 0),
		Position(1, 1, 0),
	};

	BorderGroundFamilyIndex captureGroundFamily(
		const Item* ground,
		std::vector<BorderLearningGroundFamily>& families,
		std::map<uint64_t, BorderGroundFamilyIndex>& familyIndices
	) {
		if (!ground) {
			return BORDER_GROUND_FAMILY_NONE;
		}

		const uint16_t groundItemId = ground->getID();
		GroundBrush* brush = ground->getGroundBrush();
		const uint64_t key = brush ? static_cast<uint64_t>(brush->getID()) : unknownGroundKeyFlag | groundItemId;
		auto found = familyIndices.find(key);
		if (found == familyIndices.end()) {
			BorderLearningGroundFamily family;
			family.key = key;
			family.brushId = brush ? brush->getID() : 0;
			family.representativeItemId = groundItemId;
			family.knownBrush = brush != nullptr;
			family.name = brush ? brush->getName() : "Unconfigured ground " + std::to_string(groundItemId);
			families.push_back(std::move(family));
			const auto index = static_cast<BorderGroundFamilyIndex>(families.size() - 1);
			found = familyIndices.emplace(key, index).first;
		}

		auto& itemIds = families[found->second].itemIds;
		if (std::find(itemIds.begin(), itemIds.end(), groundItemId) == itemIds.end()) {
			itemIds.push_back(groundItemId);
		}
		return found->second;
	}

} // namespace

BorderLearningSnapshot BorderLearningScanner::capture(const Selection& selection, const BaseMap& map, int floor) {
	BorderLearningSnapshot snapshot;
	snapshot.floor = floor;

	std::vector<const Tile*> selectedTiles;
	selectedTiles.reserve(selection.size());
	for (const Tile* tile : selection) {
		if (tile->getZ() == floor) {
			selectedTiles.push_back(tile);
		} else {
			++snapshot.ignoredOtherFloorTiles;
		}
	}
	std::sort(selectedTiles.begin(), selectedTiles.end(), [](const Tile* lhs, const Tile* rhs) {
		return lhs->getPosition() < rhs->getPosition();
	});
	snapshot.selectedTileCount = selectedTiles.size();
	snapshot.tiles.reserve(selectedTiles.size());

	std::map<uint64_t, BorderGroundFamilyIndex> familyIndices;
	for (const Tile* tile : selectedTiles) {
		BorderLearningTile captured;
		captured.position = tile->getPosition();
		captured.groundItemId = tile->ground ? tile->ground->getID() : 0;
		captured.groundFamily = captureGroundFamily(tile->ground, snapshot.groundFamilies, familyIndices);

		captured.items.reserve(tile->items.size());
		for (size_t stackIndex = 0; stackIndex < tile->items.size(); ++stackIndex) {
			const Item* item = tile->items[stackIndex];
			const ItemType& itemType = g_items[item->getID()];
			captured.items.push_back({
				item->getID(),
				itemType.clientID,
				static_cast<uint16_t>(std::min<size_t>(stackIndex, std::numeric_limits<uint16_t>::max())),
				itemType.isBorder,
				itemType.alwaysOnBottom,
				itemType.isWall,
				itemType.doodad_brush != nullptr,
				itemType.isMetaItem() || itemType.isTeleport() || itemType.isDoor(),
				itemType.isOptionalBorder,
				itemType.border_alignment,
				itemType.border_group,
			});
		}

		for (size_t neighbourIndex = 0; neighbourIndex < snapshotNeighbourOffsets.size(); ++neighbourIndex) {
			const Position neighbourPosition = captured.position + snapshotNeighbourOffsets[neighbourIndex];
			const Tile* neighbour = neighbourPosition.isValid() ? map.getTile(neighbourPosition) : nullptr;
			captured.neighbourFamilies[neighbourIndex] = captureGroundFamily(neighbour ? neighbour->ground : nullptr, snapshot.groundFamilies, familyIndices);
		}
		snapshot.tiles.push_back(std::move(captured));
	}

	for (auto& family : snapshot.groundFamilies) {
		std::sort(family.itemIds.begin(), family.itemIds.end());
	}
	return snapshot;
}
