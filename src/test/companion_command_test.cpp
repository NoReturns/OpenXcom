/*
 * Open X-Com Companion — tactical MOVE command core regression tests (M1-19).
 */
#include "../Mod/RuleItem.h"
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionWalkStopTokens.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "Engine/CompanionCommandRuntimeSupport.h"
#include "Engine/CompanionCommandCasualtySettlementCore.h"
#include <iostream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionCommandCore;
using namespace OpenXcom::CompanionSoldierOwnershipCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static MoveValidationInput validInput()
{
	MoveValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.destX = 3;
	input.destY = 4;
	input.destZ = 0;
	input.mapSizeX = 10;
	input.mapSizeY = 10;
	input.mapSizeZ = 1;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	return input;
}

static void testOwnershipDefaultHuman()
{
	const std::string control = "{\n  \"schema_version\": 1,\n  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n  \"assignments\": { \"1\": \"ai\" }\n}\n";
	expect(!isAiControlledUnit(control, "00000000000000000000000000000000", 1), "wrong session must default to human");
	expect(isAiControlledUnit(control, "a1b2c3d4e5f6789012345678abcdef01", 1), "matching session ai assignment");
	expect(!isAiControlledUnit(control, "a1b2c3d4e5f6789012345678abcdef01", 2), "unassigned soldier is human");
}

static void testOwnershipPowerShellSpacing()
{
	const std::string control =
		"{\n"
		"    \"schema_version\":  1,\n"
		"    \"assignments\":  {\n"
		"                        \"3\":  \"ai\"\n"
		"                    },\n"
		"    \"session_id\":  \"db56c99b83f74cad25b6ecb62447906c\"\n"
		"}\n";
	expect(isAiControlledUnit(control, "db56c99b83f74cad25b6ecb62447906c", 3), "powershell-spaced control file must authorize soldier 3");
	expect(!isAiControlledUnit(control, "db56c99b83f74cad25b6ecb62447906c", 2), "missing assignment stays human");
}

static void testOwnershipCompactJson()
{
	const std::string control = "{\"schema_version\":1,\"assignments\":{\"3\":\"ai\"},\"session_id\":\"db56c99b83f74cad25b6ecb62447906c\"}";
	expect(isAiControlledUnit(control, "db56c99b83f74cad25b6ecb62447906c", 3), "compact json must authorize soldier 3");
}

static void testOwnershipFlexibleWhitespace()
{
	const std::string control =
		"{\n"
		"  \"schema_version\" : 1 ,\n"
		"  \"assignments\" : { \"3\" : \"ai\" } ,\n"
		"  \"session_id\" : \"db56c99b83f74cad25b6ecb62447906c\"\n"
		"}\n";
	expect(isAiControlledUnit(control, "db56c99b83f74cad25b6ecb62447906c", 3), "flexible whitespace must authorize soldier 3");
}

static void testOwnershipWrongSessionRejected()
{
	const std::string control =
		"{\n  \"schema_version\": 1,\n  \"session_id\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n  \"assignments\": { \"3\": \"ai\" }\n}\n";
	expect(!isAiControlledUnit(control, "db56c99b83f74cad25b6ecb62447906c", 3), "wrong session must not authorize ai");
}

static void testOwnershipMalformedJsonRejected()
{
	const std::string control = "{ not json at all";
	SoldierControlDocument doc = {};
	expect(!parseSoldierControlDocument(control, &doc), "malformed json must not parse");
	expect(!isAiControlledUnit(control, "db56c99b83f74cad25b6ecb62447906c", 3), "malformed json defaults to human");
}

static void testOwnershipInvalidAssignmentModeRejected()
{
	const std::string control =
		"{\n  \"schema_version\": 1,\n  \"session_id\": \"db56c99b83f74cad25b6ecb62447906c\",\n  \"assignments\": { \"3\": \"remote\" }\n}\n";
	SoldierControlDocument doc = {};
	expect(!parseSoldierControlDocument(control, &doc), "invalid assignment mode must fail parse");
}

static void testDeferInFlightCompletionWhileReentrant()
{
	using namespace CompanionCommandRuntimeSupport;
	expect(!shouldDeferInFlightCommandCompletionWhileReentrant(1),
		"outer command tick may complete in-flight commands");
	expect(shouldDeferInFlightCommandCompletionWhileReentrant(2),
		"nested tick from popState must defer premature terminal completion");
}

static void testOrphanDirectedFireSessionBinding()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string live = "f45561e99281ff2d71a1a28b993e3f82";
	const std::string other = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	expect(commandSessionBindingMatchesLiveBattle(live, live, live),
		"matching command and result sessions bind to live battle");
	expect(commandSessionBindingMatchesLiveBattle(live, live, std::string()),
		"empty result session may still match when command session matches live");
	expect(!commandSessionBindingMatchesLiveBattle(live, other, live),
		"stale command session must not rehydrate after battle reload");
	expect(!commandSessionBindingMatchesLiveBattle(live, live, other),
		"stale result session must not rehydrate");
	expect(shouldRehydrateOrphanedDirectedFireForSession(live, live, live),
		"rehydrate only when sessions align with live battle");
	expect(!shouldRehydrateOrphanedDirectedFireForSession(other, live, live),
		"rehydrate blocked when live battle session rotated");
}

static void testCompanionCommandPollDuringIdleThink()
{
	using namespace CompanionCommandRuntimeSupport;
	expect(shouldRunCompanionCommandTickDuringThink(true, false, false, true, false, false),
		"idle player turn must poll commands");
	expect(!shouldRunCompanionCommandTickDuringThink(true, false, false, true, true, false),
		"busy battlefield must not poll new commands from think");
	expect(!shouldRunCompanionCommandTickDuringThink(false, false, false, true, false, false),
		"alien turn must not poll from think");
	expect(shouldRunCompanionCommandTickDuringThink(true, false, false, false, true, true),
		"in-flight move must keep polling from think");
}

static void testCompanionCommandInboxThrottle()
{
	using namespace CompanionCommandRuntimeSupport;
	expect(shouldPollCommandInboxFile(false, 101, 100, 0, 250), "mtime change must poll immediately");
	expect(!shouldPollCommandInboxFile(false, 100, 100, 10, 250), "unchanged inbox must throttle rapid polls");
	expect(shouldPollCommandInboxFile(false, 100, 100, 250, 250), "unchanged inbox must poll after interval");
	expect(shouldPollCommandInboxFile(true, 100, 100, 0, 250), "in-flight move must poll every tick");
}

static void testExecutorDisabledByDefault()
{
	expect(!isExecutorEnabledFromEnvironment(), "executor env must be unset in test environment");
	MoveValidationInput input = validInput();
	input.executorEnabled = false;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "EXECUTOR_DISABLED", "disabled executor rejects");
}

static void testHumanActorRejected()
{
	MoveValidationInput input = validInput();
	input.actorAiControlled = false;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "NOT_AI_CONTROLLED", "human-controlled soldier rejected");
}

static void testWrongTurnRejected()
{
	MoveValidationInput input = validInput();
	input.playerTurn = false;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "WRONG_TURN", "enemy turn rejected");
}

static void testStaleSnapshotRejected()
{
	MoveValidationInput input = validInput();
	input.commandSnapshotSha256 = "0000000000000000000000000000000000000000000000000000000000000001";
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "STALE_SNAPSHOT", "stale snapshot rejected");
}

static void testDuplicateCommandRejected()
{
	MoveValidationInput input = validInput();
	input.duplicateCommandId = true;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "DUPLICATE_COMMAND_ID", "duplicate command rejected");
}

static void testBusyRejected()
{
	MoveValidationInput input = validInput();
	input.battleBusy = true;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "BATTLE_BUSY", "busy battlefield rejected");
}

static void testInvalidDestinationRejected()
{
	MoveValidationInput input = validInput();
	input.destX = 99;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_DESTINATION", "invalid destination rejected");
}

static void testStaleStateVersionRejected()
{
	MoveValidationInput input = validInput();
	input.commandStateVersion = 99;
	const MoveValidationResult result = validateMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "STALE_STATE_VERSION", "stale state_version rejected");
}

static void testAuthorizedActorAccepted()
{
	const MoveValidationResult result = validateMoveCommand(validInput());
	expect(result.accepted, "authorized ai actor accepted");
}

static void testParseMoveCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"destination\": { \"x\": 2, \"y\": 3, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 1\n"
		"}\n";
	MoveCommandParsed parsed = {};
	expect(parseMoveCommandJson(json, &parsed), "parse must succeed");
	expect(parsed.valid && parsed.destX == 2 && parsed.destY == 3, "destination parsed");
}

static const char *PARSE_FIXTURE_SHA =
	"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static const char *PARSE_FIXTURE_SESSION = "a1b2c3d4e5f6789012345678abcdef01";

static void expectParseMoveSucceeds(const std::string &json, int expectedX, int expectedY, int expectedZ,
	const char *expectedCommandId = "move-test-1")
{
	MoveCommandParsed parsed = {};
	expect(parseMoveCommandJson(json, &parsed), "parse must succeed");
	expect(parsed.valid, "parsed.valid must be true");
	expect(parsed.commandId == expectedCommandId, "command_id must match");
	expect(parsed.destX == expectedX && parsed.destY == expectedY && parsed.destZ == expectedZ,
		"destination coordinates must match");
}

