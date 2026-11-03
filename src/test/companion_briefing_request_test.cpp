/*
 * Open X-Com Companion — executable regression tests for F6 briefing request (M1-15B).
 */
#include "Engine/CompanionBriefingTestSupport.h"
#include "Engine/CompanionSha256.h"
#include "Engine/RNG.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionBriefingTestSupport;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static BriefingGateInput safePlayerInput()
{
	BriefingGateInput input = {};
	input.snapshotPathConfigured = true;
	input.debugMode = false;
	input.playerTurn = true;
	input.unitsFalling = false;
	input.battleBusy = false;
	input.battleInitialized = true;
	return input;
}

static void testSafePlayerFulfillNow()
{
	BriefingPressState state = { false };
	const BriefingPressOutcome outcome = evaluateBriefingKeyPress(safePlayerInput(), &state);
	expect(outcome == BRIEFING_PRESS_FULFILL_NOW, "safe player state should fulfill immediately");
	expect(!state.pending, "immediate fulfill must not leave pending flag");
}

static void testBusyDeferral()
{
	BriefingGateInput input = safePlayerInput();
	input.battleBusy = true;
	BriefingPressState state = { false };
	const BriefingPressOutcome outcome = evaluateBriefingKeyPress(input, &state);
	expect(outcome == BRIEFING_PRESS_DEFERRED, "busy battlefield should defer briefing");
	expect(state.pending, "deferred briefing must set pending flag");
}

static void testRapidPressCoalescing()
{
	BriefingGateInput input = safePlayerInput();
	input.battleBusy = true;
	BriefingPressState state = { false };
	evaluateBriefingKeyPress(input, &state);
	evaluateBriefingKeyPress(input, &state);
	evaluateBriefingKeyPress(input, &state);
	expect(state.pending, "rapid presses while busy should remain one pending request");
	expect(shouldFulfillPendingBriefing(safePlayerInput(), state.pending),
		"pending request should fulfill once idle");
}

static void testUniqueRequestIds()
{
	const std::string id1 = makeRequestId();
	const std::string id2 = makeRequestId();
	expect(isValidRequestIdFormat(id1), "request id must be 32 lowercase hex chars");
	expect(isValidRequestIdFormat(id2), "request id must be 32 lowercase hex chars");
	expect(id1 != id2, "successive request ids must differ");
}

static void testRequestJsonShape()
{
	const std::string snapshotSha256 =
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	const std::string json = buildBriefingRequestJson(
		"0123456789abcdef0123456789abcdef",
		"fedcba9876543210fedcba9876543210",
		3,
		snapshotSha256);
	expect(json.find("\"schema_version\": 1") != std::string::npos, "json must include schema_version");
	expect(json.find("\"type\": \"tactical_briefing_request\"") != std::string::npos, "json must include type");
	expect(json.find("\"turn\": 3") != std::string::npos, "json must include turn");
	expect(json.find("\"snapshot_sha256\": \"" + snapshotSha256 + "\"") != std::string::npos,
		"json must include snapshot_sha256");
}

static void testSnapshotSha256KnownVector()
{
	std::string digest;
	expect(CompanionSha256::digestHexLowerUtf8("abc", &digest), "sha256 digest must succeed");
	expect(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		"sha256 must match NIST abc vector");
	expect(CompanionSha256::isValidHexDigest(digest), "digest must pass format validation");
}

static void testSnapshotSha256Deterministic()
{
	const std::string yaml = "schemaVersion: 4\nturn: 1\n";
	std::string first;
	std::string second;
	expect(CompanionSha256::digestHexLowerUtf8(yaml, &first), "first digest must succeed");
	expect(CompanionSha256::digestHexLowerUtf8(yaml, &second), "second digest must succeed");
	expect(first == second, "identical UTF-8 bytes must yield identical digest");
}

static void testSnapshotSha256ContentChange()
{
	std::string digestA;
	std::string digestB;
	expect(CompanionSha256::digestHexLowerUtf8("turn: 1\n", &digestA), "digest A must succeed");
	expect(CompanionSha256::digestHexLowerUtf8("turn: 2\n", &digestB), "digest B must succeed");
	expect(digestA != digestB, "different YAML bytes must yield different digest");
}

