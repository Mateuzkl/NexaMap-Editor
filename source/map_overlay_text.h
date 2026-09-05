#ifndef RME_MAP_OVERLAY_TEXT_H_
#define RME_MAP_OVERLAY_TEXT_H_

#include <wx/dc.h>
#include <wx/tokenzr.h>
#include <algorithm>
#include <vector>

// Layout uses the same font/DC as rasterization. No byte/character-count limit:
// long words and UTF-8 names (converted to wxString by the caller) wrap by width.
struct MapOverlayTextLayout {
	std::vector<wxString> lines;
	int width = 1;
	int height = 1;
	int lineHeight = 1;
	size_t hiddenLines = 0;
};

inline MapOverlayTextLayout LayoutMapOverlayText(wxDC& dc, const wxString& text, int maxWidth, int maxHeight) {
	MapOverlayTextLayout result;
	maxWidth = std::max(1, maxWidth);
	result.lineHeight = std::max(1, dc.GetCharHeight() + 2);
	wxStringTokenizer paragraphs(text, "\n", wxTOKEN_RET_EMPTY_ALL);
	while (paragraphs.HasMoreTokens()) {
		wxString remaining = paragraphs.GetNextToken();
		remaining.Replace("\r", "");
		remaining.Replace("\t", "    ");
		if (remaining.empty()) {
			result.lines.emplace_back();
		}
		while (!remaining.empty()) {
			size_t length = remaining.length();
			if (dc.GetTextExtent(remaining).x > maxWidth) {
				size_t low = 1, high = length;
				while (low < high) {
					const size_t mid = (low + high + 1) / 2;
					if (dc.GetTextExtent(remaining.Left(mid)).x <= maxWidth) {
						low = mid;
					} else {
						high = mid - 1;
					}
				}
				length = low;
				const int space = remaining.Left(length).Find(' ', true);
				if (space > 0) {
					length = static_cast<size_t>(space) + 1;
				}
			}
			result.lines.push_back(remaining.Left(length));
			remaining = remaining.Mid(length);
		}
	}
	// The viewport is the only height limit. Make unavoidable overflow explicit,
	// instead of silently cutting a name/ID or drawing beyond the canvas edge.
	const size_t maxLines = std::max(1, maxHeight / result.lineHeight);
	if (result.lines.size() > maxLines) {
		result.hiddenLines = result.lines.size() - maxLines + 1;
		result.lines.resize(maxLines - 1);
		wxString footer = wxString::Format("+%zu lines (Properties)", result.hiddenLines);
		if (dc.GetTextExtent(footer).x > maxWidth) {
			footer = wxString::Format("+%zu...", result.hiddenLines);
		}
		result.lines.push_back(footer);
	}
	for (const auto& line : result.lines) {
		result.width = std::max(result.width, dc.GetTextExtent(line).x);
	}
	result.height = std::max(1, static_cast<int>(result.lines.size()) * result.lineHeight);
	return result;
}

#endif
