/*
 * Open X-Com Companion — MC hostile remote command authority tests (M1 MC-02B).
 */
#define SDL_MAIN_HANDLED
#include "Engine/CompanionTacticalMcHostileAuthorityCore.h"
#include "Engine/CompanionTacticalMcHostilePostureExecutionCore.h"
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalMcHostileAuthorityCore;
using namespace OpenXcom::CompanionTacticalMcHostilePostureExecutionCore;
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

static const char *SESSION = "a1b2c3d4e5f6789012345678abcdef01";
static const char *CAMPAIGN = "0123456789abcdef0123456789abcdef";
static const char *TACTICAL = "fedcba9876543210fedcba9876543210";
static const char *SNAPSHOT_SHA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static Mod &fixtureMod()
{
	static Mod mod;
	return mod;
}

struct McSectoidFixture
{
	Unit rules;
	Armor armor;
	BattleUnit *unit;
	McSectoidFixture(int id, const Position &pos)
		: rules("STR_SECTOID"), armor("STR_SECTOID_ARMOR"), unit(0)
	{
		StatAdjustment adjustment;
		unit = new BattleUnit(&rules, FACTION_HOSTILE, id, &armor, &adjustment, 0);
		unit->setPosition(pos);
		unit->load(YAML::Load(
			"faction: 0\noriginalFaction: 1\nstatus: 0\nstunlevel: 0\nhealth: 26\ntu: 60\n"
			"energy: 50\nmorale: 80\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	}
};

struct TacticalFixture
{
	SavedGame game;
	Base *base;
	SavedBattleGame *battle;
	std::vector<McSectoidFixture *> mcBundles;

	TacticalFixture()
		: base(new Base(&fixtureMod())),
		  battle(new SavedBattleGame())
	{
		game.getBases()->push_back(base);
		battle->initMap(4, 4, 1);
		battle->setTestMod(&fixtureMod());
		battle->getCompanionEventLog().beginSession();
	}

	~TacticalFixture()
	{
		for (size_t i = 0; i < mcBundles.size(); ++i)
		{
			delete mcBundles[i];
		}
		mcBundles.clear();
		delete battle;
		game.getBases()->clear();
		delete base;
	}

	BattleUnit *addMcHostile(int id, const Position &pos)
	{
		McSectoidFixture *bundle = new McSectoidFixture(id, pos);
		mcBundles.push_back(bundle);
		battle->getUnits()->push_back(bundle->unit);
		return bundle->unit;
	}
};

static std::string publishedControlJson(
	TacticalFixture &fx,
	const std::set<int> &aiMc,
	const std::vector<int> &liveMc)
{
	const ControlPublicationPlan plan = buildControlPublicationJson(
		fx.battle->getCompanionEventLog().getSessionId(),
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

static McRemoteAuthorityInput baseInput(TacticalFixture &fx, int actorId, bool mcAiInRelay)
{
	const std::string sessionId = fx.battle->getCompanionEventLog().getSessionId();
	std::set<int> aiMc;
	std::vector<int> liveMc;
	if (mcAiInRelay)
	{
		aiMc.insert(actorId);
		liveMc.push_back(actorId);
	}
	const std::string controlJson = publishedControlJson(fx, aiMc, liveMc);
	McRemoteAuthorityInput input = {};
	input.campaign = &fx.game;
	input.battle = fx.battle;
	input.actorUnitId = actorId;
	input.controlFileJson = controlJson;
	input.controlFileReadOk = !controlJson.empty();
	input.battleSessionId = sessionId;
	input.relayCampaignId = CAMPAIGN;
	input.relayTacticalBattleId = TACTICAL;
	input.commandSnapshotSha256 = SNAPSHOT_SHA;
	input.lastSnapshotSha256 = SNAPSHOT_SHA;
	input.commandSessionId = sessionId;
	input.commandTurn = 1;
	input.turn = 1;
	input.commandStateVersion = 3;
	input.lastExportedStateVersion = 3;
	input.battleInitialized = true;
	input.playerTurn = true;
	input.requireBindingFreshness = true;
	return input;
}

static void testAiOwnedMcHostilePermitted()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(2, 2, 0));
	expect(fx.battle->assignCompanionTacticalMcHostileToAi(1000042), "tactical MC AI assign");
	const McRemoteAuthorityInput input = baseInput(fx, 1000042, true);
	expect(evaluateMcRemoteCommandAuthority(input).accepted, "AI-owned MC hostile passes authority");
}

static void testMcMoveJsonParse()
{
	const std::string json =
		"{\"schema_version\":1,\"command_id\":\"mc-move-1\",\"command_type\":\"MC_MOVE\","
		"\"actor_unit_id\":1000042,\"destination\":{\"x\":2,\"y\":2,\"z\":0},"
		"\"snapshot_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		"\"session_id\":\"a1b2c3d4e5f6789012345678abcdef01\",\"turn\":1,\"state_version\":0}";
	MoveCommandParsed parsed = {};
	expect(parseMcMoveCommandJson(json, &parsed), "parseMcMoveCommandJson succeeds");
	expect(parsed.commandType == "MC_MOVE", "command_type MC_MOVE");
	expect(!parseHwpMoveCommandJson(json, &parsed), "HWP parser rejects MC_MOVE");
}

static void testMcMoveContract()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(0, 0, 0));
	fx.battle->assignCompanionTacticalMcHostileToAi(1000042);
	McMoveValidationInput moveInput = {};
	moveInput.authority = baseInput(fx, 1000042, true);
	moveInput.destX = 2;
	moveInput.destY = 2;
	moveInput.destZ = 0;
	moveInput.mapSizeX = 4;
	moveInput.mapSizeY = 4;
	moveInput.mapSizeZ = 1;
	expect(validateMcMoveExecutionPreflight(moveInput).accepted, "MC_MOVE execution preflight accepts");
}

static McMoveValidationInput sampleMcMoveInput(TacticalFixture &fx, int actorId, bool mcAiInRelay)
{
	McMoveValidationInput moveInput = {};
	moveInput.authority = baseInput(fx, actorId, mcAiInRelay);
	moveInput.destX = 2;
	moveInput.destY = 2;
	moveInput.destZ = 0;
	moveInput.mapSizeX = fx.battle->getMapSizeX();
	moveInput.mapSizeY = fx.battle->getMapSizeY();
	moveInput.mapSizeZ = fx.battle->getMapSizeZ();
	return moveInput;
}

static void testReclaimedMcHostileRejected()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(1, 1, 0));
	expect(fx.battle->assignCompanionTacticalMcHostileToAi(1000042), "assign before reclaim");
	expect(fx.battle->reclaimCompanionTacticalMcHostileForHuman(1000042), "reclaim MC hostile");
	const McRemoteAuthorityInput input = baseInput(fx, 1000042, true);
	const MoveValidationResult verdict = evaluateMcRemoteCommandAuthority(input);
	expect(!verdict.accepted, "reclaimed MC hostile rejected");
	expect(verdict.rejectCode == "NOT_AI_CONTROLLED", "reclaimed MC NOT_AI_CONTROLLED");
	expect(!validateMcMoveExecutionPreflight(sampleMcMoveInput(fx, 1000042, true)).accepted,
		"reclaimed MC MOVE preflight rejected");
}

