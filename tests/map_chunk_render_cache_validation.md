# Persistent chunk VBO cache — Phase 3

## Status (2026-09-03)

Phase 3 adds an experimental persistent GPU cache for the same conservative
static 1x1 grounds accepted by the Phase 2 CPU geometry cache. The setting is
**OFF by default** and is available under Experimental as `Persistent GPU
Ground Cache`. The existing renderer remains authoritative for every fallback.

## Design

`MapDrawer` owns `MapChunkRenderCache`. An entry is scoped by map-session and
graphic-resource identity and keyed by the Phase 1 4x4 XY/floor chunk key. It
contains a VBO, the content revision used to build it, quad indices, texture
bindings and atlas page tokens. It retains no `Map*`, `Tile*`, `Item*`,
`Brush*`, `GameSprite*` or resource-session pointer.

The cache is bounded to 4,096 chunks and 8 MiB per `MapDrawer`. LRU eviction
does not evict an entry already used in the current frame. Allocation or atlas
resolution failure immediately falls back to the existing live draw path.

`AtlasPageEpochs` assigns a monotonically changing token whenever an atlas page
is allocated or recycled. Recycling the same OpenGL texture name therefore
invalidates old VBO dependencies. A page must validate and be retained for the
frame before its cached draw is queued. Resource and map identity changes clear
all VBOs.

`GLRenderer` uses its existing program, projection, stream EBO, blend state and
quad layout for retained draws. Offset and tint are live uniforms. A live draw
flushes queued retained geometry first, preserving the original floor, chunk,
tile and item-stack order. Selection, borders, walls, animated/multipart
sprites, creatures, lights, overlays and missing/deferred textures continue on
the existing path.

The CPU cache stores logical sprite image indices. Atlas texture names and UVs
are resolved only while preparing a current GPU entry; no persistent cache
assumes that a recycled `GLuint` still represents the same page contents.

## Instrumentation

The performance HUD reports requested/active state, visible chunks, hits,
misses, builds, evictions, fallbacks, replayed quads, resident chunks, bytes and
uploaded bytes. `NEXAMAP_CPU_CACHE_TRACE=1` appends the same GPU fields to the
existing per-view CSV in the user data directory.

Normal server diagnostics now omit per-directory and per-map scan lines. Set
`NEXAMAP_SERVER_SCAN_TRACE=1` to restore that verbose trace when investigating
resource discovery.

## Executed validation

- Visual Studio 18, platform toolset v145, Release x64: full Editor build
  succeeded with 0 errors and 0 warnings.
- clang-format 16.0.6 `--dry-run --Werror`: passed for changed C++ files.
- CTest model tests cover revision coalescing/boundaries and CPU-cache misses,
  hits, rebuilds, deletion/recreation, identities, failure fallback and bounds.
- The OpenGL integration test ran on Intel UHD Graphics, OpenGL 4.6. It compares
  FBO pixels with the cache OFF and ON across content interleaving, tint,
  selection, deletion/recreation, camera changes, capacity reuse, LRU pressure,
  identity changes, missing pages, and recycling the same texture name with new
  pixels and UVs. The image comparison was byte-exact.
- In the synthetic mixed scene, warm VBO stream traffic changed from 1,760 to
  480 bytes while draw calls remained 13. Four alternating 120-frame samples
  completed successfully. These timings validate the mechanism and are not a
  representative large-map benchmark.
- A Classic fixture reached a warm view with 112/112 GPU-cache hits, 1,792
  replayed ground quads, 112 resident chunks, zero fallbacks and zero warm
  upload bytes. The synthetic fixture lacks the house file named by its OTBM;
  that loader warning is unrelated to the cache.
- A user-built Canary/Crystal session completed palette construction, opened
  `world.otbm`, and the corrected close path exited normally during validation.

## Loading and shutdown fixes validated with Phase 3

Windows cannot hide its native `wxProgressDialog`. Deferred `Destroy()` left
the completed 95% palette dialog alive while the OTBM dialog was created. The
loader now completes the resource stage at 100% and synchronously destroys that
dialog before map loading starts.

`CloseAllEditors()` suppresses the notebook `+` page while tabs are removed.
`closingApplication` additionally blocks queued `RequestNewMapTab()`,
`ShowNewMapTabDialog()`, Welcome actions and notebook page work after the user
has requested shutdown. Cancelling a save resets the flag. A corrected build
closed with event-loop and application exit code 0 and did not open New Tab or
Welcome.

The Towns dialog now creates controls as children of each `wxStaticBox`, as
required by wxWidgets 3.3, removing the reported sizer ownership checks.

## Remaining limits

- The cache remains experimental and disabled by default.
- The measured traffic reduction comes from a small synthetic scene. Large
  Classic and Canary/Crystal maps still need matched OFF/ON median and p95 runs
  on representative hardware before enabling it by default.
- No connected multiplayer session was available for a live remote-edit test;
  remote actions continue through the same revision-notification path.
