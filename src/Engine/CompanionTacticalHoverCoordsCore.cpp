/*
 * Open X-Com Companion — tactical hover tile coordinate label (M1).
 */
#include "CompanionTacticalHoverCoordsCore.h"
#include <cstdio>

namespace OpenXcom
{

namespace CompanionTacticalHoverCoordsCore
{

bool isMapTileInBounds(int x, int y, int z, int mapSizeX, int mapSizeY, int mapSizeZ)
{
	return x >= 0 && y >= 0 && z >= 0
		&& x < mapSizeX && y < mapSizeY && z < mapSizeZ;
}

bool shouldShowHoverTileCoords(
	bool selectorIndicatorAvailable,
	int selectorX, int selectorY, int viewLevelZ,
	int mapSizeX, int mapSizeY, int mapSizeZ)
{
	if (!selectorIndicatorAvailable)
	{
		return false;
	}
	return isMapTileInBounds(selectorX, selectorY, viewLevelZ, mapSizeX, mapSizeY, mapSizeZ);
}

void formatHoverTileCoordsLabel(int x, int y, int z, std::string *out)
{
	char buf[48];
	std::snprintf(buf, sizeof(buf), "X:%d Y:%d Z:%d", x, y, z);
	*out = buf;
}

} // namespace CompanionTacticalHoverCoordsCore

} // namespace OpenXcom
