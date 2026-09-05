# NexaMap canonical editor data

This directory contains the version-independent, data-driven definitions used
by the classic NexaMap workflow. Item definitions do not live here: `items.otb`
and `items.xml` are loaded directly from the active Server Workspace.

The canonical item references in these XML files use ServerID. Classic DAT/SPR
sessions already index the runtime item database by ServerID. ClientID sessions
use the existing `ItemIdMapping` translation layer when canonical definitions
are introduced into that runtime.

`materials.xml` is the entry point. It includes `borders.xml`, `grounds.xml`,
`walls.xml`, `doodads.xml`, and `tilesets.xml`; those files collectively define
brushes and palettes in the existing NexaMap schema. `creatures.xml` supplies
the bundled editor creature catalog, which is then enriched by monsters and
NPCs detected in the selected server.

The initial canonical catalog was seeded from the validated 8.60 data set.
The former numeric version directories have been retired. Classic versions now
share this catalog, while `canary-crystal/` remains isolated for its genuinely
different appearances-based workflow.
