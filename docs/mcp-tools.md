# MCP tool catalog

`tools/list` returns 74 tools: 50 read-only and 24 that need **Allow write operations**. All responses are resolved against the active editor and its `EditorResourceSession`.

## Read-only tools

### Workspace and resources

| Tool | Purpose |
|---|---|
| `workspace_info` | Active workspace, map session, asset backend and item-ID mode |
| `client_info` | Loaded client/resource fingerprints and modes |
| `item_info` | Canonical item identity and normalized information |
| `item_flags` | Resource-backed flags with their source |
| `item_search` | Paginated active-resource item search |
| `appearance_get` | `item_info` through the appearances-aware identity layer |
| `appearance_search` | `item_search` over the loaded item/appearance database |
| `dat_item_info` | `item_info` through the classic DAT/OTB-aware identity layer |
| `item_sprite_info` | Loaded sprite identity and dimensions for one item |
| `spr_sprite_info` | Classic SPR or active-backend sprite identity for one item |
| `sprite_render` | Render one loaded item sprite as PNG |
| `monster_types_list` | Loaded monster types |
| `npc_types_list` | Loaded NPC types |

### Map inspection

| Tool | Purpose |
|---|---|
| `map_info` | Open map name, dimensions, tile count, generation and `mapSessionId` |
| `map_properties` | Map metadata and auxiliary file names |
| `editor_state` | Floor, zoom, current brush and multiplayer edit state |
| `selection_get` | Exact selected positions and bounds, capped for safe transport |
| `tile_get` | One tile with item identities, metadata and entities |
| `map_read_region` | Bounded region as a summary, ASCII topology or paginated tiles |
| `map_statistics` | `map_read_region` summary statistics |
| `map_find` | Search a region for items, metadata, creatures, teleports and duplicates |
| `map_search` | Alias of `map_find` |
| `house_list` | Houses in the active map |
| `house_get` | One house and its tile positions |
| `town_list` | Towns and temple positions |
| `waypoint_list` | Map waypoints |
| `zone_list` | Zone names and IDs |
| `spawn_list` | Spawn centers, radii and creatures |

### Brushes

| Tool | Purpose |
|---|---|
| `tileset_list` | Loaded tilesets and palette categories |
| `brush_list` | Brushes from a tileset and palette category |
| `brush_search` | Semantic active-palette brush discovery |
| `brush_info` | One brush with border, variation and tileset metadata |
| `brush_preview` | Render a brush's representative sprite as PNG |
| `terrain_pairing` | How two ground brushes relate in the autoborder model |

### Rendering

| Tool | Purpose |
|---|---|
| `map_render_region` | Render a bounded region as PNG |
| `client_view` | Alias of `map_render_region` for visual inspection |
| `map_screenshot` | Alias of `map_render_region` |
| `client_view_scan` | Scan a region for ground holes, unmapped leaks and blocked tiles |

### Validation

| Tool | Purpose |
|---|---|
| `map_validate` | Bounded integrity checks for ground, duplicates, walls, IDs, teleports, spawns and creatures |
| `border_check` | Missing autoborders between adjacent ground |
| `path_check` | Walkable connectivity inside a region |
| `teleport_graph` | Teleports in a region and whether their destinations are valid |
| `floor_transitions` | Stairs, ladders, holes and ramps in a region |

### Style reference (read)

| Tool | Purpose |
|---|---|
| `target_get` | The captured writable target |
| `reference_get` | The captured style reference, in several detail modes |
| `reference_render` | Render the captured reference as PNG |
| `reference_generation_guide` | Workflow rules for matching the reference style |
| `reference_similarity` | Compare a region against the reference metrics |

### Generation (read)

| Tool | Purpose |
|---|---|
| `generator_presets` | Native procedural presets understood by NexaMap |
| `generation_guide` | Safe workflow hints for the active selection and resources |

## Tools that need write permission

Each of these is refused until **Allow write operations** is on. Each must pass the current `mapSessionId`, except where noted.

The **Dialog** column shows when the user must approve the call in NexaMap. "Overrides" means when the call sets `allowExpansion=true` with tiles selected, `preserveProtected=false`, or `allowReferenceSourceOverwrite=true`.

| Tool | Purpose | Dialog |
|---|---|---|
| `brush_apply` | Apply a ground, wall, carpet, table, doodad or raw brush | Overrides |
| `tile_edit` | Validated batch of direct tile edits as one undo step | Overrides |
| `editor_action` | Undo, redo or refresh | Undo of a non-MCP edit |
| `selection_op` | Select a region, clear the selection, borderize, randomize or delete | `delete` |
| `region_copy` | Capture a region into the native copy buffer | Never |
| `region_paste` | Paste the last `region_copy` | Overrides |
| `region_transform` | Mirror, rotate or translate visual terrain and items | Overrides |
| `region_replace_items` | Replace or remove one item ID in a region | Overrides |
| `map_from_bitmap` | Paint a PNG or BMP through ground brushes | Overrides |
| `house_manage` | Create, update or delete an empty house | `delete` |
| `town_manage` | Create, update or delete a town | `delete` |
| `waypoint_manage` | Create, update or delete a waypoint | `delete` |
| `zone_manage` | Create, rename or delete a zone | `delete` |
| `spawn_manage` | Create, update or delete a spawn, or add or remove a creature | `delete` |
| `map_new` | Create a new blank map tab (no `mapSessionId`) | Always |
| `map_open` | Open a map file in a new tab (no `mapSessionId`) | Always |
| `map_import` | Merge a map file into the active map; clears undo history | Always |
| `map_properties_update` | Change map name, description or size; cannot be undone | Always |
| `export_minimap` | Write new minimap files to `mcp-exports` | Always |
| `reference_capture` | Capture the selection as a style reference; clears the selection | Never |
| `reference_clear` | Clear the captured reference, which also lifts the target write boundary (no `mapSessionId`) | Never |
| `target_capture` | Capture the selection as the writable target | Overrides |
| `target_clear` | Clear the captured target (no `mapSessionId`) | Never |
| `generator_open` | Open the procedural generator with a brief prefilled; Preview and Apply stay manual | Never |
