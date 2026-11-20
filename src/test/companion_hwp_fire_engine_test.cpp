/*
 * Open X-Com Companion — HWP_FIRE execution core (M1 HWP-S5-1b).
 */
#include "Engine/CompanionHwpFireExecutionCore.h"
#include "Engine/CompanionTacticalHwpAuthorityCore.h"
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionTuVisibilityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/UnitTurnBState.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Tile.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <sstream>
#include <string>

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionHwpFireExecutionCore;
using namespace OpenXcom::CompanionTacticalHwpAuthorityCore;
using namespace OpenXcom::CompanionCommandCore;
using namespace OpenXcom::CompanionTuVisibilityCore;

static int g_failures = 0;

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
		ds = new MapDataSet("hwp_fire_floor");
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

struct HwpFireWeaponRules
{
	RuleInventory rightHand;
	RuleItem cannon;
	RuleItem ammo;
	RuleItem laser;
	RuleItem rocketLauncher;
	RuleItem rocketAmmo;

	HwpFireWeaponRules(Mod *mod)
		: rightHand("STR_RIGHT_HAND"), cannon("STR_HWP_FIRE_CANNON"), ammo("STR_HWP_FIRE_AMMO"),
		  laser("STR_HWP_FIRE_LASER"), rocketLauncher("STR_HWP_FIRE_ROCKET_LAUNCHER"),
		  rocketAmmo("STR_HWP_FIRE_ROCKET_AMMO")
	{
		YAML::Node rh;
		rh["id"] = "STR_RIGHT_HAND";
		rh["x"] = 0;
		rh["y"] = 0;
		rh["type"] = static_cast<int>(INV_HAND);
		rh["costs"] = YAML::Node(YAML::NodeType::Map);
		rightHand.load(rh, 0);

		YAML::Node ammoNode;
		ammoNode["weight"] = 0;
		ammoNode["battleType"] = static_cast<int>(BT_AMMO);
		ammoNode["clipSize"] = 30;
		ammoNode["power"] = 60;
		ammoNode["damageType"] = static_cast<int>(DT_HE);
		ammo.load(ammoNode, mod, 0);

		YAML::Node cannonNode;
		cannonNode["weight"] = 0;
		cannonNode["battleType"] = static_cast<int>(BT_FIREARM);
		cannonNode["clipSize"] = 30;
		cannonNode["fixedWeapon"] = true;
		cannonNode["compatibleAmmo"] = YAML::Load("[STR_HWP_FIRE_AMMO]");
		cannonNode["accuracySnap"] = 50;
		cannonNode["tuSnap"] = 23;
		cannonNode["accuracyAimed"] = 54;
		cannonNode["tuAimed"] = 56;
		cannonNode["flatRate"] = true;
		cannon.load(cannonNode, mod, 0);

		YAML::Node laserNode;
		laserNode["weight"] = 0;
		laserNode["battleType"] = static_cast<int>(BT_FIREARM);
		laserNode["clipSize"] = -1;
		laserNode["fixedWeapon"] = true;
		laserNode["compatibleAmmo"] = YAML::Node(YAML::NodeType::Sequence);
		laserNode["accuracySnap"] = 30;
		laserNode["tuSnap"] = 23;
		laserNode["accuracyAimed"] = 51;
		laserNode["tuAimed"] = 52;
		laserNode["flatRate"] = true;
		laserNode["invWidth"] = 1;
		laserNode["invHeight"] = 3;
		laser.load(laserNode, mod, 0);

		YAML::Node rocketAmmoNode;
		rocketAmmoNode["weight"] = 0;
		rocketAmmoNode["battleType"] = static_cast<int>(BT_AMMO);
		rocketAmmoNode["clipSize"] = 8;
		rocketAmmoNode["power"] = 85;
		rocketAmmoNode["damageType"] = static_cast<int>(DT_HE);
		rocketAmmo.load(rocketAmmoNode, mod, 0);

		YAML::Node rocketLauncherNode;
		rocketLauncherNode["weight"] = 0;
		rocketLauncherNode["battleType"] = static_cast<int>(BT_FIREARM);
		rocketLauncherNode["clipSize"] = 0;
		rocketLauncherNode["fixedWeapon"] = true;
		rocketLauncherNode["compatibleAmmo"] = YAML::Load("[STR_HWP_FIRE_ROCKET_AMMO]");
		rocketLauncherNode["accuracySnap"] = 55;
		rocketLauncherNode["tuSnap"] = 54;
		rocketLauncherNode["accuracyAimed"] = 115;
		rocketLauncherNode["tuAimed"] = 90;
		rocketLauncherNode["flatRate"] = true;
		rocketLauncher.load(rocketLauncherNode, mod, 0);
	}
};

