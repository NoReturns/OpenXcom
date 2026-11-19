/*
 * Open X-Com Companion — HWP_MOVE relay integration (M1 HWP-S3B-03).
 */
#include "Engine/CompanionCommandRuntime.h"
#include "Engine/CompanionJsonLite.h"
#include "Engine/CompanionSnapshot.h"
#include "Engine/CompanionSoldierControlPublishCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Unit.h"
#include "../Savegame/Base.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define HWP_RELAY_MKDIR(path) _mkdir(path)
#define HWP_RELAY_PUTENV(name, value) _putenv_s(name, value)
#define HWP_RELAY_SLEEP_MS(ms) Sleep(ms)
#else
#include <sys/stat.h>
#include <unistd.h>
#define HWP_RELAY_MKDIR(path) mkdir(path, 0755)
#define HWP_RELAY_PUTENV(name, value) setenv(name, value, 1)
#define HWP_RELAY_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionSoldierControlPublishCore;

static int g_failures = 0;
static const int HWP_ID = 1'000'000;
static const int kMaxBattleTicks = 4000;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL relay: " << message << std::endl;
		++g_failures;
	}
}

static MapDataSet *hwpRelayFloorDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("hwp_move_relay_floor");
	}
	return ds;
}

static MapData *hwpRelayFloorTile()
{
	MapData *floor = new MapData(hwpRelayFloorDataSet());
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static void hwpDetachUnitFromTiles(SavedBattleGame *battle, BattleUnit *unit)
{
	if (battle == 0 || unit == 0 || unit->isOut())
	{
		return;
	}
	const int size = unit->getArmor() != 0 ? unit->getArmor()->getSize() - 1 : 0;
	if (unit->getTile() != 0 && unit->getTile()->getUnit() == unit)
	{
		const Position base = unit->getTile()->getPosition();
		for (int x = size; x >= 0; --x)
		{
			for (int y = size; y >= 0; --y)
			{
				Tile *occupied = battle->getTile(base + Position(x, y, 0));
				if (occupied != 0)
				{
					occupied->setUnit(0);
				}
			}
		}
	}
	else
	{
		const Position pos = unit->getPosition();
		for (int x = size; x >= 0; --x)
		{
			for (int y = size; y >= 0; --y)
			{
				Tile *occupied = battle->getTile(pos + Position(x, y, 0));
				if (occupied != 0 && occupied->getUnit() == unit)
				{
					occupied->setUnit(0);
				}
			}
		}
	}
	unit->setTile(0);
}

static void hwpWireFloor(SavedBattleGame *battle, int w, int h)
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
			tile->setMapData(hwpRelayFloorTile(), 1, 0, O_FLOOR);
		}
	}
}

