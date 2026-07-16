#include "main.h"

#include "spawn_format.h"

#include "creature.h"
#include "map.h"
#include "tile.h"

#include <unordered_set>

bool SpawnMapAdapter::Apply(Map& map, const SpawnDocument& document, std::vector<std::string>& warnings) {
	for (const SpawnAreaData& area : document.areas) {
		const Position center(area.centerX, area.centerY, area.centerZ);
		Tile* centerTile = map.getTile(center);
		if (!centerTile) {
			centerTile = map.allocator(map.createTileL(center));
			map.setTile(center, centerTile);
		}
		if (!centerTile->spawn) {
			centerTile->spawn = newd Spawn(std::max(1, area.radius));
			map.addSpawn(centerTile);
		} else {
			centerTile->spawn->setSize(std::max(centerTile->spawn->getSize(), std::max(1, area.radius)));
		}
		centerTile->spawn->setSourceAttributes(area.kind, area.attributes);

		for (const SpawnEntryData& entry : area.entries) {
			const Position position(entry.x, entry.y, entry.z);
			Tile* creatureTile = map.getTile(position);
			if (!creatureTile) {
				creatureTile = map.allocator(map.createTileL(position));
				map.setTile(position, creatureTile);
			}

			std::vector<SpawnVariantData> variants = entry.alternatives;
			if (variants.empty()) {
				SpawnVariantData variant;
				variant.name = entry.name;
				variant.isNpc = entry.isNpc;
				variant.weight = entry.weight;
				variant.hasWeight = entry.hasWeight;
				variant.attributes = entry.attributes;
				variants.push_back(std::move(variant));
			}

			if (!creatureTile->creature) {
				const SpawnVariantData& primary = variants.front();
				CreatureType* type = g_creatures[primary.name];
				if (!type) {
					type = g_creatures.addMissingCreatureType(primary.name, primary.isNpc);
				}
				creatureTile->creature = newd Creature(type);
				creatureTile->creature->setSpawnType(primary.isNpc);
				creatureTile->creature->setSpawnDirection(
					static_cast<Direction>(std::clamp(entry.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST))),
					entry.hasDirection
				);
				creatureTile->creature->setSpawnTime(entry.spawnTime);
				creatureTile->creature->setSpawnWeight(primary.weight, primary.hasWeight);
				creatureTile->creature->setSpawnAttributes(primary.attributes);
				creatureTile->creature->setSpawnSource(center);
				creatureTile->creature->setAlternativeKind(entry.alternativeKind);
				for (size_t index = 1; index < variants.size(); ++index) {
					creatureTile->creature->addSpawnAlternative(variants[index]);
				}
			} else {
				Creature* creature = creatureTile->creature;
				const Direction direction = static_cast<Direction>(std::clamp(entry.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST)));
				if (creature->getSpawnTime() != entry.spawnTime || creature->getDirection() != direction || creature->hasSpawnDirection() != entry.hasDirection) {
					warnings.push_back("Rejected collocated spawn entries with incompatible respawn or direction at " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z) + ".");
					return false;
				}
				if (creature->getAlternativeKind() == SpawnAlternativeKind::None) {
					creature->setAlternativeKind(SpawnAlternativeKind::CanaryWeight);
				}
				for (const SpawnVariantData& variant : variants) {
					creature->addSpawnAlternative(variant);
				}
				warnings.push_back("Preserved weighted alternatives sharing tile " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z) + ".");
			}
		}
	}
	return true;
}

