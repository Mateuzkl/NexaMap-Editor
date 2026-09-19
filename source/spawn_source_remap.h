//////////////////////////////////////////////////////////////////////
// NexaMap Editor — Spawn source remapping for relocation operations.
//
// When a copied/moved region contains creatures that reference a spawn
// center via Creature::spawn_source, relocating the tiles to a new
// position must also translate those references.  Otherwise
// SpawnMapAdapter::Capture() silently skips them and the creatures
// are lost on save/reload.
//////////////////////////////////////////////////////////////////////

#ifndef RME_SPAWN_SOURCE_REMAP_H_
#define RME_SPAWN_SOURCE_REMAP_H_

#include "position.h"
#include "spawn_format.h"

#include <map>
#include <set>
#include <vector>
#include <string>

class BaseMap;
class Map;
class Creature;
class Spawn;
class Editor;
class Action;

/// Metadata about a spawn center referenced by at least one copied creature.
struct SpawnDependency {
	Position originalCenter;
	int radius = 0;
	/// True when the spawn-center tile itself was part of the copied set.
	bool centerCopied = false;
	bool hasMonsterSource = false;
	bool hasNpcSource = false;
	bool hasMixedSource = false;
	SpawnAttributeMap monsterAttributes;
	SpawnAttributeMap npcAttributes;
	SpawnAttributeMap mixedAttributes;
};

/// Maps original spawn center position → dependency info.
using SpawnDependencyMap = std::map<Position, SpawnDependency>;

/// Scan every tile in buffer and record all spawn center references.
/// If sourceMap is provided, dependencies whose center was not in buffer
/// will be looked up in sourceMap to capture radius and attributes.
SpawnDependencyMap CaptureSpawnDependencies(const BaseMap& buffer, const Map* sourceMap = nullptr);

/// After relocation, translate every creature's spawn_source in buffer
void RemapCreatureSpawnSources(
	BaseMap& buffer,
	const Position& sourceAnchor,
	const Position& destinationAnchor,
	const SpawnDependencyMap& dependencies
);

/// Convenience overload: remap a single creature's spawn_source.
bool RemapSingleCreatureSpawnSource(
	Creature& creature,
	const Position& sourceAnchor,
	const Position& destinationAnchor,
	const SpawnDependencyMap& dependencies
);

/// Merge incoming spawn metadata into an existing spawn. Existing attributes
/// with unrelated names are preserved; incoming values win key conflicts.
void MergeSpawnMetadata(Spawn& target, const Spawn& incoming);

/// Recreate every dependency required by a creature that actually survived the
/// paste unless its center was itself pasted successfully. This also repairs a
/// dependency whose source center was selected but its translated tile was
/// skipped at the map boundary.
void EnsureDependentSpawnsExist(
	Editor& editor,
	Action& action,
	const Position& sourceAnchor,
	const Position& destinationAnchor,
	const SpawnDependencyMap& dependencies,
	const std::set<Position>& requiredCenters,
	const std::set<Position>& pastedCenters
);

#endif // RME_SPAWN_SOURCE_REMAP_H_
