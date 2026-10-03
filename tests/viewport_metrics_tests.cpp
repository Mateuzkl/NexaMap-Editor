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

#include "viewport_metrics.h"

#include <iostream>
#include <cmath>

namespace {
	int failures = 0;

	void check(bool condition, const char* message) {
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++failures;
		}
	}

	void checkEqual(int actual, int expected, const char* message) {
		if (actual != expected) {
			std::cerr << "FAILED: " << message << " (expected " << expected << ", got " << actual << ")\n";
			++failures;
		}
	}
}

int main() {
	// 1. 100% zoom (zoom = 1.0)
	{
		// Map 1000px wide (e.g. 1000 tiles of 1px or 31.25 tiles of 32px), canvas 200px, scale 1.0, zoom 1.0
		const auto m = ViewportMetrics::Compute(100, 100, 10, 200, 150, 1.0, 1.0);
		checkEqual(m.mapWidthPixels, 1000, "100% zoom map width");
		checkEqual(m.mapHeightPixels, 1000, "100% zoom map height");
		checkEqual(m.viewportWidthPixels, 200, "100% zoom visible width");
		checkEqual(m.viewportHeightPixels, 150, "100% zoom visible height");
		checkEqual(m.maxScrollX, 800, "100% zoom maxScrollX");
		checkEqual(m.maxScrollY, 850, "100% zoom maxScrollY");
		checkEqual(m.thumbX, 200, "100% zoom thumbX");
		checkEqual(m.thumbY, 150, "100% zoom thumbY");
		// Ensure max reachable scroll + visible width exactly equals map width
		checkEqual(m.maxScrollX + m.viewportWidthPixels, m.mapWidthPixels, "100% zoom scroll end aligns with map edge");
	}

	// 2. 50% zoom (zoom = 2.0)
	{
		const auto m = ViewportMetrics::Compute(100, 100, 10, 200, 150, 1.0, 2.0);
		checkEqual(m.viewportWidthPixels, 400, "50% zoom visible width");
		checkEqual(m.viewportHeightPixels, 300, "50% zoom visible height");
		checkEqual(m.maxScrollX, 600, "50% zoom maxScrollX");
		checkEqual(m.maxScrollY, 700, "50% zoom maxScrollY");
		checkEqual(m.thumbX, 400, "50% zoom thumbX");
		checkEqual(m.maxScrollX + m.viewportWidthPixels, m.mapWidthPixels, "50% zoom scroll end aligns with map edge");
	}

	// 3. 35% zoom (~2.857142857)
	{
		const double zoom35 = 100.0 / 35.0; // ~2.857142857
		const auto m = ViewportMetrics::Compute(2048, 2048, 32, 1280, 720, 1.0, zoom35);
		const int expectedViewW = static_cast<int>(std::round(1280.0 * zoom35));
		const int expectedViewH = static_cast<int>(std::round(720.0 * zoom35));
		checkEqual(m.viewportWidthPixels, expectedViewW, "35% zoom visible width");
		checkEqual(m.viewportHeightPixels, expectedViewH, "35% zoom visible height");
		checkEqual(m.maxScrollX, m.mapWidthPixels - expectedViewW, "35% zoom maxScrollX reaches true edge");
		checkEqual(m.thumbX, expectedViewW, "35% zoom thumb matches visible extent");
		check(m.maxScrollX > 0, "35% zoom scrollable on large map");
		checkEqual(m.maxScrollX + m.viewportWidthPixels, m.mapWidthPixels, "35% zoom scroll end exactly matches map edge");
	}

	// 4. 25% zoom (zoom = 4.0)
	{
		const auto m = ViewportMetrics::Compute(100, 100, 10, 200, 150, 1.0, 4.0);
		checkEqual(m.viewportWidthPixels, 800, "25% zoom visible width (4x canvas)");
		checkEqual(m.viewportHeightPixels, 600, "25% zoom visible height (4x canvas)");
		checkEqual(m.maxScrollX, 200, "25% zoom maxScrollX");
		checkEqual(m.thumbX, 800, "25% zoom thumbX");
		checkEqual(m.maxScrollX + m.viewportWidthPixels, m.mapWidthPixels, "25% zoom scroll end aligns with map edge");
	}

	// 5. Whole map visible (viewport >= map)
	{
		// Map 500x500px, canvas 800x600px, zoom 1.0 -> viewport 800x600 >= 500x500
		const auto m = ViewportMetrics::Compute(50, 50, 10, 800, 600, 1.0, 1.0);
		checkEqual(m.maxScrollX, 0, "whole map visible maxScrollX is 0");
		checkEqual(m.maxScrollY, 0, "whole map visible maxScrollY is 0");
		checkEqual(m.thumbX, 500, "whole map visible thumb clamped to range");
		checkEqual(m.thumbY, 500, "whole map visible thumb clamped to range");
	}

	// 6. Window resize: thumb grows/shrinks, maxScroll adjusts
	{
		const auto smallWin = ViewportMetrics::Compute(100, 100, 32, 1000, 600, 1.0, 1.0);
		const auto largeWin = ViewportMetrics::Compute(100, 100, 32, 1500, 900, 1.0, 1.0);
		check(largeWin.thumbX > smallWin.thumbX, "larger window gives larger horizontal thumb");
		check(largeWin.thumbY > smallWin.thumbY, "larger window gives larger vertical thumb");
		check(largeWin.maxScrollX < smallWin.maxScrollX, "larger window gives smaller maxScrollX");
		check(largeWin.maxScrollY < smallWin.maxScrollY, "larger window gives smaller maxScrollY");
	}

	// 7. Center calculation (ComputeCenterScroll)
	{
		const auto m = ViewportMetrics::Compute(100, 100, 32, 1000, 800, 1.0, 1.0);
		// Center on tile 50 (x = 1600): center scroll should be 1600 - 500 = 1100
		const int centerScroll = ViewportMetrics::ComputeCenterScroll(1600, m.viewportWidthPixels, m.maxScrollX);
		checkEqual(centerScroll, 1100, "centering at tile 50");
		// Check that center of visible window is at 1600
		checkEqual(centerScroll + m.viewportWidthPixels / 2, 1600, "visible center exactly equals target pixel");

		// Near left edge: target tile 5 (x = 160): 160 - 500 = -340, clamped to 0
		const int leftEdge = ViewportMetrics::ComputeCenterScroll(160, m.viewportWidthPixels, m.maxScrollX);
		checkEqual(leftEdge, 0, "near left edge clamps to 0");

		// Near right edge: target tile 95 (x = 3040): 3040 - 500 = 2540 > maxScrollX (2200), clamped to 2200
		const int rightEdge = ViewportMetrics::ComputeCenterScroll(3040, m.viewportWidthPixels, m.maxScrollX);
		checkEqual(rightEdge, m.maxScrollX, "near right edge clamps to maxScrollX");
	}

	// 8. Cursor-anchored zoom stability (ComputeZoomedScroll)
	{
		const int currentScroll = 500;
		const int cursorX = 300; // logical cursor pixel
		const double scale = 1.0;
		const double oldZoom = 1.0;
		const double newZoom = 2.0;
		const int maxScroll = 2000;

		// Before zoom: map pixel under cursor = currentScroll + cursorX * scale * oldZoom = 500 + 300 = 800
		const int newScroll = ViewportMetrics::ComputeZoomedScroll(currentScroll, cursorX, scale, oldZoom, newZoom, maxScroll);
		// After zoom: map pixel under cursor = newScroll + cursorX * scale * newZoom = newScroll + 300 * 2.0 = newScroll + 600
		// For 800 to remain under cursor, newScroll must be 800 - 600 = 200
		checkEqual(newScroll, 200, "cursor-anchored zoom computes expected scroll");
		checkEqual(newScroll + static_cast<int>(cursorX * scale * newZoom), 800, "map pixel under cursor remains exactly stationary");
	}

	// 9. HiDPI scaling (1.0, 1.25, 1.5, 2.0)
	{
		const double scales[] = { 1.0, 1.25, 1.5, 2.0 };
		for (double scale : scales) {
			const auto m = ViewportMetrics::Compute(100, 100, 32, 1000, 800, scale, 1.0);
			const int expectedW = static_cast<int>(std::round(1000.0 * scale));
			const int expectedH = static_cast<int>(std::round(800.0 * scale));
			checkEqual(m.viewportWidthPixels, expectedW, "HiDPI viewport width");
			checkEqual(m.viewportHeightPixels, expectedH, "HiDPI viewport height");
			checkEqual(m.maxScrollX, std::max(0, m.mapWidthPixels - expectedW), "HiDPI maxScrollX");
		}
	}

	// 10. Large map (8192 x 8192 tiles)
	{
		const auto m = ViewportMetrics::Compute(8192, 8192, 32, 1920, 1080, 1.5, 1.0);
		checkEqual(m.mapWidthPixels, 262144, "8192 map width pixels");
		checkEqual(m.mapHeightPixels, 262144, "8192 map height pixels");
		check(m.maxScrollX > 0, "8192 map maxScrollX positive");
		checkEqual(m.maxScrollX + m.viewportWidthPixels, m.mapWidthPixels, "8192 map end aligns with map edge");
		check(m.thumbX > 0, "8192 map thumbX positive and valid");
	}

	std::cout << (failures == 0 ? "Viewport metrics tests passed\n" : "Viewport metrics tests failed\n");
	return failures == 0 ? 0 : 1;
}