static void testParseMoveCommandPowerShellSpacing()
{
	const std::string json =
		"{\n"
		"    \"schema_version\":  1,\n"
		"    \"command_id\":  \"c69b213f1840427d81611e3e99df460a\",\n"
		"    \"command_type\":  \"MOVE\",\n"
		"    \"actor_unit_id\":  3,\n"
		"    \"destination\":  {\n"
		"                        \"y\":  37,\n"
		"                        \"z\":  0,\n"
		"                        \"x\":  45\n"
		"                    },\n"
		"    \"snapshot_sha256\":  \"" + std::string(PARSE_FIXTURE_SHA) + "\",\n"
		"    \"session_id\":  \"" + std::string(PARSE_FIXTURE_SESSION) + "\",\n"
		"    \"turn\":  1,\n"
		"    \"state_version\":  0\n"
		"}\n";
	expectParseMoveSucceeds(json, 45, 37, 0, "c69b213f1840427d81611e3e99df460a");
}

static void testParseMoveCommandCompactJson()
{
	const std::string json = std::string("{\"schema_version\":1,\"command_id\":\"move-test-1\",\"command_type\":\"MOVE\",")
		+ "\"actor_unit_id\":1,\"destination\":{\"x\":2,\"y\":3,\"z\":0},\"snapshot_sha256\":\"" + PARSE_FIXTURE_SHA
		+ "\",\"session_id\":\"" + PARSE_FIXTURE_SESSION + "\",\"turn\":2,\"state_version\":1}";
	expectParseMoveSucceeds(json, 2, 3, 0);
}

static void testParseMoveCommandReorderedProperties()
{
	const std::string json =
		"{\n"
		"  \"turn\": 2,\n"
		"  \"destination\": { \"z\": 0, \"x\": 2, \"y\": 3 },\n"
		"  \"state_version\": 1,\n"
		"  \"session_id\": \"" + std::string(PARSE_FIXTURE_SESSION) + "\",\n"
		"  \"snapshot_sha256\": \"" + std::string(PARSE_FIXTURE_SHA) + "\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"schema_version\": 1\n"
		"}\n";
	expectParseMoveSucceeds(json, 2, 3, 0);
}

static void testParseMoveCommandDestinationNotConfusedWithOtherObjects()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"decoy\": { \"x\": 99, \"y\": 99, \"z\": 99 },\n"
		"  \"destination\": { \"x\": 2, \"y\": 3, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"" + std::string(PARSE_FIXTURE_SHA) + "\",\n"
		"  \"session_id\": \"" + std::string(PARSE_FIXTURE_SESSION) + "\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 1\n"
		"}\n";
	expectParseMoveSucceeds(json, 2, 3, 0);
}

static void testParseMoveCommandInvalidCommandIdRejected()
{
	const std::string base =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"REPLACEME\",\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"destination\": { \"x\": 2, \"y\": 3, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"" + std::string(PARSE_FIXTURE_SHA) + "\",\n"
		"  \"session_id\": \"" + std::string(PARSE_FIXTURE_SESSION) + "\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 1\n"
		"}\n";

	auto expectInvalidCommandId = [&](const std::string &commandId, const char *label) {
		std::string json = base;
		const std::string::size_type pos = json.find("REPLACEME");
		json.replace(pos, 9, commandId);
		MoveCommandParsed parsed = {};
		expect(!parseMoveCommandJson(json, &parsed), label);
		expect(parsed.rejectCode == "INVALID_COMMAND_ID", "reject code must be INVALID_COMMAND_ID");
	};

	expectInvalidCommandId("_bad-leading-char", "leading underscore command_id rejected");
	expectInvalidCommandId("has space inside", "whitespace in command_id rejected");
	expectInvalidCommandId("", "empty command_id rejected");
}

static void testWalkCompletionDetection()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(5, 5, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	expect(evaluateWalkTerminalStatus(input) == CMD_COMPLETED, "reached destination completes");
}

static void testWalkCompletionCrossLevelZ()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(10, 5, 1);
	input.startPos = Position(10, 5, 0);
	input.currentPos = Position(10, 5, 1);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	expect(evaluateWalkTerminalStatus(input) == CMD_COMPLETED, "cross-level destination completes when z matches");
	input.currentPos = Position(10, 5, 0);
	expect(evaluateWalkTerminalStatus(input) == CMD_FAILED, "same x,y wrong z without progress is FAILED not COMPLETED");
}

static void testWalkInterruptionDetection()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(2, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = "STR_NOT_ENOUGH_TIME_UNITS";
	expect(evaluateWalkTerminalStatus(input) == CMD_INTERRUPTED, "action result interrupts");
}

static void testHumanOverrideDuringWalk()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(3, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = false;
	expect(evaluateWalkTerminalStatus(input) == CMD_INTERRUPTED, "human override interrupts");
}

static void testWalkStopDiagnosticsTurnAndSpot()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(1, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = CompanionWalkStopTokens::HOSTILE_SPOTTED;
	const CommandLifecycleStatus terminal = evaluateWalkTerminalStatus(input);
	expect(terminal == CMD_INTERRUPTED, "hostile spot with engine token interrupts");
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(terminal, input);
	expect(diag.movementStopReason == "hostile_spotted", "turn-and-spot maps hostile_spotted");
	expect(!diag.publicReason.empty(), "turn-and-spot exposes public_reason");
	expect(diag.engineActionResult == CompanionWalkStopTokens::HOSTILE_SPOTTED,
		"engine_action_result present for companion token");
}

static void testWalkStopDiagnosticsPartialUnknown()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(25, 8, 0);
	input.currentPos = Position(25, 9, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	const CommandLifecycleStatus terminal = evaluateWalkTerminalStatus(input);
	expect(terminal == CMD_INTERRUPTED, "one-tile partial move interrupts");
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(terminal, input);
	expect(diag.movementStopReason == "movement_incomplete", "partial without token is movement_incomplete");
	expect(diag.engineActionResult.empty(), "no engine_action_result without evidence");
}

static void testWalkStopDiagnosticsInsufficientTu()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(2, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = "STR_NOT_ENOUGH_TIME_UNITS";
	const CommandLifecycleStatus terminal = evaluateWalkTerminalStatus(input);
	expect(terminal == CMD_INTERRUPTED, "TU abort interrupts");
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(terminal, input);
	expect(diag.movementStopReason == "insufficient_time_units", "TU string maps to insufficient_time_units");
	expect(diag.engineActionResult == "STR_NOT_ENOUGH_TIME_UNITS", "STR token on wire");
}

static void testWalkStopDiagnosticsReactionFire()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(9, 20, 0);
	input.startPos = Position(25, 8, 0);
	input.currentPos = Position(25, 9, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = CompanionWalkStopTokens::REACTION_FIRE;
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(
		evaluateWalkTerminalStatus(input), input);
	expect(diag.movementStopReason == "reaction_fire", "reaction_fire token maps");
}

static void testWalkStopDiagnosticsCompletedOmitsReason()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(5, 5, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(
		evaluateWalkTerminalStatus(input), input);
	expect(diag.movementStopReason.empty(), "completed move has no movement_stop_reason");
	expect(evaluateWalkTerminalStatus(input) == CMD_COMPLETED, "completed at destination");
}

static void testWalkStopDiagnosticsPathBlocked()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(2, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = CompanionWalkStopTokens::PATH_BLOCKED;
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(
		evaluateWalkTerminalStatus(input), input);
	expect(diag.movementStopReason == "path_blocked", "path_blocked token maps");
}

static void testWalkStopDiagnosticsProximityGrenade()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(2, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = CompanionWalkStopTokens::PROXIMITY_GRENADE;
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(
		evaluateWalkTerminalStatus(input), input);
	expect(diag.movementStopReason == "proximity_grenade", "proximity_grenade token maps");
}

static void testWalkStopDiagnosticsActorIncapacitated()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(2, 1, 0);
	input.battleBusy = false;
	input.actorAlive = false;
	input.actorStillAiControlled = true;
	const CommandLifecycleStatus terminal = evaluateWalkTerminalStatus(input);
	expect(terminal == CMD_FAILED, "dead actor fails");
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(terminal, input);
	expect(diag.movementStopReason == "actor_incapacitated", "incapacitated maps");
}

static void testWalkStopTokenClassifier()
{
	expect(CompanionWalkStopTokens::isWalkStopActionResult(CompanionWalkStopTokens::HOSTILE_SPOTTED),
		"hostile spotted is walk stop token");
	expect(CompanionWalkStopTokens::isWalkStopActionResult(CompanionWalkStopTokens::REACTION_FIRE),
		"reaction fire is walk stop token");
	expect(!CompanionWalkStopTokens::isWalkStopActionResult("STR_NOT_ENOUGH_TIME_UNITS"),
		"TU string is not companion internal walk stop token");
}

static void testWalkStopSingleRecordedCauseHostilePrecedence()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(2, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = CompanionWalkStopTokens::HOSTILE_SPOTTED;
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(
		evaluateWalkTerminalStatus(input), input);
	expect(diag.movementStopReason == "hostile_spotted", "diagnostics record one engine token");
	expect(diag.movementStopReason != "reaction_fire",
		"reaction_fire not co-reported when hostile_spotted token alone");
}