struct FireHarness
{
	Mod mod;
	HwpFireWeaponRules *weaponRules;
	SavedBattleGame battle;
	BattlescapeGame *game;
	Armor tankArmor;

	FireHarness()
		: weaponRules(0), battle(), game(0), tankArmor("STR_HWP_FIRE_ARMOR")
	{
		tankArmor.load(YAML::Load("type: STR_HWP_FIRE_ARMOR\nsize: 2\nmovementType: 0\n"));
		battle.setTestMod(&mod);
		battle.initMap(12, 12, 1);
		battle.initUtilities(&mod);
		battle.setTUReserved(BA_NONE);
		battle.setKneelReserved(false);
		weaponRules = new HwpFireWeaponRules(&mod);
		wireOpenMap(&battle, 12, 12);
		game = new BattlescapeGame(&battle, 0);
	}

	~FireHarness()
	{
		delete game;
		delete weaponRules;
	}

	BattleUnit *addTank(int id, const Position &pos, int tu, int turretType, int hullDir, int turretDir)
	{
		Unit rules("STR_HWP_FIRE_TANK");
		StatAdjustment adjustment;
		BattleUnit *unit = new BattleUnit(&rules, FACTION_PLAYER, id, &tankArmor, &adjustment, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"
		     << "direction: " << hullDir << "\ndirectionTurret: " << turretDir
		     << "\nturretType: " << turretType << "\n";
		unit->load(YAML::Load(yaml.str()));
		battle.getUnits()->push_back(unit);
		expect(battle.setUnitPosition(unit, pos), "HWP placed");
		return unit;
	}

	BattleItem *mountCannon(BattleUnit *tank)
	{
		BattleItem *cannon = new BattleItem(&weaponRules->cannon, battle.getCurrentItemId());
		BattleItem *clip = new BattleItem(&weaponRules->ammo, battle.getCurrentItemId());
		expect(cannon->setAmmoItem(clip) == 0, "cannon accepts ammo");
		cannon->setSlot(&weaponRules->rightHand);
		cannon->setSlotX(0);
		cannon->setSlotY(0);
		cannon->moveToOwner(tank);
		battle.getItems()->push_back(cannon);
		battle.getItems()->push_back(clip);
		return cannon;
	}

	BattleItem *mountRocket(BattleUnit *tank, int rounds = 8)
	{
		BattleItem *launcher = new BattleItem(&weaponRules->rocketLauncher, battle.getCurrentItemId());
		BattleItem *clip = new BattleItem(&weaponRules->rocketAmmo, battle.getCurrentItemId());
		clip->setAmmoQuantity(rounds);
		expect(launcher->setAmmoItem(clip) == 0, "rocket accepts ammo");
		expect(clip->getAmmoQuantity() == rounds, "rocket magazine quantity");
		launcher->setSlot(&weaponRules->rightHand);
		launcher->setSlotX(0);
		launcher->setSlotY(0);
		launcher->moveToOwner(tank);
		battle.getItems()->push_back(launcher);
		battle.getItems()->push_back(clip);
		return launcher;
	}

	BattleItem *mountLaser(BattleUnit *tank)
	{
		BattleItem *laser = new BattleItem(&weaponRules->laser, battle.getCurrentItemId());
		laser->setSlot(&weaponRules->rightHand);
		laser->setSlotX(0);
		laser->setSlotY(0);
		laser->moveToOwner(tank);
		battle.getItems()->push_back(laser);
		return laser;
	}
};

static void testParseHwpFireJson()
{
	const std::string snap =
		"{\"schema_version\":1,\"command_id\":\"fire-1\",\"command_type\":\"HWP_FIRE\","
		"\"actor_unit_id\":1000000,\"fire_mode\":\"snap\",\"weapon_battle_item_id\":0,"
		"\"target\":{\"x\":8,\"y\":5,\"z\":0},"
		"\"snapshot_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		"\"session_id\":\"a1b2c3d4e5f6789012345678abcdef01\",\"turn\":1,\"state_version\":0}";
	HwpFireCommandParsed parsed = {};
	expect(parseHwpFireCommandJson(snap, &parsed), "parse snap");
	expect(parsed.weaponBattleItemId == 0, "weapon id 0 allowed");

	const std::string autoBad =
		"{\"schema_version\":1,\"command_id\":\"bad\",\"command_type\":\"HWP_FIRE\","
		"\"actor_unit_id\":1000000,\"fire_mode\":\"auto\",\"weapon_battle_item_id\":0,"
		"\"target\":{\"x\":1,\"y\":1,\"z\":0},"
		"\"snapshot_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		"\"session_id\":\"a1b2c3d4e5f6789012345678abcdef01\",\"turn\":1,\"state_version\":0}";
	expect(!parseHwpFireCommandJson(autoBad, &parsed), "reject auto mode");
}

static void testCannonSnapStartsAsyncExecution()
{
	FireHarness h;
	const Position pos(3, 3, 0);
	const Position target(8, 3, 0);
	BattleUnit *tank = h.addTank(1'000'000, pos, 120, 2, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	const int weaponId = cannon->getId();
	const int ammoBefore = cannon->getAmmoItem()->getAmmoQuantity();
	const int tuBefore = tank->getTimeUnits();
	const int hullBefore = tank->getDirection();
	const int turretBefore = tank->getTurretDirection();

	HwpFireStartOutcome outcome = {};
	std::string reject;
	const bool started = tryStartHwpFire(h.game, tank, target, "snap", weaponId, &reject, &outcome);
	expect(started, started ? "cannon snap started" : reject.c_str());
	expect(outcome.started, "outcome started");
	expect(h.game->isBusy(), "async stack busy after start — not COMPLETED");
	expect(estimateDirectedFireFacingSteps(tank, target) == outcome.facingStepsEstimated,
		"turret facing advisory matches native estimator");

	// Drive a few ticks — native mechanics may spend turn/fire TU and consume ammo on completion path.
	for (int i = 0; i < 24 && h.game->isBusy(); ++i)
	{
		h.game->think();
	}
	expect(tank->getDirection() == hullBefore, "hull unchanged when only turret aimed for snap");
	const int tuAfter = tank->getTimeUnits();
	expect(tuAfter < tuBefore || h.game->isBusy(), "TU spent or async still in flight");
	if (!h.game->isBusy())
	{
		expect(cannon->getAmmoItem()->getAmmoQuantity() < ammoBefore, "cannon ammo consumed when shot resolves");
	}
}

static void testCannonAimedStartsAsyncExecution()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'008, Position(3, 3, 0), 120, 2, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	std::string reject;
	HwpFireStartOutcome outcome = {};
	expect(tryStartHwpFire(h.game, tank, Position(8, 3, 0), "aimed", cannon->getId(), &reject, &outcome),
		"cannon aimed started");
	expect(outcome.started, "aimed outcome started");
	expect(h.game->isBusy(), "aimed async — not completed");
}

static void testWeaponBattleItemIdZeroAccepted()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'009, Position(3, 3, 0), 120, 2, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	expect(cannon->getId() == 0, "fixture assigns battle item id 0");
	std::string reject;
	expect(tryStartHwpFire(h.game, tank, Position(7, 3, 0), "snap", 0, &reject, 0),
		"weapon_battle_item_id 0 accepted");
	expect(h.game->isBusy(), "id-0 shot async");
}

