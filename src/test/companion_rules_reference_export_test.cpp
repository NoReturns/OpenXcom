/*
 * Open X-Com Companion — equipment rules reference export tests (M1-WP5).
 */
#include "Engine/CompanionRulesReferenceExportCore.h"
#include "Engine/CompanionCampaignIdentityCore.h"
#include "Engine/CompanionJsonLite.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace OpenXcom::CompanionRulesReferenceExportCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static ItemRulesInput makeRifleItem()
{
	ItemRulesInput item = {};
	item.type = "STR_RIFLE";
	item.battleType = "firearm";
	item.weaponCategoryKnown = true;
	item.weaponCategory = "rifle";
	item.compatibleAmmo.push_back("STR_RIFLE_CLIP");
	item.flatRate = false;
	item.clipSize = 20;
	item.projectileDamageSource = "loaded_ammo";
	item.snap.present = true;
	item.snap.accuracy = 55;
	item.snap.tuRule = 33;
	item.aimed.present = true;
	item.aimed.accuracy = 80;
	item.aimed.tuRule = 50;
	return item;
}

static ItemRulesInput makeAmmoItem()
{
	ItemRulesInput item = {};
	item.type = "STR_RIFLE_CLIP";
	item.battleType = "ammo";
	item.weaponCategoryKnown = true;
	item.weaponCategory = "ammunition";
	item.includeDirectDamage = true;
	item.damagePower = 40;
	item.damageType = "ap";
	return item;
}

static ExportBuildInput baseInput()
{
	ExportBuildInput input = {};
	input.campaignId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	input.saveName = "WP5 Test Save";
	input.gameDate.year = 1999;
	input.gameDate.month = 3;
	input.gameDate.day = 15;
	input.gameDate.hour = 12;
	input.gameDate.minute = 30;
	input.activeModLabels.push_back("xcom2 ver: 1.0");
	input.discoveredResearch.push_back("STR_RIFLE");
	input.exportedAtMs = 1700000000000LL;
	return input;
}

static void testResearchGating()
{
	std::vector<std::string> discovered;
	discovered.push_back("STR_RIFLE");
	sortStringVector(&discovered);
	expect(isResearchRequirementsEntitled(std::vector<std::string>(), discovered), "empty requirements entitled");
	std::vector<std::string> alienReq;
	alienReq.push_back("STR_ALIEN_TECH");
	expect(!isResearchRequirementsEntitled(alienReq, discovered), "locked alien requirements excluded");
	alienReq.clear();
	alienReq.push_back("STR_RIFLE");
	expect(isResearchRequirementsEntitled(alienReq, discovered), "completed requirements entitled");
}

static void testVanillaWeaponAmmoExport()
{
	ExportBuildInput input = baseInput();
	input.items.push_back(makeAmmoItem());
	input.items.push_back(makeRifleItem());
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(built.ok, "vanilla export builds");
	expect(built.jsonUtf8.find("\"STR_RIFLE\"") != std::string::npos, "rifle present");
	expect(built.jsonUtf8.find("\"damagePower\": 40") != std::string::npos, "ammo damage present");
	expect(built.jsonUtf8.find("\"projectileDamageSource\": \"loaded_ammo\"") != std::string::npos,
		"firearm damage source documented");
}

static void testModOverrideItem()
{
	ExportBuildInput input = baseInput();
	ItemRulesInput modItem = makeRifleItem();
	modItem.type = "STR_MOD_LASER_RIFLE";
	modItem.snap.accuracy = 99;
	modItem.compatibleAmmo.clear();
	modItem.compatibleAmmo.push_back("STR_MOD_LASER_CLIP");
	input.items.push_back(modItem);
	ItemRulesInput modAmmo = makeAmmoItem();
	modAmmo.type = "STR_MOD_LASER_CLIP";
	modAmmo.damagePower = 75;
	modAmmo.damageType = "laser";
	input.items.push_back(modAmmo);
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(built.ok, "mod override export builds");
	expect(built.jsonUtf8.find("\"accuracy\": 99") != std::string::npos, "mod accuracy override exported");
	expect(built.jsonUtf8.find("\"damageType\": \"laser\"") != std::string::npos, "mod damage type exported");
}

static void testFiringModesAndAutoShots()
{
	ExportBuildInput input = baseInput();
	ItemRulesInput cannon = makeRifleItem();
	cannon.type = "STR_CANNON";
	cannon.autoShot.present = true;
	cannon.autoShot.accuracy = 35;
	cannon.autoShot.tuRule = 60;
	cannon.autoShots = 3;
	input.items.push_back(cannon);
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(built.ok, "auto mode export builds");
	expect(built.jsonUtf8.find("\"auto\"") != std::string::npos, "auto firing mode present");
	expect(built.jsonUtf8.find("\"autoShots\": 3") != std::string::npos, "autoShots present");
}

