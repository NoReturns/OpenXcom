/*
 * Open X-Com Companion — campaign ownership YAML + validation tests (M1-23C).
 */
#include "Engine/CompanionCampaignIdentityCore.h"
#include "Engine/CompanionCampaignOwnershipCore.h"
#include <iostream>
#include <set>
#include <string>
#include <yaml-cpp/yaml.h>

using namespace OpenXcom;
using namespace OpenXcom::CompanionCampaignIdentityCore;
using namespace OpenXcom::CompanionCampaignOwnershipCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testMissingCompanionBlockDefaultsHuman()
{
	std::set<int> ai;
	YAML::Node doc;
	doc["difficulty"] = 0;
	loadCompanionAssignments(doc, &ai);
	expect(ai.empty(), "missing companion block must yield no AI assignments");
}

static void testLoadAiAssignments()
{
	YAML::Node doc;
	YAML::Node companion;
	companion["campaign_id"] = "0123456789abcdef0123456789abcdef";
	YAML::Node assignments;
	assignments["2"] = "ai";
	assignments["3"] = "ai";
	assignments["4"] = "human";
	companion["assignments"] = assignments;
	doc["companion"] = companion;
	std::set<int> ai;
	loadCompanionAssignments(doc, &ai);
	expect(ai.size() == 2u, "must load ai entries only");
	expect(ai.count(2) == 1 && ai.count(3) == 1, "ai soldier ids must match");
	expect(ai.count(4) == 0, "explicit human must not enter ai set");
}

static void testInvalidModeIgnored()
{
	YAML::Node doc;
	YAML::Node companion;
	YAML::Node assignments;
	assignments["1"] = "bot";
	assignments["2"] = "ai";
	companion["assignments"] = assignments;
	doc["companion"] = companion;
	std::set<int> ai;
	loadCompanionAssignments(doc, &ai);
	expect(ai.size() == 1u && ai.count(2) == 1, "invalid mode must not grant AI");
}

static void testMalformedAssignmentsMapIgnored()
{
	YAML::Node doc;
	YAML::Node companion;
	companion["assignments"] = "not-a-map";
	doc["companion"] = companion;
	std::set<int> ai;
	loadCompanionAssignments(doc, &ai);
	expect(ai.empty(), "malformed assignments must fail closed to all human");
}

static void testWriteOmitsHumanAndEmpty()
{
	YAML::Node doc;
	doc["difficulty"] = 0;
	std::set<int> ai;
	ai.insert(3);
	ai.insert(7);
	writeCompanionAssignments(&doc, ai);
	expect(doc["companion"]["assignments"]["3"].as<std::string>() == "ai", "written ai mode");
	expect(!doc["companion"]["assignments"]["7"].IsDefined() ||
		doc["companion"]["assignments"]["7"].as<std::string>() == "ai", "second ai entry");
	expect(!doc["companion"]["assignments"]["1"], "human defaults must not be written");

	std::set<int> cleared;
	writeCompanionAssignments(&doc, cleared);
	expect(!doc["companion"]["assignments"], "empty ai set must omit assignments key");
}

static void testCampaignIdWritePreservesAssignments()
{
	YAML::Node doc;
	std::set<int> ai;
	ai.insert(5);
	writeCompanionAssignments(&doc, ai);
	writeCompanionCampaignId(&doc, "0123456789abcdef0123456789abcdef");
	expect(doc["companion"]["campaign_id"].as<std::string>().size() == 32u, "campaign id written");
	expect(doc["companion"]["assignments"]["5"].as<std::string>() == "ai",
		"campaign id write must merge without wiping assignments");
}

static void testRoundTripYaml()
{
	YAML::Node doc;
	writeCompanionCampaignId(&doc, "0123456789abcdef0123456789abcdef");
	std::set<int> written;
	written.insert(11);
	written.insert(22);
	writeCompanionAssignments(&doc, written);
	std::set<int> loaded;
	loadCompanionAssignments(doc, &loaded);
	expect(loaded == written, "yaml round trip must preserve ai ids");
}

static void testUnknownCompanionFieldsIgnoredForLoad()
{
	YAML::Node doc;
	YAML::Node companion;
	companion["future_field"] = "must-not-grant-ai";
	YAML::Node assignments;
	assignments["9"] = "ai";
	companion["assignments"] = assignments;
	doc["companion"] = companion;
	std::set<int> ai;
	loadCompanionAssignments(doc, &ai);
	expect(ai.count(9) == 1, "valid assignments still load alongside unknown fields");
}

static void testNonNumericKeySkipped()
{
	YAML::Node doc;
	YAML::Node companion;
	YAML::Node assignments;
	assignments["soldier-name"] = "ai";
	assignments["10"] = "ai";
	companion["assignments"] = assignments;
	doc["companion"] = companion;
	std::set<int> ai;
	loadCompanionAssignments(doc, &ai);
	expect(ai.size() == 1u && ai.count(10) == 1, "name keys must not grant AI");
}

int main()
{
	testMissingCompanionBlockDefaultsHuman();
	testLoadAiAssignments();
	testInvalidModeIgnored();
	testMalformedAssignmentsMapIgnored();
	testWriteOmitsHumanAndEmpty();
	testCampaignIdWritePreservesAssignments();
	testRoundTripYaml();
	testUnknownCompanionFieldsIgnoredForLoad();
	testNonNumericKeySkipped();

	if (g_failures == 0)
	{
		std::cout << "companion_campaign_ownership_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_campaign_ownership_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
