/*
 * Open X-Com Companion — tactical MOVE command core regression tests (M1-19).
 */
#include "Engine/CompanionCommandCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "Engine/CompanionCommandRuntimeSupport.h"
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

int main()
{
	testOwnershipDefaultHuman();
	testOwnershipPowerShellSpacing();
	testOwnershipCompactJson();
	testOwnershipFlexibleWhitespace();
	testOwnershipWrongSessionRejected();
	testOwnershipMalformedJsonRejected();
	testOwnershipInvalidAssignmentModeRejected();
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
	testWalkInterruptionDetection();
	testHumanOverrideDuringWalk();
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

	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_command_test: all tests passed." << std::endl;
	return 0;
}