static void testHumanRelayBindingRejectsMcHostile()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(1, 1, 0));
	expect(fx.battle->assignCompanionTacticalMcHostileToAi(1000042), "tactical assign remains");
	const MoveValidationResult verdict = evaluateMcRemoteCommandAuthority(baseInput(fx, 1000042, false));
	expect(!verdict.accepted, "human relay binding rejects MC");
	expect(verdict.rejectCode == "NOT_AI_CONTROLLED", "human relay NOT_AI_CONTROLLED");
}

static void testStaleSnapshotRejected()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(1, 1, 0));
	fx.battle->assignCompanionTacticalMcHostileToAi(1000042);
	McRemoteAuthorityInput input = baseInput(fx, 1000042, true);
	input.commandSnapshotSha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	expect(!evaluateMcRemoteCommandAuthority(input).accepted, "stale snapshot rejected");
	expect(evaluateMcRemoteCommandAuthority(input).rejectCode == "STALE_SNAPSHOT", "STALE_SNAPSHOT");
}

static void testStaleStateVersionRejected()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(1, 1, 0));
	fx.battle->assignCompanionTacticalMcHostileToAi(1000042);
	McRemoteAuthorityInput input = baseInput(fx, 1000042, true);
	input.commandStateVersion = 99;
	expect(!evaluateMcRemoteCommandAuthority(input).accepted, "stale state_version rejected");
	expect(evaluateMcRemoteCommandAuthority(input).rejectCode == "STALE_STATE_VERSION", "STALE_STATE_VERSION");
}

