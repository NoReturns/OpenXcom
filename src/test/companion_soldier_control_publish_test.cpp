/*
 * Open X-Com Companion — soldier control publication tests (M1-23F3).
 */
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "Engine/CompanionSnapshotExportCore.h"
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionSoldierControlPublishCore;
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

static const char *SESSION_A = "a1b2c3d4e5f6789012345678abcdef01";
static const char *SESSION_B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char *CAMPAIGN_A = "0123456789abcdef0123456789abcdef";
static const char *TACTICAL_A = "fedcba9876543210fedcba9876543210";
static const char *TACTICAL_B = "11111111111111111111111111111111";

static ControlPublicationPlan planFor(
	const std::set<int> &aiIds,
	const std::vector<int> &units,
	const char *sessionId,
	const char *campaignId = CAMPAIGN_A,
	const char *tacticalBattleId = TACTICAL_A)
{
	return buildControlPublicationJson(sessionId, campaignId, tacticalBattleId, aiIds, units);
}

static CompanionSoldierOwnershipCore::RelayControlBindingContext bindingFor(
	const char *sessionId,
	const char *campaignId = CAMPAIGN_A,
	const char *tacticalBattleId = TACTICAL_A)
{
	CompanionSoldierOwnershipCore::RelayControlBindingContext ctx = {};
	ctx.battleSessionId = sessionId;
	ctx.campaignId = campaignId;
	ctx.tacticalBattleId = tacticalBattleId;
	return ctx;
}

static void testCampaignAiPublishesAiEntry()
{
	std::set<int> ai;
	ai.insert(3);
	const ControlPublicationPlan plan = planFor(ai, std::vector<int>(1, 3), SESSION_A);
	expect(plan.ok, "plan ok");
	expect(isAiControlledUnit(plan.jsonUtf8, bindingFor(SESSION_A), 3), "campaign AI publishes ai entry");
	expect(!isAiControlledUnit(plan.jsonUtf8, bindingFor(SESSION_A), 4), "other units remain human");
}

static void testCampaignHumanOmitsAiAuthorization()
{
	std::set<int> ai;
	const ControlPublicationPlan plan = planFor(ai, std::vector<int>(1, 2), SESSION_A);
	expect(plan.ok, "plan ok");
	SoldierControlDocument doc = {};
	expect(parseSoldierControlDocument(plan.jsonUtf8, &doc), "json parses");
	expect(doc.assignments.empty(), "human campaign publishes no ai assignments");
}

static void testUnsavedAssignmentIncluded()
{
	std::set<int> currentAi;
	currentAi.insert(7);
	const ControlPublicationPlan plan = planFor(currentAi, std::vector<int>(1, 7), SESSION_A);
	expect(isAiControlledUnit(plan.jsonUtf8, bindingFor(SESSION_A), 7), "unsaved current AI set publishes ai");
}

static void testReclaimedSoldierNotAuthorized()
{
	std::set<int> reclaimed;
	const ControlPublicationPlan plan = planFor(reclaimed, std::vector<int>(1, 5), SESSION_A);
	expect(!isAiControlledUnit(plan.jsonUtf8, bindingFor(SESSION_A), 5), "reclaimed soldier not ai in publication");
}

static void testInvalidSoldierExcludedFromPublicationInput()
{
	std::set<int> ai;
	ai.insert(9);
	ai.insert(10);
	const std::vector<int> publishable;
	const ControlPublicationPlan plan = planFor(ai, publishable, SESSION_A);
	expect(!isAiControlledUnit(plan.jsonUtf8, bindingFor(SESSION_A), 9), "invalid unit id not published");
}

static void testSessionIdPreservedAndReplaced()
{
	std::set<int> ai;
	ai.insert(1);
	const ControlPublicationPlan planA = planFor(ai, std::vector<int>(1, 1), SESSION_A, CAMPAIGN_A, TACTICAL_A);
	const ControlPublicationPlan planB = planFor(ai, std::vector<int>(1, 1), SESSION_B, CAMPAIGN_A, TACTICAL_B);
	expect(planA.jsonUtf8.find(SESSION_A) != std::string::npos, "session A preserved");
	expect(planB.jsonUtf8.find(SESSION_B) != std::string::npos, "new session binding");
	expect(isAiControlledUnit(planA.jsonUtf8, bindingFor(SESSION_B, CAMPAIGN_A, TACTICAL_A), 1),
		"durable ownership survives runtime session rotation");
	expect(isAiControlledUnit(planA.jsonUtf8, bindingFor(SESSION_A, CAMPAIGN_A, TACTICAL_A), 1),
		"durable binding authorizes ai");
	expect(!isAiControlledUnit(planA.jsonUtf8, bindingFor(SESSION_A, CAMPAIGN_A, TACTICAL_B), 1),
		"wrong tactical battle id does not authorize");
}

static void testInvalidSessionFailsClosed()
{
	std::set<int> ai;
	ai.insert(1);
	const ControlPublicationPlan plan = planFor(ai, std::vector<int>(1, 1), "not-a-valid-session");
	expect(!plan.ok, "invalid session fails closed");
}

static void testAtomicWriteFailurePreservesPreviousFile()
{
	const char *temp = std::getenv("TEMP");
	const std::string dest = std::string(temp != 0 ? temp : ".") + "\\oxc_companion_control_pub_test.json";
	const std::string priorContent = "{\"schema_version\":1,\"session_id\":\"" + std::string(SESSION_A) + "\"}\n";
	expect(CompanionSnapshotExportCore::writeUtf8FileAtomically(dest, priorContent),
		"seed control file write succeeds");
	std::ifstream before(dest.c_str(), std::ios::binary);
	std::string prior((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());
	before.close();

	const bool failed = !CompanionSnapshotExportCore::writeUtf8FileAtomically("", "should-not-write");
	expect(failed, "empty destination path must fail");
	std::ifstream after(dest.c_str(), std::ios::binary);
	std::string current((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());
	after.close();
	expect(current == prior, "failed publication preserves previous complete file");
}

int main()
{
	testCampaignAiPublishesAiEntry();
	testCampaignHumanOmitsAiAuthorization();
	testUnsavedAssignmentIncluded();
	testReclaimedSoldierNotAuthorized();
	testInvalidSoldierExcludedFromPublicationInput();
	testSessionIdPreservedAndReplaced();
	testInvalidSessionFailsClosed();
	testAtomicWriteFailurePreservesPreviousFile();

	if (g_failures == 0)
	{
		std::cout << "companion_soldier_control_publish_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_soldier_control_publish_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