static const char *SAMPLE_MOVE_COMMAND =
	"{\n"
	"  \"schema_version\": 1,\n"
	"  \"command_id\": \"move-test-1\",\n"
	"  \"command_type\": \"MOVE\",\n"
	"  \"actor_unit_id\": 1,\n"
	"  \"destination\": { \"x\": 2, \"y\": 3, \"z\": 0 },\n"
	"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
	"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
	"  \"turn\": 2,\n"
	"  \"state_version\": 2\n"
	"}\n";

static std::string sampleMoveDigest()
{
	std::string digest;
	expect(CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(SAMPLE_MOVE_COMMAND, &digest),
		"sample move digest must compute");
	return digest;
}

static std::string sampleCompletedResultJson(const std::string &inboxSha256)
{
	std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"status\": \"COMPLETED\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"actor_unit_id\": 1,\n";
	if (!inboxSha256.empty())
	{
		json += "  \"inbox_sha256\": \"" + inboxSha256 + "\",\n";
	}
	json += "  \"completed_at\": \"engine-local\"\n}\n";
	return json;
}

static const char *INCOMING_CMD = "move-test-1";

static OpenXcom::CompanionCommandRuntimeSupport::DuplicateInboxDecision evaluateDuplicateObs(
	bool journaledDuplicate,
	const std::string &inboxDigest,
	const std::string &existingResultJson,
	bool moveInFlight = false)
{
	return OpenXcom::CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
		journaledDuplicate, INCOMING_CMD, inboxDigest, existingResultJson, moveInFlight);
}

static void simulateRuntimeDuplicateFileEffects(
	OpenXcom::CompanionCommandRuntimeSupport::DuplicateInboxDecision decision,
	const std::string &existingResultJson,
	const std::string &sidecarJson,
	std::string *resultOut,
	std::string *sidecarOut)
{
	*resultOut = existingResultJson;
	sidecarOut->clear();
	if (CompanionCommandRuntimeSupport::shouldOverwriteTacticalResultOnDuplicateDecision(decision))
	{
		*resultOut = "WOULD_OVERWRITE_WITH_REJECTED";
	}
	if (CompanionCommandRuntimeSupport::shouldWriteConflictSidecarOnDuplicateDecision(decision))
	{
		*sidecarOut = sidecarJson;
	}
}

static void testDuplicateInboxNoOpPreservesCompletedResult()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string result = sampleCompletedResultJson(digest);
	expect(evaluateDuplicateObs(true, digest, result) == DUPLICATE_INBOX_NO_OP,
		"unchanged consumed inbox must no-op against completed result");
}

static void testDuplicateInboxNoOpPreservesLegacyTerminalResult()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string result = sampleCompletedResultJson(std::string());
	expect(evaluateDuplicateObs(true, digest, result) == DUPLICATE_INBOX_NO_OP,
		"legacy completed result without inbox_sha256 must no-op on repeat poll");
}

static void testDuplicateInboxNoOpPreservesOrphanExecutingResult()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string result =
		"{\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"status\": \"EXECUTING\",\n"
		"  \"inbox_sha256\": \"" + digest + "\"\n"
		"}\n";
	expect(evaluateDuplicateObs(true, digest, result) == DUPLICATE_INBOX_NO_OP,
		"orphaned EXECUTING result must not replay on restart poll");
}

static void testDuplicateInboxConflictPreservesCompletedResult()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string modified =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"destination\": { \"x\": 9, \"y\": 3, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	std::string modifiedDigest;
	expect(computeInboxCommandDigestHex(modified, &modifiedDigest), "modified digest");
	const std::string result = sampleCompletedResultJson(digest);
	expect(evaluateDuplicateObs(true, modifiedDigest, result) == DUPLICATE_INBOX_CONFLICT_PRESERVE,
		"modified inbox with reused command id must preserve completed result");
}

static void testLegacyModifiedInboxPreservesTerminalWithoutOverwrite()
{
	using namespace CompanionCommandRuntimeSupport;
	std::string modifiedDigest;
	const std::string modified =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"command_type\": \"MOVE\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"destination\": { \"x\": 9, \"y\": 3, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	expect(computeInboxCommandDigestHex(modified, &modifiedDigest), "modified digest");
	const std::string legacyResult = sampleCompletedResultJson(std::string());
	expect(evaluateDuplicateObs(true, modifiedDigest, legacyResult) == DUPLICATE_INBOX_NO_OP,
		"legacy terminal result must be preserved without claiming payload verification");
}

static void testDuplicateInboxRejectWhenPriorResultMissing()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	expect(evaluateDuplicateObs(true, digest, std::string()) == DUPLICATE_INBOX_REJECT,
		"journal duplicate without prior result may write rejected outcome");
}

static void testDuplicatePayloadConflictJsonRecordsMismatch()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string json = buildDuplicatePayloadConflictJson(
		"move-test-1", "COMPLETED", "observed-digest", "stored-digest");
	expect(json.find("\"conflict\": \"inbox_payload_mismatch\"") != std::string::npos,
		"conflict record must describe inbox payload mismatch");
	expect(json.find("\"prior_result_status\": \"COMPLETED\"") != std::string::npos,
		"conflict record must retain prior terminal status");
	expect(json.find("observed-digest") != std::string::npos && json.find("stored-digest") != std::string::npos,
		"conflict record must include observed and stored digests");
}

static void testDuplicateInboxProceedsForFreshCommandId()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	expect(evaluateDuplicateInboxObservation(false, INCOMING_CMD, digest, std::string(), false) == DUPLICATE_INBOX_PROCEED,
		"fresh command id proceeds to normal validation");
}

static void testDuplicateInboxPreservesInterruptedTerminalStatus()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string result =
		"{\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"status\": \"INTERRUPTED\",\n"
		"  \"inbox_sha256\": \"" + digest + "\"\n"
		"}\n";
	expect(evaluateDuplicateObs(true, digest, result) == DUPLICATE_INBOX_NO_OP,
		"interrupted terminal result must survive repeat poll");
}

static void testDuplicateInboxPreservesFailedTerminalStatus()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string result =
		"{\n"
		"  \"command_id\": \"move-test-1\",\n"
		"  \"status\": \"FAILED\",\n"
		"  \"inbox_sha256\": \"" + digest + "\"\n"
		"}\n";
	expect(evaluateDuplicateObs(true, digest, result) == DUPLICATE_INBOX_NO_OP,
		"failed terminal result must survive repeat poll");
}

static void testDuplicateInboxIdentityMismatchWhenResultCommandIdDiffers()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string unrelatedResult =
		"{\n"
		"  \"command_id\": \"other-command-id\",\n"
		"  \"status\": \"COMPLETED\",\n"
		"  \"inbox_sha256\": \"" + digest + "\"\n"
		"}\n";
	const DuplicateInboxDecision decision = evaluateDuplicateObs(true, digest, unrelatedResult);
	expect(decision == DUPLICATE_INBOX_IDENTITY_MISMATCH, "unrelated result command id must not bind");
	std::string resultAfter;
	std::string sidecar;
	const std::string sidecarJson = buildDuplicateResultIdentityConflictJson(
		INCOMING_CMD, "result_command_id_mismatch", "other-command-id", "COMPLETED", digest);
	simulateRuntimeDuplicateFileEffects(decision, unrelatedResult, sidecarJson, &resultAfter, &sidecar);
	expect(resultAfter == unrelatedResult, "unrelated result bytes must not be overwritten");
	expect(!sidecar.empty(), "identity mismatch must record conflict sidecar");
}

static void testDuplicateInboxIdentityMismatchWhenResultCommandIdMissing()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string result = "{\n  \"status\": \"COMPLETED\"\n}\n";
	const DuplicateInboxDecision decision = evaluateDuplicateObs(true, digest, result);
	expect(decision == DUPLICATE_INBOX_IDENTITY_MISMATCH, "missing result command id must not bind");
	std::string resultAfter;
	std::string sidecar;
	simulateRuntimeDuplicateFileEffects(
		decision, result, buildDuplicateResultIdentityConflictJson(
			INCOMING_CMD, "result_binding_unparseable", std::string(), std::string(), digest),
		&resultAfter, &sidecar);
	expect(resultAfter == result, "result without command id must not be overwritten");
	expect(!sidecar.empty(), "missing command id must record conflict sidecar");
}

static void testDuplicateInboxIdentityMismatchWhenResultMalformed()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const std::string malformed = "{ not valid json for result\n";
	const DuplicateInboxDecision decision = evaluateDuplicateObs(true, digest, malformed);
	expect(decision == DUPLICATE_INBOX_IDENTITY_MISMATCH, "malformed result must not bind");
	std::string resultAfter;
	std::string sidecar;
	simulateRuntimeDuplicateFileEffects(
		decision, malformed, buildDuplicateResultIdentityConflictJson(
			INCOMING_CMD, "result_binding_unparseable", std::string(), std::string(), digest),
		&resultAfter, &sidecar);
	expect(resultAfter == malformed, "malformed result bytes must be preserved");
	expect(!sidecar.empty(), "malformed result must record conflict sidecar");
}