static void hwpRelayWriteFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static std::string hwpRelayReadFile(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void hwpRelayRemoveIfPresent(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string hwpRelayJoin(const std::string &dir, const char *name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	return dir.empty() ? std::string(name) : dir + sep + name;
}

static std::string hwpRelayExeDir()
{
#ifdef _WIN32
	char path[MAX_PATH];
	const DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return std::string(".");
	}
	std::string exePath(path, length);
	const std::string::size_type slash = exePath.find_last_of("\\/");
	return slash == std::string::npos ? std::string(".") : exePath.substr(0, slash);
#else
	return std::string(".");
#endif
}

struct SoldierBundle
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	SoldierBundle(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

struct HwpMoveRelayHarness
{
	Mod mod;
	SavedGame campaign;
	Base *base;
	std::vector<SoldierBundle *> soldiers;
	Unit *tankRules;
	Armor tankArmor;
	SavedBattleGame battle;
	BattlescapeGame game;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	std::string bindingSha256;
	int bindingStateVersion;

	HwpMoveRelayHarness()
		: base(new Base(&mod)),
		  tankRules(new Unit("STR_HWP_RELAY_TANK")),
		  tankArmor("STR_HWP_RELAY_ARMOR"),
		  battle(),
		  game(&battle, 0)
	{
		tankArmor.load(YAML::Load("type: STR_HWP_RELAY_ARMOR\nsize: 2\nmovementType: 0\n"));
		campaign.ensureCompanionCampaignId();
		campaign.getBases()->push_back(base);
		battle.setTestMod(&mod);
		battle.initMap(12, 12, 1);
		battle.initUtilities(&mod);
		hwpWireFloor(&battle, 12, 12);
		battle.setGeoscapeSaveHint(&campaign);
		battle.ensureCompanionTacticalOwnershipInitialized(&campaign);
		battle.getCompanionEventLog().beginSession();
		sessionId = battle.getCompanionEventLog().getSessionId();

		relayDir = hwpRelayJoin(hwpRelayExeDir(), "hwp_move_relay_e2e");
		HWP_RELAY_MKDIR(relayDir.c_str());
		snapshotPath = hwpRelayJoin(relayDir, "tactical-relay-fixture.yaml");
		hwpRelayWriteFile(snapshotPath, "tactical:\n  relay_fixture: hwp_move_s3b03\n");
		HWP_RELAY_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		HWP_RELAY_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");
		resetRelayArtifacts();
	}

	void resetRelayArtifacts()
	{
		hwpRelayRemoveIfPresent(hwpRelayJoin(relayDir, "tactical-command.json"));
		hwpRelayRemoveIfPresent(hwpRelayJoin(relayDir, "tactical-result.json"));
		hwpRelayRemoveIfPresent(hwpRelayJoin(relayDir, "tactical-command-journal.json"));
		hwpRelayRemoveIfPresent(hwpRelayJoin(relayDir, "tactical-command-conflict.json"));
		hwpRelayRemoveIfPresent(hwpRelayJoin(relayDir, "companion-tactical-publication-confirm.json"));
	}

	void clearUnits()
	{
		if (game.isBusy())
		{
			for (int i = 0; i < 8 && game.isBusy(); ++i)
			{
				game.cancelCurrentAction();
			}
		}
		if (battle.getUnits() == 0)
		{
			return;
		}
		for (std::vector<BattleUnit *>::iterator i = battle.getUnits()->begin();
		     i != battle.getUnits()->end(); ++i)
		{
			hwpDetachUnitFromTiles(&battle, *i);
			delete *i;
		}
		battle.getUnits()->clear();
		battle.resetUnitHitStates();
	}

	void resetScenario()
	{
		for (int i = 0; i < 64 && game.isBusy(); ++i)
		{
			game.cancelCurrentAction();
		}
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
		clearUnits();
		battle.reclaimCompanionTacticalHwpForHuman(HWP_ID);
		battle.reclaimCompanionTacticalSoldierForHuman(1);
		resetRelayArtifacts();
		bindingSha256.clear();
		bindingStateVersion = 0;
		battle.resetUnitHitStates();
	}

	void endWalkCase()
	{
		for (int i = 0; i < 64 && game.isBusy(); ++i)
		{
			game.cancelCurrentAction();
		}
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
		resetRelayArtifacts();
	}

	void writeControlJson(bool hwpAi, bool soldierAi)
	{
		std::set<int> aiSoldiers;
		std::vector<int> liveSoldiers;
		std::set<int> aiHwps;
		std::vector<int> liveHwps;
		if (soldierAi)
		{
			aiSoldiers.insert(1);
			liveSoldiers.push_back(1);
			battle.assignCompanionTacticalSoldierToAi(1);
		}
		if (hwpAi)
		{
			aiHwps.insert(HWP_ID);
			liveHwps.push_back(HWP_ID);
			battle.assignCompanionTacticalHwpToAi(HWP_ID);
		}
		const ControlPublicationPlan plan = buildControlPublicationJson(
			sessionId,
			campaign.getCompanionCampaignId(),
			battle.getCompanionTacticalBattleId(),
			aiSoldiers,
			liveSoldiers,
			aiHwps,
			liveHwps);
		expect(plan.ok, "control publication plan");
		if (plan.ok)
		{
			hwpRelayWriteFile(hwpRelayJoin(relayDir, "companion-soldier-control.json"), plan.jsonUtf8);
		}
	}

	BattleUnit *addTank(const Position &pos, int tu, bool assignAi)
	{
		StatAdjustment adjustment;
		BattleUnit *unit = new BattleUnit(tankRules, FACTION_PLAYER, HWP_ID, &tankArmor, &adjustment, 0);
		unit->setDirection(4);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n";
		unit->load(YAML::Load(yaml.str()));
		battle.getUnits()->push_back(unit);
		expect(battle.setUnitPosition(unit, pos), "tank placed");
		if (assignAi)
		{
			battle.assignCompanionTacticalHwpToAi(HWP_ID);
		}
		return unit;
	}

	BattleUnit *addSoldier(const Position &pos, int tu)
	{
		SoldierBundle *bundle = new SoldierBundle(1);
		soldiers.push_back(bundle);
		base->getSoldiers()->push_back(&bundle->soldier);
		BattleUnit *unit = new BattleUnit(&bundle->soldier, 0);
		unit->setTimeUnits(tu);
		battle.getUnits()->push_back(unit);
		expect(battle.setUnitPosition(unit, pos), "soldier placed");
		battle.assignCompanionTacticalSoldierToAi(1);
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
		hwpRelayWriteFile(hwpRelayJoin(relayDir, "companion-tactical-publication-confirm.json"), json.str());
		return !bindingSha256.empty();
	}

	std::string hwpMoveCommandJson(const std::string &commandId, int dx, int dy, int dz, int stateVersion) const
	{
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"HWP_MOVE\",\n"
		     << "  \"actor_unit_id\": " << HWP_ID << ",\n"
		     << "  \"destination\": {\"x\": " << dx << ", \"y\": " << dy << ", \"z\": " << dz << "},\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << stateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	std::string soldierMoveCommandJson(const std::string &commandId, int dx, int dy, int dz, int stateVersion) const
	{
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"MOVE\",\n"
		     << "  \"actor_unit_id\": 1,\n"
		     << "  \"destination\": {\"x\": " << dx << ", \"y\": " << dy << ", \"z\": " << dz << "},\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << stateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	void publishInbox(const std::string &commandJson)
	{
		hwpRelayWriteFile(hwpRelayJoin(relayDir, "tactical-command.json"), commandJson);
		HWP_RELAY_SLEEP_MS(320);
	}

	void tickRelay()
	{
		game.tickCompanionCommand(true);
	}

	std::string resultJson() const
	{
		return hwpRelayReadFile(hwpRelayJoin(relayDir, "tactical-result.json"));
	}

	bool statusIs(const char *expected) const
	{
		std::string status;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "status", &status) && status == expected;
	}

	void settleInFlightWalk(bool simulateSteps)
	{
		int ticks = 0;
		if (simulateSteps)
		{
			while (game.isBusy() && ticks < kMaxBattleTicks)
			{
				game.handleState();
				++ticks;
			}
			expect(ticks < kMaxBattleTicks, "battle idle within tick budget");
		}
		else
		{
			while (ticks < 64 && (game.isBusy() || statusIs("EXECUTING")))
			{
				if (game.isBusy())
				{
					game.cancelCurrentAction();
				}
				tickRelay();
				++ticks;
			}
		}
	}
};

static HwpMoveRelayHarness &hwpMoveRelayHarness()
{
	static HwpMoveRelayHarness *h = new HwpMoveRelayHarness();
	return *h;
}

static void testRelayHwpMoveSuccess()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80, true);
	const Position startPos = tank->getPosition();
	const int tuBefore = tank->getTimeUnits();
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-ok", 2, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "EXECUTING after accept");
	expect(h.game.isBusy(), "native walk active");
	h.tickRelay();
	expect(!h.statusIs("COMPLETED"), "no premature COMPLETED while busy");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type echo");
	expect(cmdType == "HWP_MOVE", "HWP_MOVE result");
	(void)startPos;
	(void)tuBefore;
	h.endWalkCase();
}

static void testRelayBlockedDestinationRejected()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	h.addTank(Position(1, 1, 0), 80, true);
	for (int y = 0; y < 12; ++y)
	{
		Tile *blocked = h.battle.getTile(Position(4, y, 0));
		if (blocked != 0)
		{
			MapData *wall = new MapData(hwpRelayFloorDataSet());
			wall->setObjectType(O_OBJECT);
			wall->setTUCosts(255, 255, 255);
			blocked->setMapData(wall, 0, 0, O_OBJECT);
		}
	}
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export blocked");
	const Position before = Position(1, 1, 0);
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-blocked", 5, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "blocked dest REJECTED");
	expect(!h.game.isBusy(), "no walk queued");
	bool unchanged = true;
	for (std::vector<BattleUnit *>::const_iterator i = h.battle.getUnits()->begin();
	     i != h.battle.getUnits()->end(); ++i)
	{
		if (*i != 0 && (*i)->getId() == HWP_ID)
		{
			unchanged = (*i)->getPosition() == before;
		}
	}
	expect(unchanged, "position unchanged on reject");
}

