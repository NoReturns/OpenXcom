/*
 * Open X-Com Companion — MapData-derived terrain semantics (M1-WP1).
 */
#include "CompanionSnapshotTerrainSemantics.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Mod/Mod.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/RuleTerrain.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/RuleCraft.h"

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

namespace
{

bool dataSetInTerrain(MapDataSet *needle, RuleTerrain *terrain)
{
	if (needle == 0 || terrain == 0)
	{
		return false;
	}
	for (std::vector<MapDataSet*>::iterator i = terrain->getMapDataSets()->begin();
	     i != terrain->getMapDataSets()->end(); ++i)
	{
		if (*i == needle || (*i)->getName() == needle->getName())
		{
			return true;
		}
	}
	return false;
}

void markTerrainRole(std::vector<MapDataSetRole> &roles, SavedBattleGame *battle, RuleTerrain *terrain, MapDataSetRole role)
{
	if (battle == 0 || terrain == 0)
	{
		return;
	}
	std::vector<MapDataSet*> *sets = battle->getMapDataSets();
	for (size_t idx = 0; idx < sets->size(); ++idx)
	{
		if (dataSetInTerrain((*sets)[idx], terrain))
		{
			if (role == DATASET_ROLE_UFO || roles[idx] == DATASET_ROLE_GROUND)
			{
				roles[idx] = role;
			}
			else if (role == DATASET_ROLE_CRAFT && roles[idx] != DATASET_ROLE_UFO)
			{
				roles[idx] = role;
			}
		}
	}
}

bool isImpassableWalk(int walkTu)
{
	return walkTu < 0 || walkTu >= 255;
}

TerrainSemanticCategory classifyFromMapData(const MapData *md, TilePart part, MapDataSetRole dataSetRole)
{
	if (md == 0)
	{
		return SEM_UNKNOWN;
	}
	if (md->isUFODoor())
	{
		return SEM_UFO_DOOR;
	}
	if (md->isDoor())
	{
		return SEM_ORDINARY_DOOR;
	}
	if (md->isGravLift())
	{
		return SEM_GRAV_LIFT;
	}
	if (md->getSpecialType() == START_POINT)
	{
		return SEM_ENTRANCE;
	}
	if (md->isBaseModule())
	{
		return SEM_XCOM_BASE_MODULE;
	}

	if (dataSetRole == DATASET_ROLE_UFO)
	{
		if (part == O_FLOOR)
		{
			return SEM_UFO_INTERIOR;
		}
		if (part == O_WESTWALL || part == O_NORTHWALL || part == O_OBJECT)
		{
			const int walk = md->getTUCost(MT_WALK);
			if (isImpassableWalk(walk) || md->getBlock(DT_NONE) != 0)
			{
				return SEM_UFO_STRUCTURE;
			}
			return SEM_UFO_INTERIOR;
		}
		return SEM_UFO_STRUCTURE;
	}

	if (dataSetRole == DATASET_ROLE_CRAFT)
	{
		return SEM_XCOM_CRAFT;
	}

	const int walk = md->getTUCost(MT_WALK);
	if (part != O_FLOOR && (isImpassableWalk(walk) || md->getBlock(DT_NONE) != 0))
	{
		return SEM_ORDINARY_IMPASSABLE;
	}
	return SEM_ORDINARY;
}

MapDataSetRole roleForTilePart(const Tile *tile, TilePart part, const TerrainSemanticsContext &ctx)
{
	if (tile == 0)
	{
		return DATASET_ROLE_GROUND;
	}
	int mapDataId = -1;
	int mapDataSetId = -1;
	tile->getMapData(&mapDataId, &mapDataSetId, part);
	(void)mapDataId;
	if (mapDataSetId < 0 || static_cast<size_t>(mapDataSetId) >= ctx.dataSetRoles.size())
	{
		return DATASET_ROLE_GROUND;
	}
	return ctx.dataSetRoles[static_cast<size_t>(mapDataSetId)];
}

} // namespace

TerrainSemanticsContext buildSemanticsContext(SavedBattleGame *battle, Mod *mod)
{
	TerrainSemanticsContext ctx;
	if (battle == 0)
	{
		return ctx;
	}
	const size_t count = battle->getMapDataSets()->size();
	ctx.dataSetRoles.assign(count, DATASET_ROLE_GROUND);
	if (mod == 0)
	{
		return ctx;
	}

	for (std::vector<std::string>::const_iterator ufoId = mod->getUfosList().begin();
	     ufoId != mod->getUfosList().end(); ++ufoId)
	{
		RuleUfo *rule = mod->getUfo(*ufoId, false);
		if (rule != 0)
		{
			markTerrainRole(ctx.dataSetRoles, battle, rule->getBattlescapeTerrainData(), DATASET_ROLE_UFO);
		}
	}

	for (std::vector<std::string>::const_iterator craftId = mod->getCraftsList().begin();
	     craftId != mod->getCraftsList().end(); ++craftId)
	{
		RuleCraft *rule = mod->getCraft(*craftId, false);
		if (rule != 0 && rule->getBattlescapeTerrainData() != 0)
		{
			markTerrainRole(ctx.dataSetRoles, battle, rule->getBattlescapeTerrainData(), DATASET_ROLE_CRAFT);
		}
	}

	return ctx;
}

TerrainSemanticCategory classifyMapDataPart(const MapData *md, TilePart part, MapDataSetRole dataSetRole)
{
	return classifyFromMapData(md, part, dataSetRole);
}

TerrainSemanticCategory classifyTileBody(const Tile *tile, const TerrainSemanticsContext &ctx)
{
	if (tile == 0)
	{
		return SEM_UNKNOWN;
	}
	const MapDataSetRole floorRole = roleForTilePart(tile, O_FLOOR, ctx);
	const MapData *objectMd = tile->getMapData(O_OBJECT);
	if (objectMd != 0)
	{
		return classifyFromMapData(objectMd, O_OBJECT, roleForTilePart(tile, O_OBJECT, ctx));
	}
	const MapData *floorMd = tile->getMapData(O_FLOOR);
	if (floorMd != 0)
	{
		return classifyFromMapData(floorMd, O_FLOOR, floorRole);
	}
	return SEM_UNKNOWN;
}

TerrainSemanticCategory classifyTileWall(const Tile *tile, TilePart wallPart, const TerrainSemanticsContext &ctx)
{
	if (tile == 0)
	{
		return SEM_UNKNOWN;
	}
	const MapData *md = tile->getMapData(wallPart);
	return classifyFromMapData(md, wallPart, roleForTilePart(tile, wallPart, ctx));
}

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
