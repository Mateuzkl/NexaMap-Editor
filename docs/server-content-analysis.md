# NexaMap Server Content Editors — Analysis Report

## Scope and evidence

This report records the Phase 0 analysis for the native Server Content Editor. The implementation target is NexaMap. MME and MONx are references only; their code and runtime architectures are not embedded into NexaMap.

The analysis used these local trees:

- NexaMap Editor at `NexaMap-Editor`.
- MME at `MME`.
- MONx at `MONx`, already cloned from `Coldensjo/MONx`.
- `forgottenserver-downgrade-1.8-8.60` as a modern Lua server.
- `Crystal-Server-devv` as a Crystal/Canary server with an active `data-global` datapack.
- `Dragon-Souls-TFS-1.4-Protocol-11.00` as a legacy and mixed XML/Lua server.

No product code may store these machine-specific paths. They are test and analysis inputs only.

## NexaMap architecture relevant to server content

NexaMap already has the correct ownership boundary for this feature:

- `ServerResourceDetector` discovers the selected server root, active data pack, item databases, maps, monster directory, NPC directory, server family, and item ID mode.
- `WorkspaceSession` owns the selected client and `ServerWorkspace`, persists the selection, rescans it, and carries a generation counter.
- Each `EditorResourceSession` owns a private `WorkspaceSession`, `ItemDatabase`, `CreatureDatabase`, `Materials`, brushes, graphics, sprite appearances, and copy buffer. Activating a map tab swaps that session with the compatibility globals.
- The map tab holds the resource-session reference. Therefore a server-content index stored inside `WorkspaceSession` will naturally follow the active tab and cannot resolve World A through World B's workspace.
- `CreatureDatabase` already imports NexaMap creature catalogs, individual OT XML monster/NPC files, and recognized Lua monster/NPC definitions. It refreshes the palette after imports, but it is a display catalog rather than a lossless server source model.
- Client resources already provide outfits, item ServerID/ClientID mappings, sprites, effects, and missiles. Future previews must consume those active-session services rather than create a second asset loader.
- `FileSaveTransaction` already stages sibling files and performs replace/rollback. Future server-content saves should use it.
- The current `lua_parser.h` extracts a few known creature fields. It is adequate for palette discovery, but it is not a source-preserving editor parser.

Current gaps are a per-workspace content index, exact source metadata, category/format capabilities, ambiguity reporting, spell discovery, and lossless definition models/providers.

## MME findings

MME contains native wxWidgets dialogs that are useful UX references:

- `MonsterEditorDialog` groups general attributes, outfit controls, attacks, defenses, resistances, and loot.
- Outfit controls reuse creature resources, provide direction/frame controls, a color palette, generated look code, and palette selection.
- Loot selection reuses the item database and presents names with item IDs.
- `NPCWizardDialog` presents identity, outfit generation, shop offers, travel routes, dialogue, healing, quests, export, and palette registration.
- Both dialogs can update the in-memory creature palette without restarting the application.

The data path is not safe for editing existing server content:

- Loading a palette creature only transfers its name and outfit; it does not load its source monster definition.
- Monster save renders a new simplified XML document and omits unknown attributes and sections.
- NPC save renders a selected template as XML, Lua, or SRV instead of patching an existing definition.
- Save uses direct file output and does not perform a transactional replace or external-change check.
- Palette registration can create duplicate brush state and is not tied to exact source identity.

NexaMap should reuse the interaction ideas, preview controls, and item/creature lookup patterns. It should not reuse MME's destructive serializer or source resolution.

## MONx findings

MONx separates the frontend from Rust models, engine profiles, detection, parsing, writing, validation, and asset services. Its strongest concepts for NexaMap are:

- A normalized `MonsterDoc` covers identity, health, look, target behavior, flags, immunities, elements, attacks, defenses, voices, summons, nested loot, events, and engine metadata.
- Engine profiles expose only attributes the selected server actually reads. Detection uses scored structural evidence and reports ambiguity instead of silently selecting a weak guess.
- XML parsing retains unknown attributes, comments, layout, encoding, and source spans.
- Saving an unchanged XML document is byte-identical. A changed document copies unchanged source spans and renders only changed nodes.
- The Lua implementation recognizes known construction patterns while preserving source; it does not regenerate arbitrary custom Lua from a partial model.
- Spell areas use a normalized shape model (`beam`, `radius`, `ring`) with length, spread, radius, and ring parameters.
- The spell stage derives affected tiles, caster direction, target range, projectile direction, cooldown/chance, and impact timing. Registered or scripted spells whose geometry is unknown are shown honestly as unresolved rather than guessed.
- Effects, missiles, outfits, corpse items, and loot items are resolved through the selected client/server assets.
- Dirty buffers, external-change conflict UI, lints, and save checks are independent from map undo.

