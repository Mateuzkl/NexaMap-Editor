// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_TOOL_UTILS_H_
#define NEXAMAP_MCP_TOOL_UTILS_H_

#include "mcp_common.h"
#include "mcp_context.h"

#include "../position.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Brush;
class Item;

namespace mcp {
	struct Region {
		Position from;
		Position to;

		[[nodiscard]] size_t width() const;
		[[nodiscard]] size_t height() const;
		[[nodiscard]] size_t area() const;
	};

	[[nodiscard]] Json EmptyObjectSchema();
	[[nodiscard]] Json PositionSchema();
	[[nodiscard]] Json PositionJson(const Position& position);
	[[nodiscard]] Position ParsePosition(const Json& value, const char* fieldName = "position");
	[[nodiscard]] Region ParseRegion(const Json& arguments, size_t maximumArea = 16384);
	// Rejects a missing or stale mapSessionId; for mutating calls that do not edit map data.
	void VerifyMapSession(const EditorContext& context, const Json& arguments);
	void VerifyWritableContext(const EditorContext& context, const Json& arguments);
	void EnforceSelectionBoundary(const EditorContext& context, const std::vector<Position>& positions, bool allowExpansion);
	void EnforceSelectionBoundary(const EditorContext& context, const Region& region, bool allowExpansion);
	void EnforceReferenceTargetBoundary(const EditorContext& context, const std::vector<Position>& positions, bool allowReferenceSourceOverwrite);
	[[nodiscard]] bool IsProtectedItem(const Item& item);
	[[nodiscard]] uint16_t RequiredItemId(const Json& value);
	[[nodiscard]] Json OnGui(std::function<Json(const EditorContext&)> function);
	// Native confirmation naming the active map. Call before OnGui, never inside it: the dialog
	// outlives the GUI-call timeout, and the write that follows re-verifies mapSessionId.
	void RequireMapConfirmation(const Json& arguments, const std::string& description);
	// Native confirmation for client flags that switch a safety boundary off.
	void RequireSafetyOverrideConfirmation(const Json& arguments);
	// StructuredResult for text a map file controls, which may carry prompt injection.
	[[nodiscard]] Json UntrustedMapResult(Json value);
	[[nodiscard]] std::string LowerText(std::string value);
	[[nodiscard]] std::string BrushKind(const Brush& brush);
	[[nodiscard]] Brush* FindBrush(const std::string& name);
}

#endif
