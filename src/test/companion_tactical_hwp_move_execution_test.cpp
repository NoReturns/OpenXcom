/*
 * Open X-Com Companion — HWP_MOVE native execution tests (M1 HWP-S3B-02).
 */
#include "Engine/CompanionTacticalHwpMoveExecutionCore.h"
#include "Engine/CompanionTacticalHwpAuthorityCore.h"
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "Engine/CompanionWalkStopTokens.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/UnitWalkBState.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Savegame/Tile.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalHwpAuthorityCore;
using namespace OpenXcom::CompanionTacticalHwpMoveExecutionCore;
using namespace OpenXcom::CompanionCommandCore;
using namespace OpenXcom::CompanionSoldierControlPublishCore;
using namespace OpenXcom::CompanionSoldierOwnershipCore;

static int g_failures = 0;

static void drainBattleStates(BattlescapeGame *game, int maxSteps = 16)
{
	if (game == 0)
	{
		return;
	}
	for (int i = 0; i < maxSteps && game->isBusy(); ++i)
	{
		game->cancelCurrentAction();
	}
}

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		std::cerr.flush();
		++g_failures;
	}
}

static const char *SESSION = "a1b2c3d4e5f6789012345678abcdef01";
static const char *CAMPAIGN = "0123456789abcdef0123456789abcdef";
static const char *TACTICAL = "fedcba9876543210fedcba9876543210";
static const char *SNAPSHOT_SHA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static Mod &fixtureMod()
{
	static Mod mod;
	return mod;
}

static MapDataSet *floorDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("hwp_move_floor");
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