MONx is a useful behavioral specification. Its Tauri/React/Rust application structure should not be ported into the wxWidgets C++ application.

## Real server structures

### TFS 1.8 8.60 modern Lua base

Observed content:

- `data/monsters`: 1,809 Lua files and no XML definitions.
- `data/npc`: 1,093 Lua definitions plus library content.
- `data/scripts`: 1,224 Lua files, including revscript spells under `data/scripts/spells`.
- `data/items/items.xml` and `items.otb` provide server item metadata.

Monster definitions follow a recognizable registered-table shape:

```lua
local mType = Game.createMonsterType("Azure Frog")
local monster = {}
monster.name = "Azure Frog"
monster.outfit = { lookType = 226, ... }
monster.loot = { ... }
monster.attacks = { ... }
mType:register(monster)
```

NPC definitions use `Game.createNpcType`, direct outfit calls, handlers, keyword graphs, callbacks, shops, and arbitrary functions. The examined banker contains substantial custom control flow. Only identity and simple literal fields can initially be treated as safely editable.

Revscript spells use `Spell("instant")`, callbacks and arbitrary combat logic, then literal metadata calls such as `spell:name(...)`, `spell:words(...)`, and `spell:register()`. Visual area support must distinguish literal `createCombatArea` tables from dynamically computed Lua.

### Crystal/Canary active datapack base

The examined Crystal server selects `data-global` through `config.lua`. Server content discovery must follow that configured datapack instead of indexing the parallel `data-crystal` tree. Monsters and NPCs use registered Lua definitions.

Crystal's monster registration API differs from TFS 1.8 for summons. It reads `monster.summon = { maxSummons = ..., summons = {...} }`, and each summon uses `count`. The provider therefore records the server family on each indexed source, normalizes the nested table for the editor, and writes the same nested shape back. It never converts this section to the TFS `monster.summons` list.

### Dragon Souls / TFS 1.4 legacy and mixed base

Observed content:

- `data/monster`: 1,448 XML files and one Lua file, including `monsters.xml` registration data.
- `data/npc`: 166 XML NPC definitions and 231 Lua files. Most Lua files are scripts referenced by XML NPCs and must not be indexed as independent NPC definitions.
- `data/spells`: one `spells.xml` registry and 563 Lua scripts.
- `data/items/items.xml` and `items.otb` provide server item metadata.

Monster XML uses root attributes plus `health`, `look`, `targetchange`, flags, attacks, defenses, elements, immunities, summons, voices, and nested loot through `inside`. Custom files add nodes and attributes beyond a stock schema, so unknown XML must survive edits.

NPC XML stores identity, script, movement/access settings, health/mana, look, parameters, and references to behavior Lua. Editing an XML NPC and editing its behavior script are separate capabilities.

`spells.xml` registers rune/instant spell metadata and points to Lua scripts. A spell source therefore may have an XML registration source and a related Lua implementation source. The index must retain both paths.

## Differences that shape the design

| Concern | Modern Lua base | Dragon Souls base |
|---|---|---|
| Monsters | One registered Lua definition per file | XML definitions plus `monsters.xml` registry |
| NPCs | Registered Lua definitions with arbitrary callbacks | XML identity/config plus related Lua behavior scripts |
| Spells | Revscript Lua metadata and callbacks | XML registry entries plus Lua implementations |
| Safe initial edits | Literal fields in recognized table/call patterns | DOM/source-span edits to known attributes and nodes |
| Area discovery | Literal combat area only; dynamic code may be unresolved | Registry plus Lua combat area tables |
| Registration | Usually `:register()` in the definition | Separate registry for monsters/spells |

The server cannot have one global `isLua` flag. Format and capability must be recorded per source entry and per category.

## Risks

- Re-rendering Lua from a normalized model can delete callbacks and custom logic.
- Re-rendering a complete XML DOM can reorder attributes, change comments/whitespace, and drop unsupported nodes.
- A filename or case-insensitive name alone is not a unique identity; duplicate and alias definitions exist.
- Traditional NPC behavior Lua can look like content while lacking `Game.createNpcType`; indexing every `.lua` file would create false NPCs.
- A spell registration and its Lua implementation may change independently.
- Scanning thousands of files on every click would stall the UI.
- A content cache outside `WorkspaceSession` would cross-contaminate map tabs.
- Asset previews can be wrong if they retain pointers from an inactive resource session.

