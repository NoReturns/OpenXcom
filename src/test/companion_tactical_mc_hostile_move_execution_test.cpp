/*
 * Open X-Com Companion — MC_MOVE native execution tests (M1 MC-02B).
 */
#define SDL_MAIN_HANDLED
#include "Engine/CompanionTacticalMcHostileMoveExecutionCore.h"
#include "Engine/CompanionTacticalMcHostileAuthorityCore.h"
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Savegame/Tile.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalMcHostileAuthorityCore;
using namespace OpenXcom::CompanionTacticalMcHostileMoveExecutionCore;
using namespace OpenXcom::CompanionSoldierControlPublishCore;
using namespace OpenXcom::CompanionCommandCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

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
		ds = new MapDataSet("mc_move_floor");
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

static void wireUnitAt(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile exists for unit placement");
	if (tile == 0)
	{
		return;
	}
	unit->setPosition(pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
	expect(battle->setUnitPosition(unit, pos), "setUnitPosition for MC hostile");
}

struct McHostileFixture
{
	Unit rules;
	Armor armor;
	BattleUnit *unit;

	McHostileFixture(int id, const Position &pos, int tu)
		: rules("STR_MC_MOVE_TEST_ALIEN"),
		  armor("STR_MC_MOVE_TEST_ARMOR"),
		  unit(0)
	{
		armor.load(YAML::Load("type: STR_MC_MOVE_TEST_ARMOR\nsize: 1\nmovementType: 0\n"));
		StatAdjustment adjustment;
		unit = new BattleUnit(&rules, FACTION_HOSTILE, id, &armor, &adjustment, 0);
		unit->setDirection(4);
		unit->load(YAML::Load(
			"faction: 0\noriginalFaction: 1\nstatus: 0\nstunlevel: 0\nhealth: 26\n"
			+ std::string("tu: ") + std::to_string(tu) +
			"\nenergy: 50\nmorale: 80\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
		unit->setPosition(pos);
	}
};

struct TacticalFixture
{
	SavedGame game;
	Base *base;
	SavedBattleGame *battle;
	std::vector<McHostileFixture *> mcBundles;

	TacticalFixture(int mapW, int mapH)
		: base(new Base(&fixtureMod())),
		  battle(new SavedBattleGame())
	{
		game.getBases()->push_back(base);
		battle->initMap(mapW, mapH, 1);
		battle->setTestMod(&fixtureMod());
		battle->getCompanionEventLog().beginSession();
	}

	~TacticalFixture()
	{
		delete battle;
		battle = 0;
		for (size_t i = 0; i < mcBundles.size(); ++i)
		{
			delete mcBundles[i];
		}
		mcBundles.clear();
		game.getBases()->clear();
		delete base;
		base = 0;
	}

	BattleUnit *addMcHostile(int id, const Position &pos, int tu, bool assignTacticalAi)
	{
		McHostileFixture *bundle = new McHostileFixture(id, pos, tu);
		mcBundles.push_back(bundle);
		battle->getUnits()->push_back(bundle->unit);
		if (assignTacticalAi)
		{
			expect(battle->assignCompanionTacticalMcHostileToAi(id), "tactical MC AI assign");
		}
		return bundle->unit;
	}

	std::string controlJsonFor(int mcId, bool mcAiInRelay)
	{
		std::set<int> aiMc;
		std::vector<int> liveMc;
		if (mcAiInRelay)
		{
			aiMc.insert(mcId);
			liveMc.push_back(mcId);
		}
		const ControlPublicationPlan plan = buildControlPublicationJson(
			battle->getCompanionEventLog().getSessionId(),
			CAMPAIGN,
			TACTICAL,
			std::set<int>(),
			std::vector<int>(),
			std::set<int>(),
			std::vector<int>(),
			aiMc,
			liveMc);
		return plan.ok ? plan.jsonUtf8 : std::string();
	}

	McMoveValidationInput moveInputFor(int actorId, int dx, int dy, int dz, bool mcAiInRelay)
	{
		McMoveValidationInput moveInput = {};
		moveInput.destX = dx;
		moveInput.destY = dy;
		moveInput.destZ = dz;
		moveInput.mapSizeX = battle->getMapSizeX();
		moveInput.mapSizeY = battle->getMapSizeY();
		moveInput.mapSizeZ = battle->getMapSizeZ();
		const std::string controlJson = controlJsonFor(actorId, mcAiInRelay);
		McRemoteAuthorityInput &auth = moveInput.authority;
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
		return moveInput;
	}
};

struct TacticalHarness : TacticalFixture
{
	BattlescapeGame *battleGame;

	TacticalHarness(int mapW, int mapH)
		: TacticalFixture(mapW, mapH),
		  battleGame(0)
	{
		battle->initUtilities(&fixtureMod());
		wireOpenMap(battle, mapW, mapH);
		battleGame = new BattlescapeGame(battle, 0);
	}

	~TacticalHarness()
	{
		if (battleGame != 0)
		{
			delete battleGame;
			battleGame = 0;
		}
	}

	BattleUnit *addMcHostileOnMap(int id, const Position &pos, int tu, bool assignTacticalAi)
	{
		BattleUnit *unit = addMcHostile(id, pos, tu, assignTacticalAi);
		wireUnitAt(battle, unit, pos);
		return unit;
	}
};

static void testRelayPreflightAcceptsBoundMcHostile()
{
	expect(mcMoveRelayExecutionSupported(), "MC_MOVE relay gate on");
	TacticalFixture h(6, 6);
	h.addMcHostile(1'000'042, Position(1, 1, 0), 60, true);
	const McMoveValidationInput moveInput = h.moveInputFor(1'000'042, 2, 1, 0, true);
	expect(validateMcMoveExecutionPreflight(moveInput).accepted, "MC_MOVE execution preflight accepts");
}

static void testMcWalkQueuesPlayerOrderedUnitWalk()
{
	// Headless BattlescapeGame has no Map; full tile+TU completion belongs in
	// companion_mc_hostile_move_relay_terminal_test (HWP S3B-03A pattern).
	TacticalHarness h(6, 6);
	const Position from(1, 1, 0);
	const Position dest(2, 1, 0);
	BattleUnit *alien = h.addMcHostileOnMap(1'000'042, from, 60, true);
	const McMoveValidationInput moveInput = h.moveInputFor(1'000'042, dest.x, dest.y, dest.z, true);
	McMoveStartOutcome walkStart = {};
	expect(tryStartMcMove(h.battleGame, moveInput, &walkStart), "tryStartMcMove starts walk");
	expect(walkStart.started, "started flag");
	expect(walkStart.startPos == from, "start position recorded");
	expect(h.battleGame->isBusy(), "battle busy while UnitWalkBState queued");
	const BattleAction *current = h.battleGame->getCurrentAction();
	expect(current != 0 && current->playerOrdered, "MC walk current action is playerOrdered");
	expect(current->type == BA_WALK, "MC walk action type");
	expect(current->target == dest, "MC walk target");
	BattleAction queued = {};
	expect(h.battleGame->getRearmostQueuedBattleAction(&queued), "queued walk state");
	expect(queued.playerOrdered, "UnitWalkBState copy playerOrdered");
	expect(queued.type == BA_WALK, "queued walk type");
	expect(queued.target == dest, "queued destination");
	(void)alien;
}

static void testUnassignedMcHostileRejectedBeforeWalk()
{
	TacticalHarness h(6, 6);
	h.addMcHostileOnMap(1'000'042, Position(1, 1, 0), 60, false);
	const McMoveValidationInput moveInput = h.moveInputFor(1'000'042, 2, 1, 0, true);
	McMoveStartOutcome rejected = {};
	expect(!tryStartMcMove(h.battleGame, moveInput, &rejected), "unassigned MC rejected");
	expect(!h.battleGame->isBusy(), "no walk queued");
}

static void testReclaimedMcHostileRejectedBeforeWalk()
{
	TacticalHarness h(6, 6);
	h.addMcHostileOnMap(1'000'042, Position(1, 1, 0), 60, true);
	expect(h.battle->reclaimCompanionTacticalMcHostileForHuman(1'000'042), "reclaim for walk gate");
	const McMoveValidationInput moveInput = h.moveInputFor(1'000'042, 2, 1, 0, true);
	McMoveStartOutcome rejected = {};
	expect(!tryStartMcMove(h.battleGame, moveInput, &rejected), "reclaimed MC cannot start walk");
	expect(rejected.rejectCode == "NOT_AI_CONTROLLED", "reclaimed NOT_AI_CONTROLLED");
	expect(!h.battleGame->isBusy(), "no walk after reclaim");
}

static void testAuthorityLossMarksLifecycleInterrupted()
{
	TacticalHarness h(6, 6);
	h.addMcHostileOnMap(1'000'042, Position(1, 1, 0), 60, true);
	const Position dest(4, 1, 0);
	const McMoveValidationInput moveInput = h.moveInputFor(1'000'042, dest.x, dest.y, dest.z, true);
	McMoveStartOutcome started = {};
	expect(tryStartMcMove(h.battleGame, moveInput, &started), "walk queued for lifecycle");
	expect(h.battle->reclaimCompanionTacticalMcHostileForHuman(1'000'042), "reclaim before observe");
	if (h.battleGame->isBusy())
	{
		h.battleGame->cancelCurrentAction();
	}
	McMoveLifecycleObservation obs = {};
	obs.startPos = started.startPos;
	obs.requestedDest = dest;
	McMoveValidationInput afterReclaim = h.moveInputFor(1'000'042, dest.x, dest.y, dest.z, true);
	expect(observeMcMoveLifecycle(h.battleGame, afterReclaim.authority, obs, 0) == CMD_INTERRUPTED,
		"observeMcMoveLifecycle INTERRUPTED after reclaim");
}

int companion_tactical_mc_hostile_move_execution_test_body()
{
	testRelayPreflightAcceptsBoundMcHostile();
	testUnassignedMcHostileRejectedBeforeWalk();
	testReclaimedMcHostileRejectedBeforeWalk();
	testMcWalkQueuesPlayerOrderedUnitWalk();
	testAuthorityLossMarksLifecycleInterrupted();

	if (g_failures == 0)
	{
		std::cout << "companion_tactical_mc_hostile_move_execution_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_tactical_mc_hostile_move_execution_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}