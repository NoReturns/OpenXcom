/*
 * Open X-Com Companion — canonical tactical authority tests (M1-23F1).
 */
#include "Engine/CompanionTacticalAuthorityCore.h"
#include "Engine/CompanionCampaignOwnershipCore.h"
#include "Engine/CompanionCommandCore.h"
#include <iostream>
#include <set>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalAuthorityCore;
using namespace OpenXcom::CompanionCampaignOwnershipCore;
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

static std::string controlWithAi(int unitId)
{
	return std::string("{\n  \"schema_version\": 1,\n  \"session_id\": \"") + SESSION +
	       "\",\n  \"assignments\": { \"" + std::to_string(unitId) + "\": \"ai\" }\n}\n";
}

static TacticalAuthorityDecision decide(
	bool campaignContextPresent,
	const std::set<int> &currentAiIds,
	bool live,
	int unitId,
	const std::string &controlJson,
	const std::string &sessionId)
{
	const TacticalAuthorityInput input = buildTacticalAuthorityInput(
		campaignContextPresent, currentAiIds, live, unitId, controlJson, sessionId);
	return evaluateTacticalAuthority(input);
}

static void testHumanCampaignRelayAi()
{
	std::set<int> currentAi;
	const TacticalAuthorityDecision d = decide(true, currentAi, true, 1, controlWithAi(1), SESSION);
	expect(!d.aiCommandPermitted, "HUMAN campaign + relay AI must deny AI command");
	expect(d.humanOrderPermitted, "HUMAN campaign + relay AI must allow human orders");
}

static void testAiCampaignRelayAiMatchingSession()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision d = decide(true, currentAi, true, 1, controlWithAi(1), SESSION);
	expect(d.aiCommandPermitted, "AI campaign + relay AI + session must allow AI command");
	expect(!d.humanOrderPermitted, "AI campaign must deny human orders");
}

static void testAiCampaignMissingRelay()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision d = decide(true, currentAi, true, 1, std::string(), SESSION);
	expect(!d.aiCommandPermitted, "AI campaign + missing relay must deny AI command");
	expect(!d.humanOrderPermitted, "AI campaign must deny human orders when relay missing");
}

static void testAiCampaignMalformedRelay()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision d = decide(true, currentAi, true, 1, "{ not json", SESSION);
	expect(!d.aiCommandPermitted, "AI campaign + malformed relay must deny AI command");
	expect(!d.humanOrderPermitted, "AI campaign must deny human orders when relay malformed");
}

static void testAiCampaignWrongSessionRelay()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision d = decide(
		true, currentAi, true, 1, controlWithAi(1), "00000000000000000000000000000000");
	expect(!d.aiCommandPermitted, "AI campaign + wrong session relay must deny AI command");
	expect(!d.humanOrderPermitted, "AI campaign must deny human orders on wrong session");
}

static void testReclaimedHumanStaleRelayAi()
{
	std::set<int> currentAi;
	const TacticalAuthorityDecision d = decide(true, currentAi, true, 1, controlWithAi(1), SESSION);
	expect(!d.aiCommandPermitted, "reclaimed HUMAN + stale relay AI must deny AI command");
	expect(d.humanOrderPermitted, "reclaimed HUMAN + stale relay AI must allow human orders");
}

static void testInvalidDeadSoldier()
{
	std::set<int> currentAi;
	currentAi.insert(5);
	const TacticalAuthorityDecision d = decide(true, currentAi, false, 5, controlWithAi(5), SESSION);
	expect(!d.aiCommandPermitted, "invalid/dead soldier must deny AI command");
	expect(!d.humanOrderPermitted, "invalid/dead soldier must deny human orders");
}

static void testMissingCampaignContext()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision d = decide(false, currentAi, true, 1, controlWithAi(1), SESSION);
	expect(!d.aiCommandPermitted, "missing campaign context must deny AI command");
	expect(!d.humanOrderPermitted, "missing campaign context must deny human orders");
}

