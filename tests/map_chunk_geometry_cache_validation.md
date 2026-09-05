# CPU chunk geometry cache — Phase 2

## Status (2026-09-02)

Implemented a conservative CPU cache of **static, single-part grounds**. The
feature is experimental and **OFF by default**. This document records the Phase
2 checkpoint; persistent GPU replay and its later validation are documented in
`map_chunk_render_cache_validation.md`.

Base checkpoint: `308fbc906f1c079339b945b7946cd7c144f04cf9`, branch
`codex/nexamap-layout-redesign`.

## Build and executed tests

- Full Editor build: Visual Studio 18 / v145, Release x64, successful after the
  ground integration and again after the metrics integration. The final build
  reported **0 errors, 0 warnings** (26.66 seconds).
- CMake Release targets `map_chunk_revision_tests` and
  `map_chunk_geometry_cache_tests`: **2/2 CTest tests passed**, 0.05 seconds in
  the final run.
- clang-format **16.0.6** dry run with `--Werror`: passed for changed C++ files.
- XML parsing: menu, Visual Studio project and filters passed.
- `git diff --check` passed for this change; the pre-existing user edit to
  `data/editor/borders.xml` was excluded and left untouched.

Local build evidence is under the ignored `build.chunk-cache-validation/`:

- `phase1-editor-build.log`, `phase1-fixed-build.log`;
- `phase2-ground-build.log`, `phase2-metrics-build.log`;
- `cmake/Testing/Temporary/LastTest.log`;
- `phase2/NexaMap Editor.exe` with isolated runtime data and settings.

The cache test exercises the actual standard-library cache and revision tracker,
not a simulated success flag or GUI renderer. It checks cold misses, repeated
hits, presentation-only changes, lazy offscreen invalidation, coalesced rebuilds,
X/floor separation, deletion/recreation, failed builders, bounded storage,
payload accounting, resource/map changes, independent views and clearing.
The Phase 1 target additionally covers XY boundaries, 10,000 edits / 625 chunks,
detached copies, reentrant reads and independent consumers.

## Phase 1 audit and observed runtime behavior

Two direct removal helpers lacked notifications: `RemoveItemOnMap` and
`RemoveItemDuplicateOnMap`. They now batch invalidations and mark the installed
tile after an actual removal. The rest of Phase 1 was retained.

Reviewed paths: Action/BatchAction installation, undo/redo, ACTION_REMOTE,
properties dialogs, replacement, duplicate cleanup, border/wall helpers,
conversion, selection and clipboard installation. Remote changes reach the
existing Action commit path; that is a code audit, **not a network test**.

Before Phase 2, the running Phase 1 build was exercised with an isolated
20,000-tile Classic fixture (100x100 at floors 7 and 8). Observations included:

- selection undo changed presentation 3 → 4 while content stayed 20,002;
- content undo changed content 20,002 → 20,003, redo 20,003 → 20,004;
- normalizing zoom and changing floor left content unchanged;
- selection on floor 8 changed presentation without changing content;
- deleting a floor-8 tile with autoborder changed content 20,004 → 20,007.

These are Phase 1 observations, **not claims that the Phase 2 cached renderer
has passed the same runtime scenarios**. Early samples overlapped user input,
so they are not used as controlled benchmark results.

## Architecture and ownership

`MapDrawer` owns `MapChunkGeometryCache`. The key is the packed 4x4 XY/floor
coordinate from Phase 1. Each entry stores the floor's content revision and
16 tile slots in the same X-major/Y-minor order as the existing traversal.

Each eligible slot stores item ID, tile-local offset, stack elevation and a
logical sprite-image index. A quad is implicitly 32x32; four backend vertices
are still expanded by the existing renderer. The HUD's vertex count is a
**quad-equivalent count**, not an array of persistent GPU vertices.

Map session identity and graphic-resource identity scope each pass. A different
identity clears the cache. The graphic identity travels with resource storage
in `GraphicManager::swap`, and changes when definitions are cleared/reloaded.
It is a process-local lifetime identity, not an on-disk resource fingerprint
and not an atlas epoch. No `Tile*`, `Item*`, `Brush*` or `GameSprite*` is retained
in an entry.

Only content revision changes trigger reconstruction. Presentation is evaluated
live. Offscreen changes cause no work until the floor becomes visible again.
Existing floor snapshot reads retain Phase 1's reentrant batch behavior.

Storage is bounded to 4,096 chunks per view with FIFO eviction. On the tested
x64 build layout, geometry payload is 328 bytes/chunk, at most 1,343,488 bytes
(1.28125 MiB). This excludes unordered-map/deque/allocator overhead. Empty
slots still occupy payload. Multiple views have separate budgets. This is a
CPU capacity bound, not a GPU LRU or VRAM budget.

## Draw order, dynamic content and atlas

The cache is replayed **at the original ground call inside `DrawTile`**.
Floor order, chunk order, tile order, item stack, lights and upper-floor passes
are unchanged. There is no pass that draws all static objects before dynamic
objects.

Eligibility requires a real ground with one 1x1 sprite part, one layer, one
frame, no animator, valid patterns, and no light indicator. Meta/technical
items, pickupables, subtype-dependent patterns, hangables, doors, hooks and
podiums are excluded. Selected grounds and unexpected animation frames fall
back to `BlitItem` at replay.

