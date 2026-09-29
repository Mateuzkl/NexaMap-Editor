# MCP tool catalog

The first MCP delivery exposes read-only orientation and discovery tools. All responses are resolved against the active editor and its `EditorResourceSession`.

| Tool | Purpose |
|---|---|
| `workspace_info` | Active workspace, map session, asset backend and item-ID mode |
| `client_info` | Loaded client/resource fingerprints and modes |
| `map_info` | Open map name, dimensions, tile count and generation |
| `editor_state` | Floor, zoom and current brush |
| `selection_get` | Exact selected positions and bounds, capped for safe transport |
| `item_info` | Canonical item identity and normalized information |
| `item_flags` | Resource-backed flags with their source |
| `item_search` | Paginated active-resource item search |
| `brush_search` | Semantic active-palette brush discovery |
| `generator_presets` | Native procedural presets understood by NexaMap |
| `generator_open` | Open the selected-area generator with an AI prompt prefilled; Preview/Apply remain manual |
| `generation_guide` | Safe workflow hints for the active selection and resources |

Mutating tools are deliberately refused while **Allow write operations** is off. Generator preview/apply and map editing tools are added incrementally only after their transaction and undo behavior is validated.
