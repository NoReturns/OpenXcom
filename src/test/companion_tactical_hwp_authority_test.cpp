/*
 * Open X-Com Companion — HWP remote command authority tests (M1 HWP-S3A).
 */
#define SDL_MAIN_HANDLED
#include "Engine/CompanionTacticalHwpAuthorityCore.h"
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionTacticalAuthorityCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <sstream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalHwpAuthorityCore;
using namespace OpenXcom::CompanionTacticalAuthorityCore;
using namespace OpenXcom::CompanionSoldierOwnershipCore;
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

struct SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	SoldierFixture(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

struct FriendlyHwpFixture
{
	Unit rules;
	Armor armor;
	BattleUnit *unit;
	FriendlyHwpFixture(int id, const Position &pos)
		: rules("STR_HWP_OBS_TEST_TANK"), armor("STR_SECTOID_ARMOR"), unit(0)
	{
		StatAdjustment adjustment;
		unit = new BattleUnit(&rules, FACTION_PLAYER, id, &armor, &adjustment, 0);
		unit->setPosition(pos);
		unit->setDirection(4);
		unit->load(YAML::Load(
			"status: 0\nstunlevel: 0\nhealth: 120\ntu: 75\nenergy: 60\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"));
	}
};

struct TacticalFixture
{
	SavedGame game;
	Base *base;
	SavedBattleGame *battle;
	std::vector<SoldierFixture *> soldierBundles;
	std::vector<FriendlyHwpFixture *> hwpBundles;

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
		delete battle;
		for (size_t i = 0; i < hwpBundles.size(); ++i)
		{
			delete hwpBundles[i];
		}
		for (size_t i = 0; i < soldierBundles.size(); ++i)
		{
			delete soldierBundles[i];
		}
		if (base != 0)
		{
			base->getSoldiers()->clear();
		}
		game.getBases()->clear();
		delete base;
	}

	Soldier *addRosterSoldier(int id)
	{
		SoldierFixture *bundle = new SoldierFixture(id);
		soldierBundles.push_back(bundle);
		base->getSoldiers()->push_back(&bundle->soldier);
		return &bundle->soldier;
	}

	BattleUnit *addSoldierUnit(Soldier *soldier, const Position &pos)
	{
		BattleUnit *unit = new BattleUnit(soldier, 0);
		unit->setPosition(pos);
		unit->setDirection(4);
		battle->getUnits()->push_back(unit);
		return unit;
	}

	BattleUnit *addHwp(int id, const Position &pos)
	{
		FriendlyHwpFixture *bundle = new FriendlyHwpFixture(id, pos);
		hwpBundles.push_back(bundle);
		battle->getUnits()->push_back(bundle->unit);
		return bundle->unit;
	}
};

static std::string publishedControlJson(
	TacticalFixture &fx,
	const std::set<int> &aiSoldiers,
	const std::vector<int> &liveSoldiers,
	const std::set<int> &aiHwps,
	const std::vector<int> &liveHwps)
{
	const ControlPublicationPlan plan = buildControlPublicationJson(
		fx.battle->getCompanionEventLog().getSessionId(),
		CAMPAIGN,
		TACTICAL,
		aiSoldiers,
		liveSoldiers,
		aiHwps,
		liveHwps);
	return plan.ok ? plan.jsonUtf8 : std::string();
}

