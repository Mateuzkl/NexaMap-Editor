//////////////////////////////////////////////////////////////////////
// NexaMap Editor — dependency-light spawn remapping primitives.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "spawn_source_remap.h"

#include "creature.h"
#include "spawn.h"

bool RemapSingleCreatureSpawnSource(
	Creature& creature,
	const Position& sourceAnchor,
	const Position& destinationAnchor,
	const SpawnDependencyMap& dependencies
) {
	if (!creature.hasSpawnSource()) {
		return false;
	}

	const Position originalSource = creature.getSpawnSource();
	if (!dependencies.contains(originalSource)) {
		return false;
	}

	creature.setSpawnSource(originalSource - sourceAnchor + destinationAnchor);
	return true;
}

void MergeSpawnMetadata(Spawn& target, const Spawn& incoming) {
	if (incoming.getSize() > target.getSize()) {
		target.setSize(incoming.getSize());
	}

	const auto mergeKind = [&](SpawnAreaKind kind) {
		if (!incoming.hasSourceKind(kind)) {
			return;
		}
		SpawnAttributeMap merged = target.hasSourceKind(kind)
			? target.getSourceAttributes(kind)
			: SpawnAttributeMap {};
		for (const auto& [name, value] : incoming.getSourceAttributes(kind)) {
			merged[name] = value;
		}
		target.setSourceAttributes(kind, merged);
	};

	mergeKind(SpawnAreaKind::Monsters);
	mergeKind(SpawnAreaKind::Npcs);
	mergeKind(SpawnAreaKind::Mixed);
}
