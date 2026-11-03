/*
 * Open X-Com Companion — packed-v1 terrain blob encoder (M1-17N).
 */
#include "CompanionSnapshotTerrainPacked.h"
#include <stdexcept>

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

namespace
{

const unsigned char MAGIC[4] = { 'O', 'X', 'T', '1' };
const unsigned char FORMAT_VERSION = 1;

const int DISC_WEST = 1;
const int DISC_NORTH = 2;
const int DISC_BODY = 4;

const unsigned char TF_IN_LOS = 1;
const unsigned char TF_WEST_SECTION = 2;
const unsigned char TF_NORTH_SECTION = 4;

const unsigned char BF_AIR = 1;
const unsigned char BF_NO_FLOOR = 2;
const unsigned char BF_HAS_WALK_F = 4;
const unsigned char BF_HAS_WALK_O = 8;
const unsigned char BF_BLOCK_SIGHT_F = 16;
const unsigned char BF_BLOCK_SIGHT_O = 32;

void appendU16LE(std::vector<unsigned char> &out, int value)
{
	if (value < 0 || value > 0xffff)
	{
		throw std::runtime_error("packed-v1: u16 out of range");
	}
	out.push_back(static_cast<unsigned char>(value & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

void appendU32LE(std::vector<unsigned char> &out, unsigned int value)
{
	out.push_back(static_cast<unsigned char>(value & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
	out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

void requireWalkTu(int walk, const char *label)
{
	if (walk < 0 || walk > 255)
	{
		throw std::runtime_error(label);
	}
}

void requireElev(int elev)
{
	if (elev < -128 || elev > 127)
	{
		throw std::runtime_error("packed-v1: elev out of supported range");
	}
}

void appendBodySection(std::vector<unsigned char> &out, const TerrainBodyExport &body)
{
	if (body.air)
	{
		out.push_back(BF_AIR | BF_NO_FLOOR);
		return;
	}

	unsigned char bodyFlags = 0;
	if (body.noFloor)
	{
		bodyFlags |= BF_NO_FLOOR;
	}
	if (body.hasWalkF)
	{
		bodyFlags |= BF_HAS_WALK_F;
	}
	if (body.hasWalkO)
	{
		bodyFlags |= BF_HAS_WALK_O;
	}
	if (body.blockSightF)
	{
		if (!body.hasWalkF)
		{
			throw std::runtime_error("packed-v1: blockSightF without walkF");
		}
		bodyFlags |= BF_BLOCK_SIGHT_F;
	}
	if (body.blockSightO)
	{
		if (!body.hasWalkO)
		{
			throw std::runtime_error("packed-v1: blockSightO without walkO");
		}
		bodyFlags |= BF_BLOCK_SIGHT_O;
	}

	out.push_back(bodyFlags);
	requireElev(body.elev);
	out.push_back(static_cast<unsigned char>(static_cast<signed char>(body.elev)));
	if (body.hasWalkF)
	{
		requireWalkTu(body.walkF, "packed-v1: walkF out of range");
		out.push_back(static_cast<unsigned char>(body.walkF));
	}
	if (body.hasWalkO)
	{
		requireWalkTu(body.walkO, "packed-v1: walkO out of range");
		out.push_back(static_cast<unsigned char>(body.walkO));
	}
}

void appendWallSection(std::vector<unsigned char> &out, const TerrainWallExport &wall)
{
	requireWalkTu(wall.walk, "packed-v1: wall walk out of range");
	out.push_back(static_cast<unsigned char>(wall.walk));
	unsigned char wallFlags = wall.blockSight ? 1 : 0;
	out.push_back(wallFlags);
}

void appendTileRecord(std::vector<unsigned char> &out, const TerrainTileExport &tile,
	int mapSizeX, int mapSizeY, int mapSizeZ)
{
	if (tile.x < 0 || tile.x >= mapSizeX || tile.y < 0 || tile.y >= mapSizeY || tile.z < 0 || tile.z >= mapSizeZ)
	{
		throw std::runtime_error("packed-v1: tile position out of map bounds");
	}
	if (tile.disc < 1 || tile.disc > 7)
	{
		throw std::runtime_error("packed-v1: invalid disc bitmask");
	}
	if (tile.hasWestSection && (tile.disc & DISC_WEST) == 0)
	{
		throw std::runtime_error("packed-v1: west section without discovery");
	}
	if (tile.hasNorthSection && (tile.disc & DISC_NORTH) == 0)
	{
		throw std::runtime_error("packed-v1: north section without discovery");
	}
	if ((tile.disc & DISC_BODY) != 0 && !tile.hasBody)
	{
		throw std::runtime_error("packed-v1: body discovery without body payload");
	}

	unsigned char tileFlags = 0;
	if (tile.inLos)
	{
		tileFlags |= TF_IN_LOS;
	}
	if (tile.hasWestSection)
	{
		tileFlags |= TF_WEST_SECTION;
	}
	if (tile.hasNorthSection)
	{
		tileFlags |= TF_NORTH_SECTION;
	}

	const size_t start = out.size();
	out.push_back(static_cast<unsigned char>(tile.x));
	out.push_back(static_cast<unsigned char>(tile.y));
	out.push_back(static_cast<unsigned char>(tile.z));
	out.push_back(static_cast<unsigned char>(tile.disc));
	out.push_back(tileFlags);

	if ((tile.disc & DISC_BODY) != 0)
	{
		appendBodySection(out, tile.body);
	}
	if (tile.hasWestSection)
	{
		appendWallSection(out, tile.west);
	}
	if (tile.hasNorthSection)
	{
		appendWallSection(out, tile.north);
	}

	if (out.size() - start > PACKED_V1_MAX_TILE_RECORD_BYTES)
	{
		throw std::runtime_error("packed-v1: tile record exceeds maximum size");
	}
}

} // namespace

std::vector<unsigned char> encodePackedV1Blob(int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::vector<TerrainTileExport> &tiles)
{
	if (mapSizeX <= 0 || mapSizeY <= 0 || mapSizeZ <= 0)
	{
		throw std::runtime_error("packed-v1: invalid map dimensions");
	}
	if (mapSizeX > 255 || mapSizeY > 255 || mapSizeZ > 255)
	{
		throw std::runtime_error("packed-v1: map dimensions exceed u8 tile coordinate range");
	}

	std::vector<unsigned char> out;
	out.reserve(PACKED_V1_HEADER_BYTES + tiles.size() * PACKED_V1_MAX_TILE_RECORD_BYTES);
	out.insert(out.end(), MAGIC, MAGIC + 4);
	out.push_back(FORMAT_VERSION);
	out.push_back(0); // header flags
	appendU16LE(out, 0); // headerReserved
	appendU16LE(out, mapSizeX);
	appendU16LE(out, mapSizeY);
	out.push_back(static_cast<unsigned char>(mapSizeZ));
	out.push_back(0); // blobReserved
	appendU32LE(out, static_cast<unsigned int>(tiles.size()));

	int prevSortKey = -1;
	for (std::vector<TerrainTileExport>::const_iterator i = tiles.begin(); i != tiles.end(); ++i)
	{
		const int sortKey = i->z * mapSizeX * mapSizeY + i->y * mapSizeX + i->x;
		if (prevSortKey >= 0 && sortKey < prevSortKey)
		{
			throw std::runtime_error("packed-v1: tiles must be sorted z, y, x");
		}
		prevSortKey = sortKey;
		appendTileRecord(out, *i, mapSizeX, mapSizeY, mapSizeZ);
	}

	return out;
}

std::string encodeCanonicalBase64(const std::vector<unsigned char> &data)
{
	static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string encoded;
	encoded.reserve(((data.size() + 2) / 3) * 4);

	for (size_t i = 0; i < data.size(); i += 3)
	{
		const unsigned int b0 = data[i];
		const unsigned int b1 = (i + 1 < data.size()) ? data[i + 1] : 0;
		const unsigned int b2 = (i + 2 < data.size()) ? data[i + 2] : 0;
		const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;

		encoded.push_back(alphabet[(triple >> 18) & 0x3f]);
		encoded.push_back(alphabet[(triple >> 12) & 0x3f]);
		if (i + 1 < data.size())
		{
			encoded.push_back(alphabet[(triple >> 6) & 0x3f]);
		}
		else
		{
			encoded.push_back('=');
		}
		if (i + 2 < data.size())
		{
			encoded.push_back(alphabet[triple & 0x3f]);
		}
		else
		{
			encoded.push_back('=');
		}
	}

	return encoded;
}

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
