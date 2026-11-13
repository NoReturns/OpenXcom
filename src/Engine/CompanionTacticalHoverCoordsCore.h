#pragma once
/*
 * Open X-Com Companion — tactical hover tile coordinate label (M1).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionTacticalHoverCoordsCore
{

/// Map tile indices in the Companion snapshot / MOVE command coordinate system (zero-based).
bool isMapTileInBounds(int x, int y, int z, int mapSizeX, int mapSizeY, int mapSizeZ);

/// True when the tactical selection indicator is active and the hover tile is on the map grid.
bool shouldShowHoverTileCoords(
	bool selectorIndicatorAvailable,
	int selectorX, int selectorY, int viewLevelZ,
	int mapSizeX, int mapSizeY, int mapSizeZ);

/// Compact label such as `X:45 Y:37 Z:0` using map tile indices, not screen pixels.
void formatHoverTileCoordsLabel(int x, int y, int z, std::string *out);

} // namespace CompanionTacticalHoverCoordsCore

} // namespace OpenXcom