static void testDuplicateRejectWritesOnlyWhenResultMissing()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string digest = sampleMoveDigest();
	const DuplicateInboxDecision decision = evaluateDuplicateObs(true, digest, std::string());
	expect(decision == DUPLICATE_INBOX_REJECT, "missing result file keeps safe duplicate rejection");
	std::string resultAfter;
	std::string sidecar;
	simulateRuntimeDuplicateFileEffects(decision, std::string(), std::string(), &resultAfter, &sidecar);
	expect(resultAfter == "WOULD_OVERWRITE_WITH_REJECTED", "missing result may write rejected outcome");
	expect(sidecar.empty(), "missing result duplicate must not require conflict sidecar");
}

static void testDuplicateIdentityConflictJsonShape()
{
	using namespace CompanionCommandRuntimeSupport;
	const std::string json = buildDuplicateResultIdentityConflictJson(
		INCOMING_CMD, "result_command_id_mismatch", "other-id", "COMPLETED", "digest");
	expect(json.find("\"conflict\": \"result_command_id_mismatch\"") != std::string::npos,
		"identity conflict json must name mismatch kind");
	expect(json.find("\"prior_result_command_id\": \"other-id\"") != std::string::npos,
		"identity conflict json must include prior result command id");
}

static void testRuntimeValidationPathAcceptsFreshMoveAfterDuplicateHandled()
{
	MoveValidationInput input = validInput();
	input.duplicateCommandId = false;
	expect(validateMoveCommand(input).accepted, "first valid MOVE remains accepted after duplicate pre-check split");
}

static KneelValidationInput validKneelInput()
{
	KneelValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.actorAlreadyKneeled = false;
	input.actorCanKneelPosture = true;
	input.insufficientTuForKneelDown = false;
	return input;
}

static StandValidationInput validStandInput()
{
	StandValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.actorKneeling = true;
	input.actorCanKneelPosture = true;
	input.insufficientTuForStandUp = false;
	return input;
}

static void testStandAuthorizedAccepted()
{
	expect(validateStandCommand(validStandInput()).accepted, "authorized stand accepted");
}

static void testStandAlreadyStandingRejected()
{
	StandValidationInput input = validStandInput();
	input.actorKneeling = false;
	const MoveValidationResult result = validateStandCommand(input);
	expect(!result.accepted && result.rejectCode == "ALREADY_STANDING", "already standing rejected");
}

static void testStandInsufficientTuRejected()
{
	StandValidationInput input = validStandInput();
	input.insufficientTuForStandUp = true;
	const MoveValidationResult result = validateStandCommand(input);
	expect(!result.accepted && result.rejectCode == "INSUFFICIENT_TU", "stand insufficient TU rejected");
}

static void testKneelThenStandValidationFlow()
{
	KneelValidationInput kneel = validKneelInput();
	expect(validateKneelCommand(kneel).accepted, "kneel accepted before stand");
	kneel.actorAlreadyKneeled = true;
	expect(!validateKneelCommand(kneel).accepted, "second kneel rejected");
	StandValidationInput stand = validStandInput();
	expect(validateStandCommand(stand).accepted, "stand accepted after kneel");
}

static void testParseStandCommandJson()
{
	const char *json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"stand-test-1\",\n"
		"  \"command_type\": \"STAND\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	StandCommandParsed parsed = {};
	expect(parseStandCommandJson(json, &parsed) && parsed.valid && parsed.commandType == "STAND",
		"stand command parses");
}

static void testWalkHostileSpotZeroTileInterruptedClassification()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(1, 1, 0);
	input.battleBusy = false;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	input.actionResult = CompanionWalkStopTokens::HOSTILE_SPOTTED;
	expect(evaluateWalkTerminalStatus(input) == CMD_INTERRUPTED, "zero-tile hostile spot is INTERRUPTED");
	const WalkStopDiagnostics diag = resolveWalkStopDiagnostics(
		evaluateWalkTerminalStatus(input), input);
	expect(diag.movementStopReason == "hostile_spotted", "zero-tile hostile spot reason encoded");
}

static void testKneelAuthorizedAccepted()
{
	expect(validateKneelCommand(validKneelInput()).accepted, "authorized kneel accepted");
}

static void testKneelAlreadyKneelingRejected()
{
	KneelValidationInput input = validKneelInput();
	input.actorAlreadyKneeled = true;
	const MoveValidationResult result = validateKneelCommand(input);
	expect(!result.accepted && result.rejectCode == "ALREADY_KNEELING", "already kneeling rejected");
}

static void testKneelInsufficientTuRejected()
{
	KneelValidationInput input = validKneelInput();
	input.insufficientTuForKneelDown = true;
	const MoveValidationResult result = validateKneelCommand(input);
	expect(!result.accepted && result.rejectCode == "INSUFFICIENT_TU", "insufficient TU rejected");
}

static void testKneelHumanActorRejected()
{
	KneelValidationInput input = validKneelInput();
	input.actorAiControlled = false;
	const MoveValidationResult result = validateKneelCommand(input);
	expect(!result.accepted && result.rejectCode == "NOT_AI_CONTROLLED", "human-controlled kneel rejected");
}

static void testKneelStaleSnapshotRejected()
{
	KneelValidationInput input = validKneelInput();
	input.commandSnapshotSha256 = "0000000000000000000000000000000000000000000000000000000000000001";
	const MoveValidationResult result = validateKneelCommand(input);
	expect(!result.accepted && result.rejectCode == "STALE_SNAPSHOT", "kneel stale snapshot rejected");
}

static void testKneelPriorCommandActiveRejected()
{
	KneelValidationInput input = validKneelInput();
	input.priorCommandNonTerminal = true;
	const MoveValidationResult result = validateKneelCommand(input);
	expect(!result.accepted && result.rejectCode == "PRIOR_COMMAND_ACTIVE", "kneel blocked while move in flight");
}

static void testKneelCannotKneelPostureRejected()
{
	KneelValidationInput input = validKneelInput();
	input.actorCanKneelPosture = false;
	const MoveValidationResult result = validateKneelCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_ACTOR", "non-kneeling posture rejected");
}

static void testKneelBindingUsesSharedTacticalValidation()
{
	expect(validateTacticalCommandBinding(validKneelInput()).accepted,
		"kneel binding passes shared tactical validation before posture checks");
}

static void testParseKneelCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"kneel-test-1\",\n"
		"  \"command_type\": \"KNEEL\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	KneelCommandParsed parsed = {};
	expect(parseKneelCommandJson(json, &parsed), "kneel parse must succeed");
	expect(parsed.valid && parsed.commandType == "KNEEL" && parsed.actorUnitId == 1, "kneel fields parsed");
}

static void testParseMoveRejectsKneelType()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"kneel-test-1\",\n"
		"  \"command_type\": \"KNEEL\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	MoveCommandParsed parsed = {};
	expect(!parseMoveCommandJson(json, &parsed), "MOVE parser must reject KNEEL command_type");
	expect(parsed.rejectCode == "UNSUPPORTED_COMMAND_TYPE", "MOVE parser kneel reject code");
}

static void testTacticalBindingSharedByMoveAndKneel()
{
	MoveValidationInput moveInput = validInput();
	moveInput.battleBusy = true;
	KneelValidationInput kneelInput = validKneelInput();
	kneelInput.battleBusy = true;
	expect(validateMoveCommand(moveInput).rejectCode == "BATTLE_BUSY",
		"MOVE battle busy rejection preserved after shared binding refactor");
	expect(validateKneelCommand(kneelInput).rejectCode == "BATTLE_BUSY",
		"KNEEL battle busy uses shared binding validation");
}

static void testHandleStateCompanionStageOrder()
{
	size_t count = 0;
	const CompanionCommandRuntimeSupport::HandleStateCompanionStage *order =
		CompanionCommandRuntimeSupport::handleStateCompanionStageOrder(&count);
	expect(count == 3, "handleState companion stage count");
	expect(order[0] == CompanionCommandRuntimeSupport::HANDLE_STATE_COMPANION_BRIEFING,
		"briefing fulfillment precedes command tick");
	expect(order[1] == CompanionCommandRuntimeSupport::HANDLE_STATE_COMPANION_COMMAND_TICK,
		"command tick precedes idle snapshot export");
	expect(order[2] == CompanionCommandRuntimeSupport::HANDLE_STATE_COMPANION_IDLE_SNAPSHOT,
		"idle snapshot export follows command tick");
}

static void testExportKneeledSnapshotField()
{
	expect(!CompanionCommandRuntimeSupport::exportKneeledSnapshotField(false), "standing exports false");
	expect(CompanionCommandRuntimeSupport::exportKneeledSnapshotField(true), "kneeling exports true");
}