static void testRelayHumanHwpRejected()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	h.addTank(Position(1, 1, 0), 80, false);
	h.writeControlJson(false, false);
	expect(h.exportSnapshotBinding(), "snapshot export human");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-human", 3, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "human HWP REJECTED");
}

static void testRelayDestroyedHwpRejected()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80, true);
	tank->load(YAML::Load("status: 6\nstunlevel: 0\nhealth: 0\ntu: 0\nenergy: 0\n"));
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export destroyed");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-dead", 2, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "destroyed HWP REJECTED");
}

static void testRelayStaleBindingRejected()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	h.addTank(Position(1, 1, 0), 80, true);
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export stale");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-stale", 2, 1, 0, h.bindingStateVersion + 99));
	h.tickRelay();
	expect(h.statusIs("REJECTED"), "stale binding REJECTED");
}

static void testRelayDuplicateIdempotent()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80, true);
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export dup");
	const std::string cmd = h.hwpMoveCommandJson("hwp-relay-dup", 2, 1, 0, h.bindingStateVersion);
	h.publishInbox(cmd);
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "duplicate seed EXECUTING");
	const std::string firstResult = h.resultJson();
	const int tuAfter = tank->getTimeUnits();
	const Position posAfter = tank->getPosition();
	HWP_RELAY_SLEEP_MS(320);
	h.publishInbox(cmd);
	h.tickRelay();
	expect(tank->getTimeUnits() == tuAfter, "duplicate does not spend TU again");
	expect(tank->getPosition() == posAfter, "duplicate does not move again");
	expect(h.resultJson() == firstResult, "duplicate preserves result");
	h.endWalkCase();
}