struct SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	SoldierFixture(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

struct TankArmorFixture
{
	Armor armor;
	TankArmorFixture() : armor("STR_HWP_MOVE_TEST_ARMOR")
	{
		armor.load(YAML::Load("type: STR_HWP_MOVE_TEST_ARMOR\nsize: 2\nmovementType: 0\n"));
	}
};

struct TacticalHarness
{
	SavedGame game;
	Base *base;
	SavedBattleGame *battle;
	BattlescapeGame *battleGame;
	TankArmorFixture tankArmor;
	std::vector<SoldierFixture *> soldierBundles;

	TacticalHarness(int mapW, int mapH)
		: base(new Base(&fixtureMod())),
		  battle(new SavedBattleGame()),
		  battleGame(0)
	{
		game.getBases()->push_back(base);
		battle->initMap(mapW, mapH, 1);
		battle->setTestMod(&fixtureMod());
		battle->initUtilities(&fixtureMod());
		battle->getCompanionEventLog().beginSession();
		wireOpenMap(battle, mapW, mapH);
		battleGame = new BattlescapeGame(battle, 0);
	}

	~TacticalHarness()
	{
		if (battleGame != 0)
		{
			if (battleGame->isBusy())
			{
				drainBattleStates(battleGame);
			}
			delete battleGame;
			battleGame = 0;
		}
		for (std::vector<SoldierFixture *>::iterator i = soldierBundles.begin(); i != soldierBundles.end(); ++i)
		{
			delete *i;
		}
		delete battle;
		if (base != 0)
		{
			base->getSoldiers()->clear();
		}
		game.getBases()->clear();
		delete base;
	}

	BattleUnit *addTank(int id, const Position &pos, int tu, bool assignAi)
	{
		Unit rules("STR_HWP_MOVE_TEST_TANK");
		StatAdjustment adjustment;
		BattleUnit *unit = new BattleUnit(&rules, FACTION_PLAYER, id, &tankArmor.armor, &adjustment, 0);
		unit->setDirection(4);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n";
		unit->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(unit);
		expect(battle->setUnitPosition(unit, pos), "2x2 HWP placed");
		if (assignAi)
		{
			expect(battle->assignCompanionTacticalHwpToAi(id), "tactical HWP AI assign");
		}
		return unit;
	}

	BattleUnit *addSoldier(int id, const Position &pos, int tu)
	{
		SoldierFixture *bundle = new SoldierFixture(id);
		soldierBundles.push_back(bundle);
		base->getSoldiers()->push_back(&bundle->soldier);
		BattleUnit *unit = new BattleUnit(&bundle->soldier, 0);
		unit->setPosition(pos);
		unit->setDirection(4);
		unit->setTimeUnits(tu);
		battle->getUnits()->push_back(unit);
		expect(battle->setUnitPosition(unit, pos), "soldier placed");
		return unit;
	}

	std::string controlJsonFor(int hwpId, bool hwpAiInRelay)
	{
		std::set<int> aiHwps;
		std::vector<int> liveHwps;
		if (hwpAiInRelay)
		{
			aiHwps.insert(hwpId);
			liveHwps.push_back(hwpId);
		}
		const ControlPublicationPlan plan = buildControlPublicationJson(
			battle->getCompanionEventLog().getSessionId(),
			CAMPAIGN,
			TACTICAL,
			std::set<int>(),
			std::vector<int>(),
			aiHwps,
			liveHwps);
		return plan.ok ? plan.jsonUtf8 : std::string();
	}

	HwpMoveValidationInput moveInputFor(int actorId, int dx, int dy, int dz, bool hwpAiInRelay)
	{
		HwpMoveValidationInput moveInput = {};
		moveInput.destX = dx;
		moveInput.destY = dy;
		moveInput.destZ = dz;
		moveInput.mapSizeX = battle->getMapSizeX();
		moveInput.mapSizeY = battle->getMapSizeY();
		moveInput.mapSizeZ = battle->getMapSizeZ();
		const std::string controlJson = controlJsonFor(actorId, hwpAiInRelay);
		HwpRemoteAuthorityInput &auth = moveInput.authority;
		auth.campaign = &game;
		auth.battle = battle;
		auth.actorUnitId = actorId;
		auth.controlFileJson = controlJson;
		auth.controlFileReadOk = !controlJson.empty();
		auth.battleSessionId = battle->getCompanionEventLog().getSessionId();
		auth.relayCampaignId = CAMPAIGN;
		auth.relayTacticalBattleId = TACTICAL;
		auth.commandSnapshotSha256 = SNAPSHOT_SHA;
		auth.lastSnapshotSha256 = SNAPSHOT_SHA;
		auth.commandSessionId = auth.battleSessionId;
		auth.commandTurn = 1;
		auth.turn = 1;
		auth.commandStateVersion = 3;
		auth.lastExportedStateVersion = 3;
		auth.battleInitialized = true;
		auth.playerTurn = true;
		auth.requireBindingFreshness = true;
		auth.battleBusy = false;
		auth.unitsFalling = false;
		auth.actorNativeActionInProgress = false;
		return moveInput;
	}
};

static void testLegalHwpFootprintIs2x2()
{
	TankArmorFixture armor;
	expect(armor.armor.getSize() == 2, "harness uses 2x2 armor for HWP footprint");
}

static void testTryStartRejectsWhenActorBusyFlagged()
{
	TacticalHarness h(8, 8);
	h.addTank(1'000'000, Position(2, 2, 0), 80, true);
	HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 3, 2, 0, true);
	moveInput.authority.actorNativeActionInProgress = true;
	HwpMoveStartOutcome start = {};
	expect(!tryStartHwpMove(h.battleGame, moveInput, &start), "actor busy flag rejected");
	expect(start.rejectCode == "ACTOR_BUSY", "ACTOR_BUSY");
}

static void testNoPathToDestinationRejectBeforeWalk()
{
	TacticalHarness h(6, 6);
	h.addTank(1'000'000, Position(0, 0, 0), 80, true);
	for (int y = 0; y < 6; ++y)
	{
		Tile *blocked = h.battle->getTile(Position(3, y, 0));
		if (blocked != 0)
		{
			MapData *wall = new MapData(floorDataSet());
			wall->setObjectType(O_OBJECT);
			wall->setTUCosts(255, 255, 255);
			blocked->setMapData(wall, 0, 0, O_OBJECT);
		}
	}
	const HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 5, 0, 0, true);
	HwpMoveStartOutcome start = {};
	expect(!tryStartHwpMove(h.battleGame, moveInput, &start), "no path rejects before walk");
	expect(start.rejectCode == "MOVE_START_FAILED", "MOVE_START_FAILED");
	expect(!h.battleGame->isBusy(), "no walk queued");
}

static void testOccupiedDestinationRejectBeforeWalk()
{
	TacticalHarness h(10, 8);
	h.addTank(1'000'000, Position(1, 1, 0), 80, true);
	h.addSoldier(2, Position(4, 1, 0), 40);
	const HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 4, 1, 0, true);
	HwpMoveStartOutcome start = {};
	expect(!tryStartHwpMove(h.battleGame, moveInput, &start), "occupied destination rejected");
	expect(!h.battleGame->isBusy(), "no movement started");
}

