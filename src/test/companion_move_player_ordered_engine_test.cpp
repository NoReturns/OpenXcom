/*
 * P1-MOVE-INTERRUPT-02A — Companion MOVE must flag playerOrdered on the queued UnitWalkBState.
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <iostream>
#include <string>

#ifdef main
#undef main
#endif

using namespace OpenXcom;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static MapData *makeFloorMapData(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static MapDataSet *walkTestMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("move_player_ordered_test");
	}
	return ds;
}

static void wireFloorTile(SavedBattleGame *battle, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile exists");
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	tile->setMapData(makeFloorMapData(walkTestMapDataSet()), 1, 0, O_FLOOR);
}

static void wireUnitAt(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile exists");
	if (tile == 0)
	{
		return;
	}
	unit->setPosition(pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
	battle->setUnitPosition(unit, pos);
}

static void testCompanionMoveQueuesWalkWithPlayerOrdered()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 1);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	battle.getUnits()->push_back(unit);
	unit->setTimeUnits(80);

	const Position start(1, 1, 0);
	const Position dest(2, 1, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, dest);
	wireUnitAt(&battle, unit, start);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool started = game.tryExecuteCompanionMove(unit, dest, &reject);
	expect(started, started ? "companion move started" : reject.c_str());
	if (!started)
	{
		return;
	}

	const BattleAction *current = game.getCurrentAction();
	expect(current != 0 && current->playerOrdered, "currentAction.playerOrdered after tryExecuteCompanionMove");
	expect(current->type == BA_WALK, "current action is walk");

	BattleAction queued = {};
	expect(game.getRearmostQueuedBattleAction(&queued), "rearmost queued battle state action available");
	expect(queued.playerOrdered, "UnitWalkBState action copy has playerOrdered");
	expect(queued.type == BA_WALK, "queued action is walk");
	expect(queued.target == dest, "queued walk target matches destination");
}

int main()
{
	testCompanionMoveQueuesWalkWithPlayerOrdered();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_move_player_ordered_engine_test: all tests passed." << std::endl;
	return 0;
}