SpawnDocument SpawnMapAdapter::Capture(Map& map) {
	SpawnDocument document;
	document.format = map.getSpawnFormat();
	std::unordered_set<Creature*> captured;
	for (const Position& center : map.spawns) {
		Tile* centerTile = map.getTile(center);
		if (!centerTile || !centerTile->spawn) {
			continue;
		}
		SpawnAreaData area;
		area.centerX = center.x;
		area.centerY = center.y;
		area.centerZ = center.z;
		area.radius = centerTile->spawn->getSize();
		area.kind = SpawnAreaKind::Mixed;
		area.attributes = centerTile->spawn->getSourceAttributes(SpawnAreaKind::Mixed);

		for (int y = -area.radius; y <= area.radius; ++y) {
			for (int x = -area.radius; x <= area.radius; ++x) {
				Tile* tile = map.getTile(center + Position(x, y, 0));
				if (!tile || !tile->creature || captured.contains(tile->creature)) {
					continue;
				}
				Creature* creature = tile->creature;
				if (creature->hasSpawnSource() && creature->getSpawnSource() != center) {
					continue;
				}
				captured.insert(creature);
				SpawnEntryData entry;
				entry.name = creature->getName();
				entry.isNpc = creature->isNpc();
				entry.x = tile->getX();
				entry.y = tile->getY();
				entry.z = tile->getZ();
				entry.spawnTime = creature->getSpawnTime();
				entry.direction = creature->getDirection();
				entry.hasDirection = creature->hasSpawnDirection();
				entry.weight = creature->getWeight();
				entry.hasWeight = creature->hasSpawnWeight();
				entry.attributes = creature->getSpawnAttributes();
				entry.alternativeKind = creature->getAlternativeKind();
				if (!creature->getSpawnAlternatives().empty()) {
					SpawnVariantData primary;
					primary.name = entry.name;
					primary.isNpc = entry.isNpc;
					primary.weight = entry.weight;
					primary.hasWeight = entry.hasWeight;
					primary.attributes = entry.attributes;
					entry.alternatives.push_back(std::move(primary));
					entry.alternatives.insert(entry.alternatives.end(), creature->getSpawnAlternatives().begin(), creature->getSpawnAlternatives().end());
				}
				area.entries.push_back(std::move(entry));
			}
		}
		if (document.format != SpawnFormat::CanaryCrystal) {
			document.areas.push_back(std::move(area));
			continue;
		}

		SpawnAreaData monsterArea = area;
		SpawnAreaData npcArea = area;
		monsterArea.kind = SpawnAreaKind::Monsters;
		npcArea.kind = SpawnAreaKind::Npcs;
		monsterArea.attributes = centerTile->spawn->getSourceAttributes(SpawnAreaKind::Monsters);
		npcArea.attributes = centerTile->spawn->getSourceAttributes(SpawnAreaKind::Npcs);
		monsterArea.entries.clear();
		npcArea.entries.clear();
		for (const SpawnEntryData& entry : area.entries) {
			if (entry.alternatives.empty()) {
				(entry.isNpc ? npcArea.entries : monsterArea.entries).push_back(entry);
				continue;
			}
			SpawnEntryData monsters = entry;
			SpawnEntryData npcs = entry;
			monsters.alternatives.clear();
			npcs.alternatives.clear();
			for (const SpawnVariantData& variant : entry.alternatives) {
				(variant.isNpc ? npcs.alternatives : monsters.alternatives).push_back(variant);
			}
			if (!monsters.alternatives.empty()) {
				monsters.name = monsters.alternatives.front().name;
				monsters.isNpc = false;
				monsterArea.entries.push_back(std::move(monsters));
			}
			if (!npcs.alternatives.empty()) {
				npcs.name = npcs.alternatives.front().name;
				npcs.isNpc = true;
				npcArea.entries.push_back(std::move(npcs));
			}
		}
		bool emittedArea = false;
		if (!monsterArea.entries.empty() || centerTile->spawn->hasSourceKind(SpawnAreaKind::Monsters)) {
			document.areas.push_back(std::move(monsterArea));
			emittedArea = true;
		}
		if (!npcArea.entries.empty() || centerTile->spawn->hasSourceKind(SpawnAreaKind::Npcs)) {
			document.areas.push_back(std::move(npcArea));
			emittedArea = true;
		}
		if (!emittedArea && area.entries.empty()) {
			area.kind = SpawnAreaKind::Monsters;
			document.areas.push_back(std::move(area));
		}
	}
	return document;
}
