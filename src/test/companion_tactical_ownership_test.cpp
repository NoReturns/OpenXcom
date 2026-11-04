/*
 * Open X-Com Companion — tactical ownership persistence tests (M1-25B).
 */
#include "Engine/CompanionTacticalOwnershipCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "Engine/CompanionTacticalAuthorityCore.h"
#include "Engine/CompanionCampaignIdentityCore.h"
#include <iostream>
#include <set>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionTacticalOwnershipCore;
using namespace OpenXcom::CompanionSoldierOwnershipCore;
using namespace OpenXcom::CompanionTacticalAuthorityCore;
using namespace OpenXcom::CompanionCampaignIdentityCore;

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
static const char *TACTICAL = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static void testBattleSaveRoundTrip()
{
	std::set<int> ai;
	ai.insert(3);
	ai.insert(7);
	YAML::Node battle;
	writeToBattleSaveNode(&battle, TACTICAL, ai);
	std::string loadedId;
	std::set<int> loadedAi;
	loadFromBattleSaveNode(battle, &loadedId, &loadedAi);
	expect(loadedId == TACTICAL, "tactical_battle_id round trip");
	expect(loadedAi.count(3) == 1 && loadedAi.count(7) == 1, "assignments round trip");
}

static void testLegacyBattleNodeDefaultsHuman()
{
	YAML::Node battle;
	battle["turn"] = 1;
	std::string loadedId;
	std::set<int> loadedAi;
	loadFromBattleSaveNode(battle, &loadedId, &loadedAi);
	expect(loadedId.empty(), "legacy battle has no tactical id");
	expect(loadedAi.empty(), "legacy battle has no tactical assignments");
}

static void testSeedFromCampaignDoesNotInheritStaleTactical()
{
	std::set<int> campaign;
	campaign.insert(2);
	std::set<int> priorTactical;
	priorTactical.insert(99);
	std::set<int> seeded;
	seedTacticalAiFromCampaign(campaign, &seeded);
	expect(seeded.count(2) == 1, "seed copies campaign ai");
	expect(seeded.count(99) == 0, "seed does not retain unrelated tactical ids");
}

static RelayControlBindingContext durableBinding()
{
	RelayControlBindingContext ctx = {};
	ctx.battleSessionId = SESSION;
	ctx.campaignId = CAMPAIGN;
	ctx.tacticalBattleId = TACTICAL;
	return ctx;
}

static void testDurableBindingSurvivesSessionChange()
{
	const std::string control =
		"{\n  \"schema_version\": 1,\n  \"session_id\": \"00000000000000000000000000000000\",\n"
		"  \"campaign_id\": \"" + std::string(CAMPAIGN) + "\",\n"
		"  \"tactical_battle_id\": \"" + std::string(TACTICAL) + "\",\n"
		"  \"assignments\": { \"3\": \"ai\" }\n}\n";
	RelayControlBindingContext ctx = durableBinding();
	ctx.battleSessionId = SESSION;
	expect(isAiControlledUnit(control, ctx, 3), "durable ids authorize ai despite stale session field");
}

static void testLegacySessionBindingStillWorks()
{
	const std::string control =
		"{\n  \"schema_version\": 1,\n  \"session_id\": \"" + std::string(SESSION) + "\",\n"
		"  \"assignments\": { \"3\": \"ai\" }\n}\n";
	RelayControlBindingContext ctx = {};
	ctx.battleSessionId = SESSION;
	expect(isAiControlledUnit(control, ctx, 3), "legacy session-bound control still authorizes");
}

static void testStaleExternalJsonTriggersHumanOverridePath()
{
	std::set<int> tacticalHuman;
	const std::string control =
		"{\n  \"schema_version\": 1,\n  \"session_id\": \"" + std::string(SESSION) + "\",\n"
		"  \"campaign_id\": \"" + std::string(CAMPAIGN) + "\",\n"
		"  \"tactical_battle_id\": \"" + std::string(TACTICAL) + "\",\n"
		"  \"assignments\": { \"3\": \"ai\" }\n}\n";
	const TacticalAuthorityInput input = buildTacticalAuthorityInput(
		true, tacticalHuman, true, 3, control, SESSION, CAMPAIGN, TACTICAL);
	expect(isStaleRelayAiAfterCampaignHumanAssignment(input, control, SESSION),
		"stale external ai json after tactical reclaim uses HUMAN_OVERRIDE gate");
}

static void testTacticalAiPermitsExecutorWhenRelayMatches()
{
	std::set<int> tacticalAi;
	tacticalAi.insert(3);
	const std::string control =
		"{\n  \"schema_version\": 1,\n  \"session_id\": \"" + std::string(SESSION) + "\",\n"
		"  \"campaign_id\": \"" + std::string(CAMPAIGN) + "\",\n"
		"  \"tactical_battle_id\": \"" + std::string(TACTICAL) + "\",\n"
		"  \"assignments\": { \"3\": \"ai\" }\n}\n";
	const TacticalAuthorityDecision d = evaluateTacticalAuthority(buildTacticalAuthorityInput(
		true, tacticalAi, true, 3, control, SESSION, CAMPAIGN, TACTICAL));
	expect(d.aiCommandPermitted, "tactical ai + published relay permits ai commands");
	expect(!d.humanOrderPermitted, "tactical ai denies human orders");
}

int main()
{
	testBattleSaveRoundTrip();
	testLegacyBattleNodeDefaultsHuman();
	testSeedFromCampaignDoesNotInheritStaleTactical();
	testDurableBindingSurvivesSessionChange();
	testLegacySessionBindingStillWorks();
	testStaleExternalJsonTriggersHumanOverridePath();
	testTacticalAiPermitsExecutorWhenRelayMatches();

	if (g_failures == 0)
	{
		std::cout << "companion_tactical_ownership_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_tactical_ownership_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
