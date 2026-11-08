#pragma once
/*
 * Open X-Com Companion — MapData-derived terrain semantics for export (M1-WP1).
 */
#include <vector>
#include <cstddef>
#include "../Mod/MapData.h"

namespace OpenXcom
{

class MapData;
class Mod;
class SavedBattleGame;
class Tile;

namespace CompanionSnapshotTerrain
{

enum TerrainSemanticCategory : unsigned char
{
	SEM_UNKNOWN = 0,
	SEM_ORDINARY = 1,
	SEM_ORDINARY_IMPASSABLE = 2,
	SEM_UFO_STRUCTURE = 3,
	SEM_UFO_INTERIOR = 4,
	SEM_UFO_DOOR = 5,
	SEM_ORDINARY_DOOR = 6,
	SEM_ENTRANCE = 7,
	SEM_GRAV_LIFT = 8,
	SEM_XCOM_CRAFT = 9,
	SEM_XCOM_BASE_MODULE = 10
};

enum MapDataSetRole : int
{
	DATASET_ROLE_GROUND = 0,
	DATASET_ROLE_UFO = 1,
	DATASET_ROLE_CRAFT = 2
};

struct TerrainSemanticsContext
{
	std::vector<MapDataSetRole> dataSetRoles;
};

/** Build per-index roles for map datasets present in this battle (requires Mod when available). */
TerrainSemanticsContext buildSemanticsContext(SavedBattleGame *battle, Mod *mod);

TerrainSemanticCategory classifyMapDataPart(const MapData *md, TilePart part, MapDataSetRole dataSetRole);

TerrainSemanticCategory classifyTileBody(const Tile *tile, const TerrainSemanticsContext &ctx);

TerrainSemanticCategory classifyTileWall(const Tile *tile, TilePart wallPart, const TerrainSemanticsContext &ctx);

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