static void testUnsavedAiUsesCurrentCampaignSet()
{
	std::set<int> currentAi;
	std::set<int> persistedAi;
	currentAi.insert(7);
	expect(isCompanionSoldierAssignmentUnsaved(currentAi, persistedAi, 7),
		"fixture: unsaved AI assignment on soldier 7");
	const TacticalAuthorityDecision d = decide(true, currentAi, true, 7, controlWithAi(7), SESSION);
	expect(d.aiCommandPermitted, "UNSAVED AI in current campaign set must permit AI when relay matches");
	expect(!d.humanOrderPermitted, "UNSAVED AI must deny human orders");
	const TacticalAuthorityDecision persistedOnly = decide(true, persistedAi, true, 7, controlWithAi(7), SESSION);
	expect(!persistedOnly.aiCommandPermitted,
		"last-saved-only set must not grant AI when current campaign reclaimed");
	expect(persistedOnly.humanOrderPermitted,
		"when current campaign is HUMAN, human orders allowed despite stale relay");
}

static void testResolveCampaignModeMatchesSavedGameSemantics()
{
	std::set<int> ai;
	ai.insert(3);
	expect(resolveCampaignSoldierControlMode(ai, 3, true) == CompanionSoldierOwnershipCore::CONTROL_AI,
		"live soldier in current AI set resolves AI");
	expect(resolveCampaignSoldierControlMode(ai, 3, false) == CompanionSoldierOwnershipCore::CONTROL_HUMAN,
		"non-live soldier in AI set resolves HUMAN");
	expect(resolveCampaignSoldierControlMode(ai, 4, true) == CompanionSoldierOwnershipCore::CONTROL_HUMAN,
		"soldier not in AI set resolves HUMAN");
}

static MoveValidationInput executorValidationInputFromAuthority(
	const TacticalAuthorityDecision &authority,
	bool staleRelayHumanOverride,
	bool livePlayerSoldier = true)
{
	MoveValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = SESSION;
	input.turn = 2;
	input.commandSessionId = SESSION;
	input.commandTurn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = livePlayerSoldier;
	input.actorAlive = livePlayerSoldier;
	input.actorAiControlled = authority.aiCommandPermitted;
	input.destX = 3;
	input.destY = 4;
	input.destZ = 0;
	input.mapSizeX = 10;
	input.mapSizeY = 10;
	input.mapSizeZ = 1;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = staleRelayHumanOverride;
	return input;
}

static void testExecutorValidationCampaignAiValidRelay()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision authority = decide(true, currentAi, true, 1, controlWithAi(1), SESSION);
	const MoveValidationInput input = executorValidationInputFromAuthority(authority, false);
	expect(validateMoveCommand(input).accepted, "executor path: campaign AI + valid relay accepts MOVE");
}

static void testExecutorValidationCampaignHumanStaleRelayRejected()
{
	std::set<int> currentAi;
	const TacticalAuthorityInput authInput = buildTacticalAuthorityInput(
		true, currentAi, true, 1, controlWithAi(1), SESSION);
	const TacticalAuthorityDecision authority = evaluateTacticalAuthority(authInput);
	const bool staleRelay = isStaleRelayAiAfterCampaignHumanAssignment(authInput, controlWithAi(1), SESSION);
	const MoveValidationInput input = executorValidationInputFromAuthority(authority, staleRelay);
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted, "executor path: campaign HUMAN + stale relay rejects MOVE");
	expect(result.rejectCode == "HUMAN_OVERRIDE", "stale relay after reclaim uses HUMAN_OVERRIDE");
}

static void testExecutorValidationCampaignAiMissingRelayRejected()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision authority = decide(true, currentAi, true, 1, std::string(), SESSION);
	const MoveValidationInput input = executorValidationInputFromAuthority(authority, false);
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "NOT_AI_CONTROLLED",
		"campaign AI + missing relay rejects MOVE");
}

