# Lua extensions

Lua extensions are optional. Builds without Lua still keep the normal editor and procedural generator. With Lua installed through `vcpkg.json`, the default CMake option `NEXAMAP_ENABLE_LUA_EXTENSIONS=ON` enables the sandboxed SDK.

## Use in the editor

1. Open a map and select the **destination** tiles. Irregular selections are preserved exactly. If AI Style Reference has a captured SOURCE, also use **Set Current Selection as Target** there; the SOURCE is never used as the destination.
2. Open **Tools → Lua Extensions…**. Click **Reload** to discover files. Discovery does not execute scripts.
3. Select a script and click **Inspect** to load its metadata, then fill in its parameters. Use brush names from the active client resources; no item IDs are invented.
4. Click **Preview**. This runs the script in a sandbox and displays the validated operation counts; it does not change any map tile.
5. Click **Apply Preview** to commit one undoable edit. If the map, target, workspace or script changed since preview, it is rejected and must be previewed again.

Bundled scripts are in `extensions/lua` next to the executable. **Open Folder** opens the user extension directory under the NexaMap user-data directory. Scripts in either location must be `.lua` regular files (not symlinks) and at most 256 KiB. The manager only runs a script after an explicit Inspect or Preview click; newly discovered files never auto-run.

The first three bundled examples are `forest_generator.lua`, `cave_generator.lua`, and `reference_room.lua`. The forest and cave examples require brush names from the current workspace. The reference room requires a SOURCE reference and a separate captured TARGET.

## Script contract

```lua
return {
    name = "Example",
    version = "1.0",
    category = "Procedural",
    description = "Paint a selected area",
    parameters = {
        { name = "seed", type = "seed", default = 123 },
        { name = "groundBrush", type = "brush", kind = "ground", default = "" },
    },
    run = function(ctx, params)
        local ground = Brushes.find(params.groundBrush)
        if not ground or ground:kind() ~= "ground" then error("Choose a ground brush") end
        for pos in ctx.selection:positions() do ground:apply(pos) end
    end,
}
```

`Position.new(x,y,z)` creates a value position with `:offset` and `:distanceTo`. `Editor.selection()`/`Target.current()` expose `:count`, `:isEmpty`, `:floor`, `:bounds`, `:contains`, and `:positions`. `Map.getTile(position)` gives read-only tile values; `Items.get(activeId)` gives current-resource item identity; `Brushes.find/search/list` return resource brush descriptors with `:apply` and `:applyMany`. `Map.placeItem(position, activeId)` is a raw, validated fallback. `Reference.current()` exposes captured ground families, brush names and source-verified IDs as read-only values. In reference mode, planned brushes and raw IDs must have been verified in the SOURCE. `Random.new(seed)` supports `:float`, `:int`, and `:choice`; `Noise.value(x,y,seed)` and `Noise.perlin(options)` provide deterministic noise. `Geo.rectangle/circle/line/union/intersection/subtract` and `Algo.cellularAutomata` provide bounded geometry and cave masks. The built-in `math.random` is unavailable; use `Random.new`.

The extension receives only copied values and stages edits. It cannot access filesystem, network, process, native modules, OS environment, or editor-owning pointers through its API. Limits include 32 MiB Lua memory, 3 million instructions, 2 seconds, 65,536 operations and 65,536 selected tiles. The editor also rejects protected gameplay tiles and composite-only doodad brushes, which cannot be safely confined to an exact target mask. A failed apply rolls back the entire transaction.

## MCP

The loopback MCP server offers `lua_extension_list`, `lua_extension_info`, `lua_extension_reload`, `lua_extension_preview`, `lua_extension_validate`, and `lua_extension_run`. `run` requires a saved `previewId`, matching map session/workspace generations, and write authorization. Arbitrary inline Lua is deliberately unavailable. The source file is re-read before Apply and any change invalidates the preview.

## Current limits

The Preview pane provides a validated operation summary, not a rendered map overlay. Scripts cannot set larger scopes such as entire map: the exact current selection or captured TARGET is mandatory. Lua helpers are intentionally bounded; not every possible geometry/noise/algorithm helper is implemented. This keeps the initial SDK deterministic and safe while allowing additional helpers to be added without expanding write authority.