static void testStaleInboxArchiveEvaluation()
{
	using namespace CompanionCommandRuntimeSupport;
	InboxCommandBinding inbox = {};
	inbox.parsed = true;
	inbox.commandId = "move-test-1";
	inbox.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	inbox.snapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	inbox.turn = 2;
	inbox.stateVersion = 2;
	SnapshotExportBinding current = {};
	current.sessionId = inbox.sessionId;
	current.snapshotSha256 = inbox.snapshotSha256;
	current.turn = 2;
	current.stateVersion = 3;
	expect(evaluateStaleInboxArchive(inbox, current, false, false, std::string()) ==
		STALE_INBOX_BINDING_MISMATCH, "state_version mismatch archives");
	expect(evaluateStaleInboxArchive(inbox, current, true, false, std::string()) ==
		STALE_INBOX_JOURNAL_DUPLICATE, "journal duplicate archives");
	expect(evaluateStaleInboxArchive(inbox, current, false, true, inbox.commandId) ==
		STALE_INBOX_KEEP, "in-flight same command kept");
	expect(parseInboxCommandBinding(SAMPLE_MOVE_COMMAND, &inbox), "sample move parses binding");
}

static void testRosterCasualtySettlementDeferBounds()
{
	using namespace CompanionCommandCasualtySettlementCore;
	expect(ROSTER_CASUALTY_SETTLEMENT_DEFER_MAX_MS < 8000,
		"defer budget stays below cooperative relay local wait");
	expect(shouldDeferRosterCasualtySettlement(0, 1000), "unsettled roster casualty defers immediately");
	expect(shouldDeferRosterCasualtySettlement(1000, 2000), "within budget still defers");
	expect(shouldDeferRosterCasualtySettlement(1000, 1000 + ROSTER_CASUALTY_SETTLEMENT_DEFER_MAX_MS - 1),
		"last millisecond inside budget defers");
	expect(!shouldDeferRosterCasualtySettlement(1000, 1000 + ROSTER_CASUALTY_SETTLEMENT_DEFER_MAX_MS),
		"timeout releases defer for best-effort terminal export");
	expect(!shouldDeferRosterCasualtySettlement(1000, 1000 + ROSTER_CASUALTY_SETTLEMENT_DEFER_MAX_MS + 5000),
		"well past timeout does not defer");
}

static void testPostCommandSnapshotExportPolicy()
{
	using namespace CompanionCommandRuntimeSupport;
	expect(shouldExportSnapshotBeforeTerminalCommandResult(CMD_COMPLETED),
		"completed commands export snapshot before terminal result");
	expect(shouldExportSnapshotBeforeTerminalCommandResult(CMD_INTERRUPTED),
		"interrupted commands export snapshot before terminal result");
	expect(shouldExportSnapshotBeforeTerminalCommandResult(CMD_FAILED),
		"failed commands export snapshot before terminal result");
	expect(!shouldExportSnapshotBeforeTerminalCommandResult(CMD_EXECUTING),
		"executing must not bind terminal snapshot export");
	expect(!shouldExportSnapshotBeforeTerminalCommandResult(CMD_REJECTED),
		"rejected commands must not export snapshot for result binding");
}

static void testWalkExecutingDefersTerminalSnapshotBinding()
{
	WalkCompletionInput input = {};
	input.requestedDest = Position(5, 5, 0);
	input.startPos = Position(1, 1, 0);
	input.currentPos = Position(3, 1, 0);
	input.battleBusy = true;
	input.actorAlive = true;
	input.actorStillAiControlled = true;
	expect(evaluateWalkTerminalStatus(input) == CMD_EXECUTING, "busy walk stays executing");
	expect(!CompanionCommandRuntimeSupport::shouldExportSnapshotBeforeTerminalCommandResult(CMD_EXECUTING),
		"executing walk must not export terminal snapshot");
}

static SnapShotValidationInput validSnapInput()
{
	SnapShotValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.targetX = 5;
	input.targetY = 5;
	input.targetZ = 0;
	input.mapSizeX = 10;
	input.mapSizeY = 10;
	input.mapSizeZ = 1;
	input.activeHandValid = true;
	input.weaponPresentInHand = true;
	input.weaponSupportsFireMode = true;
	input.weaponHasAmmo = true;
	input.weaponUsable = true;
	input.insufficientTuForFireMode = false;
	input.visibleUnitTargetDefinitelyBlocked = false;
	return input;
}

static void testSnapShotAuthorizedAccepted()
{
	expect(validateSnapShotCommand(validSnapInput()).accepted, "authorized snap accepted");
}

static void testSnapShotInvalidTargetRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.targetX = 99;
	const MoveValidationResult result = validateSnapShotCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_TARGET", "invalid target rejected");
}

static void testSnapShotNoAmmoRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.weaponHasAmmo = false;
	const MoveValidationResult result = validateSnapShotCommand(input);
	expect(!result.accepted && result.rejectCode == "NO_AMMUNITION", "no ammo rejected");
}

static void testSnapShotInsufficientTuRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.insufficientTuForFireMode = true;
	const MoveValidationResult result = validateSnapShotCommand(input);
	expect(!result.accepted && result.rejectCode == "INSUFFICIENT_TU", "insufficient TU rejected");
}

static void testSnapShotUnsupportedWeaponRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.weaponSupportsFireMode = false;
	const MoveValidationResult result = validateSnapShotCommand(input);
	expect(!result.accepted && result.rejectCode == "UNSUPPORTED_WEAPON", "unsupported weapon rejected");
}

static void testSnapShotHumanActorRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.actorAiControlled = false;
	const MoveValidationResult result = validateSnapShotCommand(input);
	expect(!result.accepted && result.rejectCode == "NOT_AI_CONTROLLED", "human-controlled snap rejected");
}

static void testDirectedFireVisibleUnitBlockedRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.visibleUnitTargetDefinitelyBlocked = true;
	const MoveValidationResult snap = validateSnapShotCommand(input);
	expect(!snap.accepted && snap.rejectCode == "NO_LINE_OF_FIRE", "snap blocked by visible unit voxel preflight");
	SnapShotValidationInput autoInput = validSnapInput();
	autoInput.visibleUnitTargetDefinitelyBlocked = true;
	const MoveValidationResult autoShot = validateAutoShotCommand(autoInput);
	expect(!autoShot.accepted && autoShot.rejectCode == "NO_LINE_OF_FIRE", "auto blocked by preflight");
	SnapShotValidationInput aimInput = validSnapInput();
	aimInput.visibleUnitTargetDefinitelyBlocked = true;
	const MoveValidationResult aimed = validateAimedShotCommand(aimInput);
	expect(!aimed.accepted && aimed.rejectCode == "NO_LINE_OF_FIRE", "aimed blocked by preflight");
}

static void testDirectedFireVisibleUnitBlockedFlagFalseAccepted()
{
	SnapShotValidationInput input = validSnapInput();
	input.visibleUnitTargetDefinitelyBlocked = false;
	expect(validateSnapShotCommand(input).accepted, "preflight clear keeps snap accepted");
	expect(validateAutoShotCommand(input).accepted, "preflight clear keeps auto accepted");
	expect(validateAimedShotCommand(input).accepted, "preflight clear keeps aimed accepted");
}

static void testParseSnapShotCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"snap-test-1\",\n"
		"  \"command_type\": \"SNAP_SHOT\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		"  \"target\": { \"x\": 5, \"y\": 5, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	SnapShotCommandParsed parsed = {};
	expect(parseSnapShotCommandJson(json, &parsed), "snap parse must succeed");
	expect(parsed.valid && parsed.commandType == "SNAP_SHOT" && parsed.targetX == 5, "snap fields parsed");
	expect(parsed.activeHand == "STR_RIGHT_HAND", "active hand parsed");
}

static void testSnapShotTerminalLifecycle()
{
	SnapShotCompletionInput busy = {};
	busy.battleBusy = true;
	busy.actorAlive = true;
	busy.actorStillAiControlled = true;
	busy.projectileLaunched = false;
	expect(evaluateSnapShotTerminalStatus(busy) == CMD_EXECUTING, "busy snap stays executing");

	SnapShotCompletionInput done = {};
	done.battleBusy = false;
	done.actorAlive = true;
	done.actorStillAiControlled = true;
	done.projectileLaunched = true;
	expect(evaluateSnapShotTerminalStatus(done) == CMD_COMPLETED, "resolved snap completes after projectile");

	SnapShotCompletionInput missIdle = done;
	expect(evaluateSnapShotTerminalStatus(missIdle) == CMD_COMPLETED, "miss still completes when projectile launched");

	SnapShotCompletionInput aborted = {};
	aborted.battleBusy = false;
	aborted.actorAlive = true;
	aborted.actorStillAiControlled = true;
	aborted.projectileLaunched = false;
	aborted.actionResult = "STR_NO_LINE_OF_FIRE";
	expect(evaluateSnapShotTerminalStatus(aborted) == CMD_FAILED, "idle without projectile is failed not completed");

	SnapShotCompletionInput interrupted = {};
	interrupted.battleBusy = false;
	interrupted.actorAlive = true;
	interrupted.actorStillAiControlled = false;
	interrupted.projectileLaunched = true;
	expect(evaluateSnapShotTerminalStatus(interrupted) == CMD_INTERRUPTED, "authority loss interrupts snap");

	SnapShotCompletionInput burstMidFlight = done;
	burstMidFlight.battleBusy = true;
	burstMidFlight.projectileLaunched = true;
	expect(evaluateSnapShotTerminalStatus(burstMidFlight) == CMD_EXECUTING,
		"auto burst stays executing while battle busy after first projectile");
}

