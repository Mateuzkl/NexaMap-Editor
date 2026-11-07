# MCP security model

The NexaMap MCP server is local and opt-in. It treats every client as potentially steered by text it has read, so permission decisions stay with the user in the NexaMap window.

## Access

- The server binds only to the loopback interface, never to the LAN.
- Every request must carry `Authorization: Bearer <token>`. The server compares the token in constant time and refuses to start without one.
- The token is 32 bytes from the system cryptographic random generator, created once per install and stored in `mcp-token` in NexaMap's local data directory (`~/.local/share/nexamap/data/` on Linux).
- On Linux and other POSIX systems the file is created with mode `0600`. A symlink is refused, and a file that other users could read is replaced with a new token. On Windows the file relies on the permissions of the data directory, so it has no equivalent of `0600`, and an in-directory installation inherits whatever its folder allows.
- To rotate the token, close NexaMap, delete `mcp-token`, and copy the new configuration from the panel.
- Browser pages are refused by default. Any request with an `Origin` header gets `403`, unless the user ticks **Allow browser pages on this computer (localhost)**. That option lasts until NexaMap closes, admits only `localhost`, `127.0.0.1` and `[::1]` origins, and still requires the token.
- `Host` must name the loopback address and the server's port, which blocks DNS rebinding.
- `Content-Type` must be `application/json`, so a page cannot send a request without a CORS preflight.

## Limits

- HTTP headers are limited to 64 KiB and request bodies to 1 MiB.
- JSON may nest at most 64 levels, and `id` must be a string, number or null.
- A batch may hold at most 32 entries. A response may reach at most 32 MiB; entries past that limit return an error and do not run.
- Reading a request has a 10 second deadline for the whole request, and sending a response has a 30 second deadline. GUI-thread calls time out after 10 seconds, and a batch stops starting entries after 3 minutes.
- Four workers serve connections, so one slow client cannot block the others. Stopping the server ends all of these waits within a fraction of a second.

## Thread and dialog safety

- Editor and resource access is marshaled onto the GUI thread.
- While any modal dialog is open in NexaMap, calls that need the GUI thread are refused. This includes the generator dialog and the confirmation dialogs below, so a client has to wait for the user to close them.
- A GUI call that times out reports one of two errors. `not applied` means it never started. `outcome unknown` means it was already running and may still take effect, so inspect the map before retrying.

## Writes

- Tools that modify editor state are refused unless **Allow write operations** is enabled. It is off on every launch and is visible in the MCP panel.
- This covers tools that change no map data but change editor state: `reference_capture`, `reference_clear`, `target_capture`, `target_clear`, `region_copy` and `generator_open`.
- Every mutating call must pass the current `mapSessionId` from `map_info`, so a write cannot land on another tab. `map_new` and `map_open` are exempt because they act on no existing map, and `reference_clear` and `target_clear` because they only clear stored captures.
- `brush_apply` accepts ground, wall, carpet, table, doodad and raw brushes. Other kinds need parameters this path does not supply. Use `spawn_manage`, `house_manage` and the other entity tools instead.

## Confirmation in the NexaMap window

A client-supplied flag cannot authorize a destructive operation, because a client can be told to set it by text inside a map. The user answers a native dialog in NexaMap, and the default answer is No. If nobody answers within 2 minutes the call fails, and a later answer has no effect.

A dialog appears for:

- `map_new`, `map_open`, `map_import`, `map_properties_update` and `export_minimap`.
- Deletes through `selection_op`, `house_manage`, `town_manage`, `waypoint_manage`, `zone_manage` and `spawn_manage`.
- `editor_action` undo when the newest edit was not made by an MCP map tool. `brush_apply` records its edits the same way a manual brush stroke does, so undoing one also asks.
- `allowExpansion=true` while tiles are selected, `preserveProtected=false`, and `allowReferenceSourceOverwrite=true`.

`confirm=true` is still required where the tool's schema offers it.

## Files

- `export_minimap` writes only to `mcp-exports` inside NexaMap's local data directory. It refuses a symlinked folder, refuses any target that already exists, including a symlink, and accepts only a plain name of letters, digits, `.`, `_` and `-`.
- Image minimap exports through MCP are limited to 2048 tiles across on a floor, the same limit as selected-area exports. Use the `otmm` format or `selected-area` mode on larger maps.
- `map_from_bitmap` accepts PNG and BMP files up to 64 MiB, and reads the size from the file header before decoding. Other image formats are refused.
- There is no general file-read tool and no map-save tool. `map_open`, `map_import` and `map_from_bitmap` read existing files only, and on Windows they refuse UNC network paths.
- Tool results return file names, not full paths.

## Untrusted map text

Names, descriptions and item text come from the map file and may contain instructions aimed at an AI client. Results from tools that return such text carry an `untrustedMapText` note, and the `initialize` instructions tell clients to treat that text as data.

## Monitoring

Only enable writes while using a trusted local MCP client, and disable the server when it is not needed.

The panel log shows server start and stop, permission changes, and rejected requests, at most one rejection per second. The log is only collected while the panel is open and is not saved. Successful requests are not logged, so a client holding a valid token leaves no trace there. If you suspect a leak, rotate the token.
