#pragma once
/*
 * Open X-Com Companion — shared terrain capture for verbose and packed-v1 export.
 */
#include <functional>
#include <vector>
#include "CompanionSnapshotTerrainSemantics.h"

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
	unsigned char bodySemantic;
	/// True when O_FLOOR MapData has isGravLift() (movement-relevant; independent of object-first bodySemantic).
	bool bodyFloorGravLift;
	bool bodyDoorStateKnown;
	bool bodyDoorOpen;
	bool hasWestSection;
	TerrainWallExport west;
	unsigned char westSemantic;
	bool westDoorStateKnown;
	bool westDoorOpen;
	bool hasNorthSection;
	TerrainWallExport north;
	unsigned char northSemantic;
	bool northDoorStateKnown;
	bool northDoorOpen;
};

struct TerrainCaptureOptions
{
	const TerrainSemanticsContext *semanticsContext;
	/// Test harness only: emit undiscovered cells with synthetic discovery mask; does not mutate tiles.
	bool testFullRevealGeometry;
	TerrainCaptureOptions() : semanticsContext(0), testFullRevealGeometry(false) {}
};

void captureTerrainTiles(int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	std::vector<TerrainTileExport> &outTiles,
	const TerrainCaptureOptions *options = 0);

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
