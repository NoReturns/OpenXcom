/*
 * Open X-Com Companion — sparse terrain export for tactical snapshots (M1-17B / M1-17N).
 */
#include "CompanionSnapshotTerrain.h"
#include "CompanionSnapshotTerrainCapture.h"
#include "CompanionSnapshotTerrainPacked.h"
#include "CompanionSnapshotTerrainSemantics.h"
#include "CompanionSnapshotTerrainSemanticsSparse.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Mod/Mod.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

const char *MEMORY_NOTE()
{
	return "Discovered geometry is read from live engine tiles; there is no separate historical terrain memory. "
	       "Tiles with inLos false may show changes (doors, damage) not yet re-observed by X-COM.";
}

namespace
{

const int DISC_BODY = 4;

void appendBodyYaml(YAML::Node &entry, const TerrainBodyExport &body)
{
	YAML::Node bodyNode;
	if (body.air)
	{
		bodyNode["air"] = true;
		bodyNode["noFloor"] = true;
	}
	else
	{
		bodyNode["noFloor"] = body.noFloor;
		bodyNode["elev"] = body.elev;
		if (body.hasWalkF)
		{
			bodyNode["walkF"] = body.walkF;
			if (body.blockSightF)
			{
				bodyNode["blockSightF"] = true;
			}
		}
		if (body.hasWalkO)
		{
			bodyNode["walkO"] = body.walkO;
			if (body.blockSightO)
			{
				bodyNode["blockSightO"] = true;
			}
		}
	}
	entry["body"] = bodyNode;
}

void appendWallYaml(YAML::Node &entry, const char *key, const TerrainWallExport &wall)
{
	YAML::Node wallNode;
	wallNode["walk"] = wall.walk;
	if (wall.blockSight)
	{
		wallNode["blockSight"] = true;
	}
	entry[key] = wallNode;
}

void appendSemanticsYaml(YAML::Node &terrain, const std::vector<TerrainTileExport> &records)
{
	const std::vector<unsigned char> blob = encodeSemanticsSparseV2Blob(records);
	if (blob.size() <= SEMANTICS_SPARSE_V1_HEADER_BYTES)
	{
		return;
	}
	unsigned int recordCount = 0;
	recordCount |= static_cast<unsigned int>(blob[8]);
	recordCount |= static_cast<unsigned int>(blob[9]) << 8;
	recordCount |= static_cast<unsigned int>(blob[10]) << 16;
	recordCount |= static_cast<unsigned int>(blob[11]) << 24;
	if (recordCount == 0)
	{
		return;
	}
	YAML::Node semantics;
	semantics["encoding"] = "sparse-v2";
	semantics["recordCount"] = recordCount;
	semantics["data"] = encodeCanonicalBase64(blob);
	terrain["semantics"] = semantics;
}

void appendTileSemanticsVerbose(YAML::Node &entry, const TerrainTileExport &record)
{
	if (!record.inLos)
	{
		return;
	}
	YAML::Node sem;
	bool any = false;
	if (record.bodySemantic >= SEM_ORDINARY)
	{
		sem["body"] = static_cast<int>(record.bodySemantic);
		any = true;
	}
	if (record.hasWestSection && record.westSemantic >= SEM_ORDINARY)
	{
		sem["west"] = static_cast<int>(record.westSemantic);
		any = true;
	}
	if (record.hasNorthSection && record.northSemantic >= SEM_ORDINARY)
	{
		sem["north"] = static_cast<int>(record.northSemantic);
		any = true;
	}
	if (any)
	{
		entry["sem"] = sem;
	}
}

void appendTileRecordVerbose(YAML::Node &tiles, const TerrainTileExport &record)
{
	YAML::Node entry;
	entry["p"] = YAML::Load("[" + std::to_string(record.x) + ", " + std::to_string(record.y) + ", "
		+ std::to_string(record.z) + "]");
	entry["disc"] = record.disc;
	entry["inLos"] = record.inLos;

	if ((record.disc & DISC_BODY) != 0)
	{
		appendBodyYaml(entry, record.body);
	}
	if (record.hasWestSection)
	{
		appendWallYaml(entry, "west", record.west);
	}
	if (record.hasNorthSection)
	{
		appendWallYaml(entry, "north", record.north);
	}
	appendTileSemanticsVerbose(entry, record);
	if (record.bodyFloorGravLift)
	{
		YAML::Node sm;
		sm["floorGravLift"] = true;
		entry["structureMeta"] = sm;
	}

	tiles.push_back(entry);
}

} // namespace

void appendTerrainVerbose(YAML::Node &root, int mapSizeX, int mapSizeY, int mapSizeZ,
	SavedBattleGame *battle, Mod *mod,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	bool testFullRevealGeometry)
{
	TerrainSemanticsContext semCtx = buildSemanticsContext(battle, mod);
	TerrainCaptureOptions captureOptions;
	captureOptions.semanticsContext = &semCtx;
	captureOptions.testFullRevealGeometry = testFullRevealGeometry;
	std::vector<TerrainTileExport> records;
	captureTerrainTiles(mapSizeX, mapSizeY, mapSizeZ, getTile, records, &captureOptions);

	YAML::Node terrain;
	terrain["memoryNote"] = MEMORY_NOTE();
	YAML::Node tiles = YAML::Node(YAML::NodeType::Sequence);
	for (std::vector<TerrainTileExport>::const_iterator i = records.begin(); i != records.end(); ++i)
	{
		appendTileRecordVerbose(tiles, *i);
	}
	terrain["tiles"] = tiles;
	appendSemanticsYaml(terrain, records);
	root["terrain"] = terrain;
}

void appendTerrainVerbose(YAML::Node &root, int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	bool testFullRevealGeometry)
{
	appendTerrainVerbose(root, mapSizeX, mapSizeY, mapSizeZ, 0, 0, getTile, testFullRevealGeometry);
}

void appendTerrain(YAML::Node &root, int mapSizeX, int mapSizeY, int mapSizeZ,
	SavedBattleGame *battle, Mod *mod,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	bool testFullRevealGeometry)
{
	if (mapSizeX > 255 || mapSizeY > 255 || mapSizeZ > 255)
	{
		throw std::runtime_error("packed-v1 terrain export unsupported for map dimensions above 255");
	}

	TerrainSemanticsContext semCtx = buildSemanticsContext(battle, mod);
	TerrainCaptureOptions captureOptions;
	captureOptions.semanticsContext = &semCtx;
	captureOptions.testFullRevealGeometry = testFullRevealGeometry;
	std::vector<TerrainTileExport> records;
	captureTerrainTiles(mapSizeX, mapSizeY, mapSizeZ, getTile, records, &captureOptions);
	const std::vector<unsigned char> blob = encodePackedV1Blob(mapSizeX, mapSizeY, mapSizeZ, records);
	const std::string data = encodeCanonicalBase64(blob);

	YAML::Node terrain;
	terrain["memoryNote"] = MEMORY_NOTE();
	terrain["encoding"] = "packed-v1";
	terrain["recordCount"] = static_cast<unsigned int>(records.size());
	terrain["data"] = data;
	appendSemanticsYaml(terrain, records);
	root["terrain"] = terrain;
}

void appendTerrain(YAML::Node &root, int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile,
	bool testFullRevealGeometry)
{
	appendTerrain(root, mapSizeX, mapSizeY, mapSizeZ, 0, 0, getTile, testFullRevealGeometry);
}

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