static void testLaserSnapWithoutMagazine()
{
	FireHarness h;
	const Position pos(5, 5, 0);
	const Position target(9, 5, 0);
	BattleUnit *tank = h.addTank(1'000'001, pos, 120, 2, 4, 6);
	BattleItem *laser = h.mountLaser(tank);
	std::string reject;
	HwpFireStartOutcome outcome = {};
	expect(tryStartHwpFire(h.game, tank, target, "snap", laser->getId(), &reject, &outcome), "laser snap");
	expect(!laser->needsAmmo(), "laser no magazine");
	expect(h.game->isBusy(), "laser shot async");
}

static void testEmptyCannonRejectedNoMutation()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'002, Position(2, 2, 0), 80, 0, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	cannon->getAmmoItem()->setAmmoQuantity(0);
	const int tuBefore = tank->getTimeUnits();
	std::string reject;
	expect(!preflightHwpFire(h.game, tank, Position(6, 2, 0), "snap", cannon->getId(), &reject, 0),
		"empty clip rejected");
	expect(tank->getTimeUnits() == tuBefore, "no TU spent on reject");
	expect(!h.game->isBusy(), "no async state on reject");
}

static void testWrongWeaponIdRejected()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'003, Position(2, 2, 0), 80, 0, 4, 6);
	h.mountCannon(tank);
	std::string reject;
	expect(!preflightHwpFire(h.game, tank, Position(6, 2, 0), "snap", 9999, &reject, 0), "wrong weapon id");
}