static void testInFlightReclaimRevokesAiAuthorization()
{
	std::set<int> ai;
	ai.insert(1);
	const TacticalAuthorityDecision inFlightStart = decide(true, ai, true, 1, controlWithAi(1), SESSION);
	expect(inFlightStart.aiCommandPermitted, "in-flight fixture: AI authorized at start");
	std::set<int> reclaimed;
	const TacticalAuthorityDecision afterReclaim = decide(true, reclaimed, true, 1, controlWithAi(1), SESSION);
	expect(!afterReclaim.aiCommandPermitted, "reclaim during MOVE revokes AI authorization");
	expect(afterReclaim.humanOrderPermitted, "reclaim restores human order permission");
	expect(evaluateWalkTerminalStatus(
		WalkCompletionInput { Position(5, 5, 0), Position(1, 1, 0), Position(3, 1, 0), false, true, afterReclaim.aiCommandPermitted, std::string() })
		== CMD_INTERRUPTED,
		"walk completion treats reclaim as interrupted");
}

static void testMissingCampaignContextRejectsExecutorMove()
{
	std::set<int> currentAi;
	currentAi.insert(1);
	const TacticalAuthorityDecision authority = decide(false, currentAi, true, 1, controlWithAi(1), SESSION);
	const MoveValidationInput input = executorValidationInputFromAuthority(authority, false, false);
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_ACTOR",
		"missing campaign context fails closed for MOVE");
}

/// F4A human movement admission uses humanOrderPermitted when companion policy applies.
static bool f4aHumanMovementAllowedWhenPolicyApplies(const TacticalAuthorityDecision &d)
{
	return d.humanOrderPermitted;
}

static void testF4AHumanMovementAdmission()
{
	std::set<int> humanCampaign;
	const TacticalAuthorityDecision humanOk = decide(true, humanCampaign, true, 1, controlWithAi(1), SESSION);
	expect(f4aHumanMovementAllowedWhenPolicyApplies(humanOk), "F4A: HUMAN soldier movement allowed");

	std::set<int> asterCampaign;
	asterCampaign.insert(2);
	const TacticalAuthorityDecision asterRelay = decide(true, asterCampaign, true, 2, controlWithAi(2), SESSION);
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(asterRelay), "F4A: ASTER movement denied with valid relay");

	const TacticalAuthorityDecision asterNoRelay = decide(true, asterCampaign, true, 2, std::string(), SESSION);
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(asterNoRelay), "F4A: ASTER movement denied with missing relay");

	const TacticalAuthorityDecision asterStale = decide(true, asterCampaign, true, 2, controlWithAi(2), "wrong-session");
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(asterStale), "F4A: ASTER movement denied with stale/wrong relay");

	const TacticalAuthorityDecision reclaimed = decide(true, humanCampaign, true, 2, controlWithAi(2), SESSION);
	expect(f4aHumanMovementAllowedWhenPolicyApplies(reclaimed), "F4A: reclaimed HUMAN movement allowed despite stale relay AI");

	const TacticalAuthorityDecision companionAiMove = decide(true, asterCampaign, true, 2, controlWithAi(2), SESSION);
	expect(companionAiMove.aiCommandPermitted, "F4A: companion MOVE still authorized via F2 aiCommandPermitted");
}

static void testF4B1HumanCombatOrderAdmission()
{
	std::set<int> humanCampaign;
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 1, std::string(), SESSION)),
		"F4B1: HUMAN firearm/throw allowed without relay JSON");

	std::set<int> asterCampaign;
	asterCampaign.insert(3);
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 3, controlWithAi(3), SESSION)),
		"F4B1: ASTER combat denied with valid relay");
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 3, std::string(), SESSION)),
		"F4B1: ASTER combat denied with missing relay (campaign canonical)");
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 3, controlWithAi(3), "wrong-session-id")),
		"F4B1: ASTER combat denied with stale/wrong relay");

	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 3, controlWithAi(3), SESSION)),
		"F4B1: reclaimed HUMAN combat allowed with stale relay AI");

	expect(!decide(false, asterCampaign, true, 3, std::string(), SESSION).humanOrderPermitted,
		"F4B1: missing campaign context fails closed in authority evaluate (companion MOVE path)");
}

