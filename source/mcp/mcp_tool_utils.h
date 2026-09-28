// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEXAMAP_MCP_TOOL_UTILS_H_
#define NEXAMAP_MCP_TOOL_UTILS_H_

#include "mcp_common.h"
#include "mcp_context.h"

#include "../position.h"

#include <cstddef>
#include <functional>
#include <string>

class Brush;

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
	[[nodiscard]] Json OnGui(std::function<Json(const EditorContext&)> function);
	[[nodiscard]] std::string LowerText(std::string value);
	[[nodiscard]] std::string BrushKind(const Brush& brush);
	[[nodiscard]] Brush* FindBrush(const std::string& name);
}

#endif