static void testMcKneelPreflightRejectsStockAlienPosture()
{
	TacticalFixture fx;
	BattleUnit *alien = fx.addMcHostile(1000042, Position(1, 1, 0));
	fx.battle->assignCompanionTacticalMcHostileToAi(1000042);
	McKneelValidationInput kneelInput = {};
	kneelInput.authority = baseInput(fx, 1000042, true);
	kneelInput.actorAlreadyKneeled = alien != 0 && alien->isKneeled();
	kneelInput.actorCanChangePosture =
		alien != 0 && !alien->isFloating() && alien->getType() == "SOLDIER";
	kneelInput.insufficientTuForKneelDown = false;
	const MoveValidationResult verdict = validateMcKneelExecutionPreflight(kneelInput);
	expect(!verdict.accepted, "MC_KNEEL preflight rejects alien");
	expect(verdict.rejectCode == "INVALID_ACTOR", "MC_KNEEL INVALID_ACTOR posture");
}

static void testMcTurnFacePreflightAcceptsBoundAlien()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(2, 2, 0));
	fx.battle->assignCompanionTacticalMcHostileToAi(1000042);
	McTurnFaceValidationInput turnInput = {};
	turnInput.authority = baseInput(fx, 1000042, true);
	turnInput.targetX = 3;
	turnInput.targetY = 2;
	turnInput.targetZ = 0;
	turnInput.mapSizeX = 4;
	turnInput.mapSizeY = 4;
	turnInput.mapSizeZ = 1;
	turnInput.targetSameHorizontalAsActor = false;
	turnInput.insufficientTuForTurn = false;
	expect(validateMcTurnFaceExecutionPreflight(turnInput).accepted, "MC_TURN_FACE preflight accepts bound alien");
}

static void testWrongTurnRejected()
{
	TacticalFixture fx;
	fx.addMcHostile(1000042, Position(1, 1, 0));
	fx.battle->assignCompanionTacticalMcHostileToAi(1000042);
	McRemoteAuthorityInput input = baseInput(fx, 1000042, true);
	input.playerTurn = false;
	const MoveValidationResult verdict = evaluateMcRemoteCommandAuthority(input);
	expect(!verdict.accepted, "enemy turn rejected");
	expect(verdict.rejectCode == "WRONG_TURN", "WRONG_TURN");
	McMoveValidationInput moveInput = sampleMcMoveInput(fx, 1000042, true);
	moveInput.authority.playerTurn = false;
	expect(validateMcMoveExecutionPreflight(moveInput).rejectCode == "WRONG_TURN", "MC_MOVE preflight WRONG_TURN");
}

int companion_tactical_mc_hostile_authority_test_body()
{
	expect(mcMoveRelayExecutionSupported(), "MC_MOVE relay gate on");
	testAiOwnedMcHostilePermitted();
	testMcMoveJsonParse();
	testMcMoveContract();
	testReclaimedMcHostileRejected();
	testHumanRelayBindingRejectsMcHostile();
	testStaleSnapshotRejected();
	testStaleStateVersionRejected();
	testWrongTurnRejected();
	testMcKneelPreflightRejectsStockAlienPosture();
	testMcTurnFacePreflightAcceptsBoundAlien();

	if (g_failures == 0)
	{
		std::cout << "companion_tactical_mc_hostile_authority_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_tactical_mc_hostile_authority_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}