static void testCompanionPolicyScopeIndependentOfRelayPresence()
{
	std::set<int> asterCampaign;
	asterCampaign.insert(9);
	const TacticalAuthorityDecision withoutRelay = decide(true, asterCampaign, true, 9, std::string(), SESSION);
	expect(!withoutRelay.humanOrderPermitted && !withoutRelay.aiCommandPermitted,
		"ASTER without relay denies both human orders and AI commands");
	const TacticalAuthorityDecision withRelay = decide(true, asterCampaign, true, 9, controlWithAi(9), SESSION);
	expect(!withRelay.humanOrderPermitted && withRelay.aiCommandPermitted,
		"ASTER with matching relay permits AI only");
}

static void testF4B2MeleePsiSecondaryAdmission()
{
	std::set<int> humanCampaign;
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 4, std::string(), SESSION)),
		"F4B2: HUMAN melee/psi/secondary policy allows human orders");

	std::set<int> asterCampaign;
	asterCampaign.insert(4);
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 4, controlWithAi(4), SESSION)),
		"F4B2: ASTER melee/psi/secondary denied with valid relay");
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 4, std::string(), SESSION)),
		"F4B2: ASTER denied with missing relay");
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 4, controlWithAi(4), SESSION)),
		"F4B2: reclaimed HUMAN allowed with stale relay AI");
}

static void testF4CInventoryItemActionAdmission()
{
	std::set<int> humanCampaign;
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 5, std::string(), SESSION)),
		"F4C: HUMAN inventory/item actions allowed under companion policy");

	std::set<int> asterCampaign;
	asterCampaign.insert(5);
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 5, controlWithAi(5), SESSION)),
		"F4C: ASTER inventory/item mutations denied with valid relay");
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 5, std::string(), SESSION)),
		"F4C: ASTER inventory/item mutations denied with missing relay");
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 5, controlWithAi(5), SESSION)),
		"F4C: reclaimed HUMAN inventory/item mutations allowed with stale relay AI");
}

static void testF4C2SelectedUnitTacticalControlAdmission()
{
	std::set<int> humanCampaign;
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 6, std::string(), SESSION)),
		"F4C2: HUMAN kneel/zero-TU/hand-use policy allows human orders");

	std::set<int> asterCampaign;
	asterCampaign.insert(6);
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 6, controlWithAi(6), SESSION)),
		"F4C2: ASTER selected-unit tactical mutations denied with valid relay");
	expect(!f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, asterCampaign, true, 6, std::string(), SESSION)),
		"F4C2: ASTER denied with missing relay");
	expect(f4aHumanMovementAllowedWhenPolicyApplies(
		decide(true, humanCampaign, true, 6, controlWithAi(6), SESSION)),
		"F4C2: reclaimed HUMAN selected-unit actions allowed with stale relay AI");
}

int main()
{
	testHumanCampaignRelayAi();
	testAiCampaignRelayAiMatchingSession();
	testAiCampaignMissingRelay();
	testAiCampaignMalformedRelay();
	testAiCampaignWrongSessionRelay();
	testReclaimedHumanStaleRelayAi();
	testInvalidDeadSoldier();
	testMissingCampaignContext();
	testUnsavedAiUsesCurrentCampaignSet();
	testResolveCampaignModeMatchesSavedGameSemantics();
	testExecutorValidationCampaignAiValidRelay();
	testExecutorValidationCampaignHumanStaleRelayRejected();
	testExecutorValidationCampaignAiMissingRelayRejected();
	testInFlightReclaimRevokesAiAuthorization();
	testMissingCampaignContextRejectsExecutorMove();
	testF4AHumanMovementAdmission();
	testF4B1HumanCombatOrderAdmission();
	testCompanionPolicyScopeIndependentOfRelayPresence();
	testF4B2MeleePsiSecondaryAdmission();
	testF4CInventoryItemActionAdmission();
	testF4C2SelectedUnitTacticalControlAdmission();

	if (g_failures == 0)
	{
		std::cout << "companion_tactical_authority_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_tactical_authority_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