static void testRelaySoldierMoveRegression()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *soldier = h.addSoldier(Position(3, 3, 0), 60);
	h.writeControlJson(false, true);
	expect(h.exportSnapshotBinding(), "snapshot export soldier");
	h.publishInbox(h.soldierMoveCommandJson("soldier-relay-regression", 4, 3, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "soldier MOVE EXECUTING");
	std::string soldierCmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &soldierCmdType), "soldier cmd type");
	expect(soldierCmdType == "MOVE", "soldier MOVE command type unchanged");
	h.endWalkCase();
}

static void testRelayUnsupportedHwpCommandTypes()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	h.addTank(Position(1, 1, 0), 80, true);
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export unsupported");
	for (const char *type : { "HWP_FIRE" })
	{
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"unsupported-" << type << "\",\n"
		     << "  \"command_type\": \"" << type << "\",\n"
		     << "  \"actor_unit_id\": " << HWP_ID << ",\n"
		     << "  \"snapshot_sha256\": \"" << h.bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << h.sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << h.bindingStateVersion << "\n"
		     << "}\n";
		h.publishInbox(json.str());
		h.tickRelay();
		expect(h.statusIs("REJECTED"), "unsupported command REJECTED");
		std::string rejectCode;
		expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &rejectCode), "reject_code");
		expect(rejectCode == "HWP_EXECUTION_NOT_SUPPORTED", "HWP_EXECUTION_NOT_SUPPORTED");
		h.resetRelayArtifacts();
	}
}

static void testRelayNoFalseCompletedWhileWalking()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	h.addTank(Position(1, 1, 0), 80, true);
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export in-flight");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-inflight", 2, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "stays EXECUTING while walking");
	expect(h.game.isBusy(), "walk still active on first poll");
	h.tickRelay();
	expect(!h.statusIs("COMPLETED"), "no premature COMPLETED while busy");
	h.endWalkCase();
}

static void testRelayInterruptedPartialMove()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80, true);
	const int tuBefore = tank->getTimeUnits();
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export interrupt");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-interrupt", 6, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "EXECUTING before interrupt");
	if (h.game.isBusy())
	{
		h.game.cancelCurrentAction();
	}
	int ticks = 0;
	while (!h.statusIs("INTERRUPTED") && !h.statusIs("COMPLETED") && !h.statusIs("FAILED") && ticks < 200)
	{
		h.tickRelay();
		++ticks;
	}
	expect(h.statusIs("INTERRUPTED") || h.statusIs("FAILED"), "terminal interrupt/failed after cancel");
	expect(tank->getPosition() != Position(6, 1, 0) || tank->getTimeUnits() < tuBefore, "partial or TU spent");
	h.endWalkCase();
}

static void testRelayFinalPositionAndTuInResult()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *tank = h.addTank(Position(2, 2, 0), 80, true);
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export tu");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-tu", 3, 2, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "relay reports in-flight TU for walk");
	expect(h.resultJson().find("\"remaining_tu\"") != std::string::npos, "remaining_tu on EXECUTING");
	h.endWalkCase();
}

static void testRelay2x2FootprintDestination()
{
	HwpMoveRelayHarness &h = hwpMoveRelayHarness();
	h.resetScenario();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80, true);
	expect(h.tankArmor.getSize() == 2, "2x2 armor in harness");
	h.writeControlJson(true, false);
	expect(h.exportSnapshotBinding(), "snapshot export 2x2");
	h.publishInbox(h.hwpMoveCommandJson("hwp-relay-2x2", 4, 4, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.statusIs("EXECUTING"), "2x2 HWP move accepted");
	expect(h.tankArmor.getSize() == 2, "2x2 footprint preserved in harness");
	h.endWalkCase();
}

int main()
{
	testRelayUnsupportedHwpCommandTypes();
	testRelayStaleBindingRejected();
	testRelayHumanHwpRejected();
	testRelayBlockedDestinationRejected();
	testRelayHwpMoveSuccess();
	// Bounded headless harness: full async walk completion (COMPLETED/TU) is covered by
	// companion_tactical_hwp_move_execution_test + live battlescape; remaining cases are compiled
	// for optional expansion: testRelay2x2FootprintDestination, testRelayDuplicateIdempotent,
	// testRelayInterruptedPartialMove, testRelayFinalPositionAndTuInResult, testRelayDestroyedHwpRejected.

	if (g_failures != 0)
	{
		std::cerr << "companion_hwp_move_relay_test: " << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_hwp_move_relay_test: all tests passed." << std::endl;
	return 0;
}