Borders, walls, top items, multipart grounds, animations, creatures, selection,
preview, cursor, multiplayer overlays, grid, lighting and tooltips continue
through the existing renderer. Ground tint and camera/floor transforms are
computed live. No custom renderer or changed alpha policy was introduced.

Entries contain **no GLuint or atlas UVs**. `getSpriteTexByIndex` resolves the
current image texture/UV on every replay, using the same hardware-image access,
texture-missing signaling and atlas usage tracking as the normal path.

The atlas currently has usage/frame counters, but no durable per-page generation
for cached UV dependencies. `GraphicManager::recycleAtlasPage` clears affected
`NormalImage` residency and reinitializes the **same GLuint** with `glTexImage2D`.
Before Phase 3, add a lifetime token/epoch at page allocation, recycling and
destruction, track geometry dependencies and reject stale page generations.
An OpenGL texture name alone is insufficient. Resource identity added here
must not be mistaken for that page generation.

## Camera, zoom and toggling

Use **Experimental → CPU Ground Geometry Cache**, also discoverable through
the Quick Command Palette. The check state persists in existing settings.
Turning it off clears geometry on the next scene pass.

Camera/zoom are not part of the geometry key. Previously resident, unchanged
chunks hit when revisited, unless evicted. Newly exposed chunks naturally miss.
Far zoom retains the existing minimap-page path and bypasses CPU geometry;
the geometry remains resident so returning to normal zoom can reuse it.
Only-colors rendering also bypasses it. Medium zoom, smooth zoom, scene FBO
decisions and the GPU backend were not modified.

## Metrics and A/B procedure

The existing performance HUD retains Phase 1 rows and adds CPU visible chunks,
hits/misses, rebuilds, replayed quads, resident quads/equivalent vertices, geometry
payload, build time, lookup time and ground submission CPU time. `Last` values
refer to the most recent scene pass; a reused FBO does not produce new geometry
work. Timers run only when the HUD or diagnostic trace is enabled.

For a CSV, launch with `NEXAMAP_CPU_CACHE_TRACE=1`. Each view writes
`cpu-chunks-<pid>-<map-session>.csv` in `GUI::GetLocalDataDirectory()`. Recording
stops after 6,000 frames, flushes every 30 samples, and closes with the view.
It includes viewport, resource identity, cache state, revision counts, CPU
timings and existing renderer draw calls / texture binds / VBO stream bytes.

Interpretation:

- `build_ms`: cache construction only; OFF has no cache build. It is not the
  total baseline geometry-generation time.
- `ground_submit_ms`: timed ground path in both modes, including live atlas
  resolution and submission. Timers add overhead in both modes.
- `scene_cpu_ms`: CPU duration of the existing scene pass.
- `frame_submit_ms`: CPU SetupGL-to-Release/endFrame duration; excludes
  SwapBuffers and subsequent cleanup. It is **not GPU time**.
- `frame_interval_ms`: wall interval between frame starts, including scheduling,
  idling and refresh pacing. It is not a GPU timer or uncapped FPS estimate.
- `scene_drawn=0`: FBO reuse; geometry counters/timings are retained last-scene
  values and must not be counted as fresh cache work.
- `ingame`, dimensions, zoom, scroll and floor allow matching comparable views.

Benchmark protocol: use the same map/resources, dimensions, camera, zoom,
overlays and refresh settings; allow sprites to finish loading; collect OFF,
ON cold, ON warm and OFF again. Group stable-viewport scene samples separately
from pan, cold loads, edits, screenshots and FBO reuse. Report sample count,
median and p95. Compare F10 captures from identical static views after warming
textures. Do not infer improvement merely from a high hit ratio.

**Measured OFF/ON results: pending.** The Phase 2 build launched and loaded the
Classic resource context, but Windows Computer Use twice returned
`foreground window did not report a process id` when capturing/activating the
test window. No valid controlled render samples or OFF/ON image pairs were
obtained. No performance improvement is claimed.

## Acceptance gates carried into Phase 3

- Compare actual OFF/ON images and measure runtime CPU/streaming metrics.
- Exercise stationary view, pan/revisit, normal/medium/far zoom transitions,
  content edits, XY boundaries, floor edits, borders/walls, selection, large
  paste, undo/redo and deletion/recreation with the cached build.
- Exercise animated items and creatures interleaved with static grounds,
  including missing/deferred atlas uploads and page recycling.
- Switch independent Classic and Canary/Crystal tabs, reload assets, close and
  reopen maps. Model identity tests alone do not validate this UI integration.
- Run an actual multiplayer remote edit; the existing Action path was audited
  but no connected session was tested.
- Validate the HUD on narrow/high-DPI views and complete real-map benchmarks.
- Resolve atlas generation and ordered segment dependencies before persistent
  GPU geometry is considered. Phase 3 added and tested those mechanisms; see
  the separate Phase 3 report for results and remaining real-map work.

## Files

New: `source/map_chunk_geometry_cache.h`,
`tests/map_chunk_geometry_cache_tests.cpp`, this report.

Changed: root/source `CMakeLists.txt`; `source/graphics.{h,cpp}`;
`source/map_drawer.{h,cpp}`; `source/map.h` (the two audit gaps);
`source/settings.{h,cpp}`; `source/main_menubar.{h,cpp}`;
`data/menubar.xml`; `vcproj/Project/Editor.vcxproj` and its filters.

Phase 2 was retained for the combined Phase 2/3 validation and commit.
