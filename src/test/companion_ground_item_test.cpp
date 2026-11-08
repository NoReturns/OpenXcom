/*
 * Open X-Com Companion — ground-item observation core tests (M1-WP9).
 */
#include "Engine/CompanionGroundItemObservationCore.h"
#include "../Mod/RuleItem.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace OpenXcom;
using namespace CompanionGroundItemObservationCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testUndiscoveredTileOmitsExport()
{
	GroundTileObservationInput input = {};
	input.bodyDiscovered = false;
	input.inLineOfSight = true;
	input.hasGroundItems = true;
	GroundTileObservationPlan plan = planTileObservation(input);
	expect(!plan.exportTile, "undiscovered tile must not export");
}

static void testHiddenOffLosOmitsExport()
{
	GroundTileObservationInput input = {};
	input.bodyDiscovered = true;
	input.inLineOfSight = false;
	input.hasGroundItems = true;
	GroundTileObservationPlan plan = planTileObservation(input);
	expect(!plan.exportTile, "off-LOS tile must not export without memory layer");
}

static void testVisibleWeaponIdentifiedWhenResearched()
{
	GroundTileObservationInput input = {};
	input.bodyDiscovered = true;
	input.inLineOfSight = true;
	input.hasGroundItems = true;
	GroundItemRulesView rifle;
	rifle.itemType = "STR_RIFLE";
	rifle.battleType = BT_FIREARM;
	rifle.weight = 30;
	rifle.researched = true;
	input.items.push_back(rifle);
	GroundTileObservationPlan plan = planTileObservation(input);
	expect(plan.exportTile, "visible ground weapon should export");
	expect(plan.items.size() == 1, "one item expected");
	expect(plan.items[0].hasItemType, "researched primary item exposes itemType");
	expect(plan.items[0].itemType == "STR_RIFLE", "itemType expected");
}

static void testUnresearchedAlienHidesItemType()
{
	GroundTileObservationInput input = {};
	input.bodyDiscovered = true;
	input.inLineOfSight = true;
	input.hasGroundItems = true;
	GroundItemRulesView alien;
	alien.itemType = "STR_ALIEN_RIFLE";
	alien.battleType = BT_FIREARM;
	alien.weight = 20;
	alien.researched = false;
	input.items.push_back(alien);
	GroundTileObservationPlan plan = planTileObservation(input);
	expect(!plan.items[0].hasItemType, "unresearched item must not expose itemType");
	expect(plan.items[0].itemCategory == std::string("unidentified_firearm"), "generic category expected");
}

static void testMultipleItemsMarksSecondaryConcealed()
{
	GroundTileObservationInput input = {};
	input.bodyDiscovered = true;
	input.inLineOfSight = true;
	input.hasGroundItems = true;
	GroundItemRulesView rifle;
	rifle.itemType = "STR_RIFLE";
	rifle.battleType = BT_FIREARM;
	rifle.weight = 30;
	rifle.researched = true;
	GroundItemRulesView clip;
	clip.itemType = "STR_RIFLE_CLIP";
	clip.battleType = BT_AMMO;
	clip.weight = 3;
	clip.ammoQuantity = 25;
	clip.researched = true;
	input.items.push_back(clip);
	input.items.push_back(rifle);
	GroundTileObservationPlan plan = planTileObservation(input);
	expect(plan.items.size() == 2, "two items on tile");
	bool sawConcealed = false;
	bool sawPrimaryType = false;
	for (size_t i = 0; i < plan.items.size(); ++i)
	{
		if (plan.items[i].concealedOnTile)
		{
			sawConcealed = true;
		}
		if (plan.items[i].hasItemType && plan.items[i].itemType == "STR_RIFLE")
		{
			sawPrimaryType = true;
		}
	}
	expect(sawConcealed, "non-primary stack item should be concealed");
	expect(sawPrimaryType, "heaviest item should remain identifiable");
}

static void testPrimedGrenadeWithoutFuseTimer()
{
	GroundTileObservationInput input = {};
	input.bodyDiscovered = true;
	input.inLineOfSight = true;
	input.hasGroundItems = true;
	GroundItemRulesView grenade;
	grenade.itemType = "STR_GRENADE";
	grenade.battleType = BT_GRENADE;
	grenade.weight = 5;
	grenade.researched = true;
	grenade.primedGrenade = true;
	input.items.push_back(grenade);
	GroundTileObservationPlan plan = planTileObservation(input);
	expect(plan.items[0].hasPrimed && plan.items[0].primed, "primed grenade may expose primed flag");
}

int main()
{
	testUndiscoveredTileOmitsExport();
	testHiddenOffLosOmitsExport();
	testVisibleWeaponIdentifiedWhenResearched();
	testUnresearchedAlienHidesItemType();
	testMultipleItemsMarksSecondaryConcealed();
	testPrimedGrenadeWithoutFuseTimer();
	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_ground_item_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