## Proposed architecture

Phase 1 introduces a native, UI-independent indexing layer:

```text
WorkspaceSession
  ├─ ServerWorkspace
  └─ ServerContentIndex
       ├─ ServerContentSource[]
       ├─ ServerContentCapabilities
       ├─ exact and case-folded lookup
       ├─ source fingerprints
       └─ scan diagnostics
```

`ServerContentSource` records category, source format, exact name, declaration path, optional registration path, optional related script path, registration status, and a fingerprint. Lookups return all matches so the caller can report ambiguity.

The Phase 1 indexer will:

1. use only directories derived from the selected `ServerWorkspace`;
2. enumerate XML/Lua candidates with limits and deterministic ordering;
3. identify definitions by structure, not extension alone;
4. parse XML read-only with pugixml;
5. use a small Lua lexer for known declaration calls while ignoring comments and unrelated strings;
6. reuse unchanged indexed files by fingerprint on rescan;
7. expose per-category XML/Lua capabilities and mixed-format status;
8. remain owned by `WorkspaceSession`, so `EditorResourceSession::swapWithGlobals` isolates it with the map tab.

Later providers will sit above the index:

```text
ServerContentProvider
  ├─ XmlServerContentProvider
  ├─ LuaServerContentProvider
  └─ MixedServerContentProvider
```

The UI will consume normalized `MonsterDefinition`, `NpcDefinition`, and `SpellDefinition` objects while each provider retains source-specific document state and capabilities.

## Source preservation strategy

### Lua

- Tokenize before interpreting; do not use broad regular-expression replacement.
- Recognize only proven construction patterns from the selected engine profile.
- Attach source spans to recognized literal assignments and calls.
- Patch a literal token in place when the requested edit is unambiguous.
- Preserve every byte outside the edited spans, including comments, functions, unknown tables, and line endings.
- Disable a field when its value is computed or appears in ambiguous places.
- Compare the current fingerprint with the loaded fingerprint before saving.
- Stage and atomically replace with `FileSaveTransaction`.

### XML

- Parse and validate structure while retaining original bytes and spans.
- Address nodes by stable source identity and local path rather than rebuilding the document.
- Patch only known attributes/nodes; preserve unknown attributes, comments, ordering, whitespace, encoding, and line endings.
- Treat registry edits and definition edits as one multi-file transaction when both are required.
- Verify fingerprints for all participating files before committing.

## Preview strategy

- Resolve outfits, items, effects, and missiles from the active `EditorResourceSession` resources.
- Keep normalized preview inputs as IDs/values, never raw sprite pointers across tab swaps.
- Implement area geometry as a pure model with engine-profile rules and unit tests.
- Mark scripted or dynamic areas as unresolved unless a provider can prove their geometry.
- Render caster direction, affected tiles, target/range, projectile flight, and effect animation through existing NexaMap graphics services.

## Planned implementation sequence

1. **Phase 1:** content roots, format/capability detection, source metadata, per-session index, fingerprint cache, ambiguity-safe lookup, synthetic and real-base tests.
2. **Phase 2:** normalized monster identity/main/look model, source-preserving XML provider, conservative Lua literal provider, source viewer, map/palette entry points.
3. **Phase 3:** nested loot, defenses, resistances, summons, voices, validation, palette refresh.
4. **Phase 4:** normalized attack/spell and area model, effect/projectile selectors, visual preview.
5. **Phase 5:** NPC identity/look and progressively enabled behavior providers.
6. **Phase 6:** global spell registry/editor with paired registration/script sources.
7. **Phase 7:** new/clone/template workflows using explicit provider output formats.
8. **Phase 8:** file watching, compare/conflict workflow, performance polish, and documentation.

Probable components include `server_content_index`, provider/model modules per category, editor dialogs, spell-area preview widgets, source patch/transaction helpers, context-menu integration, and focused tests. Phase 1 deliberately adds no large editor window and does not modify server files.

## Phase 5 implementation

Phase 5 adds a native NPC editor for the active `WorkspaceSession`. The browser searches by name, format, or relative source path and opens registered Lua NPC definitions or XML NPC definitions without mixing resources from another map tab. The editor keeps fixed Save/Cancel controls and scrollable Main, Look, Messages, Shop, Travel, Behavior, and Source pages.

