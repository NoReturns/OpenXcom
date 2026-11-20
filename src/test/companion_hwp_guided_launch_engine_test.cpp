/*
 * M1 HWP-S6 — HWP_GUIDED_LAUNCH engine integration (fusion hovertank ruleset shape).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Engine/CompanionCommandCore.h"
#include "../Engine/CompanionHwpGuidedLaunchExecutionCore.h"
#include "../Engine/CompanionHwpFireExecutionCore.h"
#include "../Engine/CompanionTacticalHwpAuthorityCore.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace CompanionCommandCore;
using namespace CompanionHwpGuidedLaunchExecutionCore;
using namespace CompanionTacticalHwpAuthorityCore;

namespace
{

int g_failures = 0;

void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static MapDataSet *harnessMapDataSet()
{
	static MapDataSet *ds = nullptr;
	if (ds == nullptr)
	{
		ds = new MapDataSet("hwp_guided_harness");
	}
	return ds;
}

static MapData *makeFloorTile()
{
	MapData *floor = new MapData(harnessMapDataSet());
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static void wireFloor(SavedBattleGame *battle, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	tile->setMapData(makeFloorTile(), 1, 0, O_FLOOR);
}

struct Harness
{
	Mod mod;
	RuleInventory rightHand;
	RuleItem launcher;
	RuleItem fusionAmmo;
	Armor tankArmor;
	SavedBattleGame battle;
	BattlescapeGame *game;

	Harness()
		: rightHand("STR_RIGHT_HAND"),
		  launcher("STR_HWP_GUIDED_TEST_LAUNCHER"),
		  fusionAmmo("STR_HWP_GUIDED_TEST_FUSION"),
		  tankArmor("STR_HWP_GUIDED_TEST_ARMOR")
	{
		YAML::Node rh;
		rh["id"] = "STR_RIGHT_HAND";
		rh["x"] = 0;
		rh["y"] = 0;
		rh["type"] = static_cast<int>(INV_HAND);
		rightHand.load(rh, 0);

		YAML::Node ammoNode;
		ammoNode["weight"] = 0;
		ammoNode["battleType"] = static_cast<int>(BT_AMMO);
		ammoNode["clipSize"] = 8;
		ammoNode["power"] = 140;
		fusionAmmo.load(ammoNode, &mod, 0);

		YAML::Node launcherNode;
		launcherNode["weight"] = 0;
		launcherNode["battleType"] = static_cast<int>(BT_FIREARM);
		launcherNode["clipSize"] = 0;
		launcherNode["fixedWeapon"] = true;
		launcherNode["waypoints"] = 9;
		launcherNode["compatibleAmmo"] = YAML::Load("[STR_HWP_GUIDED_TEST_FUSION]");
		launcherNode["tuAimed"] = 66;
		launcherNode["flatRate"] = true;
		launcher.load(launcherNode, &mod, 0);

		tankArmor.load(YAML::Load("type: STR_HWP_GUIDED_TEST_ARMOR\nsize: 2\nmovementType: 1\n"));
		battle.setTestMod(&mod);
		battle.initMap(16, 16, 1);
		battle.initUtilities(&mod);
		battle.setTUReserved(BA_NONE);
		for (int y = 0; y < 16; ++y)
		{
			for (int x = 0; x < 16; ++x)
			{
				wireFloor(&battle, Position(x, y, 0));
			}
		}
		game = new BattlescapeGame(&battle, 0);
	}

	~Harness()
	{
		delete game;
	}

	BattleUnit *addHover(int id, const Position &pos, int tu)
	{
		Unit rules("STR_HWP_GUIDED_TEST_HOVER");
		StatAdjustment adjustment;
		BattleUnit *unit = new BattleUnit(&rules, FACTION_PLAYER, id, &tankArmor, &adjustment, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\ntu: " << tu
		     << "\nhealth: 110\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"
		     << "direction: 4\ndirectionTurret: 6\nturretType: 4\n";
		unit->load(YAML::Load(yaml.str()));
		battle.getUnits()->push_back(unit);
		expect(battle.setUnitPosition(unit, pos), "place HWP");
		return unit;
	}

	BattleItem *mountFusion(BattleUnit *tank, int rounds = 8)
	{
		BattleItem *launcherItem = new BattleItem(&launcher, battle.getCurrentItemId());
		BattleItem *clip = new BattleItem(&fusionAmmo, battle.getCurrentItemId());
		clip->setAmmoQuantity(rounds);
		expect(launcherItem->setAmmoItem(clip) == 0, "load fusion");
		launcherItem->setSlot(&rightHand);
		launcherItem->setSlotX(0);
		launcherItem->setSlotY(0);
		launcherItem->moveToOwner(tank);
		battle.getItems()->push_back(launcherItem);
		battle.getItems()->push_back(clip);
		return launcherItem;
	}
};

static void testParseHwpGuidedLaunchJson()
{
	const std::string snap =
		"{\"schema_version\":1,\"command_id\":\"guided-1\",\"command_type\":\"HWP_GUIDED_LAUNCH\","
		"\"actor_unit_id\":1000003,\"weapon_battle_item_id\":8,"
		"\"waypoints\":[{\"x\":25,\"y\":20,\"z\":0},{\"x\":30,\"y\":20,\"z\":0}],"
		"\"snapshot_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		"\"session_id\":\"a1b2c3d4e5f6789012345678abcdef01\",\"turn\":1,\"state_version\":0}";
	HwpGuidedLaunchCommandParsed parsed = {};
	expect(parseHwpGuidedLaunchCommandJson(snap, &parsed), "parse");
	expect(parsed.waypoints.size() == 2, "two waypoints");
}

static void testFusionGuidedLaunchPreflight()
{
	Harness h;
	BattleUnit *tank = h.addHover(1'000'003, Position(5, 5, 0), 120);
	BattleItem *weapon = h.mountFusion(tank, 8);
	std::vector<Position> wps;
	wps.push_back(Position(10, 5, 0));
	wps.push_back(Position(12, 6, 0));
	std::string reject;
	expect(fixedWeaponSupportsGuidedLaunch(weapon), "fusion launcher supports guided");
	expect(guidedLaunchWaypointLimit(weapon) == 9, "nine waypoint cap");
	expect(preflightHwpGuidedLaunch(h.game, tank, wps, weapon->getId(), &reject), reject.c_str());
	expect(weapon->getAmmoItem()->getAmmoQuantity() == 8, "preflight does not spend ammo");
	expect(tank->getTimeUnits() == 120, "preflight does not spend TU");
}

} // namespace

int main()
{
	testParseHwpGuidedLaunchJson();
	testFusionGuidedLaunchPreflight();
	expect(hwpGuidedLaunchRelayExecutionSupported(), "relay gate enabled");
	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "PASS" << std::endl;
	return 0;
}