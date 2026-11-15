/*
 * WP8E-02A — GUIDED_LAUNCH through native BA_LAUNCH / ProjectileFlyBState with real Map + Mod assets.
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/Map.h"
#include "../Engine/CompanionCommandCore.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/RNG.h"
#include "../Engine/State.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleInterface.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace CompanionCommandCore;

namespace
{

int g_failures = 0;
const int kMaxBattleTicks = 120000;

void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

struct LaunchHarness
{
	std::unique_ptr<Game> game;
	SavedBattleGame *battle;
	std::unique_ptr<BattlescapeState> battlescape;

	Mod *mod() const { return game.get()->getMod(); }
	BattlescapeGame *battleGame() const
	{
		return battlescape != 0 ? battlescape->getBattleGame() : 0;
	}
};

static MapData *floorData(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static int loadedAmmoQuantity(BattleItem *launcher)
{
	if (launcher == 0 || launcher->getAmmoItem() == 0)
	{
		return 0;
	}
	return launcher->getAmmoItem()->getAmmoQuantity();
}

static MapDataSet *harnessMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("wp8e_guided_launch");
	}
	return ds;
}

static void wireOpenFloor(SavedBattleGame *battle, int x0, int y0, int x1, int y1, int z)
{
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			const Position pos(x, y, z);
			Tile *tile = battle->getTile(pos);
			if (tile == 0)
			{
				continue;
			}
			tile->setDiscovered(true, 2);
			tile->setMapData(floorData(harnessMapDataSet()), 1, 0, O_FLOOR);
		}
	}
}

static void wireUnit(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	if (tile == 0)
	{
		return;
	}
	unit->setPosition(pos);
	Tile *below = pos.z > 0 ? battle->getTile(Position(pos.x, pos.y, pos.z - 1)) : 0;
	unit->setTile(tile, below);
	battle->setUnitPosition(unit, pos);
}

static std::string defaultDataDir(const char *argv0)
{
	(void)argv0;
	const char *candidates[] = {
		"E:/Open X-Com Companion/runtime/m0/bin",
		"../../../../runtime/m0/bin",
		"../../../runtime/m0/bin",
		"standard",
		"../standard",
		"../../standard",
	};
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
	{
		const std::string pck = std::string(candidates[i]) + "/UFO/TERRAIN/BLANKS.PCK";
		const std::string rules = std::string(candidates[i]) + "/standard/xcom1/items.rul";
		std::ifstream pckIn(pck.c_str());
		std::ifstream rulesIn(rules.c_str());
		if (pckIn && rulesIn)
		{
			return candidates[i];
		}
	}
	return "standard";
}

static bool initOptionsAndGame(const char *argv0, LaunchHarness *out, const std::string &dataDir)
{
	Options::mute = true;
	std::vector<char *> fakeArgv;
	fakeArgv.push_back(const_cast<char *>(argv0));
	std::string dataArg = dataDir;
	fakeArgv.push_back(const_cast<char *>("-data"));
	fakeArgv.push_back(const_cast<char *>(dataArg.c_str()));
	const int fakeArgc = static_cast<int>(fakeArgv.size());
	if (!Options::init(fakeArgc, fakeArgv.data()))
	{
		std::cerr << "Options::init failed" << std::endl;
		return false;
	}
	std::cerr << "wp8e harness: options ok" << std::endl;

	out->game.reset(new Game("WP8E guided launch harness"));
	std::cerr << "wp8e harness: game constructed" << std::endl;
	State::setGamePtr(out->game.get());
	Options::updateMods();
	out->game->loadMods();
	std::cerr << "wp8e harness: mods loaded" << std::endl;

	SavedGame *campaign = new SavedGame();
	out->game->setSavedGame(campaign);
	std::cerr << "wp8e harness: saved game" << std::endl;
	out->battle = new SavedBattleGame();
	campaign->setBattleGame(out->battle);
	out->battle->setTestMod(out->mod());
	out->battle->initMap(8, 8, 1);
	out->battle->initUtilities(out->mod());
	std::cerr << "wp8e harness: battle shell" << std::endl;

	if (Options::baseXResolution <= 0)
	{
		Options::baseXResolution = Options::baseXBattlescape > 0 ? Options::baseXBattlescape : 320;
	}
	if (Options::baseYResolution <= 0)
	{
		Options::baseYResolution = Options::baseYBattlescape > 0 ? Options::baseYBattlescape : 200;
	}
	out->battlescape.reset(new BattlescapeState());
	out->battle->setBattleState(out->battlescape.get());
	out->battlescape->init();
	std::cerr << "wp8e harness: battlescape ready" << std::endl;
	return true;
}

static void shutdownHarness(LaunchHarness *h)
{
	if (h->battle != 0)
	{
		h->battle->setBattleState(0);
	}
	h->battlescape.reset();
	h->battle = 0;
	h->game.reset();
}

static void setupFreshBattle(LaunchHarness *h, int mapW, int mapH, int mapD)
{
	if (h->battle != 0)
	{
		h->battle->setBattleState(0);
	}
	h->battlescape.reset();
	SavedBattleGame *nextBattle = new SavedBattleGame();
	nextBattle->setTestMod(h->mod());
	nextBattle->initMap(mapW, mapH, mapD);
	nextBattle->initUtilities(h->mod());
	h->game->getSavedGame()->setBattleGame(nextBattle);
	h->battle = nextBattle;
	h->battlescape.reset(new BattlescapeState());
	h->battle->setBattleState(h->battlescape.get());
	h->battlescape->init();
}

static BattleUnit *placeBlasterSoldier(LaunchHarness *h, const Position &pos, int timeUnits)
{
	RuleSoldier *rs = h->mod()->getSoldier("STR_SOLDIER", false);
	Armor *ar = h->mod()->getArmor("STR_POWER_SUIT_UC", false);
	if (ar == 0)
	{
		ar = h->mod()->getArmor("STR_JUMPSUIT", false);
	}
	if (rs == 0)
	{
		std::cerr << "FAIL: STR_SOLDIER missing in ruleset" << std::endl;
		++g_failures;
		return 0;
	}
	if (ar == 0)
	{
		std::cerr << "FAIL: soldier armor missing in ruleset" << std::endl;
		++g_failures;
		return 0;
	}
	Soldier *soldier = new Soldier(rs, ar, 8801);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	unit->getBaseStats()->tu = 100;
	unit->setTimeUnits(timeUnits);
	unit->setActiveHand("STR_RIGHT_HAND");
	h->battle->getUnits()->push_back(unit);
	wireUnit(h->battle, unit, pos);

	RuleItem *launcherRules = h->mod()->getItem("STR_BLASTER_LAUNCHER", false);
	RuleItem *bombRules = h->mod()->getItem("STR_BLASTER_BOMB", false);
	if (launcherRules == 0 || bombRules == 0)
	{
		std::cerr << "FAIL: stock blaster rules missing; run with -data standard" << std::endl;
		++g_failures;
		return 0;
	}
	BattleItem *bomb = new BattleItem(bombRules, h->battle->getCurrentItemId());
	bomb->setAmmoQuantity(1);
	BattleItem *launcher = new BattleItem(launcherRules, h->battle->getCurrentItemId());
	launcher->setAmmoItem(bomb);
	RuleInventory *hand = h->mod()->getInventory("STR_RIGHT_HAND", true);
	launcher->setSlot(hand);
	launcher->setSlotX(0);
	launcher->setSlotY(0);
	launcher->moveToOwner(unit);
	h->battle->getItems()->push_back(launcher);
	h->battle->getItems()->push_back(bomb);
	return unit;
}

static int runUntilIdle(BattlescapeGame *game, const char *label)
{
	int ticks = 0;
	while (game->isBusy() && ticks < kMaxBattleTicks)
	{
		game->handleState();
		++ticks;
	}
	expect(ticks < kMaxBattleTicks, label);
	return ticks;
}

struct LaunchOutcome
{
	bool started;
	std::string reject;
	int tuBefore;
	int tuAfter;
	int ammoBefore;
	int ammoAfter;
	int ticks;
};

static LaunchOutcome runGuidedLaunch(
	LaunchHarness *h,
	BattleUnit *unit,
	BattleItem *launcher,
	const std::vector<Position> &waypoints)
{
	LaunchOutcome out = {};
	out.tuBefore = unit->getTimeUnits();
	out.ammoBefore = loadedAmmoQuantity(launcher);
	BattlescapeGame *bg = h->battleGame();
	out.started = bg->tryExecuteCompanionLaunch(unit, waypoints, launcher, &out.reject);
	if (!out.started)
	{
		return out;
	}
	expect(bg->isBusy(), "launch queues battle states");
	SnapShotCompletionInput mid = {};
	mid.battleBusy = bg->isBusy();
	mid.projectileLaunched = true;
	mid.actorAlive = true;
	mid.actorStillAiControlled = true;
	expect(evaluateGuidedLaunchTerminalStatus(mid) == CMD_EXECUTING, "COMPLETED must not occur while battle busy");
	out.ticks = runUntilIdle(bg, "guided launch resolves without hang");
	out.tuAfter = unit->getTimeUnits();
	out.ammoAfter = loadedAmmoQuantity(launcher);
	std::cerr << "wp8e evidence: tu " << out.tuBefore << "->" << out.tuAfter
	          << " ammo " << out.ammoBefore << "->" << out.ammoAfter
	          << " ticks=" << out.ticks << std::endl;
	expect(!bg->isBusy(), "no orphaned launch state");
	SnapShotCompletionInput done = {};
	done.battleBusy = false;
	done.projectileLaunched = true;
	done.actorAlive = true;
	done.actorStillAiControlled = true;
	expect(evaluateGuidedLaunchTerminalStatus(done) == CMD_COMPLETED, "terminal COMPLETED after idle resolution");
	return out;
}

static void testOneWaypointLaunch(LaunchHarness *h)
{
	setupFreshBattle(h, 12, 12, 1);
	wireOpenFloor(h->battle, 0, 0, 11, 11, 0);
	const Position start(2, 2, 0);
	BattleUnit *unit = placeBlasterSoldier(h, start, 100);
	if (unit == 0)
	{
		return;
	}
	BattleItem *launcher = unit->getItem("STR_RIGHT_HAND");
	const int launchTu = unit->getActionTUs(BA_LAUNCH, launcher);
	std::vector<Position> wps;
	wps.push_back(Position(8, 8, 0));
	const LaunchOutcome out = runGuidedLaunch(h, unit, launcher, wps);
	expect(out.started, out.started ? "one-waypoint launch started" : out.reject.c_str());
	if (!out.started)
	{
		return;
	}
	expect(out.ammoBefore - out.ammoAfter == 1, "ammunition consumed once");
	const int tuSpent = out.tuBefore - out.tuAfter;
	expect(tuSpent >= launchTu, "TU spent at least BA_LAUNCH cost");
}

static void testThreeWaypointLaunch(LaunchHarness *h)
{
	setupFreshBattle(h, 12, 12, 1);
	wireOpenFloor(h->battle, 0, 0, 11, 11, 0);
	BattleUnit *unit = placeBlasterSoldier(h, Position(1, 1, 0), 100);
	BattleItem *launcher = unit->getItem("STR_RIGHT_HAND");
	std::vector<Position> wps;
	wps.push_back(Position(4, 4, 0));
	wps.push_back(Position(7, 4, 0));
	wps.push_back(Position(9, 9, 0));
	const LaunchOutcome out = runGuidedLaunch(h, unit, launcher, wps);
	expect(out.started, out.started ? "three-waypoint launch started" : out.reject.c_str());
	if (out.started)
	{
		expect(out.ammoBefore - out.ammoAfter == 1, "three-waypoint cascade spends one round");
	}
}

static void testVerticalWaypoints(LaunchHarness *h)
{
	setupFreshBattle(h, 8, 8, 3);
	for (int z = 0; z < 3; ++z)
	{
		wireOpenFloor(h->battle, 0, 0, 7, 7, z);
	}
	BattleUnit *unit = placeBlasterSoldier(h, Position(2, 2, 0), 100);
	BattleItem *launcher = unit->getItem("STR_RIGHT_HAND");
	std::vector<Position> wps;
	wps.push_back(Position(3, 3, 1));
	wps.push_back(Position(5, 5, 2));
	const LaunchOutcome out = runGuidedLaunch(h, unit, launcher, wps);
	expect(out.started, out.started ? "vertical waypoints launch started" : out.reject.c_str());
}

static void testNineWaypointCap(LaunchHarness *h)
{
	setupFreshBattle(h, 20, 20, 1);
	wireOpenFloor(h->battle, 0, 0, 19, 19, 0);
	BattleUnit *unit = placeBlasterSoldier(h, Position(1, 1, 0), 120);
	BattleItem *launcher = unit->getItem("STR_RIGHT_HAND");
	std::vector<Position> wps;
	for (int i = 0; i < 9; ++i)
	{
		wps.push_back(Position(2 + i, 2 + i, 0));
	}
	const LaunchOutcome out = runGuidedLaunch(h, unit, launcher, wps);
	expect(out.started, out.started ? "nine-waypoint launch accepted" : out.reject.c_str());
}

static void testObstructionEarlyDetonation(LaunchHarness *h)
{
	setupFreshBattle(h, 10, 10, 1);
	wireOpenFloor(h->battle, 0, 0, 9, 9, 0);
	Tile *wallTile = h->battle->getTile(Position(3, 1, 0));
	if (wallTile != 0)
	{
		MapData *wall = new MapData(harnessMapDataSet());
		wall->setObjectType(O_NORTHWALL);
		wall->setTUCosts(255, 255, 255);
		wallTile->setMapData(wall, 1, 0, O_NORTHWALL);
	}
	BattleUnit *unit = placeBlasterSoldier(h, Position(1, 1, 0), 100);
	BattleItem *launcher = unit->getItem("STR_RIGHT_HAND");
	std::vector<Position> wps;
	wps.push_back(Position(4, 1, 0));
	wps.push_back(Position(8, 8, 0));
	const LaunchOutcome out = runGuidedLaunch(h, unit, launcher, wps);
	expect(out.started, out.started ? "obstruction launch started" : out.reject.c_str());
	if (out.started)
	{
		expect(out.ammoBefore - out.ammoAfter == 1, "obstructed launch still consumes loaded round once");
		expect(!h->battleGame()->isBusy(), "obstructed launch leaves battle idle");
	}
}

static void testPreflightNoMutation(LaunchHarness *h)
{
	setupFreshBattle(h, 6, 6, 1);
	wireOpenFloor(h->battle, 0, 0, 5, 5, 0);
	BattleUnit *unit = placeBlasterSoldier(h, Position(1, 1, 0), 100);
	BattleItem *launcher = unit->getItem("STR_RIGHT_HAND");
	const int tuBefore = unit->getTimeUnits();
	const int ammoBefore = launcher->getAmmoItem()->getAmmoQuantity();
	std::vector<Position> wps;
	wps.push_back(Position(99, 99, 0));
	std::string reject;
	BattlescapeGame *bg = h->battleGame();
	const bool ok = bg->tryExecuteCompanionLaunch(unit, wps, launcher, &reject);
	expect(!ok, "OOB preflight rejected");
	expect(tuBefore == unit->getTimeUnits(), "TU unchanged on reject");
	expect(ammoBefore == launcher->getAmmoItem()->getAmmoQuantity(), "ammo unchanged on reject");
	expect(!bg->isBusy(), "no states queued on reject");
}

} // namespace

int main(int argc, char **argv)
{
	try
	{
		const std::string dataDir = argc > 1 ? argv[1] : defaultDataDir(argv[0]);
		std::cerr << "wp8e harness: using data dir " << dataDir << std::endl;
		LaunchHarness harness = {};
		if (!initOptionsAndGame(argv[0], &harness, dataDir))
		{
			return 1;
		}
		std::cerr << "wp8e harness: game and map ready" << std::endl;

		testOneWaypointLaunch(&harness);
		std::cerr << "wp8e harness: one waypoint done" << std::endl;
		testThreeWaypointLaunch(&harness);
		testVerticalWaypoints(&harness);
		testNineWaypointCap(&harness);
		testObstructionEarlyDetonation(&harness);
		testPreflightNoMutation(&harness);

		shutdownHarness(&harness);

		if (g_failures == 0)
		{
			std::cerr << "companion_guided_launch_engine_test: all tests passed." << std::endl;
		}
		return g_failures != 0 ? 1 : 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << "wp8e harness exception: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "wp8e harness unknown exception" << std::endl;
		return 1;
	}
}