The providers expose only fields backed by unambiguous literal source spans. Saving patches those spans through `FileSaveTransaction`, validates the result, checks the original fingerprint, and preserves callbacks, comments, unknown attributes, formatting, and custom behavior outside the changed spans. Dynamic Lua behavior remains visible and read-only. XML NPC behavior scripts remain separate sources and are not rewritten while editing the definition.

New NPC creation selects a provider from the active server workspace. Modern TFS Lua output uses a registered `Game.createNpcType` definition; traditional TFS output creates an XML definition and its related Lua behavior script. Both outputs are parsed and validated before the transaction is committed.

The implementation is covered by synthetic preservation and creation tests plus discovery/opening checks against the TFS 1.8 8.60 Lua base, the configured Crystal/Canary datapack, and the Dragon Souls XML/Lua base. Menu, palette, and map context actions rescan the workspace and refresh the creature palette after a successful save.

## Phase 8 implementation

### External source changes

Monster, NPC, and Spell editors now keep lightweight fingerprints for every declaration, registration, and related implementation file that was loaded. A timer checks those exact files while the editor is open. When another program changes or deletes one of them, NexaMap pauses autosave, keeps the edited buffer in memory, and exposes a side-by-side comparison between the loaded and disk versions. The user can keep inspecting the current buffer or close the editor and reopen the browser to load the new source. NexaMap never silently overwrites an external change.

### Workspace and editor performance

- Selecting or restoring a Server Workspace no longer builds the full content index on the UI-critical configuration path. The index is created only when a content browser or editor needs it.
- Ordinary Monster, NPC, and Spell saves refresh only their declaration, registration, and related implementation paths. They no longer recursively rescan the server tree.
- Parsed file records, the assembled source snapshot, spell-area metadata, visual constants, and vocation metadata are shared per `WorkspaceSession`. An unchanged rescan performs no definition reads or parses and reuses the immutable assembled snapshot.
- Content lookup uses exact and case-folded indexes. The chooser uses a virtual list, precomputed search keys, and a debounced filter.
- Editor callbacks synchronize only the changed field. Autosave and visual previews are independently debounced, and nonvisual edits do not rebuild the preview.
- Canary/Crystal effect and projectile appearances are materialized only when first requested by a preview.

Deterministic counters in `ServerContentScanStats`, `WorkspaceMetadataCacheStats`, and `GraphicManager` cover full scans, targeted refreshes, reads, parses, shared cache records, metadata builds, deferred visuals, and materialized visuals. Regression tests assert zero file reads/parses for unchanged content and one parse for a one-file targeted refresh.

## Reusable spell-area creation

The global Spell Editor and Monster attack editor expose a visual reusable-area creator. It discovers existing area libraries from the active workspace, including TFS `data/scripts/lib/spell_lib.lua`, traditional `data/spells/lib/spells.lua`, and Canary/Crystal `data/scripts/lib/register_spells.lua` layouts. The user draws affected tiles, chooses exactly one caster/target center, and selects the real destination library.

Before writing, NexaMap validates the `AREA_*` identifier and matrix, checks the original fingerprint, rejects duplicate names across the active workspace, appends using the source file's line endings, parses the generated definition again, and compares the resolved tiles with the drawn shape. The transaction preserves every existing function and definition byte-for-byte. The new constant is immediately refreshed in the active workspace metadata and can be selected by compatible registered spell sources. Inline TFS monster attacks continue to use the engine-supported radius, ring, length, and spread fields; NexaMap does not inject an ignored custom area field into that registration API.

## Custom TFS appearances profile

NexaMap also detects a structural custom-TFS layout in which all three item resources have distinct roles:

- `items.otb` is a bounded little-endian `uint32 serverId, uint32 clientId` pair table;
- `appearances.dat` contains protobuf appearance and item-property data;
- `items.xml` applies server-side item customizations.

This profile uses the appearances asset loader while retaining the TFS content providers and ServerID map semantics. The pair table is validated before the profile is selected, then appearances are remapped from ClientID to ServerID before `items.xml` is applied. A normal OTB plus an unrelated `appearances.dat` remains a normal TFS workspace.

## Final phase status

- [x] Phase 8a: debounced autosave and visible save/error state
- [x] Phase 8b: external file monitoring and compare/conflict workflow
- [x] Phase 8c: performance cleanup, deterministic counters, regression coverage, formatting, and documentation

Phase 7b clone/templates remains a separate future workflow and is not required for the completed Phase 8 safety and performance pass.
