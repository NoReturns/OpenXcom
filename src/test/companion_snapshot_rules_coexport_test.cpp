/*
 * ASTER-M1-RULES-REFERENCE-AUTO — F6 snapshot co-exports campaign-matched rules reference.
 */
#include "Engine/CompanionSnapshot.h"
#include "Engine/CompanionRulesReferenceExport.h"
#include "../Battlescape/Position.h"
#include "../Mod/Mod.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define COEXPORT_MKDIR(P) _mkdir(P)
#define COEXPORT_PUTENV(N, V) _putenv_s(N, V)
#else
#include <sys/stat.h>
#include <unistd.h>
#define COEXPORT_MKDIR(P) mkdir(P, 0755)
#define COEXPORT_PUTENV(N, V) setenv(N, V, 1)
#endif

using namespace OpenXcom;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static RuleInventory *makeHandInventory(Mod &mod, const char *id)
{
	RuleInventory *inv = new RuleInventory(id);
	YAML::Node rh;
	rh["id"] = id;
	rh["x"] = 0;
	rh["y"] = 0;
	rh["type"] = static_cast<int>(INV_HAND);
	mod.registerTestInventoryRule(inv);
	inv->load(rh, 0);
	return inv;
}

static RuleInventory *registerGroundInventory(Mod &mod)
{
	RuleInventory *existing = mod.getInventory("STR_GROUND", false);
	if (existing != 0)
	{
		return existing;
	}
	RuleInventory *ground = new RuleInventory("STR_GROUND");
	YAML::Node g;
	g["id"] = "STR_GROUND";
	g["x"] = 0;
	g["y"] = 0;
	g["type"] = static_cast<int>(INV_GROUND);
	mod.registerTestInventoryRule(ground);
	ground->load(g, 0);
	return ground;
}

static MapDataSet *floorMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("coexport_rules_floor");
	}
	return ds;
}

static MapData *makeFloorMapData()
{
	MapData *floor = new MapData(floorMapDataSet());
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static void wireOpenFloor(SavedBattleGame *battle, int x0, int y0, int x1, int y1, int z)
{
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			Tile *tile = battle->getTile(Position(x, y, z));
			if (tile == 0)
			{
				continue;
			}
			tile->setDiscovered(true, 2);
			tile->setMapData(makeFloorMapData(), 1, 0, O_FLOOR);
		}
	}
}

static void wireUnitTile(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile exists for unit");
	if (tile == 0)
	{
		return;
	}
	unit->setPosition(pos);
	unit->setTile(tile, battle->getTile(Position(pos.x, pos.y, pos.z - 1)));
	tile->setUnit(unit);
}

struct SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	SoldierFixture(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

static std::string readFileUtf8(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

static void testManualRulesExportPreservesCtrlShiftR()
{
	Mod mod;
	std::ostringstream dir;
	dir << "coexport_manual_" << static_cast<unsigned long long>(std::time(0));
	const std::string root = dir.str();
	expect(COEXPORT_MKDIR(root.c_str()) == 0, "manual export temp dir created");
	const std::string snapPath = root + "/tactical-live.yaml";
	COEXPORT_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapPath.c_str());
	SavedGame campaign;
	campaign.ensureCompanionCampaignId();
	expect(CompanionRulesReferenceExport::tryExport(&campaign, &mod), "manual Ctrl+Shift+R tryExport");
	const std::string rulesPath = root + "/companion-rules-reference.json";
	std::ifstream rulesIn(rulesPath.c_str());
	expect(rulesIn.good(), "manual export writes companion-rules-reference.json");
}

static void testSnapshotCoexportsRulesReference()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	makeHandInventory(mod, "STR_LEFT_HAND");
	registerGroundInventory(mod);

	std::ostringstream dir;
	dir << "coexport_rules_" << static_cast<unsigned long long>(std::time(0));
	const std::string root = dir.str();
	expect(COEXPORT_MKDIR(root.c_str()) == 0, "temp dir created");
	const std::string snapPath = root + "/tactical-live.yaml";
	COEXPORT_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapPath.c_str());

	SavedGame campaign;
	campaign.ensureCompanionCampaignId();
	const std::string campaignId = campaign.getCompanionCampaignId();
	Base *base = new Base(&mod);
	campaign.getBases()->push_back(base);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);
	battle.setGeoscapeSaveHint(&campaign);
	battle.ensureCompanionTacticalOwnershipInitialized(&campaign);
	battle.getCompanionEventLog().beginSession();
	battle.assignCompanionTacticalSoldierToAi(1);

	wireOpenFloor(&battle, 0, 5, 0, 5, 0);

	SoldierFixture fixture(1);
	base->getSoldiers()->push_back(&fixture.soldier);
	BattleUnit *unit = new BattleUnit(&fixture.soldier, 0);
	battle.getUnits()->push_back(unit);
	wireUnitTile(&battle, unit, Position(1, 1, 0));
	unit->setTimeUnits(50);
	battle.setSelectedUnit(unit);

	std::string digest;
	expect(CompanionSnapshot::tryExport(&battle, &digest, &mod), "snapshot tryExport with mod");
	expect(!digest.empty(), "snapshot digest produced");

	const std::string rulesPath = root + "/companion-rules-reference.json";
	std::ifstream rulesIn(rulesPath.c_str());
	expect(rulesIn.good(), "companion-rules-reference.json written beside snapshot");

	const std::string rulesJson = readFileUtf8(rulesPath);
	expect(rulesJson.find("companion-rules-reference") != std::string::npos, "rules artifact id present");
	expect(rulesJson.find(campaignId) != std::string::npos, "rules reference campaign_id matches campaign");

	if (g_failures == 0)
	{
		std::cout << "companion_snapshot_rules_coexport_test: all passed." << std::endl;
		std::_Exit(EXIT_SUCCESS);
	}
}

#ifdef main
#undef main
#endif
int main()
{
	testManualRulesExportPreservesCtrlShiftR();
	testSnapshotCoexportsRulesReference();
	std::cerr << g_failures << " test(s) failed." << std::endl;
	return EXIT_FAILURE;
}