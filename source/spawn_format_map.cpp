#include "main.h"

#include "spawn_format.h"

#include "creature.h"
#include "map.h"
#include "tile.h"

#include <map>
#include <unordered_set>

namespace {
	struct PositionEntryInfo {
		std::string name;
		Position center;
		int spawnTime;
		Direction direction;
		bool hasDirection;
		uint32_t weight;
		bool hasWeight;
		bool isNpc;
		SpawnAlternativeKind alternativeKind;
	};

	std::string FormatEntryDetails(const std::string& name, int spawnTime, Direction direction, bool hasDirection, uint32_t weight, bool hasWeight) {
		std::string s = "'" + name + "' (spawntime=" + std::to_string(spawnTime) + ", direction=" + std::to_string(static_cast<int>(direction)) + ", explicitDirection=" + (hasDirection ? "true" : "false");
		if (hasWeight) {
			s += ", weight=" + std::to_string(weight);
		}
		s += ")";
		return s;
	}

	std::string FormatConflictWarning(const Position& pos, const Position& center, const std::string& existingDesc, const std::string& incomingDesc, const std::string& reason) {
		return "Spawn conflict at " + std::to_string(pos.x) + ":" + std::to_string(pos.y) + ":" + std::to_string(pos.z) + " (center " + std::to_string(center.x) + ":" + std::to_string(center.y) + ":" + std::to_string(center.z) + "): existing " + existingDesc + ", incoming " + incomingDesc + "; reason: " + reason + "; no spawn data was applied.";
	}

	SpawnVariantData CapturePrimaryRecord(const Creature& creature) {
		SpawnVariantData primary;
		primary.name = creature.getName();
		primary.isNpc = creature.isNpc();
		primary.weight = creature.getWeight();
		primary.hasWeight = creature.hasSpawnWeight();
		primary.spawnTime = creature.getSpawnTime();
		primary.direction = creature.getDirection();
		primary.hasDirection = creature.hasSpawnDirection();
		primary.attributes = creature.getSpawnAttributes();
		return primary;
	}

	bool HasValidSpawnIdentity(const SpawnEntryData& entry) {
		if (!entry.name.empty()) {
			return true;
		}
		if (entry.alternatives.empty()) {
			return false;
		}
		for (const SpawnVariantData& variant : entry.alternatives) {
			if (variant.name.empty()) {
				return false;
			}
		}
		return true;
	}

	void ValidateAgainstExistingCreature(const Tile* tile, const Position& pos, const Position& center, const std::string& entryName, SpawnAlternativeKind entryKind, size_t& conflictCount, size_t maxWarnings, std::vector<std::string>& warnings) {
		if (!tile || !tile->creature) {
			return;
		}
		const Creature* mapCreature = tile->creature;
		SpawnAlternativeKind mergedKind = SpawnAlternativeKind::None;
		if (!MergeSpawnAlternativeKinds(mapCreature->getAlternativeKind(), entryKind, mergedKind)) {
			if (conflictCount < maxWarnings) {
				warnings.push_back(FormatConflictWarning(pos, center, "map creature '" + mapCreature->getName() + "'", "entry '" + entryName + "'", "incompatible alternative kinds"));
			}
			++conflictCount;
		}
	}

