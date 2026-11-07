# NexaMap MCP server

NexaMap includes a native Model Context Protocol endpoint for AI clients. Open **Tools > MCP Server** to enable it, choose the loopback port, watch the log, and copy a client configuration.

The endpoint is `http://127.0.0.1:7331/mcp` by default. It implements JSON-RPC over HTTP and the MCP `initialize`, `ping`, `tools/list`, and `tools/call` methods. Every request needs a bearer token; see [Client configuration](#client-configuration).

Start an AI workflow with these read-only tools:

- `workspace_info` and `client_info` identify the active resource session and ID mode.
- `map_info`, `editor_state`, and `selection_get` describe the current editing context.
- `item_info`, `item_flags`, and `item_search` resolve items against the active client assets.
- `brush_search` discovers semantic editor brushes.
- `generation_guide` explains the safe generation workflow for the current context.

[mcp-tools.md](mcp-tools.md) lists every tool. [mcp-security.md](mcp-security.md) describes the security model.

The server is disabled by default. Its enabled state and port are persisted in NexaMap settings. **Allow write operations** is not persisted: it is off on every launch, including when the server starts automatically.

## Client configuration

NexaMap creates a random access token the first time the server or the MCP panel needs it. It stays the same across launches. Clients must send it on every request:

```
Authorization: Bearer <token>
```

Requests also need `Content-Type: application/json` and a `Host` of `127.0.0.1:<port>`, `localhost:<port>` or `[::1]:<port>`, which ordinary HTTP clients send when given the endpoint URL.

Use the panel's buttons to get the exact values:

- **Copy JSON Config** copies a configuration that already contains the token.
- **Copy Access Token** copies only the token.
- **Copy Codex Command** copies a command that reads the token from an environment variable.

Treat all three like passwords. For Codex, set the variable, then run the command:

```powershell
$env:NEXAMAP_MCP_TOKEN = "<token>"
codex mcp add nexamap --url http://127.0.0.1:7331/mcp --bearer-token-env-var NEXAMAP_MCP_TOKEN
```

A typical HTTP-capable MCP client uses:

```json
{
  "mcpServers": {
    "nexamap": {
      "type": "http",
      "url": "http://127.0.0.1:7331/mcp",
      "headers": {
        "Authorization": "Bearer <token>"
      }
    }
  }
}
```

The editor must remain open while a client uses its tools.

## Upgrading existing clients

This is a breaking change for configurations made before the token existed. A client without the token now gets `401 Unauthorized`.

1. Copy the configuration again from the panel.
2. Turn on **Allow write operations** each session if the client needs to write. The old saved setting is ignored.
3. Pass `mapSessionId` from `map_info` on every map-changing call, not just some of them.
4. Expect a dialog in NexaMap for destructive operations. `confirm=true` is still required but no longer sufficient.
5. Call `export_minimap` with a file name only. The `directory` argument is gone.

[mcp-security.md](mcp-security.md) lists the other behaviour changes.
