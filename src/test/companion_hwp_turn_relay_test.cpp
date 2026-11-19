/*
 * Open X-Com Companion — HWP_TURN relay integration (M1 HWP-S4-1c).
 */
#include "Engine/CompanionCommandRuntime.h"
#include "Engine/CompanionJsonLite.h"
#include "Engine/CompanionSnapshot.h"
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "Engine/Options.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/Unit.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Base.h"
#include "../Savegame/Tile.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <yaml-cpp/yaml.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define HWP_TURN_RELAY_MKDIR(path) _mkdir(path)
#define HWP_TURN_RELAY_PUTENV(name, value) _putenv_s(name, value)
#define HWP_TURN_RELAY_SLEEP_MS(ms) Sleep(ms)
#else
#include <sys/stat.h>
#include <unistd.h>
#define HWP_TURN_RELAY_MKDIR(path) mkdir(path, 0755)
#define HWP_TURN_RELAY_PUTENV(name, value) setenv(name, value, 1)
#define HWP_TURN_RELAY_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionSoldierControlPublishCore;

static int g_failures = 0;
static const int HWP_ID = 1'000'000;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL relay: " << message << std::endl;
		++g_failures;
	}
}

static MapDataSet *hwpTurnRelayFloorDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("hwp_turn_relay_floor");
	}
	return ds;
}

static MapData *hwpTurnRelayFloorTile()
{
	MapData *floor = new MapData(hwpTurnRelayFloorDataSet());
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static void hwpTurnWireFloor(SavedBattleGame *battle, int w, int h)
{
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			Tile *tile = battle->getTile(Position(x, y, 0));
			if (tile == 0)
			{
				continue;
			}
			tile->setDiscovered(true, 2);
			tile->setMapData(hwpTurnRelayFloorTile(), 1, 0, O_FLOOR);
		}
	}
}

static std::string hwpTurnRelayJoin(const std::string &dir, const char *name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	return dir.empty() ? std::string(name) : dir + sep + name;
}

static std::string hwpTurnRelayExeDir()
{
#ifdef _WIN32
	char path[MAX_PATH];
	const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
	{
		return ".";
	}
	std::string p(path, n);
	const auto slash = p.find_last_of("\\/");
	return slash == std::string::npos ? "." : p.substr(0, slash);
#else
	return ".";
#endif
}

