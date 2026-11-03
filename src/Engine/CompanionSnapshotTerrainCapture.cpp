/*
 * Open X-Com Companion — shared terrain capture for verbose and packed-v1 export.
 */
#include "CompanionSnapshotTerrainCapture.h"
#include "../Savegame/Tile.h"
#include "../Mod/MapData.h"

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

namespace
{

const int DISC_WEST = 1;
const int DISC_NORTH = 2;
const int DISC_BODY = 4;

int discoveryMask(const Tile *tile)
{
	int disc = 0;
	if (tile->isDiscovered(0))
	{
		disc |= DISC_WEST;
	}
	if (tile->isDiscovered(1))
	{
		disc |= DISC_NORTH;
	}
	if (tile->isDiscovered(2))
	{
		disc |= DISC_BODY;
	}
	return disc;
}

bool blockSight(const MapData *md)
{
	return md != 0 && md->getBlock(DT_NONE) != 0;
}

int walkTUCost(const Tile *tile, TilePart part, bool inLos)
{
	const MapData *md = tile->getMapData(part);
	if (md == 0)
	{
		return -1;
	}
	(void)inLos;
	return tile->getTUCost(part, MT_WALK);
}

void captureBody(const Tile *tile, bool inLos, TerrainBodyExport &body)
{
	const bool openAir = tile->isVoid();
	if (openAir)
	{
		body.air = true;
		body.noFloor = true;
		body.elev = 0;
		body.hasWalkF = false;
		body.walkF = 0;
		body.blockSightF = false;
		body.hasWalkO = false;
		body.walkO = 0;
		body.blockSightO = false;
		return;
	}

	body.air = false;
	body.noFloor = tile->hasNoFloor(0);
	body.elev = tile->getTerrainLevel();
	body.hasWalkF = false;
	body.walkF = 0;
	body.blockSightF = false;
	body.hasWalkO = false;
	body.walkO = 0;
	body.blockSightO = false;

	const int walkFloor = walkTUCost(tile, O_FLOOR, inLos);
	const MapData *floorMd = tile->getMapData(O_FLOOR);
	if (walkFloor >= 0)
	{
		body.hasWalkF = true;
		body.walkF = walkFloor;
		body.blockSightF = blockSight(floorMd);
	}

	const MapData *objectMd = tile->getMapData(O_OBJECT);
	if (objectMd != 0)
	{
		const int walkObject = walkTUCost(tile, O_OBJECT, inLos);
		if (walkObject >= 0)
		{
			body.hasWalkO = true;
			body.walkO = walkObject;
			body.blockSightO = blockSight(objectMd);
		}
	}
}

bool captureWall(const Tile *tile, TilePart part, bool inLos, TerrainWallExport &wall)
{
	const MapData *md = tile->getMapData(part);
	if (md == 0)
	{
		return false;
	}
	const int walk = walkTUCost(tile, part, inLos);
	if (walk < 0)
	{
		return false;
	}
	wall.walk = walk;
	wall.blockSight = blockSight(md);
	return true;
}

void captureTileRecord(const Tile *tile, TerrainTileExport &out)
{
	out.x = tile->getPosition().x;
	out.y = tile->getPosition().y;
	out.z = tile->getPosition().z;
	out.disc = discoveryMask(tile);
	out.inLos = tile->getVisible() > 0;
	out.hasBody = (out.disc & DISC_BODY) != 0;
	out.hasWestSection = false;
	out.hasNorthSection = false;

	if (out.hasBody)
	{
		captureBody(tile, out.inLos, out.body);
	}
	if ((out.disc & DISC_WEST) != 0)
	{
		out.hasWestSection = captureWall(tile, O_WESTWALL, out.inLos, out.west);
	}
	if ((out.disc & DISC_NORTH) != 0)
	{
		out.hasNorthSection = captureWall(tile, O_NORTHWALL, out.inLos, out.north);
	}
}

} // namespace

void captureTerrainTiles(int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	std::vector<TerrainTileExport> &outTiles)
{
	outTiles.clear();
	if (mapSizeX <= 0 || mapSizeY <= 0 || mapSizeZ <= 0)
	{
		return;
	}
	if (mapSizeX > 255 || mapSizeY > 255 || mapSizeZ > 255)
	{
		return;
	}

	outTiles.reserve(static_cast<size_t>(mapSizeX) * static_cast<size_t>(mapSizeY) * static_cast<size_t>(mapSizeZ));

	for (int z = 0; z < mapSizeZ; ++z)
	{
		for (int y = 0; y < mapSizeY; ++y)
		{
			for (int x = 0; x < mapSizeX; ++x)
			{
				Tile *tile = getTile(x, y, z);
				if (tile == 0)
				{
					continue;
				}
				const int disc = discoveryMask(tile);
				if (disc == 0)
				{
					continue;
				}
				TerrainTileExport record;
				captureTileRecord(tile, record);
				outTiles.push_back(record);
			}
		}
	}
}

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