static void testAutoShotAuthorizedAccepted()
{
	SnapShotValidationInput input = validSnapInput();
	expect(validateAutoShotCommand(input).accepted, "authorized auto accepted");
}

static void testAutoShotUnsupportedWeaponRejected()
{
	SnapShotValidationInput input = validSnapInput();
	input.weaponSupportsFireMode = false;
	expect(!validateAutoShotCommand(input).accepted, "unsupported auto rejected");
}

static void testAimedShotAuthorizedAccepted()
{
	SnapShotValidationInput input = validSnapInput();
	expect(validateAimedShotCommand(input).accepted, "authorized aimed accepted");
}

static void testParseAutoShotCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"auto-test-1\",\n"
		"  \"command_type\": \"AUTO_SHOT\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		"  \"target\": { \"x\": 5, \"y\": 5, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	SnapShotCommandParsed parsed = {};
	expect(parseAutoShotCommandJson(json, &parsed), "auto parse must succeed");
	expect(parsed.commandType == "AUTO_SHOT", "auto command type parsed");
}

static void testParseAimedShotCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"aim-test-1\",\n"
		"  \"command_type\": \"AIMED_SHOT\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		"  \"target\": { \"x\": 5, \"y\": 5, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	SnapShotCommandParsed parsed = {};
	expect(parseAimedShotCommandJson(json, &parsed), "aimed parse must succeed");
	expect(parsed.commandType == "AIMED_SHOT", "aimed command type parsed");
}

static PrimeValidationInput validPrimeInput()
{
	PrimeValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.activeHandValid = true;
	input.grenadePresentInHand = true;
	input.grenadeSupportsPrime = true;
	input.grenadeAlreadyPrimed = false;
	input.invalidFuseTimer = false;
	input.grenadeUsable = true;
	input.insufficientTuForPrime = false;
	return input;
}

static ThrowValidationInput validThrowInput()
{
	ThrowValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.targetX = 5;
	input.targetY = 5;
	input.targetZ = 0;
	input.mapSizeX = 10;
	input.mapSizeY = 10;
	input.mapSizeZ = 1;
	input.activeHandValid = true;
	input.grenadePresentInHand = true;
	input.grenadeSupportsThrow = true;
	input.grenadeUsable = true;
	input.targetOutOfThrowRange = false;
	input.insufficientTuForThrow = false;
	return input;
}

static void testPrimeAuthorizedAccepted()
{
	expect(validatePrimeCommand(validPrimeInput()).accepted, "authorized prime accepted");
}

static void testPrimeAlreadyPrimedRejected()
{
	PrimeValidationInput input = validPrimeInput();
	input.grenadeAlreadyPrimed = true;
	const MoveValidationResult result = validatePrimeCommand(input);
	expect(!result.accepted && result.rejectCode == "ALREADY_PRIMED", "already primed rejected");
}

static void testPrimeInvalidFuseRejected()
{
	PrimeValidationInput input = validPrimeInput();
	input.invalidFuseTimer = true;
	const MoveValidationResult result = validatePrimeCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_FUSE_TIMER", "invalid fuse rejected");
}

static void testParsePrimeCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"prime-test-1\",\n"
		"  \"command_type\": \"PRIME\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		"  \"fuse_timer\": 3,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	PrimeCommandParsed parsed = {};
	expect(parsePrimeCommandJson(json, &parsed), "prime parse must succeed");
	expect(parsed.valid && parsed.fuseTimer == 3, "fuse timer parsed");
}

static void testParsePrimeCommandJsonFuseZero()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"prime-fuse0\",\n"
		"  \"command_type\": \"PRIME\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		"  \"fuse_timer\": 0,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	PrimeCommandParsed parsed = {};
	expect(parsePrimeCommandJson(json, &parsed), "prime fuse 0 parse must succeed");
	expect(parsed.valid && parsed.fuseTimer == 0, "fuse timer 0 is truthy-preserved");
}

static void testParseThrowCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"throw-test-1\",\n"
		"  \"command_type\": \"THROW\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"active_hand\": \"STR_LEFT_HAND\",\n"
		"  \"target\": { \"x\": 5, \"y\": 5, \"z\": 0 },\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	ThrowCommandParsed parsed = {};
	expect(parseThrowCommandJson(json, &parsed), "throw parse must succeed");
	expect(parsed.valid && parsed.targetX == 5, "throw target parsed");
}

static ReloadValidationInput validReloadInput()
{
	ReloadValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.weaponBattleItemId = 10;
	input.ammoBattleItemId = 20;
	input.weaponResolved = true;
	input.ammoResolved = true;
	input.campaignResearchAvailable = true;
	input.weaponBelongsToActor = true;
	input.ammoBelongsToActor = true;
	input.weaponInEitherHand = true;
	input.weaponIsFirearm = true;
	input.weaponNeedsPhysicalMagazine = true;
	input.weaponChamberEmptyForReloadUi = true;
	input.ammoIsPhysicalClip = true;
	input.ammoCarriedOffGround = true;
	input.ammoNotAlreadyLoadedInWeapon = true;
	input.ammunitionCompatible = true;
	input.weaponUsableEnvironment = true;
	input.weaponResearchAuthorized = true;
	input.ammoResearchAuthorized = true;
	input.insufficientTuForReload = false;
	return input;
}

static void testReloadAuthorizedAccepted()
{
	expect(validateReloadCommand(validReloadInput()).accepted, "authorized reload accepted");
}

static void testReloadNoCampaignResearchRejected()
{
	ReloadValidationInput input = validReloadInput();
	input.campaignResearchAvailable = false;
	const MoveValidationResult result = validateReloadCommand(input);
	expect(!result.accepted && result.rejectCode == "RESEARCH_CONTEXT_UNAVAILABLE", "missing campaign rejects reload");
}

static void testReloadWeaponAlreadyLoadedRejected()
{
	ReloadValidationInput input = validReloadInput();
	input.weaponChamberEmptyForReloadUi = false;
	const MoveValidationResult result = validateReloadCommand(input);
	expect(!result.accepted && result.rejectCode == "WEAPON_ALREADY_LOADED", "loaded weapon rejects reload");
}

static void testParseReloadCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"reload-test-1\",\n"
		"  \"command_type\": \"RELOAD\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"weapon_battle_item_id\": 10,\n"
		"  \"ammo_battle_item_id\": 20,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	ReloadCommandParsed parsed = {};
	expect(parseReloadCommandJson(json, &parsed), "reload parse must succeed");
	expect(parsed.valid && parsed.weaponBattleItemId == 10 && parsed.ammoBattleItemId == 20, "reload item ids parsed");
}

static UnloadValidationInput validUnloadInput()
{
	UnloadValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.weaponBattleItemId = 10;
	input.loadedAmmoBattleItemId = 20;
	input.weaponResolved = true;
	input.campaignResearchAvailable = true;
	input.weaponBelongsToActor = true;
	input.weaponInEitherHand = true;
	input.weaponIsFirearm = true;
	input.weaponNeedsPhysicalMagazine = true;
	input.weaponHasLoadedMagazine = true;
	input.loadedAmmoIdentityMatches = true;
	input.loadedAmmoIsPhysicalClip = true;
	input.otherHandClearExceptWeapon = true;
	input.weaponUsableEnvironment = true;
	input.weaponResearchAuthorized = true;
	input.ammoResearchAuthorized = true;
	input.insufficientTuForUnload = false;
	return input;
}

static void testUnloadAuthorizedAccepted()
{
	expect(validateUnloadCommand(validUnloadInput()).accepted, "authorized unload accepted");
}

static void testUnloadNoMagazineRejected()
{
	UnloadValidationInput input = validUnloadInput();
	input.weaponHasLoadedMagazine = false;
	input.loadedAmmoIdentityMatches = false;
	input.loadedAmmoIsPhysicalClip = false;
	const MoveValidationResult result = validateUnloadCommand(input);
	expect(!result.accepted && result.rejectCode == "NO_MAGAZINE_INSERTED", "no magazine rejects unload");
}

static void testUnloadMagazineIdentityMismatchRejected()
{
	UnloadValidationInput input = validUnloadInput();
	input.loadedAmmoIdentityMatches = false;
	const MoveValidationResult result = validateUnloadCommand(input);
	expect(!result.accepted && result.rejectCode == "MAGAZINE_IDENTITY_MISMATCH", "wrong magazine id rejects unload");
}

static void testUnloadHandsNotFreeRejected()
{
	UnloadValidationInput input = validUnloadInput();
	input.otherHandClearExceptWeapon = false;
	const MoveValidationResult result = validateUnloadCommand(input);
	expect(!result.accepted && result.rejectCode == "UNLOAD_HANDS_NOT_FREE", "occupied off-hand rejects unload");
}

static void testUnloadNoCampaignResearchRejected()
{
	UnloadValidationInput input = validUnloadInput();
	input.campaignResearchAvailable = false;
	const MoveValidationResult result = validateUnloadCommand(input);
	expect(!result.accepted && result.rejectCode == "RESEARCH_CONTEXT_UNAVAILABLE", "missing campaign rejects unload");
}

