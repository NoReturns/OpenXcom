/*
 * Open X-Com Companion — TURN_FACE executor (M1 WP-TURN-1b).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Engine/CompanionTuVisibilityCore.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Unit.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Soldier.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/TileEngine.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Tile.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

static MapDataSet *turnTestMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("turn_face_test");
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
	tile->setMapData(makeFloorMapData(turnTestMapDataSet()), 1, 0, O_FLOOR);
}

static void wireUnitAt(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	wireFloorTile(battle, pos);
	unit->setPosition(pos);
	Tile *tile = battle->getTile(pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
	battle->setUnitPosition(unit, pos);
}

static BattleUnit *makeRosterSoldier(SavedBattleGame *battle, int id, int tu)
{
	Mod *mod = battle->getTestMod();
	RuleSoldier *rs = new RuleSoldier("STR_SOLDIER");
	Armor *ar = new Armor("STR_JUMPSUIT");
	Soldier *soldier = new Soldier(rs, ar, id);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle->getUnits()->push_back(unit);
	unit->setTimeUnits(tu);
	unit->setDirection(0);
	return unit;
}

static BattlescapeGame *makeHarness(SavedBattleGame *battle)
{
	return new BattlescapeGame(battle, 0);
}

static void testClockwiseTurnTwoSteps()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 2);

	const Position start(5, 5, 0);
	const Position target(7, 5, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 50);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(0);

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	int tuSpent = -1;
	int finalDir = -1;
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, &tuSpent, &finalDir);
	expect(ok, ok ? "clockwise turn ok" : reject.c_str());
	expect(unit->getPosition() == start, "position unchanged after turn");
	expect(tuSpent == 2, "two TU for two facing steps");
	expect(finalDir == unit->directionTo(target), "final facing matches bearing");
	delete game;
}

static void testCounterClockwiseTurnOneStep()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 1);

	const Position start(5, 5, 0);
	const Position target(4, 4, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 50);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(0);

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	int tuSpent = -1;
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, &tuSpent, 0);
	expect(ok, ok ? "counter-clockwise turn ok" : reject.c_str());
	expect(tuSpent == 1, "one TU for single step");
	delete game;
}

static void testAlreadyFacingNoTu()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 1);

	const Position start(5, 5, 0);
	const Position target(6, 5, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 50);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(unit->directionTo(target));

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	int tuSpent = -1;
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, &tuSpent, 0);
	expect(ok, ok ? "already facing ok" : reject.c_str());
	expect(tuSpent == 0, "zero TU when already facing");
	delete game;
}

static void testInsufficientTuBeforeMutation()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 1);

	const Position start(5, 5, 0);
	const Position target(7, 5, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 1);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(0);
	const int tuBefore = unit->getTimeUnits();
	const int dirBefore = unit->getDirection();

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, 0, 0);
	expect(!ok, "insufficient TU rejected");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged after reject");
	expect(unit->getDirection() == dirBefore, "facing unchanged after reject");
	delete game;
}

static void testReservedTuBlocksTurn()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 1);
	battle.setKneelReserved(true);

	const Position start(5, 5, 0);
	const Position target(7, 5, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 5);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(0);

	const int tuBefore = unit->getTimeUnits();

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, 0, 0);
	expect(!ok, "reserved TU blocks turn");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged when reserved TU blocks");
	delete game;
}

static void testHwpActorRejected()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 1);

	const Position start(3, 3, 0);
	const Position target(4, 3, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);

	static Unit tankRules("STR_TURN_TEST_TANK");
	static Armor tankArmor("STR_TURN_TEST_TANK_ARMOR");
	StatAdjustment adjustment = {};
	BattleUnit *unit = new BattleUnit(&tankRules, FACTION_PLAYER, BattleUnit::MAX_SOLDIER_ID, &tankArmor, &adjustment, 0);
	unit->setTimeUnits(50);
	unit->setDirection(0);
	battle.getUnits()->push_back(unit);
	wireUnitAt(&battle, unit, start);

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, 0, 0);
	expect(!ok, "HWP actor rejected");
	delete game;
}

static void testInvalidTargetBoundsAndSameHorizontal()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 2);

	const Position start(5, 5, 0);
	wireFloorTile(&battle, start);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 50);
	wireUnitAt(&battle, unit, start);

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;

	expect(!game->tryExecuteCompanionTurnFace(unit, Position(10, 5, 0), &reject, 0, 0), "out of bounds rejected");
	expect(!game->tryExecuteCompanionTurnFace(unit, Position(5, 5, 1), &reject, 0, 0),
		"same horizontal xy rejected");

	delete game;
}

static std::string turnFaceTestDataDir(const char *argv0)
{
	(void)argv0;
	const char *candidates[] = {
		"E:/Open X-Com Companion/runtime/m0/bin",
		"../../../../runtime/m0/bin",
		"../../../runtime/m0/bin",
		"standard",
		"../standard",
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
	return "";
}

static void testMapCacheRefreshWhenMapAvailable(const char *argv0)
{
	const std::string dataDir = turnFaceTestDataDir(argv0);
	if (dataDir.empty())
	{
		std::cout << "SKIP: map cache presentation test (no game data)." << std::endl;
		return;
	}

	Options::mute = true;
	std::vector<char *> fakeArgv;
	fakeArgv.push_back(const_cast<char *>(argv0));
	std::string dataArg = dataDir;
	fakeArgv.push_back(const_cast<char *>("-data"));
	fakeArgv.push_back(const_cast<char *>(dataArg.c_str()));
	if (!Options::init(static_cast<int>(fakeArgv.size()), fakeArgv.data()))
	{
		std::cout << "SKIP: map cache presentation test (Options::init failed)." << std::endl;
		return;
	}

	std::unique_ptr<Game> game(new Game("turn_face map cache harness"));
	State::setGamePtr(game.get());
	Options::updateMods();
	game->loadMods();

	SavedGame *campaign = new SavedGame();
	game->setSavedGame(campaign);
	SavedBattleGame *battle = new SavedBattleGame();
	campaign->setBattleGame(battle);
	battle->setTestMod(game->getMod());
	battle->initMap(10, 10, 1);
	battle->initUtilities(game->getMod());

	if (Options::baseXResolution <= 0)
	{
		Options::baseXResolution = Options::baseXBattlescape > 0 ? Options::baseXBattlescape : 320;
	}
	if (Options::baseYResolution <= 0)
	{
		Options::baseYResolution = Options::baseYBattlescape > 0 ? Options::baseYBattlescape : 200;
	}

	std::unique_ptr<BattlescapeState> battlescape(new BattlescapeState());
	battle->setBattleState(battlescape.get());
	battlescape->init();

	const Position start(5, 5, 0);
	const Position target(7, 5, 0);
	wireFloorTile(battle, start);
	wireFloorTile(battle, target);

	RuleSoldier *rs = game->getMod()->getSoldier("STR_SOLDIER", false);
	Armor *ar = game->getMod()->getArmor("STR_POWER_SUIT_UC", false);
	if (ar == 0)
	{
		ar = game->getMod()->getArmor("STR_JUMPSUIT", false);
	}
	expect(rs != 0 && ar != 0, "mod soldier and armor for map cache test");
	if (rs == 0 || ar == 0)
	{
		battle->setBattleState(0);
		return;
	}

	BattlescapeGame *battleGame = battlescape->getBattleGame();
	Map *map = battlescape->getMap();
	expect(map != 0 && battleGame != 0, "battlescape map and battle game");
	if (map == 0 || battleGame == 0)
	{
		battle->setBattleState(0);
		return;
	}

	battle->assignCompanionTacticalSoldierToAi(1);
	battle->assignCompanionTacticalSoldierToAi(2);

	Soldier *rejectSoldier = new Soldier(rs, ar, 2);
	BattleUnit *rejectUnit = new BattleUnit(rejectSoldier, 0);
	battle->getUnits()->push_back(rejectUnit);
	rejectUnit->setTimeUnits(1);
	rejectUnit->setVisible(true);
	wireUnitAt(battle, rejectUnit, Position(4, 5, 0));
	rejectUnit->setDirection(0);
	map->cacheUnit(rejectUnit);
	Surface *cacheBeforeReject = rejectUnit->getCache(0);
	const int rejectTuBefore = rejectUnit->getTimeUnits();
	const int rejectDirBefore = rejectUnit->getDirection();
	std::string reject;
	expect(!battleGame->tryExecuteCompanionTurnFace(rejectUnit, target, &reject, 0, 0),
		"reject when insufficient TU with map present");
	expect(rejectUnit->getTimeUnits() == rejectTuBefore, "no TU mutation on reject with map");
	expect(rejectUnit->getDirection() == rejectDirBefore, "no facing mutation on reject with map");
	expect(rejectUnit->getCache(0) == cacheBeforeReject, "cache untouched on rejected turn");

	Soldier *soldier = new Soldier(rs, ar, 1);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle->getUnits()->push_back(unit);
	unit->setTimeUnits(50);
	unit->setVisible(true);
	wireUnitAt(battle, unit, start);
	unit->setDirection(0);

	map->cacheUnit(unit);
	expect(!unit->isCacheInvalid(), "unit cache baked before turn");
	expect(unit->getDirection() == 0, "facing east before turn");

	int tuSpent = -1;
	const bool ok = battleGame->tryExecuteCompanionTurnFace(unit, target, &reject, &tuSpent, 0);
	expect(ok, ok ? "map-present turn ok" : reject.c_str());
	expect(tuSpent == 2, "map-present turn spent two TU");
	expect(unit->getDirection() == unit->directionTo(target), "facing updated after successful turn");
	expect(!unit->isCacheInvalid(), "unit cache valid after successful turn refresh");
	expect(unit->getCache(0) != 0, "unit cache surface present after refresh");

	battle->setBattleState(0);
}

static void clearTileDiscovery(Tile *tile)
{
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(false, 0);
	tile->setDiscovered(false, 1);
	tile->setDiscovered(false, 2);
}

static void nativeTurnStepsToward(BattlescapeGame *game, BattleUnit *unit, const Position &target)
{
	const bool turret = false;
	unit->lookAt(target, turret);
	TileEngine *tileEngine = game->getTileEngine();
	while (unit->getStatus() == STATUS_TURNING)
	{
		unit->turn(turret);
		if (tileEngine != 0)
		{
			tileEngine->calculateFOV(unit);
		}
	}
}

static void testTurnFaceNoFovWithoutTileEngine()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(12, 12, 1);

	const Position start(6, 6, 0);
	const Position ahead(9, 6, 0);
	const Position faceAway(3, 6, 0);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, ahead);
	wireFloorTile(&battle, faceAway);

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 50);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(unit->directionTo(faceAway));
	unit->setVisible(true);

	Tile *aheadTile = battle.getTile(ahead);
	clearTileDiscovery(aheadTile);

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	expect(game->getTileEngine() == 0, "harness without initUtilities has no TileEngine");
	const bool ok = game->tryExecuteCompanionTurnFace(unit, ahead, &reject, 0, 0);
	expect(ok, ok ? "turn ok without TileEngine" : reject.c_str());
	expect(aheadTile != 0 && !aheadTile->isDiscovered(2),
		"undiscovered tile stays hidden when TileEngine is absent (harness-only)");
	delete game;
}

static void testTurnFaceFovDiscoversTerrain(const char *argv0)
{
	const std::string dataDir = turnFaceTestDataDir(argv0);
	if (dataDir.empty())
	{
		std::cout << "SKIP: turn_face FOV test (no game data)." << std::endl;
		return;
	}

	Options::mute = true;
	std::vector<char *> fakeArgv;
	fakeArgv.push_back(const_cast<char *>(argv0));
	std::string dataArg = dataDir;
	fakeArgv.push_back(const_cast<char *>("-data"));
	fakeArgv.push_back(const_cast<char *>(dataArg.c_str()));
	if (!Options::init(static_cast<int>(fakeArgv.size()), fakeArgv.data()))
	{
		std::cout << "SKIP: turn_face FOV test (Options::init failed)." << std::endl;
		return;
	}

	std::unique_ptr<Game> game(new Game("turn_face fov harness"));
	State::setGamePtr(game.get());
	Options::updateMods();
	game->loadMods();

	SavedGame *campaign = new SavedGame();
	game->setSavedGame(campaign);
	SavedBattleGame *battle = new SavedBattleGame();
	campaign->setBattleGame(battle);
	battle->setTestMod(game->getMod());
	battle->initMap(16, 16, 1);
	battle->initUtilities(game->getMod());
	battle->assignCompanionTacticalSoldierToAi(1);

	RuleSoldier *rs = game->getMod()->getSoldier("STR_SOLDIER", false);
	Armor *ar = game->getMod()->getArmor("STR_POWER_SUIT_UC", false);
	if (ar == 0)
	{
		ar = game->getMod()->getArmor("STR_JUMPSUIT", false);
	}
	if (rs == 0 || ar == 0)
	{
		std::cout << "SKIP: turn_face FOV test (soldier/armor rules missing)." << std::endl;
		return;
	}

	const Position start(7, 7, 0);
	const Position ahead(11, 7, 0);
	const Position faceAway(3, 7, 0);
	for (int x = 2; x <= 12; ++x)
	{
		wireFloorTile(battle, Position(x, 7, 0));
	}

	Soldier *soldier = new Soldier(rs, ar, 1);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle->getUnits()->push_back(unit);
	unit->setTimeUnits(50);
	unit->setVisible(true);
	wireUnitAt(battle, unit, start);
	unit->setDirection(unit->directionTo(faceAway));

	Tile *aheadTile = battle->getTile(ahead);
	clearTileDiscovery(aheadTile);
	expect(aheadTile != 0, "ahead tile exists");

	BattlescapeGame battleGame(battle, 0);
	TileEngine *tileEngine = battleGame.getTileEngine();
	expect(tileEngine != 0, "live-style harness has TileEngine");
	if (tileEngine != 0)
	{
		tileEngine->calculateFOV(unit);
	}
	expect(aheadTile != 0 && !aheadTile->isDiscovered(2),
		"ahead tile remains undiscovered when facing away");

	std::string reject;
	int tuSpent = -1;
	const bool companionOk = battleGame.tryExecuteCompanionTurnFace(unit, ahead, &reject, &tuSpent, 0);
	expect(companionOk, companionOk ? "companion turn toward undiscovered terrain" : reject.c_str());
	expect(tuSpent > 0, "companion turn spent TU");
	expect(unit->getDirection() == unit->directionTo(ahead), "companion final facing toward ahead tile");
	expect(aheadTile->isDiscovered(2), "companion turn discovers ahead tile (floor)");

	clearTileDiscovery(aheadTile);
	unit->setDirection(unit->directionTo(faceAway));
	unit->setTimeUnits(50);
	unit->abortTurn();
	if (tileEngine != 0)
	{
		tileEngine->calculateFOV(unit);
	}
	expect(!aheadTile->isDiscovered(2), "ahead tile cleared for native control");

	nativeTurnStepsToward(&battleGame, unit, ahead);
	expect(unit->getDirection() == unit->directionTo(ahead), "native manual turn matches bearing");
	expect(aheadTile->isDiscovered(2), "native manual turn discovers same ahead tile");
}

static void testDifferentZHorizontalBearing()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(10, 10, 2);

	const Position start(5, 5, 0);
	const Position target(6, 5, 1);
	wireFloorTile(&battle, start);
	wireFloorTile(&battle, target);
	wireFloorTile(&battle, Position(6, 5, 1));

	BattleUnit *unit = makeRosterSoldier(&battle, 1, 50);
	wireUnitAt(&battle, unit, start);
	unit->setDirection(0);

	BattlescapeGame *game = makeHarness(&battle);
	std::string reject;
	int tuSpent = -1;
	const int expectedSteps = CompanionTuVisibilityCore::estimateDirectedFireFacingSteps(unit, target);
	const bool ok = game->tryExecuteCompanionTurnFace(unit, target, &reject, &tuSpent, 0);
	expect(ok, ok ? "different Z bearing turn ok" : reject.c_str());
	expect(tuSpent == expectedSteps, "TU matches estimated facing steps");
	expect(unit->getDirection() == unit->directionTo(target), "bearing uses horizontal math");
	delete game;
}

int main(int argc, char **argv)
{
	const char *argv0 = argc > 0 ? argv[0] : "companion_turn_face_engine_test";
	testClockwiseTurnTwoSteps();
	testCounterClockwiseTurnOneStep();
	testAlreadyFacingNoTu();
	testInsufficientTuBeforeMutation();
	testReservedTuBlocksTurn();
	testHwpActorRejected();
	testInvalidTargetBoundsAndSameHorizontal();
	testDifferentZHorizontalBearing();
	testTurnFaceNoFovWithoutTileEngine();
	testMapCacheRefreshWhenMapAvailable(argv0);
	testTurnFaceFovDiscoversTerrain(argv0);
	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_turn_face_engine_test: all tests passed." << std::endl;
	return 0;
}