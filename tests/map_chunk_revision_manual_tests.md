# Chunk revisions — Phase 1 validation

This phase records CPU revisions only. It does not cache geometry or change the
map traversal, painter order, atlas, streaming VBOs, FBO decisions, LOD or lights.
Runtime validation must precede any GPU-cache implementation.

## Model and integration

- Each existing `Floor` owns content and presentation revisions for its 4x4
  locations. No floor or map location is allocated just to invalidate it.
- The object pool gains a 1152-byte class so floors with ownership/revision
  metadata remain pooled. Existing smaller classes are unchanged; a static
  assertion checks that a floor plus its allocation header fits the pool.
- The clock and counters belong to `BaseMap`. Dirty state is a comparison with a
  consumer's snapshot; there is no shared `clearDirty()` that could hide edits
  from another view. Revisions are not serialized into maps.
- `QTreeNode::setTile` covers creation, coordinate assignment and `swapTile`.
  The direct `BaseMap::setTile(TileLocation*, ...)` overload uses the same
  replacement policy, including deletion of the last tile.
- Action commit/undo and synchronous batch commit/undo/redo coalesce repeated
  notifications per chunk and channel. Redo and multiplayer's `ACTION_REMOTE`
  already use `Action::commit`. Incrementally committed actions retain separate
  intervals; no batch scope is stored in an undo-history object.
- Selection actions use presentation revisions. Installed-tile mutators cover
  items, borders, walls, flags, zones and houses. Copies sharing a `TileLocation`
  are ignored unless that location currently contains the same `Tile*`.
- Direct conversion, cleanup, randomization and spawn-import paths are also
  marked. `Tile::update()` is the conservative content notification after an
  in-place edit. Writers of public tile/item members must still notify explicitly
  (or install the completed tile through the existing assignment APIs).
- A snapshot read ends the coalescing interval, so a reentrant paint cannot hide
  later edits in a long operation. This tracker does not introduce thread safety
  for map mutations; it follows existing map ownership.

Content is intentionally conservative: it includes persistent creature changes
and render-affecting map flags. It is not yet a classification of static VBO
geometry. Camera movement and animation frame advancement do not mark chunks.
Resource/atlas generations and visual cache keys are later phases.

## HUD

Use the existing **Show Performance Stats** command (also discoverable in the
Quick Command Palette). Additional rows show:

- `Last chunks V/N/C`: existing 4x4/floor blocks visited, newly observed blocks,
  and blocks whose content changed since the preceding sampled main-map pass.
- `Last chunks P/S`: blocks with presentation changes and unchanged blocks.
  A block may have both content and presentation changes.
- `Marks C/P`: cumulative content/presentation invalidation requests for this map.
- `Revisions C/P`: cumulative actual revision advances after coalescing.
- `Coalesced`: requests combined with another mark in the same interval/channel.

The observer samples the existing main-map traversal only with the HUD enabled.
It stores two visible sets by value and resets on resource-session changes.
Higher-floor overlay traversal is not included. An allocated empty floor can
still be counted. Newly visible blocks are **not** cache misses, and unchanged
blocks are **not** GPU-cache hits: no persistent cache exists yet.
FBO reuse leaves the last sample on screen; minimap LOD is explicitly unsampled.

## Automated model tests

`map_chunk_revision_tests` is a CMake/CTest target requiring C++20 and the standard
library only. It covers:

- 4x4 boundaries, floor separation and extreme legal coordinates;
- 10,000 tile edits / 625 chunks, nested scopes and duplicate notifications;
- independent content/presentation revisions and separate mutation intervals;
- detached copies, creation, replacement, deletion and recreation;
- reentrant reads, exception cleanup and independent maps/views;
- bounded viewport observations and resource-session reset.

These tests exercise the shared revision/notification model, not a running
`Editor`, GL renderer, `ActionQueue`, file loader or multiplayer connection.
After building the target, run CTest with `-R map_chunk_revision_tests`.

## Manual integration checks

Record counter deltas, not just cumulative totals (loading the map also marks
chunks). Keep the same viewport for local edit comparisons. Disable autoborder
when checking a precise count of affected blocks.

1. Create ground at (100,100,7). Edit inside 100..103 on each axis: only that
   chunk should change. Edit (104,100,7), (100,104,7) and (100,100,8): each belongs
   to a different chunk/floor. Confirm the first chunk is unaffected.
2. Delete every object in a chunk, undo, redo and paint there again. Its content
   revision must continue advancing; an empty floor must not reuse an old state.
3. Select/deselect ground, one item, a creature and a multi-tile selection.
   `Revisions C` must stay unchanged; `Revisions P` should advance. Repeat with
   undo/redo of selection. Move the mouse and camera without editing: neither
   revision counter should advance.
4. Paint ground, walls, doodads and borders; erase, fill, replace, randomize and
   run the procedural generator. Test PZ, zone and house changes. Only affected
   chunks should change, including neighbors actually edited by autoborder.
5. Paste a 100x100 area, undo and redo. Check that repeated notifications within
   each applied action coalesce. Cross-client paste should notify the destination
   map while the source map's counters remain unchanged.
6. Open two views of one map, edit, then inspect each. Both views must observe
   the changed revisions. Switch between independent Classic and Canary/Crystal
   maps: cumulative counters belong to each map and visible snapshots must reset
   when resources switch. Close/reopen a map and verify fresh counters.
7. With two multiplayer clients, edit and undo on one peer. Check the receiving
   map's content counters after authoritative application. Palette selection,
   chat and camera motion must not count as content edits.
8. Check preview animation, overlapping sprites, upper floors, lights, medium
   and minimap LOD, with FBO enabled/disabled. The rendered scene should match
   the previous renderer. The new rows must remain readable on the tested DPI.

Do not infer a performance gain from these counters. First compare CPU time,
frame time and memory against the preceding build, including a large paste and
undo/redo. Draw order, atlas-generation validation and GPU-cache benchmarks are
separate acceptance gates.
