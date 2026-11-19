/*
 * Open X-Com Companion — TURN_FACE relay inbox E2E (M1-WP-TURN-1c).
 * Included from companion_turn_face_engine_test.cpp.
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Mod/Armor.h"
#include "../Mod/Mod.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/Base.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include "Engine/CompanionCommandRuntime.h"
#include "Engine/CompanionJsonLite.h"
#include "Engine/CompanionSnapshot.h"
#include "../Mod/RuleInventory.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define TURN_RELAY_MKDIR(path) _mkdir(path)
#define TURN_RELAY_PUTENV(name, value) _putenv_s(name, value)
#define TURN_RELAY_SLEEP_MS(ms) Sleep(ms)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TURN_RELAY_MKDIR(path) mkdir(path, 0755)
#define TURN_RELAY_PUTENV(name, value) setenv(name, value, 1)
#define TURN_RELAY_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

using namespace OpenXcom;

static RuleInventory *turnMakeHandInventory(Mod &mod, const char *id)
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

static RuleInventory *turnRegisterGroundInventory(Mod &mod)
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

static MapData *turnFloorMapData(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static MapDataSet *turnHarnessFloorMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("turn_face_relay_floor");
	}
	return ds;
}

static void turnWireFloorTile(SavedBattleGame *battle, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	tile->setMapData(turnFloorMapData(turnHarnessFloorMapDataSet()), 1, 0, O_FLOOR);
}

static bool turnWireUnitTile(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	if (tile == 0 || unit == 0)
	{
		return false;
	}
	turnWireFloorTile(battle, pos);
	if (!battle->setUnitPosition(unit, pos))
	{
		return false;
	}
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
	return tile->getUnit() == unit;
}

static void turnDetachUnitFromTiles(SavedBattleGame *battle, BattleUnit *unit)
{
	if (battle == 0 || unit == 0 || unit->isOut())
	{
		return;
	}
	const int size = unit->getArmor()->getSize() - 1;
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

static void turnRelayWriteFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static std::string turnRelayReadFile(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void turnRelayRemoveIfPresent(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string turnRelayJoin(const std::string &dir, const char *name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	if (dir.empty())
	{
		return std::string(name);
	}
	return dir + sep + name;
}

static std::string turnRelayExeDir()
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

struct TurnSoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	TurnSoldierFixture(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

struct TurnFaceRelayHarness
{
	Mod mod;
	SavedGame campaign;
	Base *base;
	std::vector<TurnSoldierFixture *> soldierFixtures;
	SavedBattleGame battle;
	BattlescapeGame game;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	BattleUnit *actor;
	std::string bindingSha256;
	int bindingStateVersion;

	TurnFaceRelayHarness()
		: base(new Base(&mod)),
		  battle(),
		  game(&battle, 0),
		  actor(0),
		  bindingStateVersion(0)
	{
		turnMakeHandInventory(mod, "STR_LEFT_HAND");
		turnMakeHandInventory(mod, "STR_RIGHT_HAND");
		turnRegisterGroundInventory(mod);
		campaign.ensureCompanionCampaignId();
		campaign.getBases()->push_back(base);
		battle.setTestMod(&mod);
		battle.initMap(10, 10, 1);
		battle.initUtilities(&mod);
		for (int y = 0; y < 10; ++y)
		{
			for (int x = 0; x < 10; ++x)
			{
				turnWireFloorTile(&battle, Position(x, y, 0));
			}
		}
		battle.setGeoscapeSaveHint(&campaign);
		battle.ensureCompanionTacticalOwnershipInitialized(&campaign);
		battle.getCompanionEventLog().beginSession();
		sessionId = battle.getCompanionEventLog().getSessionId();
		battle.assignCompanionTacticalSoldierToAi(1);

		relayDir = turnRelayJoin(turnRelayExeDir(), "turn_face_relay_e2e");
		TURN_RELAY_MKDIR(relayDir.c_str());
		snapshotPath = turnRelayJoin(relayDir, "tactical-relay-fixture.yaml");
		turnRelayWriteFile(snapshotPath, "tactical:\n  relay_fixture: turn_face_1c\n");
		TURN_RELAY_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		TURN_RELAY_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");

		const std::string controlJson =
			"{\n"
			"  \"schema_version\": 1,\n"
			"  \"session_id\": \"" + sessionId + "\",\n"
			"  \"assignments\": { \"1\": \"ai\" }\n"
			"}\n";
		turnRelayWriteFile(turnRelayJoin(relayDir, "companion-soldier-control.json"), controlJson);
		resetRelayArtifacts();
	}

	void clearBattleUnits()
	{
		if (battle.getUnits() == 0)
		{
			actor = 0;
			return;
		}
		for (std::vector<BattleUnit *>::iterator i = battle.getUnits()->begin();
		     i != battle.getUnits()->end(); ++i)
		{
			turnDetachUnitFromTiles(&battle, *i);
			delete *i;
		}
		battle.getUnits()->clear();
		actor = 0;
	}

	void resetScenario()
	{
		clearBattleUnits();
		if (base != 0 && base->getSoldiers() != 0)
		{
			base->getSoldiers()->clear();
		}
		for (std::vector<TurnSoldierFixture *>::iterator i = soldierFixtures.begin();
		     i != soldierFixtures.end(); ++i)
		{
			delete *i;
		}
		soldierFixtures.clear();
		resetRelayArtifacts();
		bindingSha256.clear();
		bindingStateVersion = 0;
		battle.resetUnitHitStates();
	}

	void resetRelayArtifacts()
	{
		turnRelayRemoveIfPresent(turnRelayJoin(relayDir, "tactical-command.json"));
		turnRelayRemoveIfPresent(turnRelayJoin(relayDir, "tactical-result.json"));
		turnRelayRemoveIfPresent(turnRelayJoin(relayDir, "tactical-command-journal.json"));
		turnRelayRemoveIfPresent(turnRelayJoin(relayDir, "tactical-command-conflict.json"));
		turnRelayRemoveIfPresent(turnRelayJoin(relayDir, "companion-tactical-publication-confirm.json"));
	}

	Soldier *addRosterSoldier(int soldierId)
	{
		TurnSoldierFixture *fixture = new TurnSoldierFixture(soldierId);
		soldierFixtures.push_back(fixture);
		base->getSoldiers()->push_back(&fixture->soldier);
		return &fixture->soldier;
	}

	bool wireActor(const Position &pos, int tu, int direction)
	{
		clearBattleUnits();
		Soldier *soldier = addRosterSoldier(1);
		actor = new BattleUnit(soldier, 0);
		battle.getUnits()->push_back(actor);
		actor->setTimeUnits(tu);
		actor->setDirection(direction);
		return turnWireUnitTile(&battle, actor, pos);
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
		     << "  \"status\": \"published\",\n"
		     << "  \"source\": \"turn_face_relay_e2e\",\n"
		     << "  \"confirmed_at_ms\": 0\n"
		     << "}\n";
		turnRelayWriteFile(turnRelayJoin(relayDir, "companion-tactical-publication-confirm.json"), json.str());
		return !bindingSha256.empty();
	}

	std::string turnFaceCommandJson(const std::string &commandId, int tx, int ty, int tz, int stateVersion) const
	{
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"TURN_FACE\",\n"
		     << "  \"actor_unit_id\": 1,\n"
		     << "  \"target\": {\"x\": " << tx << ", \"y\": " << ty << ", \"z\": " << tz << "},\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << stateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	void publishInbox(const std::string &commandJson)
	{
		turnRelayWriteFile(turnRelayJoin(relayDir, "tactical-command.json"), commandJson);
		TURN_RELAY_SLEEP_MS(320);
	}

	void tickRelay()
	{
		game.tickCompanionCommand(true);
	}

	std::string resultJson() const
	{
		return turnRelayReadFile(turnRelayJoin(relayDir, "tactical-result.json"));
	}

	bool statusIs(const char *expected) const
	{
		std::string status;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "status", &status) && status == expected;
	}
};

int turnRelayFailures = 0;

static TurnFaceRelayHarness &turnFaceRelayHarness()
{
	static TurnFaceRelayHarness *harness = new TurnFaceRelayHarness();
	return *harness;
}

static void turnRelayExpect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL relay: " << message << std::endl;
		++turnRelayFailures;
	}
}

static void testRelayTurnFaceSuccessTwoTu()
{
	TurnFaceRelayHarness &h = turnFaceRelayHarness();
	h.resetScenario();
	turnRelayExpect(h.wireActor(Position(5, 5, 0), 50, 0), "actor tile wiring for turn success");
	turnRelayExpect(h.exportSnapshotBinding(), "snapshot export for turn success");
	const Position start = h.actor->getPosition();
	h.publishInbox(h.turnFaceCommandJson("turn-face-relay-ok", 7, 5, 0, h.bindingStateVersion));
	h.tickRelay();
	turnRelayExpect(h.statusIs("COMPLETED"), "turn COMPLETED");
	int tuSpent = -1;
	int finalDir = -1;
	turnRelayExpect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent), "tu_spent present");
	turnRelayExpect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_direction", &finalDir), "final_direction present");
	turnRelayExpect(tuSpent == 2, "relay two TU spent");
	turnRelayExpect(finalDir == h.actor->directionTo(Position(7, 5, 0)), "final direction matches bearing");
	turnRelayExpect(h.actor->getPosition() == start, "no movement on success");
}

static void testRelayTurnFaceAlreadyFacingZeroTu()
{
	TurnFaceRelayHarness &h = turnFaceRelayHarness();
	h.resetScenario();
	turnRelayExpect(h.wireActor(Position(5, 5, 0), 50, 0), "actor tile wiring already-facing");
	const Position target(6, 5, 0);
	h.actor->setDirection(h.actor->directionTo(target));
	turnRelayExpect(h.exportSnapshotBinding(), "snapshot export already-facing");
	const int tuBefore = h.actor->getTimeUnits();
	h.publishInbox(h.turnFaceCommandJson("turn-face-relay-zero", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	turnRelayExpect(h.statusIs("COMPLETED"), "already facing COMPLETED");
	int tuSpent = -1;
	turnRelayExpect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent), "tu_spent present");
	turnRelayExpect(tuSpent == 0, "zero tu_spent");
	turnRelayExpect(h.actor->getTimeUnits() == tuBefore, "TU unchanged");
}

static void testRelayTurnFaceInvalidTargetSameHorizontal()
{
	TurnFaceRelayHarness &h = turnFaceRelayHarness();
	h.resetScenario();
	turnRelayExpect(h.wireActor(Position(5, 5, 0), 50, 0), "actor tile wiring invalid target");
	turnRelayExpect(h.exportSnapshotBinding(), "snapshot export invalid target");
	const int tuBefore = h.actor->getTimeUnits();
	const int dirBefore = h.actor->getDirection();
	h.publishInbox(h.turnFaceCommandJson("turn-face-relay-same-xy", 5, 5, 1, h.bindingStateVersion));
	h.tickRelay();
	turnRelayExpect(h.statusIs("REJECTED"), "same horizontal REJECTED");
	turnRelayExpect(h.actor->getTimeUnits() == tuBefore, "TU unchanged on reject");
	turnRelayExpect(h.actor->getDirection() == dirBefore, "facing unchanged on reject");
}

static void testRelayTurnFaceInsufficientTu()
{
	TurnFaceRelayHarness &h = turnFaceRelayHarness();
	h.resetScenario();
	turnRelayExpect(h.wireActor(Position(5, 5, 0), 1, 0), "actor tile wiring insufficient TU");
	turnRelayExpect(h.exportSnapshotBinding(), "snapshot export insufficient TU");
	const int tuBefore = h.actor->getTimeUnits();
	h.publishInbox(h.turnFaceCommandJson("turn-face-relay-low-tu", 7, 5, 0, h.bindingStateVersion));
	h.tickRelay();
	turnRelayExpect(h.statusIs("REJECTED"), "insufficient TU REJECTED");
	turnRelayExpect(h.actor->getTimeUnits() == tuBefore, "TU unchanged insufficient");
}

static void testRelayTurnFaceDuplicateCommandId()
{
	TurnFaceRelayHarness &h = turnFaceRelayHarness();
	h.resetScenario();
	turnRelayExpect(h.wireActor(Position(5, 5, 0), 50, 0), "actor tile wiring duplicate");
	turnRelayExpect(h.exportSnapshotBinding(), "snapshot export duplicate");
	const std::string cmd = h.turnFaceCommandJson("turn-face-relay-dup", 6, 5, 0, h.bindingStateVersion);
	h.publishInbox(cmd);
	h.tickRelay();
	turnRelayExpect(h.statusIs("COMPLETED"), "duplicate first pass COMPLETED");
	const std::string firstCommandJson = turnRelayReadFile(turnRelayJoin(h.relayDir, "tactical-command.json"));
	const int tuAfterFirst = h.actor->getTimeUnits();
	const int dirAfterFirst = h.actor->getDirection();
	const std::string firstResult = h.resultJson();
	TURN_RELAY_SLEEP_MS(320);
	h.publishInbox(firstCommandJson);
	h.tickRelay();
	turnRelayExpect(h.actor->getTimeUnits() == tuAfterFirst, "duplicate replay leaves actor TU");
	turnRelayExpect(h.actor->getDirection() == dirAfterFirst, "duplicate replay leaves facing");
	turnRelayExpect(h.resultJson() == firstResult, "duplicate preserves terminal result json");
}

static void testRelayTurnFaceStaleBinding()
{
	TurnFaceRelayHarness &h = turnFaceRelayHarness();
	h.resetScenario();
	turnRelayExpect(h.wireActor(Position(5, 5, 0), 50, 0), "actor tile wiring stale");
	turnRelayExpect(h.exportSnapshotBinding(), "snapshot export stale");
	h.publishInbox(h.turnFaceCommandJson("turn-face-relay-stale", 6, 5, 0, h.bindingStateVersion + 99));
	h.tickRelay();
	turnRelayExpect(h.statusIs("REJECTED"), "stale binding REJECTED");
}

void runTurnFaceRelayE2eSuite()
{
	turnRelayFailures = 0;
	testRelayTurnFaceSuccessTwoTu();
	testRelayTurnFaceAlreadyFacingZeroTu();
	testRelayTurnFaceInvalidTargetSameHorizontal();
	testRelayTurnFaceInsufficientTu();
	testRelayTurnFaceDuplicateCommandId();
	testRelayTurnFaceStaleBinding();
	if (turnRelayFailures != 0)
	{
		std::cerr << turnRelayFailures << " TURN_FACE relay failure(s)." << std::endl;
	}
}