	bool ValidateBeforeApply(const Map& map, const SpawnDocument& document, std::vector<std::string>& warnings) {
		constexpr size_t MAX_CONFLICT_WARNINGS = 10;
		size_t conflictCount = 0;
		auto recordConflict = [&](const std::string& message) {
			if (conflictCount < MAX_CONFLICT_WARNINGS) {
				warnings.push_back(message);
			}
			++conflictCount;
		};

		std::map<Position, PositionEntryInfo> monsterSignatures;
		std::map<Position, PositionEntryInfo> npcSignatures;

		for (const SpawnAreaData& area : document.areas) {
			const Position center(area.centerX, area.centerY, area.centerZ);
			if (area.centerX < 0 || area.centerX > 65535 || area.centerY < 0 || area.centerY > 65535 || area.centerZ < 0 || area.centerZ > 255 || area.radius < 0 || area.radius > 255) {
				recordConflict("Spawn document contains an invalid area (center " + std::to_string(area.centerX) + ":" + std::to_string(area.centerY) + ":" + std::to_string(area.centerZ) + ", radius " + std::to_string(area.radius) + "); no spawn data was applied.");
				continue;
			}
			for (const SpawnEntryData& entry : area.entries) {
				if (!HasValidSpawnIdentity(entry) || entry.x < 0 || entry.x > 65535 || entry.y < 0 || entry.y > 65535 || entry.z != area.centerZ || entry.spawnTime < 1) {
					recordConflict("Spawn document contains an invalid creature entry '" + entry.name + "' at " + std::to_string(entry.x) + ":" + std::to_string(entry.y) + ":" + std::to_string(entry.z) + " (center " + std::to_string(area.centerX) + ":" + std::to_string(area.centerY) + ":" + std::to_string(area.centerZ) + "); no spawn data was applied.");
					continue;
				}
				bool hasUnnamedAlternative = false;
				for (const SpawnVariantData& variant : entry.alternatives) {
					if (variant.name.empty()) {
						recordConflict("Spawn document contains an unnamed creature alternative at " + std::to_string(entry.x) + ":" + std::to_string(entry.y) + ":" + std::to_string(entry.z) + " (center " + std::to_string(area.centerX) + ":" + std::to_string(area.centerY) + ":" + std::to_string(area.centerZ) + "); no spawn data was applied.");
						hasUnnamedAlternative = true;
						break;
					}
				}
				if (hasUnnamedAlternative) {
					continue;
				}

				const Position position(entry.x, entry.y, entry.z);
				const Direction direction = static_cast<Direction>(std::clamp(entry.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST)));
				const SpawnAlternativeKind initialKind = AuthoritativeAlternativeKind(document.format, entry.alternativeKind, entry.hasWeight, entry.alternatives.size());
				const std::string entryName = entry.name.empty() && !entry.alternatives.empty() ? entry.alternatives.front().name : entry.name;

				const PositionEntryInfo incoming {
					entryName,
					center,
					entry.spawnTime,
					direction,
					entry.hasDirection,
					entry.weight,
					entry.hasWeight,
					entry.isNpc,
					initialKind
				};

				auto& signatures = entry.isNpc ? npcSignatures : monsterSignatures;
				auto [iterator, inserted] = signatures.emplace(position, incoming);
				if (!inserted) {
					PositionEntryInfo& existing = iterator->second;
					SpawnAlternativeKind mergedKind = SpawnAlternativeKind::None;
					if (!MergeSpawnAlternativeKinds(existing.alternativeKind, incoming.alternativeKind, mergedKind)) {
						recordConflict(FormatConflictWarning(position, incoming.center, "entry " + FormatEntryDetails(existing.name, existing.spawnTime, existing.direction, existing.hasDirection, existing.weight, existing.hasWeight), "entry " + FormatEntryDetails(incoming.name, incoming.spawnTime, incoming.direction, incoming.hasDirection, incoming.weight, incoming.hasWeight), "incompatible alternative kinds"));
					} else {
						existing.alternativeKind = AuthoritativeAlternativeKind(document.format, mergedKind, existing.hasWeight || incoming.hasWeight, 2);
						existing.direction = incoming.direction;
						existing.hasDirection = incoming.hasDirection;
					}
				}

				// Validate compatibility against existing map creature (evaluated for every entry)
				ValidateAgainstExistingCreature(map.getTile(position), position, center, entryName, initialKind, conflictCount, MAX_CONFLICT_WARNINGS, warnings);
			}
		}

		if (conflictCount > 0) {
			if (conflictCount > MAX_CONFLICT_WARNINGS) {
				warnings.push_back("Spawn validation failed: " + std::to_string(conflictCount - MAX_CONFLICT_WARNINGS) + " additional conflict(s) omitted.");
			}
			return false;
		}
		return true;
	}

}

