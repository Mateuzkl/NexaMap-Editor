# NexaMap MCP server

NexaMap includes a native Model Context Protocol endpoint for AI clients. Open **Tools > MCP Server** to enable it, choose the loopback port, inspect logs, and copy a client configuration.

The endpoint is `http://127.0.0.1:7331/mcp` by default. It implements JSON-RPC over HTTP and the MCP `initialize`, `ping`, `tools/list`, and `tools/call` methods.

Start an AI workflow with these read-only tools:

- `workspace_info` and `client_info` identify the active resource session and ID mode.
- `map_info`, `editor_state`, and `selection_get` describe the current editing context.
- `item_info`, `item_flags`, and `item_search` resolve items against the active client assets.
- `brush_search` discovers semantic editor brushes.
- `generation_guide` explains the safe generation workflow for the current context.

The server is disabled by default. Its enabled state, port, and write permission are persisted in NexaMap settings.

## Client configuration

Use the panel's **Copy Codex Command** or **Copy JSON Config** button for the exact configuration. For Codex:

```powershell
codex mcp add nexamap --url http://127.0.0.1:7331/mcp
```

A typical HTTP-capable MCP client uses:

```json
{
  "mcpServers": {
    "nexamap": {
      "url": "http://127.0.0.1:7331/mcp"
    }
  }
}
```

The editor must remain open while a client uses its tools.
