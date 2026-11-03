/*
 * Open X-Com Companion — campaign identity regression tests (M1-23B).
 */
#include "Engine/CompanionCampaignIdentityCore.h"
#include <iostream>
#include <string>
#include <yaml-cpp/yaml.h>

using namespace OpenXcom;
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

static void testNewCampaignIdFormatAndUniqueness()
{
	const std::string id1 = makeCampaignId();
	const std::string id2 = makeCampaignId();
	expect(isValidCampaignIdFormat(id1), "new campaign id must be 32 lowercase hex");
	expect(isValidCampaignIdFormat(id2), "new campaign id must be 32 lowercase hex");
	expect(id1 != id2, "independent new campaigns must receive distinct ids");
}

static void testLegacySaveMigrationInMemory()
{
	YAML::Node doc;
	doc["difficulty"] = 0;
	doc["rng"] = static_cast<uint64_t>(123456789);
	std::string legacyId;
	const CampaignIdLoadOutcome outcome = loadCompanionCampaignId(doc, &legacyId);
	expect(outcome == CAMPAIGN_ID_LEGACY_GENERATED, "legacy save without companion block must generate in memory");
	expect(isValidCampaignIdFormat(legacyId), "legacy migration id must be valid");
	expect(!doc["companion"], "load must not mutate YAML document (persist only on save)");
}

static void testSaveLoadRoundTrip()
{
	YAML::Node doc;
	doc["difficulty"] = 0;
	std::string generated;
	loadCompanionCampaignId(doc, &generated);
	writeCompanionCampaignId(&doc, generated);
	std::string reloaded;
	const CampaignIdLoadOutcome outcome = loadCompanionCampaignId(doc, &reloaded);
	expect(outcome == CAMPAIGN_ID_LOADED_FROM_SAVE, "saved companion block must reload from save document");
	expect(reloaded == generated, "round trip must preserve campaign id");
}

static void testSaveAsAndRenamePreserveIdentity()
{
	YAML::Node original;
	original["difficulty"] = 0;
	std::string id;
	loadCompanionCampaignId(original, &id);
	writeCompanionCampaignId(&original, id);

	YAML::Node saveAsCopy = YAML::Clone(original);
	saveAsCopy["name"] = "Renamed Campaign Branch";
	std::string saveAsId;
	expect(loadCompanionCampaignId(saveAsCopy, &saveAsId) == CAMPAIGN_ID_LOADED_FROM_SAVE,
		"Save As copy must keep stored campaign id");
	expect(saveAsId == id, "Save As must not mint a new campaign id");
}

static void testIndependentCampaignDocuments()
{
	YAML::Node campaignA;
	YAML::Node campaignB;
	std::string idA;
	std::string idB;
	loadCompanionCampaignId(campaignA, &idA);
	loadCompanionCampaignId(campaignB, &idB);
	expect(idA != idB, "independent legacy loads must not share generated ids in one process");
}

static void testExistingSaveWithCompanionBlock()
{
	const std::string stored = "0123456789abcdef0123456789abcdef";
	YAML::Node doc;
	doc["difficulty"] = 1;
	YAML::Node companion;
	companion["campaign_id"] = stored;
	doc["companion"] = companion;
	std::string loaded;
	expect(loadCompanionCampaignId(doc, &loaded) == CAMPAIGN_ID_LOADED_FROM_SAVE,
		"existing companion.campaign_id must load unchanged");
	expect(loaded == stored, "stored campaign id must round-trip exactly");
}

static void testInvalidStoredIdTreatedAsLegacy()
{
	YAML::Node doc;
	doc["difficulty"] = 0;
	YAML::Node companion;
	companion["campaign_id"] = "not-a-valid-id";
	doc["companion"] = companion;
	std::string loaded;
	expect(loadCompanionCampaignId(doc, &loaded) == CAMPAIGN_ID_LEGACY_GENERATED,
		"invalid stored campaign id must regenerate for safety");
	expect(isValidCampaignIdFormat(loaded), "regenerated id must be valid");
}

static void testWriteRejectsInvalidId()
{
	YAML::Node doc;
	doc["difficulty"] = 0;
	writeCompanionCampaignId(&doc, "INVALID");
	expect(!doc["companion"], "invalid id must not be written to save document");
}

int main()
{
	testNewCampaignIdFormatAndUniqueness();
	testLegacySaveMigrationInMemory();
	testSaveLoadRoundTrip();
	testSaveAsAndRenamePreserveIdentity();
	testIndependentCampaignDocuments();
	testExistingSaveWithCompanionBlock();
	testInvalidStoredIdTreatedAsLegacy();
	testWriteRejectsInvalidId();

	if (g_failures == 0)
	{
		std::cout << "companion_campaign_identity_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_campaign_identity_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
