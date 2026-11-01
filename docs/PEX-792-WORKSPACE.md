# PEX Season 7.92 workspace

The `7.92 PEX Season` profile recognizes DAT `459E7B73` and SPR `45880FE8`.
It uses the 7.8-era DAT flag reader, not the 8.6 flag reader. Extended sprite
references, alpha transparency, animation durations and outfit frame groups
continue to come from the client's `Tibia.otfi`.

The PEX item database is **OTB major 2, client/minor 6**. Its map header uses
**OTBM version 1 (zero-based), client/minor 6**; this is configured as
`<otbm version="2"/>` in NexaMap's one-based profile XML. A dedicated OTB mapping
preserves the existing generic 7.92 mapping and the existing 8.60 profiles.

## Open the existing project

1. Reopen NexaMap so it reloads `data/clients.xml`.
2. Select the actual PEX client folder: `Client-Peason/data/things/792`.
   Keep `Tibia.dat`, `Tibia.spr` and `Tibia.otfi` together.
3. Select the PEX server folder: `Pex-Season`.
4. Open `data/world/Evolutions.otbm` from that server folder.
5. Confirm the workspace identifies the client as `7.92 PEX Season`.

Do not pair the PEX client with an unrelated 8.60 server item database. The
Astra client's native 8.60 signatures remain supported by the original profile.
No DAT, SPR, OTB or OTBM header needs editing or signature-check bypasses.

This change is profile data only; rebuilding the editor is unnecessary. The
shared `data/editor` brushes and materials remain in their canonical location.
The source assets and world map are not modified by this setup.
