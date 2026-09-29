# MCP security model

The NexaMap MCP server is local and opt-in:

- It binds only to the loopback interface, never to the LAN.
- Browser origins are restricted to `localhost`, `127.0.0.1`, and `[::1]`.
- HTTP headers are limited to 64 KiB and request bodies to 1 MiB.
- Requests use bounded socket and GUI-thread waits.
- Editor and resource access is marshaled onto the GUI thread.
- Tools that modify editor state are refused unless **Allow map modifications** is explicitly enabled.
- Write permission returns to its persisted setting after restart and is visible in the MCP panel.

Only enable writes while using a trusted local MCP client. Inspect the panel log if an unexpected client connects, and disable the server when it is not needed.