static void testNoRequestWithoutSuccessfulExport()
{
	expect(!shouldWriteBriefingRequestAfterExport(false, ""), "failed export must not publish request");
	expect(!shouldWriteBriefingRequestAfterExport(true, "not-a-valid-digest"),
		"export with invalid digest must not publish request");
	const std::string valid =
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	expect(shouldWriteBriefingRequestAfterExport(true, valid),
		"successful export with valid digest may publish request");
}

static void testExportedYamlByteIdentity()
{
	const std::string yaml =
		"schemaVersion: 4\nturn: 2\nside: 0\nselectedUnitId: 1\nmap:\n  width: 1\n  height: 1\n  depth: 1\nunits: []\n";
	std::string digest;
	expect(CompanionSha256::digestHexLowerUtf8(yaml, &digest), "yaml digest must succeed");
	expect(CompanionSha256::isValidHexDigest(digest), "yaml digest must be valid hex");
	expect(digest.size() == 64, "yaml digest must be 64 chars");
}

static void testDebugRejection()
{
	BriefingGateInput input = safePlayerInput();
	input.debugMode = true;
	BriefingPressState state = { false };
	const BriefingPressOutcome outcome = evaluateBriefingKeyPress(input, &state);
	expect(outcome == BRIEFING_PRESS_REJECTED, "debug/reveal mode must reject briefing");
	expect(!state.pending, "rejected briefing must not set pending");
}

static void testAlienTurnRejection()
{
	BriefingGateInput input = safePlayerInput();
	input.playerTurn = false;
	BriefingPressState state = { false };
	const BriefingPressOutcome outcome = evaluateBriefingKeyPress(input, &state);
	expect(outcome == BRIEFING_PRESS_REJECTED, "alien turn must reject briefing (no deferral)");
	expect(!state.pending, "alien-turn rejection must not set pending");
}

static void testMissingPathIgnored()
{
	BriefingGateInput input = safePlayerInput();
	input.snapshotPathConfigured = false;
	BriefingPressState state = { false };
	const BriefingPressOutcome outcome = evaluateBriefingKeyPress(input, &state);
	expect(outcome == BRIEFING_PRESS_IGNORED, "missing snapshot path must ignore briefing");
}

static void testNonBattlescapeInitializationIgnored()
{
	BriefingGateInput input = safePlayerInput();
	input.battleInitialized = false;
	BriefingPressState state = { false };
	const BriefingPressOutcome outcome = evaluateBriefingKeyPress(input, &state);
	expect(outcome == BRIEFING_PRESS_IGNORED, "uninitialized battle must ignore briefing");
}

static void testGameplayRngUntouched()
{
	RNG::setSeed(0xABCDEF1234567890ULL);
	const uint64_t before = RNG::getSeed();
	(void)makeRequestId();
	(void)makeRequestId();
	BriefingPressState state = { false };
	evaluateBriefingKeyPress(safePlayerInput(), &state);
	const uint64_t after = RNG::getSeed();
	expect(before == after, "briefing request logic must not consume gameplay RNG");
}

static void testParentDirectory()
{
	expect(parentDirectoryOfSnapshotPath("E:/Open X-Com Companion/local/m0/tactical.yaml") == "E:/Open X-Com Companion/local/m0",
		"parent directory must strip snapshot filename");
	expect(parentDirectoryOfSnapshotPath("tactical.yaml").empty(), "snapshot path without directory must fail");
}

int main()
{
	testSafePlayerFulfillNow();
	testBusyDeferral();
	testRapidPressCoalescing();
	testUniqueRequestIds();
	testRequestJsonShape();
	testSnapshotSha256KnownVector();
	testSnapshotSha256Deterministic();
	testSnapshotSha256ContentChange();
	testNoRequestWithoutSuccessfulExport();
	testExportedYamlByteIdentity();
	testDebugRejection();
	testAlienTurnRejection();
	testMissingPathIgnored();
	testNonBattlescapeInitializationIgnored();
	testGameplayRngUntouched();
	testParentDirectory();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}

	std::cout << "companion_briefing_request_test: all tests passed." << std::endl;
	return 0;
}