static InventoryMoveValidationInput validInventoryMoveInput()
{
	InventoryMoveValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.itemBattleItemId = 200;
	input.sourceSlotId = "STR_BELT";
	input.destinationSlotId = "STR_RIGHT_HAND";
	input.destinationX = 0;
	input.destinationY = 0;
	input.sourceCoordsGuard = false;
	input.sourceX = 0;
	input.sourceY = 0;
	input.itemResolved = true;
	input.itemBelongsToActor = true;
	input.itemCarriedOffGround = true;
	input.itemSlotMatchesCommand = true;
	input.sourceCoordsMatch = true;
	input.itemIsFixedWeapon = false;
	input.itemNestedInFirearm = false;
	input.primedExplosiveBlocked = false;
	input.destinationSlotResolved = true;
	input.destinationIsGround = false;
	input.destinationAllowedCarriedType = true;
	input.destinationSameSectionAsCommand = true;
	input.itemFitsDestinationGeometry = true;
	input.destinationOccupied = false;
	input.transferCostResolved = true;
	input.transferCostTu = 4;
	input.insufficientTuForInventoryMove = false;
	return input;
}

/**
 * P2-UI-REFRESH-02 reachability: companion inbox polling only runs from
 * BattlescapeGame::think() (player turn) and popState. Game::think() invokes only
 * the top state; InventoryState does not delegate to BattlescapeState, so while
 * inventory is open the companion command tick does not run. btnInventoryClick also
 * calls cancelAllActions() and requires allowButtons() (!isBusy()), blocking overlap
 * with in-flight MOVE/FIRE battle states.
 */
static void testP2UiRefreshCompanionPollBlockedWhenBattleBusy()
{
	using namespace CompanionCommandRuntimeSupport;
	expect(!shouldRunCompanionCommandTickDuringThink(true, false, false, true, true, false),
		"P2: think-path inbox poll disabled when battle animation states active");

	ReloadValidationInput reload = validReloadInput();
	reload.battleBusy = true;
	const MoveValidationResult reloadBusy = validateReloadCommand(reload);
	expect(!reloadBusy.accepted && reloadBusy.rejectCode == "BATTLE_BUSY",
		"P2: RELOAD preflight rejected while battle busy");

	InventoryMoveValidationInput inv = validInventoryMoveInput();
	inv.battleBusy = true;
	const MoveValidationResult invBusy = validateInventoryMoveCommand(inv);
	expect(!invBusy.accepted && invBusy.rejectCode == "BATTLE_BUSY",
		"P2: INVENTORY_MOVE preflight rejected while battle busy");
}

static void testInventoryMoveAuthorizedAccepted()
{
	expect(validateInventoryMoveCommand(validInventoryMoveInput()).accepted, "authorized inventory move accepted");
}

static void testInventoryMoveItemNotFoundRejected()
{
	InventoryMoveValidationInput input = validInventoryMoveInput();
	input.itemResolved = false;
	const MoveValidationResult result = validateInventoryMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_ITEM_ID", "missing item rejects");
}

static void testInventoryMoveGroundDestinationRejected()
{
	InventoryMoveValidationInput input = validInventoryMoveInput();
	input.destinationIsGround = true;
	const MoveValidationResult result = validateInventoryMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "INVALID_DESTINATION_SECTION", "ground destination rejects");
}

static void testInventoryMoveNestedAmmoRejected()
{
	InventoryMoveValidationInput input = validInventoryMoveInput();
	input.itemNestedInFirearm = true;
	const MoveValidationResult result = validateInventoryMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "NESTED_AMMUNITION", "nested magazine rejects move");
}

static void testInventoryMovePrimedExplosiveRejected()
{
	InventoryMoveValidationInput input = validInventoryMoveInput();
	input.primedExplosiveBlocked = true;
	const MoveValidationResult result = validateInventoryMoveCommand(input);
	expect(!result.accepted && result.rejectCode == "PRIMED_EXPLOSIVE_NOT_MOVABLE", "primed explosive rejects move");
}

static PickupValidationInput validPickupInput()
{
	PickupValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.itemBattleItemId = 900;
	input.groundX = 5;
	input.groundY = 6;
	input.groundZ = 0;
	input.destinationSlotId = "STR_RIGHT_HAND";
	input.destinationX = 0;
	input.destinationY = 0;

	input.campaignResearchAvailable = true;
	input.groundPositionMatchesActor = true;
	input.itemResolvedOnActorGround = true;
	input.itemResearchAuthorized = true;
	input.itemIsFixedWeapon = false;
	input.destinationSlotResolved = true;
	input.destinationAllowedCarriedType = true;
	input.destinationSameSectionAsCommand = true;
	input.itemFitsDestinationGeometry = true;
	input.destinationOccupied = false;
	input.transferCostResolved = true;
	input.transferCostTu = 8;
	input.insufficientTuForPickup = false;
	return input;
}

static void testPickupAuthorizedAccepted()
{
	expect(validatePickupCommand(validPickupInput()).accepted, "authorized pickup accepted");
}

static void testPickupGroundPositionRejected()
{
	PickupValidationInput input = validPickupInput();
	input.groundPositionMatchesActor = false;
	const MoveValidationResult result = validatePickupCommand(input);
	expect(!result.accepted && result.rejectCode == "GROUND_POSITION_MISMATCH", "ground position mismatch rejects pickup");
}

static void testPickupGroundItemUnknownRejected()
{
	PickupValidationInput input = validPickupInput();
	input.itemResolvedOnActorGround = false;
	const MoveValidationResult result = validatePickupCommand(input);
	expect(!result.accepted && result.rejectCode == "GROUND_ITEM_NOT_KNOWN", "missing ground export rejects pickup");
}

static void testParsePickupCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"pickup-test-1\",\n"
		"  \"command_type\": \"PICKUP\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"item_battle_item_id\": 900,\n"
		"  \"ground_position\": { \"x\": 5, \"y\": 6, \"z\": 0 },\n"
		"  \"destination_slot\": \"STR_RIGHT_HAND\",\n"
		"  \"destination_x\": 0,\n"
		"  \"destination_y\": 0,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	PickupCommandParsed parsed = {};
	expect(parsePickupCommandJson(json, &parsed), "pickup parse must succeed");
	expect(parsed.valid && parsed.itemBattleItemId == 900 && parsed.groundX == 5 && parsed.destinationSlot == "STR_RIGHT_HAND",
		"pickup fields parsed");
}

static DropValidationInput validDropInput()
{
	DropValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = true;
	input.battleBusy = false;
	input.battleInitialized = true;
	input.debugMode = false;
	input.unitsFalling = false;
	input.sessionId = "a1b2c3d4e5f6789012345678abcdef01";
	input.turn = 2;
	input.lastSnapshotSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	input.commandSnapshotSha256 = input.lastSnapshotSha256;
	input.commandSessionId = input.sessionId;
	input.commandTurn = 2;
	input.commandStateVersion = 2;
	input.lastExportedStateVersion = 2;
	input.actorUnitId = 1;
	input.actorIsPlayerSoldier = true;
	input.actorAlive = true;
	input.actorAiControlled = true;
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = false;
	input.humanOverrideRevoked = false;
	input.itemBattleItemId = 400;
	input.sourceSlotId = "STR_BELT";
	input.sourceCoordsGuard = false;
	input.sourceX = 0;
	input.sourceY = 0;

	input.campaignResearchAvailable = true;
	input.itemResolved = true;
	input.itemBelongsToActor = true;
	input.itemCarriedOffGround = true;
	input.itemSlotMatchesCommand = true;
	input.sourceCoordsMatch = true;
	input.itemIsFixedWeapon = false;
	input.itemNestedInFirearm = false;
	input.itemResearchAuthorized = true;
	input.groundRuleResolved = true;
	input.transferCostResolved = true;
	input.transferCostTu = 6;
	input.groundHasRoom = true;
	input.insufficientTuForDrop = false;
	return input;
}

static void testDropAuthorizedAccepted()
{
	expect(validateDropCommand(validDropInput()).accepted, "authorized drop accepted");
}

static void testDropNestedAmmoRejected()
{
	DropValidationInput input = validDropInput();
	input.itemNestedInFirearm = true;
	const MoveValidationResult result = validateDropCommand(input);
	expect(!result.accepted && result.rejectCode == "NESTED_AMMUNITION", "nested magazine rejects drop");
}

static void testParseDropCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"drop-test-1\",\n"
		"  \"command_type\": \"DROP\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"item_battle_item_id\": 400,\n"
		"  \"source_slot\": \"STR_BELT\",\n"
		"  \"source_x\": 0,\n"
		"  \"source_y\": 1,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	DropCommandParsed parsed = {};
	expect(parseDropCommandJson(json, &parsed), "drop parse must succeed");
	expect(parsed.valid && parsed.itemBattleItemId == 400 && parsed.sourceCoordsGuard &&
		parsed.sourceX == 0 && parsed.sourceY == 1, "drop optional source coords parsed");
}