static void testInsufficientTuRejected()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'004, Position(2, 2, 0), 10, 0, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	const int tuBefore = tank->getTimeUnits();
	std::string reject;
	expect(!preflightHwpFire(h.game, tank, Position(10, 2, 0), "aimed", cannon->getId(), &reject, 0),
		"insufficient TU for aimed");
	expect(tank->getTimeUnits() == tuBefore, "TU unchanged");
}

static void testReservedTuRejectsFire()
{
	FireHarness h;
	h.battle.setTUReserved(BA_SNAPSHOT);
	BattleUnit *tank = h.addTank(1'000'005, Position(2, 2, 0), 40, 0, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	const int tuBefore = tank->getTimeUnits();
	std::string reject;
	expect(!preflightHwpFire(h.game, tank, Position(8, 2, 0), "snap", cannon->getId(), &reject, 0),
		"reserved TU blocks snap");
	expect(tank->getTimeUnits() == tuBefore, "TU unchanged under reserve block");
}

static void testInvalidTargetBounds()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'006, Position(2, 2, 0), 80, 0, 4, 6);
	BattleItem *cannon = h.mountCannon(tank);
	std::string reject;
	expect(!preflightHwpFire(h.game, tank, Position(99, 2, 0), "snap", cannon->getId(), &reject, 0),
		"OOB target rejected");
}

static void testFireRelayGateOn()
{
	expect(hwpFireRelayExecutionSupported(), "HWP_FIRE relay execution enabled");
}

static void testRocketSnapCompletesWithAmmoAndTu()
{
	FireHarness h;
	const Position target(8, 3, 0);
	BattleUnit *tank = h.addTank(1'000'010, Position(3, 3, 0), 120, 1, 4, 6);
	BattleItem *rocket = h.mountRocket(tank, 8);
	expect(rocket->getAmmoItem()->getAmmoQuantity() == 8, "rocket clip starts at 8");
	const int ammoBefore = rocket->getAmmoItem()->getAmmoQuantity();
	const int tuBefore = tank->getTimeUnits();
	const int snapTu = tank->getActionTUs(BA_SNAPSHOT, rocket);
	expect(snapTu > 0, "rocket snap TU");
	std::string reject;
	HwpFireStartOutcome outcome = {};
	expect(tryStartHwpFire(h.game, tank, target, "snap", rocket->getId(), &reject, &outcome), "rocket snap started");
	expect(h.game->isBusy(), "rocket async stack busy after start");
	for (int i = 0; i < 120000 && h.game->isBusy(); ++i)
	{
		h.game->think();
	}
	const int tuAfter = tank->getTimeUnits();
	expect(tuAfter < tuBefore || h.game->isBusy(), "rocket TU spent or stack still resolving");
	if (!h.game->isBusy())
	{
		expect(rocket->getAmmoItem()->getAmmoQuantity() == ammoBefore - 1, "rocket one round consumed when resolved");
	}
}

static void testRocketEmptyMagazineRejected()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'011, Position(2, 2, 0), 80, 1, 4, 4);
	BattleItem *rocket = h.mountRocket(tank, 0);
	rocket->getAmmoItem()->setAmmoQuantity(0);
	std::string reject;
	expect(!preflightHwpFire(h.game, tank, Position(6, 2, 0), "snap", rocket->getId(), &reject, 0),
		"empty rocket clip rejected");
}

static void testTurretVsHullFacingAdvisory()
{
	FireHarness h;
	BattleUnit *tank = h.addTank(1'000'007, Position(4, 4, 0), 80, 2, 4, 6);
	h.mountCannon(tank);
	const Position target(4, 8, 0);
	const int turretSteps = estimateDirectedFireFacingSteps(tank, target);
	const int hullSteps = octagonalTurnSteps(tank->getDirection(), tank->directionTo(target));
	expect(turretSteps != hullSteps, "turret and hull step counts differ on disagreement");
}

int main()
{
	testParseHwpFireJson();
	testCannonSnapStartsAsyncExecution();
	testCannonAimedStartsAsyncExecution();
	testWeaponBattleItemIdZeroAccepted();
	testLaserSnapWithoutMagazine();
	testEmptyCannonRejectedNoMutation();
	testWrongWeaponIdRejected();
	testInsufficientTuRejected();
	testReservedTuRejectsFire();
	testInvalidTargetBounds();
	testFireRelayGateOn();
	testRocketSnapCompletesWithAmmoAndTu();
	testRocketEmptyMagazineRejected();
	testTurretVsHullFacingAdvisory();

	if (g_failures == 0)
	{
		std::cout << "companion_hwp_fire_engine_test: PASS" << std::endl;
		return 0;
	}
	std::cerr << "companion_hwp_fire_engine_test: FAIL (" << g_failures << ")" << std::endl;
	return 1;
}