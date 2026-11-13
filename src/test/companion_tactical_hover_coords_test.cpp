/*
 * Open X-Com Companion — tactical hover tile coordinate label (M1).
 */
#include "Engine/CompanionTacticalHoverCoordsCore.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalHoverCoordsCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testFormatLabel()
{
	std::string label;
	formatHoverTileCoordsLabel(45, 37, 0, &label);
	expect(label == "X:45 Y:37 Z:0", "compact hover label format");
	formatHoverTileCoordsLabel(0, 0, 2, &label);
	expect(label == "X:0 Y:0 Z:2", "zero-based indices preserved");
}

static void testVisibility()
{
	const int mapX = 60, mapY = 60, mapZ = 3;
	expect(!shouldShowHoverTileCoords(false, 10, 10, 0, mapX, mapY, mapZ), "hidden when selector unavailable");
	expect(shouldShowHoverTileCoords(true, 10, 10, 0, mapX, mapY, mapZ), "shown on valid tile");
	expect(!shouldShowHoverTileCoords(true, -1, 10, 0, mapX, mapY, mapZ), "hidden outside map (negative)");
	expect(!shouldShowHoverTileCoords(true, mapX, 10, 0, mapX, mapY, mapZ), "hidden outside map (x == size)");
	expect(!shouldShowHoverTileCoords(true, 10, mapY, 0, mapX, mapY, mapZ), "hidden outside map (y == size)");
	expect(!shouldShowHoverTileCoords(true, 10, 10, mapZ, mapX, mapY, mapZ), "hidden outside map (z == size)");
	expect(isMapTileInBounds(59, 59, 2, mapX, mapY, mapZ), "in bounds corner tile");
}

int main()
{
	testFormatLabel();
	testVisibility();

	if (g_failures != 0)
	{
		std::cerr << "companion_tactical_hover_coords_test: " << g_failures << " failure(s)." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_tactical_hover_coords_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