static void testParseInventoryMoveCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"inv-move-1\",\n"
		"  \"command_type\": \"INVENTORY_MOVE\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"item_battle_item_id\": 200,\n"
		"  \"source_slot\": \"STR_BELT\",\n"
		"  \"destination_slot\": \"STR_RIGHT_HAND\",\n"
		"  \"destination_x\": 0,\n"
		"  \"destination_y\": 0,\n"
		"  \"source_x\": 1,\n"
		"  \"source_y\": 0,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	InventoryMoveCommandParsed parsed = {};
	expect(parseInventoryMoveCommandJson(json, &parsed), "inventory move parse must succeed");
	expect(parsed.valid && parsed.itemBattleItemId == 200 && parsed.sourceCoordsGuard &&
		parsed.sourceX == 1 && parsed.sourceY == 0, "inventory move optional source coords parsed");
}

static void testParseUnloadCommandJson()
{
	const std::string json =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_id\": \"unload-test-1\",\n"
		"  \"command_type\": \"UNLOAD\",\n"
		"  \"actor_unit_id\": 1,\n"
		"  \"weapon_battle_item_id\": 10,\n"
		"  \"loaded_ammo_battle_item_id\": 20,\n"
		"  \"snapshot_sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\n"
		"  \"session_id\": \"a1b2c3d4e5f6789012345678abcdef01\",\n"
		"  \"turn\": 2,\n"
		"  \"state_version\": 2\n"
		"}\n";
	UnloadCommandParsed parsed = {};
	expect(parseUnloadCommandJson(json, &parsed), "unload parse must succeed");
	expect(parsed.valid && parsed.weaponBattleItemId == 10 && parsed.loadedAmmoBattleItemId == 20, "unload item ids parsed");
}

static void testThrowOutOfRangeRejected()
{
	ThrowValidationInput input = validThrowInput();
	input.targetOutOfThrowRange = true;
	const MoveValidationResult result = validateThrowCommand(input);
	expect(!result.accepted && result.rejectCode == "OUT_OF_RANGE", "out of range throw rejected");
}

static void testThrowTerminalLifecycle()
{
	SnapShotCompletionInput done = {};
	done.battleBusy = false;
	done.actorAlive = true;
	done.actorStillAiControlled = true;
	done.projectileLaunched = true;
	expect(evaluateThrowTerminalStatus(done) == CMD_COMPLETED, "throw completes when grenade released");
}

static void testPrimeFuseTimerBattleTypeRules()
{
	expect(isValidPrimeFuseTimerForBattleType(0, BT_GRENADE), "timed grenade fuse 0 ok");
	expect(isValidPrimeFuseTimerForBattleType(3, BT_GRENADE), "timed grenade fuse 3 ok");
	expect(!isValidPrimeFuseTimerForBattleType(24, BT_GRENADE), "timed grenade fuse 24 invalid");
	expect(isValidPrimeFuseTimerForBattleType(0, BT_PROXIMITYGRENADE), "proximity fuse 0 ok");
	expect(!isValidPrimeFuseTimerForBattleType(1, BT_PROXIMITYGRENADE), "proximity fuse 1 invalid");
}

int main()
{
	testOwnershipDefaultHuman();
	testOwnershipPowerShellSpacing();
	testOwnershipCompactJson();
	testOwnershipFlexibleWhitespace();
	testOwnershipWrongSessionRejected();
	testOwnershipMalformedJsonRejected();
	testOwnershipInvalidAssignmentModeRejected();
	testDeferInFlightCompletionWhileReentrant();
	testOrphanDirectedFireSessionBinding();
	testCompanionCommandPollDuringIdleThink();
	testCompanionCommandInboxThrottle();
	testExecutorDisabledByDefault();
	testHumanActorRejected();
	testWrongTurnRejected();
	testStaleSnapshotRejected();
	testDuplicateCommandRejected();
	testBusyRejected();
	testInvalidDestinationRejected();
	testStaleStateVersionRejected();
	testAuthorizedActorAccepted();
	testParseMoveCommandJson();
	testParseMoveCommandPowerShellSpacing();
	testParseMoveCommandCompactJson();
	testParseMoveCommandReorderedProperties();
	testParseMoveCommandDestinationNotConfusedWithOtherObjects();
	testParseMoveCommandInvalidCommandIdRejected();
	testWalkCompletionDetection();
	testWalkCompletionCrossLevelZ();
	testWalkInterruptionDetection();
	testHumanOverrideDuringWalk();
	testWalkStopDiagnosticsTurnAndSpot();
	testWalkStopDiagnosticsPartialUnknown();
	testWalkStopDiagnosticsInsufficientTu();
	testWalkStopDiagnosticsReactionFire();
	testWalkStopDiagnosticsCompletedOmitsReason();
	testWalkStopDiagnosticsPathBlocked();
	testWalkStopDiagnosticsProximityGrenade();
	testWalkStopDiagnosticsActorIncapacitated();
	testWalkStopTokenClassifier();
	testWalkStopSingleRecordedCauseHostilePrecedence();
	testWalkHostileSpotZeroTileInterruptedClassification();
	testStandAuthorizedAccepted();
	testStandAlreadyStandingRejected();
	testStandInsufficientTuRejected();
	testKneelThenStandValidationFlow();
	testParseStandCommandJson();
	testDuplicateInboxNoOpPreservesCompletedResult();
	testDuplicateInboxNoOpPreservesLegacyTerminalResult();
	testDuplicateInboxNoOpPreservesOrphanExecutingResult();
	testDuplicateInboxConflictPreservesCompletedResult();
	testLegacyModifiedInboxPreservesTerminalWithoutOverwrite();
	testDuplicateInboxRejectWhenPriorResultMissing();
	testDuplicatePayloadConflictJsonRecordsMismatch();
	testDuplicateInboxProceedsForFreshCommandId();
	testDuplicateInboxPreservesInterruptedTerminalStatus();
	testDuplicateInboxPreservesFailedTerminalStatus();
	testDuplicateInboxIdentityMismatchWhenResultCommandIdDiffers();
	testDuplicateInboxIdentityMismatchWhenResultCommandIdMissing();
	testDuplicateInboxIdentityMismatchWhenResultMalformed();
	testDuplicateRejectWritesOnlyWhenResultMissing();
	testDuplicateIdentityConflictJsonShape();
	testRuntimeValidationPathAcceptsFreshMoveAfterDuplicateHandled();
	testKneelAuthorizedAccepted();
	testKneelAlreadyKneelingRejected();
	testKneelInsufficientTuRejected();
	testKneelHumanActorRejected();
	testKneelStaleSnapshotRejected();
	testKneelPriorCommandActiveRejected();
	testKneelCannotKneelPostureRejected();
	testKneelBindingUsesSharedTacticalValidation();
	testParseKneelCommandJson();
	testParseMoveRejectsKneelType();
	testTacticalBindingSharedByMoveAndKneel();
	testHandleStateCompanionStageOrder();
	testExportKneeledSnapshotField();
	testStaleInboxArchiveEvaluation();
	testRosterCasualtySettlementDeferBounds();
	testPostCommandSnapshotExportPolicy();
	testWalkExecutingDefersTerminalSnapshotBinding();
	testSnapShotAuthorizedAccepted();
	testSnapShotInvalidTargetRejected();
	testSnapShotNoAmmoRejected();
	testSnapShotInsufficientTuRejected();
	testSnapShotUnsupportedWeaponRejected();
	testSnapShotHumanActorRejected();
	testDirectedFireVisibleUnitBlockedRejected();
	testDirectedFireVisibleUnitBlockedFlagFalseAccepted();
	testParseSnapShotCommandJson();
	testSnapShotTerminalLifecycle();
	testAutoShotAuthorizedAccepted();
	testAutoShotUnsupportedWeaponRejected();
	testAimedShotAuthorizedAccepted();
	testParseAutoShotCommandJson();
	testParseAimedShotCommandJson();
	testPrimeAuthorizedAccepted();
	testPrimeAlreadyPrimedRejected();
	testPrimeInvalidFuseRejected();
	testParsePrimeCommandJson();
	testParsePrimeCommandJsonFuseZero();
	testParseThrowCommandJson();
	testReloadAuthorizedAccepted();
	testReloadNoCampaignResearchRejected();
	testReloadWeaponAlreadyLoadedRejected();
	testParseReloadCommandJson();
	testP2UiRefreshCompanionPollBlockedWhenBattleBusy();
	testUnloadAuthorizedAccepted();
	testUnloadNoMagazineRejected();
	testUnloadMagazineIdentityMismatchRejected();
	testUnloadHandsNotFreeRejected();
	testUnloadNoCampaignResearchRejected();
	testParseUnloadCommandJson();
	testInventoryMoveAuthorizedAccepted();
	testInventoryMoveItemNotFoundRejected();
	testInventoryMoveGroundDestinationRejected();
	testInventoryMoveNestedAmmoRejected();
	testInventoryMovePrimedExplosiveRejected();
	testParseInventoryMoveCommandJson();
	testPickupAuthorizedAccepted();
	testPickupGroundPositionRejected();
	testPickupGroundItemUnknownRejected();
	testParsePickupCommandJson();
	testDropAuthorizedAccepted();
	testDropNestedAmmoRejected();
	testParseDropCommandJson();
	testThrowOutOfRangeRejected();
	testThrowTerminalLifecycle();
	testPrimeFuseTimerBattleTypeRules();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_command_test: all tests passed." << std::endl;
	return 0;
}