bool SpawnMapAdapter::Apply(Map& map, const SpawnDocument& document, std::vector<std::string>& warnings) {
	if (!ValidateBeforeApply(map, document, warnings)) {
		return false;
	}
	MapChunkRevisionTracker::Batch chunkBatch(map.getChunkRevisionTracker());

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
				variant.spawnTime = entry.spawnTime;
				variant.direction = entry.direction;
				variant.hasDirection = entry.hasDirection;
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
				creatureTile->markRenderChunkChanged();
				creatureTile->creature->setSpawnType(primary.isNpc);
				creatureTile->creature->setSpawnDirection(
					static_cast<Direction>(std::clamp(primary.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST))),
					primary.hasDirection
				);
				creatureTile->creature->setSpawnTime(primary.spawnTime);
				creatureTile->creature->setSpawnWeight(primary.weight, primary.hasWeight);
				creatureTile->creature->setSpawnAttributes(primary.attributes);
				creatureTile->creature->setSpawnPrimaryRecord(primary);
				creatureTile->creature->setSpawnSource(center);
				const SpawnAlternativeKind initialKind = AuthoritativeAlternativeKind(document.format, entry.alternativeKind, primary.hasWeight, variants.size());
				creatureTile->creature->setAlternativeKind(initialKind);
				for (size_t index = 1; index < variants.size(); ++index) {
					creatureTile->creature->addSpawnAlternative(variants[index]);
				}
				if (document.format == SpawnFormat::CanaryCrystal && variants.size() > 1) {
					const SpawnVariantData& effective = variants.back();
					creatureTile->creature->setSpawnTime(effective.spawnTime);
					creatureTile->creature->setSpawnDirection(
						static_cast<Direction>(std::clamp(effective.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST))),
						effective.hasDirection
					);
				}
			} else {
				Creature* creature = creatureTile->creature;
				if (!creature->hasSpawnPrimaryRecord()) {
					creature->setSpawnPrimaryRecord(CapturePrimaryRecord(*creature));
				}
				const SpawnAlternativeKind incomingKind = AuthoritativeAlternativeKind(document.format, entry.alternativeKind, entry.hasWeight, variants.size());
				SpawnAlternativeKind mergedKind = SpawnAlternativeKind::None;
				if (!MergeSpawnAlternativeKinds(creature->getAlternativeKind(), incomingKind, mergedKind)) {
					warnings.push_back("SpawnMapAdapter::Apply: Failed to merge alternative kinds at " + std::to_string(position.x) + ":" + std::to_string(position.y) + ":" + std::to_string(position.z));
					return false;
				}
				creature->setAlternativeKind(AuthoritativeAlternativeKind(document.format, mergedKind, entry.hasWeight || creature->hasSpawnWeight(), creature->getSpawnAlternatives().size() + variants.size()));
				for (const SpawnVariantData& variant : variants) {
					creature->addSpawnAlternative(variant);
				}
				if (!variants.empty()) {
					const SpawnVariantData& lastVariant = variants.back();
					if (document.format == SpawnFormat::CanaryCrystal) {
						creature->setSpawnTime(lastVariant.spawnTime);
					}
					creature->setSpawnDirection(
						static_cast<Direction>(std::clamp(lastVariant.direction, static_cast<int>(DIRECTION_FIRST), static_cast<int>(DIRECTION_LAST))),
						lastVariant.hasDirection
					);
				}
				// Multiple weighted entries on one Canary/Crystal tile are valid.
				// They were merged and preserved above, so this is not a loader error.
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
					const SpawnVariantData primary = creature->hasSpawnPrimaryRecord()
						? creature->getSpawnPrimaryRecord()
						: CapturePrimaryRecord(*creature);
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

SpawnValidationResult SpawnMapAdapter::Validate(Map& map) {
	SpawnValidationResult result;
	std::unordered_set<Creature*> allCreatures;

	for (const Position& center : map.spawns) {
		Tile* centerTile = map.getTile(center);
		if (!centerTile || !centerTile->spawn) {
			++result.staleRegistryEntries;
			result.valid = false;
			result.warnings.push_back("Spawn registry contains center (" + std::to_string(center.x) + "," + std::to_string(center.y) + "," + std::to_string(center.z) + ") but tile has no Spawn.");
		}
	}

	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it)->get();
		if (!tile) {
			continue;
		}

		if (tile->spawn) {
			if (std::find(map.spawns.begin(), map.spawns.end(), tile->getPosition()) == map.spawns.end()) {
				++result.unregisteredSpawns;
				result.valid = false;
				result.warnings.push_back("Tile at (" + std::to_string(tile->getX()) + "," + std::to_string(tile->getY()) + "," + std::to_string(tile->getZ()) + ") has a Spawn but is not in map spawn registry.");
			}
		}

		if (tile->creature) {
			allCreatures.insert(tile->creature);
			Creature* creature = tile->creature;
			if (creature->hasSpawnSource()) {
				const Position source = creature->getSpawnSource();
				Tile* sourceTile = map.getTile(source);
				if (!sourceTile || !sourceTile->spawn) {
					++result.orphanedCreatures;
					result.valid = false;
					result.warnings.push_back("Creature '" + creature->getName() + "' at (" + std::to_string(tile->getX()) + "," + std::to_string(tile->getY()) + "," + std::to_string(tile->getZ()) + ") references missing spawn center (" + std::to_string(source.x) + "," + std::to_string(source.y) + "," + std::to_string(source.z) + ").");
				} else {
					const int radius = std::max(1, sourceTile->spawn->getSize());
					const int dx = std::abs(tile->getX() - source.x);
					const int dy = std::abs(tile->getY() - source.y);
					if (dx > radius || dy > radius || tile->getZ() != source.z) {
						++result.outOfRadiusCreatures;
						result.valid = false;
						result.warnings.push_back("Creature '" + creature->getName() + "' at (" + std::to_string(tile->getX()) + "," + std::to_string(tile->getY()) + "," + std::to_string(tile->getZ()) + ") is outside spawn radius of center (" + std::to_string(source.x) + "," + std::to_string(source.y) + "," + std::to_string(source.z) + ").");
					}
				}
			}
		}
	}

	SpawnDocument capturedDoc = Capture(map);
	size_t totalCaptured = capturedDoc.entryCount();
	if (totalCaptured < allCreatures.size()) {
		result.uncapturableCreatures = static_cast<int>(allCreatures.size() - totalCaptured);
		result.valid = false;
		result.warnings.push_back("Spawn validation: " + std::to_string(result.uncapturableCreatures) + " creature(s) cannot be serialized because their spawn source is invalid.");
	}

	return result;
}
