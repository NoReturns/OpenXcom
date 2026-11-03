#pragma once
/*
 * Open X-Com Companion — shared terrain capture for verbose and packed-v1 export.
 */
#include <functional>
#include <vector>

namespace OpenXcom
{

class Tile;

namespace CompanionSnapshotTerrain
{

struct TerrainWallExport
{
	int walk;
	bool blockSight;
};

struct TerrainBodyExport
{
	bool air;
	bool noFloor;
	int elev;
	bool hasWalkF;
	int walkF;
	bool blockSightF;
	bool hasWalkO;
	int walkO;
	bool blockSightO;
};

struct TerrainTileExport
{
	int x;
	int y;
	int z;
	int disc;
	bool inLos;
	bool hasBody;
	TerrainBodyExport body;
	bool hasWestSection;
	TerrainWallExport west;
	bool hasNorthSection;
	TerrainWallExport north;
};

void captureTerrainTiles(int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	std::vector<TerrainTileExport> &outTiles);

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