static void hwpTurnRelayWrite(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static std::string hwpTurnRelayRead(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

struct HwpTurnRelayHarness
{
	Mod mod;
	SavedGame campaign;
	Base *base;
	Unit *tankRules;
	Armor tankArmor;
	SavedBattleGame battle;
	BattlescapeGame game;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	std::string bindingSha256;
	int bindingStateVersion;
	bool savedStrafe;

	HwpTurnRelayHarness()
		: base(new Base(&mod)),
		  tankRules(new Unit("STR_HWP_TURN_RELAY_TANK")),
		  tankArmor("STR_HWP_TURN_RELAY_ARMOR"),
		  battle(),
		  game(&battle, 0),
		  savedStrafe(Options::strafe)
	{
		tankArmor.load(YAML::Load("type: STR_HWP_TURN_RELAY_ARMOR\nsize: 2\nmovementType: 0\n"));
		campaign.ensureCompanionCampaignId();
		campaign.getBases()->push_back(base);
		battle.setTestMod(&mod);
		battle.initMap(12, 12, 1);
		battle.initUtilities(&mod);
		hwpTurnWireFloor(&battle, 12, 12);
		battle.setGeoscapeSaveHint(&campaign);
		battle.ensureCompanionTacticalOwnershipInitialized(&campaign);
		battle.getCompanionEventLog().beginSession();
		sessionId = battle.getCompanionEventLog().getSessionId();

		relayDir = hwpTurnRelayJoin(hwpTurnRelayExeDir(), "hwp_turn_relay_e2e");
		HWP_TURN_RELAY_MKDIR(relayDir.c_str());
		snapshotPath = hwpTurnRelayJoin(relayDir, "tactical-relay-fixture.yaml");
		hwpTurnRelayWrite(snapshotPath, "tactical:\n  relay_fixture: hwp_turn_s4_1c\n");
		HWP_TURN_RELAY_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		HWP_TURN_RELAY_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");
		Options::strafe = true;
		resetRelayArtifacts();
	}

	~HwpTurnRelayHarness()
	{
		Options::strafe = savedStrafe;
	}

	void resetRelayArtifacts()
	{
		std::remove(hwpTurnRelayJoin(relayDir, "tactical-command.json").c_str());
		std::remove(hwpTurnRelayJoin(relayDir, "tactical-result.json").c_str());
		std::remove(hwpTurnRelayJoin(relayDir, "tactical-command-journal.json").c_str());
		std::remove(hwpTurnRelayJoin(relayDir, "tactical-command-conflict.json").c_str());
		std::remove(hwpTurnRelayJoin(relayDir, "companion-tactical-publication-confirm.json").c_str());
		std::remove(hwpTurnRelayJoin(relayDir, "companion-soldier-control.json").c_str());
	}

	void resetScenario()
	{
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
		if (battle.getUnits() != 0)
		{
			for (std::vector<BattleUnit *>::iterator i = battle.getUnits()->begin();
			     i != battle.getUnits()->end();
			     ++i)
			{
				delete *i;
			}
			battle.getUnits()->clear();
		}
		battle.reclaimCompanionTacticalHwpForHuman(HWP_ID);
		resetRelayArtifacts();
		bindingSha256.clear();
		bindingStateVersion = 0;
		Options::strafe = true;
	}

	void writeControlJson(bool hwpAi)
	{
		std::set<int> aiHwps;
		std::vector<int> liveHwps;
		for (std::vector<BattleUnit *>::const_iterator i = battle.getUnits()->begin();
		     i != battle.getUnits()->end(); ++i)
		{
			if (*i != 0 && (*i)->getId() == HWP_ID)
			{
				liveHwps.push_back(HWP_ID);
				break;
			}
		}
		expect(!liveHwps.empty(), "HWP must exist on map before control publish (spawn before writeControl)");
		if (hwpAi)
		{
			aiHwps.insert(HWP_ID);
			expect(battle.assignCompanionTacticalHwpToAi(HWP_ID), "tactical HWP AI assign");
		}
		else
		{
			expect(battle.reclaimCompanionTacticalHwpForHuman(HWP_ID), "tactical HWP human reclaim");
		}
		const bool tacticalAi = battle.getCompanionTacticalAiHwpIds().count(HWP_ID) != 0;
		expect(tacticalAi == hwpAi, "tactical ai HWP set matches harness intent");
		const ControlPublicationPlan plan = buildControlPublicationJson(
			sessionId,
			campaign.getCompanionCampaignId(),
			battle.getCompanionTacticalBattleId(),
			std::set<int>(),
			std::vector<int>(),
			aiHwps,
			liveHwps);
		expect(plan.ok, "control plan");
		if (plan.ok)
		{
			const std::string hwpAiMarker = "\"" + std::to_string(HWP_ID) + "\": \"ai\"";
			if (hwpAi)
			{
				expect(plan.jsonUtf8.find(hwpAiMarker) != std::string::npos,
					"companion-soldier-control includes HWP in hwp_assignments");
			}
			else
			{
				expect(plan.jsonUtf8.find(hwpAiMarker) == std::string::npos,
					"human control must not list HWP as ai");
			}
			hwpTurnRelayWrite(hwpTurnRelayJoin(relayDir, "companion-soldier-control.json"), plan.jsonUtf8);
		}
	}

	BattleUnit *addTank(const Position &pos, int tu, int turretType, int hullDir, int turretDir)
	{
		StatAdjustment adjustment;
		BattleUnit *unit = new BattleUnit(tankRules, FACTION_PLAYER, HWP_ID, &tankArmor, &adjustment, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"
		     << "direction: " << hullDir << "\ndirectionTurret: " << turretDir
		     << "\nturretType: " << turretType << "\n";
		unit->load(YAML::Load(yaml.str()));
		battle.getUnits()->push_back(unit);
		expect(battle.setUnitPosition(unit, pos), "tank placed");
		return unit;
	}

	bool exportSnapshotBinding()
	{
		bindingSha256.clear();
		if (!CompanionSnapshot::tryExport(&battle, &bindingSha256))
		{
			return false;
		}
		bindingStateVersion = battle.getCompanionEventLog().getLatestSeq();
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": " << battle.getTurn() << ",\n"
		     << "  \"state_version\": " << bindingStateVersion << ",\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"status\": \"published\"\n"
		     << "}\n";
		hwpTurnRelayWrite(hwpTurnRelayJoin(relayDir, "companion-tactical-publication-confirm.json"), json.str());
		return !bindingSha256.empty();
	}

	std::string hwpTurnCommandJson(
		const std::string &commandId,
		const char *turnPart,
		int tx,
		int ty,
		int tz) const
	{
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"HWP_TURN\",\n"
		     << "  \"actor_unit_id\": " << HWP_ID << ",\n"
		     << "  \"turn_part\": \"" << turnPart << "\",\n"
		     << "  \"target\": {\"x\": " << tx << ", \"y\": " << ty << ", \"z\": " << tz << "},\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << bindingStateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	void publishInbox(const std::string &json)
	{
		hwpTurnRelayWrite(hwpTurnRelayJoin(relayDir, "tactical-command.json"), json);
		HWP_TURN_RELAY_SLEEP_MS(320);
	}

	void tickRelay()
	{
		game.tickCompanionCommand(true);
	}

	std::string resultJson() const
	{
		return hwpTurnRelayRead(hwpTurnRelayJoin(relayDir, "tactical-result.json"));
	}

	bool statusIs(const char *expected) const
	{
		std::string status;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "status", &status) && status == expected;
	}
};

static HwpTurnRelayHarness &harness()
{
	static HwpTurnRelayHarness *h = new HwpTurnRelayHarness();
	return *h;
}

static void testBodyTurnCannon()
{
	HwpTurnRelayHarness &h = harness();
	h.resetScenario();
	const Position pos(3, 3, 0);
	BattleUnit *tank = h.addTank(pos, 80, 0, 0, 0);
	h.writeControlJson(true);
	expect(h.exportSnapshotBinding(), "snapshot");
	h.publishInbox(h.hwpTurnCommandJson("hwp-turn-body-cannon", "body", 5, 3, 0));
	h.tickRelay();
	expect(h.statusIs("COMPLETED"), "body turn COMPLETED");
	int finalDir = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_direction", &finalDir), "final_direction");
	expect(finalDir == tank->getDirection(), "final_direction matches unit");
	expect(tank->getPosition() == pos, "position unchanged");
	expect(tank->getTurretDirection() == tank->getDirection(), "turret coupled");
}

static void testTurretTurnLaser()
{
	HwpTurnRelayHarness &h = harness();
	h.resetScenario();
	const Position pos(5, 5, 0);
	const int hullBefore = 4;
	BattleUnit *tank = h.addTank(pos, 80, 2, hullBefore, 0);
	h.writeControlJson(true);
	expect(h.exportSnapshotBinding(), "snapshot");
	h.publishInbox(h.hwpTurnCommandJson("hwp-turn-turret-laser", "turret", 7, 5, 0));
	h.tickRelay();
	expect(h.statusIs("COMPLETED"), "turret turn COMPLETED");
	expect(tank->getPosition() == pos, "position unchanged");
	expect(tank->getDirection() == hullBefore, "hull unchanged");
	int turretDir = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_turret_direction", &turretDir),
		"final_turret_direction");
	expect(turretDir == tank->getTurretDirection(), "turret direction reported");
}

static void testInsufficientTuReject()
{
	HwpTurnRelayHarness &h = harness();
	h.resetScenario();
	const Position pos(1, 1, 0);
	BattleUnit *tank = h.addTank(pos, 1, 0, 0, 0);
	h.writeControlJson(true);
	const int tuBefore = tank->getTimeUnits();
	const int hullBefore = tank->getDirection();
	expect(h.exportSnapshotBinding(), "snapshot");
	h.publishInbox(h.hwpTurnCommandJson("hwp-turn-insufficient", "body", 5, 1, 0));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "insufficient TU rejected");
	expect(tank->getTimeUnits() == tuBefore, "TU unchanged");
	expect(tank->getDirection() == hullBefore, "hull unchanged");
}

static void testStrafeDisabledReject()
{
	HwpTurnRelayHarness &h = harness();
	h.resetScenario();
	Options::strafe = false;
	h.addTank(Position(2, 2, 0), 80, 0, 4, 0);
	h.writeControlJson(true);
	expect(h.exportSnapshotBinding(), "snapshot");
	h.publishInbox(h.hwpTurnCommandJson("hwp-turn-strafe-off", "turret", 4, 2, 0));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "strafe disabled rejected");
	std::string rejectCode;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &rejectCode), "reject_code");
	expect(rejectCode == "STRAFE_DISABLED", "STRAFE_DISABLED");
}

static void testHumanHwpReject()
{
	HwpTurnRelayHarness &h = harness();
	h.resetScenario();
	h.addTank(Position(3, 3, 0), 80, 0, 0, 0);
	h.writeControlJson(false);
	expect(h.exportSnapshotBinding(), "snapshot");
	h.publishInbox(h.hwpTurnCommandJson("hwp-turn-human", "body", 5, 3, 0));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "human HWP rejected");
	std::string rejectCode;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &rejectCode), "reject_code");
	expect(rejectCode == "NOT_AI_CONTROLLED", "human HWP NOT_AI_CONTROLLED");
}

int main()
{
	testBodyTurnCannon();
	testTurretTurnLaser();
	testInsufficientTuReject();
	testStrafeDisabledReject();
	testHumanHwpReject();
	if (g_failures == 0)
	{
		std::cout << "companion_hwp_turn_relay_test: PASS" << std::endl;
		return 0;
	}
	std::cerr << "companion_hwp_turn_relay_test: FAIL (" << g_failures << ")" << std::endl;
	return 1;
}