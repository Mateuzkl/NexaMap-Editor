//////////////////////////////////////////////////////////////////////
// NexaMap Editor — Spawn source remapping for relocation operations.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spawn_source_remap.h"

#include "action.h"
#include "basemap.h"
#include "creature.h"
#include "editor.h"
#include "map.h"
#include "spawn.h"
#include "tile.h"

SpawnDependencyMap CaptureSpawnDependencies(const BaseMap& buffer, const Map* sourceMap) {
	SpawnDependencyMap dependencies;

	for (MapIterator iterator = const_cast<BaseMap&>(buffer).begin();
		 iterator != const_cast<BaseMap&>(buffer).end();
		 ++iterator) {
		Tile* tile = (*iterator)->get();
		if (!tile) {
			continue;
		}

		// Record spawn centers that exist in the buffer.
		if (tile->spawn) {
			const Position center = tile->getPosition();
			auto [it, inserted] = dependencies.emplace(center, SpawnDependency {});
			it->second.originalCenter = center;
			it->second.radius = tile->spawn->getSize();
			it->second.centerCopied = true;
			it->second.hasMonsterSource = tile->spawn->hasSourceKind(SpawnAreaKind::Monsters);
			it->second.hasNpcSource = tile->spawn->hasSourceKind(SpawnAreaKind::Npcs);
			it->second.hasMixedSource = tile->spawn->hasSourceKind(SpawnAreaKind::Mixed);
			it->second.monsterAttributes = tile->spawn->getSourceAttributes(SpawnAreaKind::Monsters);
			it->second.npcAttributes = tile->spawn->getSourceAttributes(SpawnAreaKind::Npcs);
			it->second.mixedAttributes = tile->spawn->getSourceAttributes(SpawnAreaKind::Mixed);
		}

		// Record spawn sources referenced by creatures in the buffer.
		if (tile->creature && tile->creature->hasSpawnSource()) {
			const Position source = tile->creature->getSpawnSource();
			auto [it, inserted] = dependencies.emplace(source, SpawnDependency {});
			if (inserted) {
				it->second.originalCenter = source;
			}
		}
	}

	// For any dependency whose center was not inside the copied buffer,
	// look up the original tile in the source map to capture its radius and metadata.
	if (sourceMap) {
		for (auto& [pos, dep] : dependencies) {
			if (!dep.centerCopied) {
				const Tile* sourceTile = sourceMap->getTile(pos);
				if (sourceTile && sourceTile->spawn) {
					dep.radius = sourceTile->spawn->getSize();
					dep.hasMonsterSource = sourceTile->spawn->hasSourceKind(SpawnAreaKind::Monsters);
					dep.hasNpcSource = sourceTile->spawn->hasSourceKind(SpawnAreaKind::Npcs);
					dep.hasMixedSource = sourceTile->spawn->hasSourceKind(SpawnAreaKind::Mixed);
					dep.monsterAttributes = sourceTile->spawn->getSourceAttributes(SpawnAreaKind::Monsters);
					dep.npcAttributes = sourceTile->spawn->getSourceAttributes(SpawnAreaKind::Npcs);
					dep.mixedAttributes = sourceTile->spawn->getSourceAttributes(SpawnAreaKind::Mixed);
				}
			}
		}
	}

	return dependencies;
}

void RemapCreatureSpawnSources(
	BaseMap& buffer,
	const Position& sourceAnchor,
	const Position& destinationAnchor,
	const SpawnDependencyMap& dependencies
) {
	for (MapIterator iterator = buffer.begin(); iterator != buffer.end(); ++iterator) {
		Tile* tile = (*iterator)->get();
		if (!tile || !tile->creature) {
			continue;
		}
		RemapSingleCreatureSpawnSource(
			*tile->creature,
			sourceAnchor,
			destinationAnchor,
			dependencies
		);
	}
}

void EnsureDependentSpawnsExist(
	Editor& editor,
	Action& action,
	const Position& sourceAnchor,
	const Position& destinationAnchor,
	const SpawnDependencyMap& dependencies,
	const std::set<Position>& requiredCenters,
	const std::set<Position>& pastedCenters
) {
	for (const auto& [center, dep] : dependencies) {
		const Position translatedCenter = center - sourceAnchor + destinationAnchor;
		if (!translatedCenter.isValid() || !requiredCenters.contains(translatedCenter) || pastedCenters.contains(translatedCenter)) {
			continue;
		}

		TileLocation* loc = editor.map.createTileL(translatedCenter);
		Tile* existingTile = loc->get();
		Tile* newTile = nullptr;
		if (existingTile) {
			newTile = existingTile->deepCopy(editor.map);
		} else {
			newTile = editor.map.allocator(loc);
		}

		if (!newTile->spawn) {
			newTile->spawn = newd Spawn(dep.radius > 0 ? dep.radius : 3);
		} else if (dep.radius > newTile->spawn->getSize()) {
			newTile->spawn->setSize(dep.radius);
		}

		Spawn incomingSpawn(dep.radius > 0 ? dep.radius : 3);
		if (dep.hasMonsterSource) {
			incomingSpawn.setSourceAttributes(SpawnAreaKind::Monsters, dep.monsterAttributes);
		}
		if (dep.hasNpcSource) {
			incomingSpawn.setSourceAttributes(SpawnAreaKind::Npcs, dep.npcAttributes);
		}
		if (dep.hasMixedSource) {
			incomingSpawn.setSourceAttributes(SpawnAreaKind::Mixed, dep.mixedAttributes);
		}
		MergeSpawnMetadata(*newTile->spawn, incomingSpawn);

		action.addChange(newd Change(newTile));
	}
}
