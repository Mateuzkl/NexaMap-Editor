//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_VIEWPORT_METRICS_H_
#define RME_VIEWPORT_METRICS_H_

#include <algorithm>
#include <cmath>
#include <cstdint>

struct ViewportMetrics {
	int mapWidthPixels = 0;
	int mapHeightPixels = 0;
	int viewportWidthPixels = 0;
	int viewportHeightPixels = 0;
	int maxScrollX = 0;
	int maxScrollY = 0;
	int thumbX = 0;
	int thumbY = 0;
	int pageX = 0;
	int pageY = 0;

	static ViewportMetrics Compute(
		int mapWidthTiles,
		int mapHeightTiles,
		int tileSize,
		int canvasLogicalWidth,
		int canvasLogicalHeight,
		double contentScaleFactor,
		double zoom
	) {
		ViewportMetrics metrics;
		metrics.mapWidthPixels = std::max(0, mapWidthTiles * tileSize);
		metrics.mapHeightPixels = std::max(0, mapHeightTiles * tileSize);

		const double scale = (contentScaleFactor > 0.0) ? contentScaleFactor : 1.0;
		const double currentZoom = (zoom > 0.0) ? zoom : 1.0;

		const double viewW = std::max(1.0, static_cast<double>(canvasLogicalWidth) * scale * currentZoom);
		const double viewH = std::max(1.0, static_cast<double>(canvasLogicalHeight) * scale * currentZoom);

		metrics.viewportWidthPixels = std::max(1, static_cast<int>(std::round(viewW)));
		metrics.viewportHeightPixels = std::max(1, static_cast<int>(std::round(viewH)));

		metrics.maxScrollX = std::max(0, metrics.mapWidthPixels - metrics.viewportWidthPixels);
		metrics.maxScrollY = std::max(0, metrics.mapHeightPixels - metrics.viewportHeightPixels);

		// wxScrollBar thumb represents visible extent clamped within map range
		metrics.thumbX = std::clamp(metrics.viewportWidthPixels, 1, std::max(1, metrics.mapWidthPixels));
		metrics.thumbY = std::clamp(metrics.viewportHeightPixels, 1, std::max(1, metrics.mapHeightPixels));

		// Page size represents 90% of visible viewport
		metrics.pageX = std::max(1, metrics.thumbX * 9 / 10);
		metrics.pageY = std::max(1, metrics.thumbY * 9 / 10);

		return metrics;
	}

	static int ComputeCenterScroll(int targetPixel, int viewportPixels, int maxScroll) {
		const int targetScroll = targetPixel - (viewportPixels / 2);
		return std::clamp(targetScroll, 0, maxScroll);
	}

	static int ComputeZoomedScroll(int currentScroll, int anchorLogical, double scale, double oldZoom, double newZoom, int maxScroll) {
		const double physicalAnchor = static_cast<double>(anchorLogical) * scale;
		const double deltaZoom = newZoom - oldZoom;
		const int newScroll = static_cast<int>(std::round(static_cast<double>(currentScroll) - physicalAnchor * deltaZoom));
		return std::clamp(newScroll, 0, maxScroll);
	}
};

#endif
