# Natural-language map generation

## Generate inside NexaMap

The native procedural generator already provides a prompt-like workflow without an external AI service:

1. Select the exact map area.
2. Open **Tools > Procedural Map Generator**.
3. On **Preset**, write a description in **Generation Brief**.
4. Choose **Interpret Brief** and review the structured settings.
5. Choose **Generate Preview**.
6. Review **Preview** and **Validation**.
7. Choose **Apply** only when the result is acceptable.

Apply is one undoable editor transaction and keeps protected gameplay content out of the generated mask.

## Generate through an AI agent

Open **Tools > MCP Server**, enable the endpoint, and use **Copy Codex Command** or **Copy JSON Config**. Write the natural-language request in the connected AI client's chat—not in the MCP log.

The intended agent workflow is selection inspection, active-resource discovery, plan, preview, explicit apply, render, and validation. Until the generator mutation tools are advertised by `tools/list`, use the native procedural dialog for Preview and Apply. The MCP panel remains read-only by default.
