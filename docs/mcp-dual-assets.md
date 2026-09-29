# MCP item identities and client assets

MCP tools resolve every item against the active editor resource session. They do not use a process-global client version as a substitute for the map's actual session.

The response reports both the map ID mode and the asset mode:

- **Classic DAT/SPR** clients can expose server and client IDs when the loaded mapping makes both identities known.
- **Appearances** clients expose the identity available from the appearances-backed resource session.
- Unknown or ambiguous mappings remain absent instead of being fabricated.

AI clients should call `workspace_info` before resolving items, keep the returned map session and generation information with their plan, and refresh the plan when the active tab or resource session changes. Item flags include their source so a client can distinguish normalized editor data from assumptions.
