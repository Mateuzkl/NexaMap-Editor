#include "ground_brush.h"
#include "wall_brush.h"
#include "carpet_brush.h"
#include "table_brush.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

uint32_t GroundBrush::border_types[256];
uint32_t WallBrush::full_border_types[16];
uint32_t WallBrush::half_border_types[16];
uint32_t CarpetBrush::carpet_types[256];
uint32_t TableBrush::table_types[256];

namespace {

	bool contains(const std::array<BorderType, 4>& directions, BorderType expected) {
		return std::find(directions.begin(), directions.end(), expected) != directions.end();
	}

	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

} // namespace

int main() {
	GroundBrush::init();

	struct CanonicalCase {
		uint8_t mask;
		BorderType expected;
	};
	const CanonicalCase canonicalCases[] = {
		{ TILE_NORTH, NORTH_HORIZONTAL },
		{ TILE_EAST, EAST_HORIZONTAL },
		{ TILE_SOUTH, SOUTH_HORIZONTAL },
		{ TILE_WEST, WEST_HORIZONTAL },
		{ TILE_NORTHWEST, NORTHWEST_CORNER },
		{ TILE_NORTHEAST, NORTHEAST_CORNER },
		{ TILE_SOUTHWEST, SOUTHWEST_CORNER },
		{ TILE_SOUTHEAST, SOUTHEAST_CORNER },
		{ TILE_NORTH | TILE_WEST, NORTHWEST_DIAGONAL },
		{ TILE_NORTH | TILE_EAST, NORTHEAST_DIAGONAL },
		{ TILE_SOUTH | TILE_WEST, SOUTHWEST_DIAGONAL },
		{ TILE_SOUTH | TILE_EAST, SOUTHEAST_DIAGONAL },
	};

	for (const auto& canonicalCase : canonicalCases) {
		require(contains(GroundBrush::classifyBorderMask(canonicalCase.mask), canonicalCase.expected), "canonical border mask mismatch");
	}

	const auto compound = GroundBrush::classifyBorderMask(TILE_NORTH | TILE_EAST | TILE_WEST);
	require(compound[0] == NORTH_HORIZONTAL, "compound north mismatch");
	require(compound[1] == WEST_HORIZONTAL, "compound west mismatch");
	require(compound[2] == EAST_HORIZONTAL, "compound east mismatch");
	require(compound[3] == BORDER_NONE, "compound terminator mismatch");

	std::cout << "border_topology_tests passed\n";
	return 0;
}