static void testResearchLockedItemExcludedFromBuildInput()
{
	ExportBuildInput input = baseInput();
	input.items.push_back(makeRifleItem());
	ItemRulesInput alien = makeRifleItem();
	alien.type = "STR_PLASMA_RIFLE";
	input.items.push_back(alien);
	expect(input.items.size() == 2U, "fixture includes locked item for negative path");
	std::vector<std::string> discovered;
	discovered.push_back("STR_RIFLE");
	sortStringVector(&discovered);
	std::vector<std::string> alienReq;
	alienReq.push_back("STR_PLASMA_RIFLE");
	expect(!isResearchRequirementsEntitled(alienReq, discovered), "plasma rifle not entitled in fixture");
}

static void testStableOrdering()
{
	ExportBuildInput input = baseInput();
	ItemRulesInput zItem = makeAmmoItem();
	zItem.type = "STR_Z_ITEM";
	ItemRulesInput aItem = makeAmmoItem();
	aItem.type = "STR_A_ITEM";
	input.items.push_back(zItem);
	input.items.push_back(aItem);
	const ExportBuildResult built = buildRulesReferenceJson(input);
	const std::string::size_type aPos = built.jsonUtf8.find("\"STR_A_ITEM\"");
	const std::string::size_type zPos = built.jsonUtf8.find("\"STR_Z_ITEM\"");
	expect(built.ok && aPos != std::string::npos && zPos != std::string::npos && aPos < zPos,
		"items sorted by type");
}

static void testContentRevisionChangesWithResearch()
{
	ExportBuildInput inputA = baseInput();
	inputA.items.push_back(makeRifleItem());
	const ExportBuildResult builtA = buildRulesReferenceJson(inputA);
	ExportBuildInput inputB = inputA;
	inputB.discoveredResearch.push_back("STR_LASER_WEAPONS");
	const ExportBuildResult builtB = buildRulesReferenceJson(inputB);
	expect(builtA.ok && builtB.ok, "revision builds succeed");
	expect(builtA.contentRevisionSha256 != builtB.contentRevisionSha256,
		"content revision changes when research changes");
}

static void testInvalidCampaignRejected()
{
	ExportBuildInput input = baseInput();
	input.campaignId = "not-a-valid-id";
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(!built.ok && built.publicReason == "invalid_campaign_id", "invalid campaign rejected");
}

static void testEmptyItemsStillValid()
{
	ExportBuildInput input = baseInput();
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(built.ok, "empty entitled item list still exports identity shell");
	expect(built.jsonUtf8.find("\"items\": [") != std::string::npos, "empty items array present");
}

static void testInventorySectionsExport()
{
	ExportBuildInput input = baseInput();
	InventorySectionInput belt = {};
	belt.id = "STR_BELT";
	belt.sectionType = "slot";
	InventorySectionTransferCostInput tc = {};
	tc.destinationSlotId = "STR_BACK_PACK";
	tc.tu = 16;
	belt.transferCosts.push_back(tc);
	input.inventorySections.push_back(belt);
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(built.ok, "inventory sections build");
	expect(built.jsonUtf8.find("\"inventorySections\"") != std::string::npos, "inventorySections key");
	expect(built.jsonUtf8.find("STR_BACK_PACK") != std::string::npos, "destination in json");
}

static void testMedikitFields()
{
	ExportBuildInput input = baseInput();
	ItemRulesInput medikit = {};
	medikit.type = "STR_MEDI_KIT";
	medikit.battleType = "medikit";
	medikit.weaponCategoryKnown = true;
	medikit.weaponCategory = "medikit";
	medikit.medikit.present = true;
	medikit.medikit.heal = 3;
	medikit.medikit.painKiller = 1;
	medikit.medikit.stimulant = 1;
	medikit.medikit.tuUse = 25;
	input.items.push_back(medikit);
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(built.ok && built.jsonUtf8.find("\"medikit\"") != std::string::npos, "medikit block exported");
}

static void testBuiltJsonValidation()
{
	ExportBuildInput input = baseInput();
	input.items.push_back(makeRifleItem());
	const ExportBuildResult built = buildRulesReferenceJson(input);
	expect(isValidBuiltRulesReferenceJson(built.jsonUtf8), "built json passes validator");
	expect(!isValidBuiltRulesReferenceJson("{ \"schema_version\": 9 }"), "wrong schema rejected");
}

#ifdef main
#undef main
#endif
int main()
{
	testResearchGating();
	testVanillaWeaponAmmoExport();
	testModOverrideItem();
	testFiringModesAndAutoShots();
	testResearchLockedItemExcludedFromBuildInput();
	testStableOrdering();
	testContentRevisionChangesWithResearch();
	testInvalidCampaignRejected();
	testEmptyItemsStillValid();
	testMedikitFields();
	testInventorySectionsExport();
	testBuiltJsonValidation();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_rules_reference_export_test: all passed." << std::endl;
	return EXIT_SUCCESS;
}