static HwpRemoteAuthorityInput baseInput(TacticalFixture &fx, int actorId, bool hwpAiInRelay)
{
	const std::string sessionId = fx.battle->getCompanionEventLog().getSessionId();
	std::set<int> aiHwps;
	std::vector<int> liveHwps;
	if (hwpAiInRelay)
	{
		aiHwps.insert(actorId);
		liveHwps.push_back(actorId);
	}
	const std::string controlJson = publishedControlJson(fx, std::set<int>(), std::vector<int>(), aiHwps, liveHwps);
	HwpRemoteAuthorityInput input = {};
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

static void testAiOwnedOperationalHwpPermitted()
{
	TacticalFixture fx;
	fx.addHwp(1000000, Position(2, 2, 0));
	expect(fx.battle->assignCompanionTacticalHwpToAi(1000000), "tactical HWP AI assign");
	const HwpRemoteAuthorityInput input = baseInput(fx, 1000000, true);
	const CompanionCommandCore::MoveValidationResult verdict = evaluateHwpRemoteCommandAuthority(input);
	expect(verdict.accepted, "AI-owned operational HWP passes authority");
}

static void testHumanOwnedHwpRejected()
{
	TacticalFixture fx;
	fx.addHwp(1000000, Position(2, 2, 0));
	const HwpRemoteAuthorityInput input = baseInput(fx, 1000000, true);
	expect(!evaluateHwpRemoteCommandAuthority(input).accepted, "human-owned HWP rejected");
	expect(evaluateHwpRemoteCommandAuthority(input).rejectCode == "NOT_AI_CONTROLLED",
		"human-owned HWP NOT_AI_CONTROLLED");
}

static void testDestroyedHwpRejected()
{
	TacticalFixture fx;
	BattleUnit *tank = fx.addHwp(1000000, Position(2, 2, 0));
	fx.battle->assignCompanionTacticalHwpToAi(1000000);
	tank->load(YAML::Load(
		"status: 6\nstunlevel: 0\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	const HwpRemoteAuthorityInput input = baseInput(fx, 1000000, true);
	expect(!evaluateHwpRemoteCommandAuthority(input).accepted, "destroyed HWP rejected");
}

static void testNonexistentHwpRejected()
{
	TacticalFixture fx;
	const HwpRemoteAuthorityInput input = baseInput(fx, 1000099, true);
	expect(!evaluateHwpRemoteCommandAuthority(input).accepted, "nonexistent HWP rejected");
}

static void testHostileRejected()
{
	TacticalFixture fx;
	Unit rules("STR_SECTOID");
	Armor armor("STR_SECTOID_ARMOR");
	StatAdjustment adjustment;
	BattleUnit *alien = new BattleUnit(&rules, FACTION_HOSTILE, 1000005, &armor, &adjustment, 0);
	alien->setPosition(Position(1, 1, 0));
	fx.battle->getUnits()->push_back(alien);
	fx.battle->assignCompanionTacticalHwpToAi(1000005);
	const HwpRemoteAuthorityInput input = baseInput(fx, 1000005, true);
	expect(!evaluateHwpRemoteCommandAuthority(input).accepted, "hostile unit rejected");
}

static void testSoldierIdAsHwpRejected()
{
	TacticalFixture fx;
	Soldier *roster = fx.addRosterSoldier(3);
	fx.addSoldierUnit(roster, Position(1, 1, 0));
	const HwpRemoteAuthorityInput input = baseInput(fx, 3, true);
	expect(!evaluateHwpRemoteCommandAuthority(input).accepted, "soldier id as HWP rejected");
}

static void testStaleSessionRejected()
{
	TacticalFixture fx;
	fx.addHwp(1000000, Position(2, 2, 0));
	fx.battle->assignCompanionTacticalHwpToAi(1000000);
	HwpRemoteAuthorityInput input = baseInput(fx, 1000000, true);
	input.commandSessionId = "00000000000000000000000000000000";
	expect(!evaluateHwpRemoteCommandAuthority(input).accepted, "stale session rejected");
	expect(evaluateHwpRemoteCommandAuthority(input).rejectCode == "STALE_SESSION", "stale session code");
}

static void testMixedSquadSoldierUnchanged()
{
	TacticalFixture fx;
	Soldier *roster = fx.addRosterSoldier(2);
	fx.addSoldierUnit(roster, Position(1, 1, 0));
	fx.addHwp(1000000, Position(2, 2, 0));
	fx.battle->assignCompanionTacticalHwpToAi(1000000);
	fx.game.assignCompanionSoldierToAi(2);
	std::set<int> soldierAi;
	soldierAi.insert(2);
	const std::string sessionId = fx.battle->getCompanionEventLog().getSessionId();
	std::set<int> aiHwps;
	aiHwps.insert(1000000);
	const std::string mixedControl = publishedControlJson(
		fx, soldierAi, std::vector<int>(1, 2), aiHwps, std::vector<int>(1, 1000000));
	const TacticalAuthorityInput soldierInput = buildTacticalAuthorityInput(
		true, soldierAi, true, 2, mixedControl, sessionId, CAMPAIGN, TACTICAL);
	expect(evaluateTacticalAuthority(soldierInput).aiCommandPermitted, "soldier AI authority unchanged");
	const HwpRemoteAuthorityInput hwpInput = baseInput(fx, 1000000, true);
	expect(evaluateHwpRemoteCommandAuthority(hwpInput).accepted, "HWP authority independent of soldier");
}

static void testExecutionStillUnavailable()
{
	expect(!hwpTacticalCommandExecutionSupported(), "HWP non-MOVE execution globally disabled");
	expect(hwpMoveRelayExecutionSupported(), "HWP_MOVE relay execution enabled");
	expect(hwpTurnRelayExecutionSupported(), "HWP_TURN relay execution enabled");
	expect(hwpFireRelayExecutionSupported(), "HWP_FIRE relay execution enabled");
	expect(hwpGuidedLaunchRelayExecutionSupported(), "HWP_GUIDED_LAUNCH relay execution enabled");
	TacticalFixture fx;
	fx.addHwp(1000000, Position(2, 2, 0));
	fx.battle->assignCompanionTacticalHwpToAi(1000000);
	const HwpRemoteAuthorityInput input = baseInput(fx, 1000000, true);
	const MoveValidationResult preflight =
		validateHwpTacticalCommandExecutionPreflight(input);
	expect(!preflight.accepted, "preflight rejects execution");
	expect(preflight.rejectCode == "HWP_EXECUTION_NOT_SUPPORTED", "HWP_EXECUTION_NOT_SUPPORTED");
}

static void testHwpMoveJsonParseAndMoveParserRejects()
{
	const std::string json =
		"{\"schema_version\":1,\"command_id\":\"hwp-move-1\",\"command_type\":\"HWP_MOVE\","
		"\"actor_unit_id\":1000000,\"destination\":{\"x\":2,\"y\":2,\"z\":0},"
		"\"snapshot_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		"\"session_id\":\"a1b2c3d4e5f6789012345678abcdef01\",\"turn\":1,\"state_version\":0}";
	MoveCommandParsed parsed = {};
	expect(parseHwpMoveCommandJson(json, &parsed), "parseHwpMoveCommandJson succeeds");
	expect(parsed.valid, "HWP_MOVE parsed valid");
	expect(parsed.commandType == "HWP_MOVE", "command_type HWP_MOVE");
	MoveCommandParsed moveParsed = {};
	expect(!parseMoveCommandJson(json, &moveParsed), "MOVE parser rejects HWP_MOVE");
}

static void testHwpMoveContractValidatedWithoutPath()
{
	TacticalFixture fx;
	fx.addHwp(1000000, Position(0, 0, 0));
	fx.battle->assignCompanionTacticalHwpToAi(1000000);
	HwpMoveValidationInput moveInput = {};
	moveInput.authority = baseInput(fx, 1000000, true);
	moveInput.destX = 2;
	moveInput.destY = 2;
	moveInput.destZ = 0;
	moveInput.mapSizeX = 4;
	moveInput.mapSizeY = 4;
	moveInput.mapSizeZ = 1;
	const MoveValidationResult contract = validateHwpMoveCommand(moveInput);
	expect(contract.accepted, "HWP_MOVE contract accepts in-bounds destination");
	const MoveValidationResult exec = validateHwpMoveExecutionPreflight(moveInput);
	expect(exec.accepted, "HWP_MOVE execution preflight accepts in-band AI HWP");
}

static void testHwpMoveInvalidDestinationRejected()
{
	TacticalFixture fx;
	fx.addHwp(1000000, Position(0, 0, 0));
	fx.battle->assignCompanionTacticalHwpToAi(1000000);
	HwpMoveValidationInput moveInput = {};
	moveInput.authority = baseInput(fx, 1000000, true);
	moveInput.destX = 9;
	moveInput.destY = 0;
	moveInput.destZ = 0;
	moveInput.mapSizeX = 4;
	moveInput.mapSizeY = 4;
	moveInput.mapSizeZ = 1;
	const MoveValidationResult contract = validateHwpMoveCommand(moveInput);
	expect(!contract.accepted, "OOB destination rejected");
	expect(contract.rejectCode == "INVALID_DESTINATION", "INVALID_DESTINATION");
}

int companion_tactical_hwp_authority_test_body()
{
	testAiOwnedOperationalHwpPermitted();
	testHumanOwnedHwpRejected();
	testDestroyedHwpRejected();
	testNonexistentHwpRejected();
	testHostileRejected();
	testSoldierIdAsHwpRejected();
	testStaleSessionRejected();
	testMixedSquadSoldierUnchanged();
	testExecutionStillUnavailable();
	testHwpMoveJsonParseAndMoveParserRejects();
	testHwpMoveContractValidatedWithoutPath();
	testHwpMoveInvalidDestinationRejected();

	if (g_failures == 0)
	{
		std::cout << "companion_tactical_hwp_authority_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_tactical_hwp_authority_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}