static void testWalkLifecycleUsesKnownInterruptToken()
{
	TacticalHarness h(6, 6);
	h.addTank(1'000'000, Position(1, 1, 0), 80, true);
	const HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 2, 1, 0, true);
	HwpMoveLifecycleObservation obs = {};
	obs.startPos = Position(1, 1, 0);
	obs.requestedDest = Position(2, 1, 0);
	obs.walkActionResult = CompanionWalkStopTokens::PATH_BLOCKED;
	WalkStopDiagnostics diag = {};
	expect(observeHwpMoveLifecycle(h.battleGame, moveInput.authority, obs, &diag) == CMD_INTERRUPTED,
		"path_blocked yields INTERRUPTED");
	expect(diag.movementStopReason == "path_blocked", "diagnostics use known token mapping");
}

static void testHumanOwnedHwpRejectedByAuthority()
{
	TacticalHarness h(8, 6);
	h.addTank(1'000'000, Position(1, 1, 0), 80, false);
	const HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 3, 1, 0, true);
	expect(!tryStartHwpMove(h.battleGame, moveInput, 0), "human-owned HWP rejected");
}

static void testDestroyedHwpRejected()
{
	TacticalHarness h(8, 6);
	BattleUnit *tank = h.addTank(1'000'000, Position(1, 1, 0), 80, true);
	tank->load(YAML::Load(
		"status: 6\nstunlevel: 0\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	const HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 3, 1, 0, true);
	expect(!tryStartHwpMove(h.battleGame, moveInput, 0), "destroyed HWP rejected");
}

static void testSoldierMoveRegressionAndHwpBlockedOnSoldierMove()
{
	TacticalHarness h(8, 6);
	BattleUnit *soldier = h.addSoldier(2, Position(1, 1, 0), 60);
	BattleUnit *tank = h.addTank(1'000'000, Position(4, 4, 0), 80, true);
	std::string reject;
	expect(h.battleGame->tryExecuteCompanionMove(soldier, Position(2, 1, 0), &reject), "soldier MOVE still works");
	if (h.battleGame->isBusy())
	{
		h.battleGame->cancelCurrentAction();
	}
	expect(!h.battleGame->tryExecuteCompanionMove(tank, Position(5, 4, 0), &reject),
		"soldier MOVE path rejects HWP actor");
	expect(reject.find("roster soldiers") != std::string::npos, "soldier-only guard message");
}

static void testRelayExecutionLockAndMovePreflight()
{
	expect(!hwpTacticalCommandExecutionSupported(), "global HWP execution lock remains off");
	expect(hwpMoveRelayExecutionSupported(), "HWP_MOVE relay gate on");
	TacticalHarness h(6, 6);
	h.addTank(1'000'000, Position(1, 1, 0), 80, true);
	const HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 2, 1, 0, true);
	const MoveValidationResult relay = validateHwpMoveExecutionPreflight(moveInput);
	expect(relay.accepted, "HWP_MOVE relay preflight accepts bound AI tank");
}

static void testBattleBusyRejectBeforeWalk()
{
	TacticalHarness h(8, 6);
	h.addTank(1'000'000, Position(1, 1, 0), 80, true);
	HwpMoveValidationInput moveInput = h.moveInputFor(1'000'000, 3, 1, 0, true);
	moveInput.authority.battleBusy = true;
	expect(!tryStartHwpMove(h.battleGame, moveInput, 0), "battle busy rejected");
}

int companion_tactical_hwp_move_execution_test_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	testRelayExecutionLockAndMovePreflight();
	testLegalHwpFootprintIs2x2();
	testTryStartRejectsWhenActorBusyFlagged();
	testNoPathToDestinationRejectBeforeWalk();
	testOccupiedDestinationRejectBeforeWalk();
	testWalkLifecycleUsesKnownInterruptToken();
	testHumanOwnedHwpRejectedByAuthority();
	testDestroyedHwpRejected();
	testSoldierMoveRegressionAndHwpBlockedOnSoldierMove();
	testBattleBusyRejectBeforeWalk();

	std::cout << "companion_tactical_hwp_move_execution_test: failures=" << g_failures << std::endl;
	if (g_failures != 0)
	{
		return 1;
	}
	std::cout << "companion_tactical_hwp_move_execution_test: all tests passed." << std::endl;
	return 0;
}