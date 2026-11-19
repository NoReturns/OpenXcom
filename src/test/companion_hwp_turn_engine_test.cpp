/*
 * Open X-Com Companion — HWP_TURN executor (M1 HWP-S4-1b).
 */
#include "../Engine/CompanionHwpTurnExecutionCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Unit.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Engine/Options.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <sstream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionHwpTurnExecutionCore;

static int g_failures = 0;
static bool g_savedStrafe = false;

static void ensureStrafeEnabledForTurretTests()
{
	g_savedStrafe = Options::strafe;
	Options::strafe = true;
}

static void restoreStrafeOption()
{
	Options::strafe = g_savedStrafe;
}

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static MapDataSet *floorDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("hwp_turn_floor");
	}
	return ds;
}

static MapData *makeFloorTile()
{
	MapData *floor = new MapData(floorDataSet());
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static void wireFloor(SavedBattleGame *battle, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	tile->setMapData(makeFloorTile(), 1, 0, O_FLOOR);
}

static void wireOpenMap(SavedBattleGame *battle, int w, int h)
{
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			wireFloor(battle, Position(x, y, 0));
		}
	}
}

struct TankHarness
{
	Mod mod;
	Armor tankArmor;
	SavedBattleGame battle;
	BattlescapeGame *game;

	TankHarness() : tankArmor("STR_HWP_TURN_TEST_ARMOR"), battle(), game(0)
	{
		tankArmor.load(YAML::Load("type: STR_HWP_TURN_TEST_ARMOR\nsize: 2\nmovementType: 0\n"));
		battle.setTestMod(&mod);
		battle.initMap(12, 12, 1);
		battle.initUtilities(&mod);
		wireOpenMap(&battle, 12, 12);
		game = new BattlescapeGame(&battle, 0);
	}

	~TankHarness()
	{
		delete game;
	}

