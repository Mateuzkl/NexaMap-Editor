#include "main.h"

#include "spawn_format.h"

#include "creature.h"
#include "map.h"
#include "tile.h"

#include <map>
#include <unordered_set>

namespace {
	constexpr size_t MAX_REPORTED_WARNINGS = 200;

	void AppendWarning(std::vector<std::string>& warnings, std::string warning) {
		if (warnings.size() < MAX_REPORTED_WARNINGS) {
			warnings.push_back(std::move(warning));
		} else if (warnings.size() == MAX_REPORTED_WARNINGS) {
			warnings.emplace_back("Additional warnings were omitted to keep the report responsive.");
		}
	}

	struct SpawnSignature {
		int spawnTime;
		Direction direction;
		bool hasDirection;
		SpawnAlternativeKind alternativeKind;
	};

	bool ValidateBeforeApply(const Map& map, const SpawnDocument& document, std::vector<std::string>& warnings) {
		std::map<Position, SpawnSignature> signatures;
		for (const SpawnAreaData& area : document.areas) {
			if (area.centerX < 0 || area.centerX > 65535 || area.centerY < 0 || area.centerY > 65535 || area.centerZ < 0 || area.centerZ > 255 || area.radius < 0 || area.radius > 255) {
				warnings.push_back("Spawn document contains an invalid area and was not applied.");
				return false;
			}
			for (const SpawnEntryData& entry : area.entries) {
				if (entry.name.empty() || entry.x < 0 || entry.x > 65535 || entry.y < 0 || entry.y > 65535 || entry.z != area.centerZ || entry.spawnTime < 1) {
					warnings.push_back("Spawn document contains an invalid creature entry and was not applied.");
					return false;
				}
				for (const SpawnVariantData& variant : entry.alternatives) {
					if (variant.name.empty()) {
						warnings.push_back("Spawn document contains an unnamed creature alternative and was not applied.");
						return false;
					}
				}

				const Position position(entry.x, entry.y, entry.z);
				const Direction direction = static_cast<Direction>(std::clamp(entry.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST)));
				const SpawnSignature signature { entry.spawnTime, direction, entry.hasDirection, entry.alternativeKind };
				auto [iterator, inserted] = signatures.emplace(position, signature);
				if (!inserted) {
					SpawnAlternativeKind mergedKind = SpawnAlternativeKind::None;
					if (!MergeSpawnAlternativeKinds(iterator->second.alternativeKind, signature.alternativeKind, mergedKind)) {
						warnings.push_back("Spawn entries at " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z) + " use incompatible alternative kinds; no spawn data was applied.");
						return false;
					}
					iterator->second.alternativeKind = mergedKind;
					if (iterator->second.spawnTime != signature.spawnTime || iterator->second.direction != signature.direction || iterator->second.hasDirection != signature.hasDirection) {
						warnings.push_back("Spawn entries at " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z) + " disagree on respawn or direction; no spawn data was applied.");
						return false;
					}
				}

				const Tile* tile = map.getTile(position);
				if (tile && tile->creature) {
					SpawnAlternativeKind mergedKind = SpawnAlternativeKind::None;
					if (!MergeSpawnAlternativeKinds(tile->creature->getAlternativeKind(), iterator->second.alternativeKind, mergedKind) || tile->creature->getSpawnTime() != signature.spawnTime || tile->creature->getDirection() != signature.direction || tile->creature->hasSpawnDirection() != signature.hasDirection) {
						warnings.push_back("Existing creature at " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z) + " conflicts with the spawn document; no spawn data was applied.");
						return false;
					}
				}
			}
		}
		return true;
	}

}

bool SpawnMapAdapter::Apply(Map& map, const SpawnDocument& document, std::vector<std::string>& warnings) {
	if (!ValidateBeforeApply(map, document, warnings)) {
		return false;
	}

	for (const SpawnAreaData& area : document.areas) {
		const Position center(area.centerX, area.centerY, area.centerZ);
		Tile* centerTile = map.getTile(center);
		if (!centerTile) {
			centerTile = map.allocator(map.createTileL(center));
			map.setTile(center, centerTile);
		}
		if (!centerTile->spawn) {
			centerTile->spawn = newd Spawn(area.radius);
			map.addSpawn(centerTile);
		} else {
			const int expandedRadius = std::max(centerTile->spawn->getSize(), area.radius);
			if (expandedRadius != centerTile->spawn->getSize()) {
				map.removeSpawn(centerTile);
				centerTile->spawn->setSize(expandedRadius);
				map.addSpawn(centerTile);
			}
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
				SpawnAlternativeKind mergedKind = SpawnAlternativeKind::None;
				MergeSpawnAlternativeKinds(creature->getAlternativeKind(), entry.alternativeKind, mergedKind);
				creature->setAlternativeKind(mergedKind);
				for (const SpawnVariantData& variant : variants) {
					creature->addSpawnAlternative(variant);
				}
				AppendWarning(warnings, "Preserved weighted alternatives sharing tile " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z) + ".");
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
		area.radius = document.format == SpawnFormat::CanaryCrystal ? centerTile->spawn->getSize() : std::max(1, centerTile->spawn->getSize());
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
