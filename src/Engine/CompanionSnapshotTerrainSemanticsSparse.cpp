/*
 * Open X-Com Companion — sparse-v1/v2 terrain semantics blob encoder (M1-WP1 / M1-WP8).
 */
#include "CompanionSnapshotTerrainSemanticsSparse.h"
#include "CompanionSnapshotTerrainSemantics.h"
#include <stdexcept>

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

namespace
{

const unsigned char MAGIC[4] = { 'O', 'X', 'T', 'S' };
const unsigned char FORMAT_VERSION_V1 = 1;
const unsigned char FORMAT_VERSION_V2 = 2;

const int DISC_WEST = 1;
const int DISC_NORTH = 2;
const int DISC_BODY = 4;

const unsigned char PART_BODY = 1;
const unsigned char PART_WEST = 2;
const unsigned char PART_NORTH = 4;

const unsigned char META_IN_LOS = 1;
const unsigned char META_BODY_DOOR_OPEN = 2;
const unsigned char META_WEST_DOOR_OPEN = 4;
const unsigned char META_NORTH_DOOR_OPEN = 8;
const unsigned char META_FLOOR_GRAV_LIFT = 16;

void appendU32LE(std::vector<unsigned char> &out, unsigned int value)
{
	out.push_back(static_cast<unsigned char>(value & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

bool shouldEmitCategory(unsigned char category)
{
	return category >= SEM_ORDINARY;
}

bool buildRecordBytesV1(const TerrainTileExport &tile, std::vector<unsigned char> &recordOut)
{
	recordOut.clear();
	if (!tile.inLos)
	{
		return false;
	}
	unsigned char partFlags = 0;
	unsigned char bodyCat = 0;
	unsigned char westCat = 0;
	unsigned char northCat = 0;

	if ((tile.disc & DISC_BODY) != 0 && tile.hasBody)
	{
		bodyCat = tile.bodySemantic;
		if (shouldEmitCategory(bodyCat))
		{
			partFlags |= PART_BODY;
		}
	}
	if (tile.hasWestSection)
	{
		westCat = tile.westSemantic;
		if (shouldEmitCategory(westCat))
		{
			partFlags |= PART_WEST;
		}
	}
	if (tile.hasNorthSection)
	{
		northCat = tile.northSemantic;
		if (shouldEmitCategory(northCat))
		{
			partFlags |= PART_NORTH;
		}
	}

	if (partFlags == 0)
	{
	 return false;
	}

	recordOut.push_back(static_cast<unsigned char>(tile.x));
	recordOut.push_back(static_cast<unsigned char>(tile.y));
	recordOut.push_back(static_cast<unsigned char>(tile.z));
	recordOut.push_back(partFlags);
	if (partFlags & PART_BODY)
	{
		recordOut.push_back(bodyCat);
	}
	if (partFlags & PART_WEST)
	{
		recordOut.push_back(westCat);
	}
	if (partFlags & PART_NORTH)
	{
		recordOut.push_back(northCat);
	}

	if (recordOut.size() > SEMANTICS_SPARSE_V1_MAX_RECORD_BYTES)
	{
		throw std::runtime_error("sparse-v1: semantics record exceeds maximum size");
	}
	return true;
}

bool buildRecordBytesV2(const TerrainTileExport &tile, std::vector<unsigned char> &recordOut)
{
	recordOut.clear();
	unsigned char partFlags = 0;
	unsigned char bodyCat = 0;
	unsigned char westCat = 0;
	unsigned char northCat = 0;

	if ((tile.disc & DISC_BODY) != 0 && tile.hasBody)
	{
		bodyCat = tile.bodySemantic;
		if (shouldEmitCategory(bodyCat))
		{
			partFlags |= PART_BODY;
		}
	}
	if (tile.hasWestSection)
	{
		westCat = tile.westSemantic;
		if (shouldEmitCategory(westCat))
		{
			partFlags |= PART_WEST;
		}
	}
	if (tile.hasNorthSection)
	{
		northCat = tile.northSemantic;
		if (shouldEmitCategory(northCat))
		{
			partFlags |= PART_NORTH;
		}
	}

	if (partFlags == 0)
	{
		return false;
	}

	recordOut.push_back(static_cast<unsigned char>(tile.x));
	recordOut.push_back(static_cast<unsigned char>(tile.y));
	recordOut.push_back(static_cast<unsigned char>(tile.z));
	recordOut.push_back(partFlags);
	if (partFlags & PART_BODY)
	{
		recordOut.push_back(bodyCat);
	}
	if (partFlags & PART_WEST)
	{
		recordOut.push_back(westCat);
	}
	if (partFlags & PART_NORTH)
	{
		recordOut.push_back(northCat);
	}

	unsigned char meta = 0;
	if (tile.bodyFloorGravLift)
	{
		meta |= META_FLOOR_GRAV_LIFT;
	}
	if (tile.inLos)
	{
		meta |= META_IN_LOS;
		if (tile.bodyDoorStateKnown && tile.bodyDoorOpen)
		{
			meta |= META_BODY_DOOR_OPEN;
		}
		if (tile.westDoorStateKnown && tile.westDoorOpen)
		{
			meta |= META_WEST_DOOR_OPEN;
		}
		if (tile.northDoorStateKnown && tile.northDoorOpen)
		{
			meta |= META_NORTH_DOOR_OPEN;
		}
	}
	recordOut.push_back(meta);

	if (recordOut.size() > SEMANTICS_SPARSE_V2_MAX_RECORD_BYTES)
	{
		throw std::runtime_error("sparse-v2: semantics record exceeds maximum size");
	}
	return true;
}

std::vector<unsigned char> encodeBlob(unsigned char formatVersion,
	const std::vector<TerrainTileExport> &tiles,
	bool (*buildRecord)(const TerrainTileExport &, std::vector<unsigned char> &))
{
	std::vector<unsigned char> records;
	records.reserve(tiles.size() * SEMANTICS_SPARSE_V2_MAX_RECORD_BYTES);
	unsigned int semanticTileCount = 0;
	std::vector<unsigned char> oneRecord;

	for (std::vector<TerrainTileExport>::const_iterator i = tiles.begin(); i != tiles.end(); ++i)
	{
		if (buildRecord(*i, oneRecord))
		{
			++semanticTileCount;
			records.insert(records.end(), oneRecord.begin(), oneRecord.end());
		}
	}

	std::vector<unsigned char> out;
	out.reserve(SEMANTICS_SPARSE_V1_HEADER_BYTES + records.size());
	out.insert(out.end(), MAGIC, MAGIC + 4);
	out.push_back(formatVersion);
	out.push_back(0);
	out.push_back(0);
	out.push_back(0);
	appendU32LE(out, semanticTileCount);
	out.insert(out.end(), records.begin(), records.end());
	return out;
}

} // namespace

std::vector<unsigned char> encodeSemanticsSparseV1Blob(const std::vector<TerrainTileExport> &tiles)
{
	return encodeBlob(FORMAT_VERSION_V1, tiles, buildRecordBytesV1);
}

std::vector<unsigned char> encodeSemanticsSparseV2Blob(const std::vector<TerrainTileExport> &tiles)
{
	return encodeBlob(FORMAT_VERSION_V2, tiles, buildRecordBytesV2);
}

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