	BattleUnit *addTank(int id, const Position &pos, int tu, int turretType, int hullDir, int turretDir)
	{
		Unit rules("STR_HWP_TURN_TEST_TANK");
		StatAdjustment adjustment;
		BattleUnit *unit = new BattleUnit(&rules, FACTION_PLAYER, id, &tankArmor, &adjustment, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"
		     << "direction: " << hullDir << "\ndirectionTurret: " << turretDir
		     << "\nturretType: " << turretType << "\n";
		unit->load(YAML::Load(yaml.str()));
		battle.getUnits()->push_back(unit);
		expect(battle.setUnitPosition(unit, pos), "2x2 HWP placed");
		return unit;
	}
};

static void testBodyTurnCouplesHullAndTurret(int turretType, const char *label)
{
	TankHarness h;
	const Position pos(3, 3, 0);
	const Position target(5, 3, 0);
	BattleUnit *unit = h.addTank(1'000'000, pos, 80, turretType, 0, 0);

	std::string reject;
	ExecutionOutcome outcome = {};
	const bool ok = executeHwpTurn(h.game, unit, target, HwpTurnPart::Body, &reject, &outcome);
	expect(ok, ok ? (std::string(label) + " body turn ok").c_str() : reject.c_str());
	expect(unit->getPosition() == pos, "body turn position unchanged");
	expect(outcome.tuSpentTurning == 2, "body turn two TU");
	const int desired = unit->directionTo(target);
	expect(unit->getDirection() == desired, "hull at bearing");
	expect(unit->getTurretDirection() == desired, "turret coupled to hull");
}

static void testTurretTurnIndependent(int turretType, const char *label)
{
	ensureStrafeEnabledForTurretTests();
	TankHarness h;
	const Position pos(5, 5, 0);
	const Position target(7, 5, 0);
	BattleUnit *unit = h.addTank(1'000'001, pos, 80, turretType, 4, 0);

	const int hullBefore = unit->getDirection();
	const int tuBefore = unit->getTimeUnits();

	std::string reject;
	ExecutionOutcome outcome = {};
	const bool ok = executeHwpTurn(h.game, unit, target, HwpTurnPart::Turret, &reject, &outcome);
	expect(ok, ok ? (std::string(label) + " turret turn ok").c_str() : reject.c_str());
	expect(unit->getPosition() == pos, "turret turn position unchanged");
	expect(unit->getDirection() == hullBefore, "hull unchanged during turret turn");
	expect(unit->getTurretDirection() == unit->directionTo(target), "turret at bearing");
	expect(outcome.tuSpentTurning == 2, "turret turn two TU");
	expect(unit->getTimeUnits() == tuBefore - 2, "TU spent for turret");
	restoreStrafeOption();
}

static void testAlreadyAligned(int turretType)
{
	TankHarness h;
	const Position pos(2, 2, 0);
	const Position target(4, 2, 0);
	BattleUnit *unit = h.addTank(1'000'002, pos, 50, turretType, 2, 2);
	const int tuBefore = unit->getTimeUnits();

	ExecutionOutcome outcome = {};
	std::string reject;
	const bool ok = executeHwpTurn(h.game, unit, target, HwpTurnPart::Body, &reject, &outcome);
	expect(ok, "already aligned body");
	expect(outcome.tuSpentTurning == 0, "zero TU when aligned");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged when aligned");
}

static void testInsufficientTuNoMutation(int turretType)
{
	TankHarness h;
	const Position pos(1, 1, 0);
	const Position target(5, 1, 0);
	BattleUnit *unit = h.addTank(1'000'003, pos, 1, turretType, 0, 0);
	const int tuBefore = unit->getTimeUnits();
	const int hullBefore = unit->getDirection();
	const int turretBefore = unit->getTurretDirection();

	std::string reject;
	const bool ok = executeHwpTurn(h.game, unit, target, HwpTurnPart::Body, &reject, 0);
	expect(!ok, "insufficient TU rejected");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged after reject");
	expect(unit->getDirection() == hullBefore, "hull unchanged after reject");
	expect(unit->getTurretDirection() == turretBefore, "turret unchanged after reject");
}

static void testReservationPreflightBlocksNoMutation(int turretType)
{
	TankHarness h;
	const Position pos(1, 1, 0);
	const Position target(1, 8, 0);
	BattleUnit *unit = h.addTank(1'000'004, pos, 3, turretType, 0, 0);
	const int tuBefore = unit->getTimeUnits();
	const int hullBefore = unit->getDirection();

	std::string reject;
	int steps = 0;
	const bool ok = preflightHwpTurn(h.game, unit, target, HwpTurnPart::Body, &reject, &steps);
	expect(!ok, "preflight rejects when TU budget smaller than step count");
	expect(steps >= 4, "turn needs at least four steps to south bearing");
	expect(unit->getTimeUnits() == tuBefore, "preflight does not spend TU");
	expect(unit->getDirection() == hullBefore, "hull unchanged after failed preflight");
	expect(unit->getStatus() == STATUS_STANDING, "not left STATUS_TURNING after failed preflight");
}

static void testStrafeDisabledRejectsTurret(int turretType)
{
	const bool savedStrafe = Options::strafe;
	Options::strafe = false;

	TankHarness h;
	const Position pos(6, 6, 0);
	const Position target(8, 6, 0);
	BattleUnit *unit = h.addTank(1'000'005, pos, 80, turretType, 4, 0);
	const int tuBefore = unit->getTimeUnits();
	const int turretBefore = unit->getTurretDirection();

	std::string reject;
	const bool ok = executeHwpTurn(h.game, unit, target, HwpTurnPart::Turret, &reject, 0);
	expect(!ok, "turret turn rejected when strafe disabled");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged when strafe disabled");
	expect(unit->getTurretDirection() == turretBefore, "turret unchanged when strafe disabled");

	Options::strafe = savedStrafe;
}

static void testPreflightAloneNoMutation()
{
	TankHarness h;
	const Position pos(2, 4, 0);
	const Position target(6, 4, 0);
	BattleUnit *unit = h.addTank(1'000'006, pos, 1, 0, 0, 0);
	const int tuBefore = unit->getTimeUnits();

	std::string reject;
	int steps = -1;
	const bool ok = preflightHwpTurn(h.game, unit, target, HwpTurnPart::Body, &reject, &steps);
	expect(!ok, "preflight fails insufficient TU");
	expect(steps > 1, "multi-step turn estimated");
	expect(unit->getTimeUnits() == tuBefore, "preflight does not spend TU");
	expect(unit->getStatus() == STATUS_STANDING, "preflight does not start turning");
}

int companion_hwp_turn_engine_test_main()
{
	ensureStrafeEnabledForTurretTests();
	testBodyTurnCouplesHullAndTurret(0, "Cannon");
	testTurretTurnIndependent(0, "Cannon");
	testBodyTurnCouplesHullAndTurret(2, "Laser");
	testTurretTurnIndependent(2, "Laser");
	testAlreadyAligned(0);
	testInsufficientTuNoMutation(2);
	testReservationPreflightBlocksNoMutation(0);
	testStrafeDisabledRejectsTurret(2);
	testPreflightAloneNoMutation();
	restoreStrafeOption();

	if (g_failures == 0)
	{
		std::cout << "companion_hwp_turn_engine_test: PASS" << std::endl;
		return 0;
	}
	std::cerr << "companion_hwp_turn_engine_test: FAIL (" << g_failures << ")" << std::endl;
	return 1;
}

#ifdef main
#undef main
#endif

int main()
{
	return companion_hwp_turn_engine_test_main();
}