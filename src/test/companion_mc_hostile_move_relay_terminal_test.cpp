/*
 * M1 MC-02B — MC_MOVE relay terminal walk certification.
 * Real BattlescapeState + UnitWalkBState + production CompanionCommandRuntime inbox.
 * Acceptance: adjacent arrival, TU spent, relay COMPLETED (not queued-state only).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/Map.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../Engine/CompanionJsonLite.h"
#include "../Engine/CompanionSnapshot.h"
#include "../Engine/CompanionSoldierControlPublishCore.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/State.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/Unit.h"
#include "../Savegame/Base.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
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
#define CERT_MKDIR(p) _mkdir(p)
#define CERT_PUTENV(n, v) _putenv_s(n, v)
#define CERT_SLEEP_MS(ms) Sleep(ms)
#else
#include <sys/stat.h>
#include <unistd.h>
#define CERT_MKDIR(p) mkdir(p, 0755)
#define CERT_PUTENV(n, v) setenv(n, v, 1)
#define CERT_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionSoldierControlPublishCore;

namespace
{

const int MC_HOSTILE_ID = 1'000'042;
const int kMaxWalkTicks = 200000;
int g_failures = 0;

void expect(bool ok, const char *msg)
{
	if (!ok)
	{
		std::cerr << "FAIL cert: " << msg << std::endl;
		++g_failures;
	}
}

void logEvidence(const char *tag, const std::string &line)
{
	std::cerr << "[cert-evidence] " << tag << ": " << line << std::endl;
}

std::string certJoin(const std::string &dir, const char *name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	return dir.empty() ? std::string(name) : dir + sep + name;
}

std::string certExeDir()
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

void certWrite(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

std::string certRead(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

void certRemove(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string defaultDataDir()
{
	const char *candidates[] = {
		"E:/Open X-Com Companion/runtime/m0/bin",
		"../../../../runtime/m0/bin",
		"../../../runtime/m0/bin",
		"standard",
	};
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
	{
		const std::string rules = std::string(candidates[i]) + "/standard/xcom1/items.rul";
		std::ifstream in(rules.c_str());
		if (in)
		{
			return candidates[i];
		}
	}
	return "standard";
}

MapDataSet *certFloorSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("mc_relay_terminal_floor");
	}
	return ds;
}

MapData *certFloorTile()
{
	MapData *f = new MapData(certFloorSet());
	f->setObjectType(O_FLOOR);
	f->setTUCosts(4, 4, 4);
	return f;
}

void wireFloor(SavedBattleGame *battle, int w, int h)
{
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			Tile *t = battle->getTile(Position(x, y, 0));
			if (t == 0)
			{
				continue;
			}
			t->setDiscovered(true, 2);
			t->setMapData(certFloorTile(), 1, 0, O_FLOOR);
		}
	}
}

void wireUnit(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile for unit");
	unit->setDirection(4);
	expect(battle->setUnitPosition(unit, pos), "setUnitPosition");
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

struct CertHarness
{
	Game *game;
	SavedGame *campaign;
	SavedBattleGame *battle;
	BattlescapeState *battlescape;
	Mod fixtureMod;
	Base *base;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	std::string bindingSha256;
	int bindingStateVersion;

	CertHarness()
		: game(0),
		  campaign(0),
		  battle(0),
		  battlescape(0),
		  base(0),
		  bindingStateVersion(0)
	{
	}

	BattlescapeGame *battleGame() const
	{
		return battlescape != 0 ? battlescape->getBattleGame() : 0;
	}

	bool initGame(const char *argv0, const std::string &dataDir)
	{
		Options::mute = true;
		std::vector<char *> av;
		av.push_back(const_cast<char *>(argv0));
		std::string dataArg = dataDir;
		av.push_back(const_cast<char *>("-data"));
		av.push_back(const_cast<char *>(dataArg.c_str()));
		if (!Options::init(static_cast<int>(av.size()), av.data()))
		{
			return false;
		}
		game = new Game("MC_MOVE relay terminal cert");
		State::setGamePtr(game);
		Options::updateMods();
		game->loadMods();
		campaign = new SavedGame();
		game->setSavedGame(campaign);
		campaign->ensureCompanionCampaignId();
		base = new Base(game->getMod());
		battlescape = 0;
		campaign->getBases()->push_back(base);
		if (Options::baseXResolution <= 0)
		{
			Options::baseXResolution = 320;
		}
		if (Options::baseYResolution <= 0)
		{
			Options::baseYResolution = 200;
		}
		return true;
	}

	void setupBattle(int w, int h)
	{
		if (battle != 0)
		{
			battle->setBattleState(0);
		}
		if (battlescape != 0)
		{
			delete battlescape;
			battlescape = 0;
		}
		SavedBattleGame *next = new SavedBattleGame();
		campaign->setBattleGame(next);
		battle = next;
		battle->setTestMod(&fixtureMod);
		battle->initMap(w, h, 1);
		battle->initUtilities(&fixtureMod);
		battle->setGeoscapeSaveHint(campaign);
		battle->ensureCompanionTacticalOwnershipInitialized(campaign);
		battle->getCompanionEventLog().beginSession();
		sessionId = battle->getCompanionEventLog().getSessionId();
		wireFloor(battle, w, h);
		battlescape = new BattlescapeState();
		battle->setBattleState(battlescape);
		battlescape->init();
		bindingSha256.clear();
		bindingStateVersion = 0;
	}

	void initRelayEnv()
	{
		relayDir = certJoin(certExeDir(), "mc_relay_terminal_cert");
		CERT_MKDIR(relayDir.c_str());
		snapshotPath = certJoin(relayDir, "tactical-relay-fixture.yaml");
		certWrite(snapshotPath, "tactical:\n  relay_fixture: mc_terminal_mc02b\n");
		CERT_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		CERT_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");
		resetRelayFiles();
	}

	void resetRelayFiles()
	{
		certRemove(certJoin(relayDir, "tactical-command.json"));
		certRemove(certJoin(relayDir, "tactical-result.json"));
		certRemove(certJoin(relayDir, "tactical-command-journal.json"));
		certRemove(certJoin(relayDir, "tactical-command-conflict.json"));
		certRemove(certJoin(relayDir, "companion-tactical-publication-confirm.json"));
	}

	bool exportBinding()
	{
		bindingSha256.clear();
		if (!CompanionSnapshot::tryExport(battle, &bindingSha256))
		{
			return false;
		}
		bindingStateVersion = battle->getCompanionEventLog().getLatestSeq();
		std::ostringstream c;
		c << "{\n  \"schema_version\": 1,\n  \"session_id\": \"" << sessionId
		  << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << bindingStateVersion
		  << ",\n  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n  \"status\": \"published\"\n}\n";
		certWrite(certJoin(relayDir, "companion-tactical-publication-confirm.json"), c.str());
		return !bindingSha256.empty();
	}

	void writeControl(bool mcAi)
	{
		std::set<int> aiMc;
		std::vector<int> liveMc;
		if (mcAi)
		{
			aiMc.insert(MC_HOSTILE_ID);
			liveMc.push_back(MC_HOSTILE_ID);
			expect(battle->assignCompanionTacticalMcHostileToAi(MC_HOSTILE_ID), "tactical MC AI assign");
		}
		else
		{
			battle->reclaimCompanionTacticalMcHostileForHuman(MC_HOSTILE_ID);
		}
		const ControlPublicationPlan plan = buildControlPublicationJson(
			sessionId,
			campaign->getCompanionCampaignId(),
			battle->getCompanionTacticalBattleId(),
			std::set<int>(),
			std::vector<int>(),
			std::set<int>(),
			std::vector<int>(),
			aiMc,
			liveMc);
		expect(plan.ok, "control json");
		if (plan.ok)
		{
			certWrite(certJoin(relayDir, "companion-soldier-control.json"), plan.jsonUtf8);
		}
	}

	BattleUnit *addMcHostile(const Position &pos, int tu)
	{
		Mod *rules = game->getMod();
		Unit *unitRules = rules != 0 ? rules->getUnit("STR_SECTOID_SOLDIER", false) : 0;
		if (unitRules == 0 && rules != 0)
		{
			unitRules = rules->getUnit("STR_SECTOID", false);
		}
		Armor *armor = rules != 0 ? rules->getArmor("SECTOID_ARMOR0", false) : 0;
		if (armor == 0 && rules != 0)
		{
			armor = rules->getArmor("STR_SECTOID_ARMOR", false);
		}
		expect(unitRules != 0 && armor != 0, "stock sectoid rules");
		if (unitRules == 0 || armor == 0)
		{
			return 0;
		}
		StatAdjustment adj;
		BattleUnit *unit = new BattleUnit(unitRules, FACTION_HOSTILE, MC_HOSTILE_ID, armor, &adj, 0);
		std::ostringstream yaml;
		yaml << "faction: 0\noriginalFaction: 1\nstatus: 0\nstunlevel: 0\nhealth: 26\ntu: " << tu
		     << "\nenergy: 50\nmorale: 80\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n";
		unit->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(unit);
		wireUnit(battle, unit, pos);
		return unit;
	}

	std::string resultJson() const
	{
		return certRead(certJoin(relayDir, "tactical-result.json"));
	}

	std::string status() const
	{
		std::string s;
		CompanionJsonLite::extractJsonStringField(resultJson(), "status", &s);
		return s;
	}

	void publish(const std::string &json)
	{
		certWrite(certJoin(relayDir, "tactical-command.json"), json);
		CERT_SLEEP_MS(320);
	}

	void tickRelay()
	{
		BattlescapeGame *bg = battleGame();
		if (bg != 0)
		{
			bg->tickCompanionCommand(true);
		}
	}

	void endWalkCase()
	{
		BattlescapeGame *bg = battleGame();
		if (bg != 0)
		{
			for (int i = 0; i < 64 && bg->isBusy(); ++i)
			{
				bg->cancelCurrentAction();
			}
		}
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
		resetRelayFiles();
	}

	std::string mcActorCmd(const char *id, const char *commandType, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"" << commandType << "\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcTurnFaceCmd(const char *id, int tx, int ty, int tz, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_TURN_FACE\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"target\": {\"x\": " << tx << ", \"y\": " << ty << ", \"z\": " << tz << "},\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcMoveCmd(const char *id, int dx, int dy, int dz, int sv,
		const std::string *snapshotOverride = 0, int turnOverride = -1) const
	{
		const std::string &sha = snapshotOverride != 0 ? *snapshotOverride : bindingSha256;
		const int turn = turnOverride >= 0 ? turnOverride : battle->getTurn();
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_MOVE\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"destination\": {\"x\": " << dx << ", \"y\": " << dy << ", \"z\": " << dz << "},\n"
		  << "  \"snapshot_sha256\": \"" << sha << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << turn << ",\n  \"state_version\": " << sv
		  << "\n}\n";
		return j.str();
	}

	std::string rejectCode() const
	{
		std::string code;
		CompanionJsonLite::extractJsonStringField(resultJson(), "reject_code", &code);
		return code;
	}

	bool parseFinalPos(const std::string &json, int *x, int *y, int *z)
	{
		const std::string::size_type fp = json.find("\"final_position\"");
		if (fp == std::string::npos)
		{
			return false;
		}
		const std::string::size_type xk = json.find("\"x\":", fp);
		const std::string::size_type yk = json.find("\"y\":", fp);
		const std::string::size_type zk = json.find("\"z\":", fp);
		if (xk == std::string::npos || yk == std::string::npos || zk == std::string::npos)
		{
			return false;
		}
		return CompanionJsonLite::parseJsonIntValue(json, xk + 4, x) &&
		       CompanionJsonLite::parseJsonIntValue(json, yk + 4, y) &&
		       CompanionJsonLite::parseJsonIntValue(json, zk + 4, z);
	}

	int advanceUntilTerminal(bool stepBattle)
	{
		BattlescapeGame *bg = battleGame();
		expect(bg != 0, "battle game");
		int ticks = 0;
		while (ticks < kMaxWalkTicks)
		{
			if (stepBattle && bg->isBusy())
			{
				bg->handleState();
			}
			tickRelay();
			const std::string st = status();
			if (!st.empty() && st != "EXECUTING")
			{
				break;
			}
			++ticks;
		}
		expect(ticks < kMaxWalkTicks, "terminal within tick budget");
		return ticks;
	}
};

CertHarness &harness()
{
	static CertHarness h;
	return h;
}

void certAdjacentTerminalWalk()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position from(2, 2, 0);
	const Position dest(3, 2, 0);
	BattleUnit *alien = h.addMcHostile(from, 60);
	expect(alien != 0, "MC hostile on map");
	h.writeControl(true);
	const int tuBefore = alien->getTimeUnits();
	expect(h.exportBinding(), "snapshot");
	h.publish(h.mcMoveCmd("cert-mc-adjacent-walk", dest.x, dest.y, dest.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "accepted EXECUTING");
	expect(h.battleGame()->isBusy(), "busy while walking");
	h.tickRelay();
	expect(h.status() != "COMPLETED", "no premature COMPLETED");
	const int ticks = h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "terminal COMPLETED");
	expect(!h.battleGame()->isBusy(), "idle after walk");
	expect(!CompanionCommandRuntime::isMoveInFlight(), "active command cleared");
	expect(alien->getPosition() == dest, "engine at adjacent destination");
	int rem = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &rem), "remaining_tu");
	expect(rem == alien->getTimeUnits(), "remaining_tu matches engine");
	int fx = 0, fy = 0, fz = 0;
	expect(h.parseFinalPos(h.resultJson(), &fx, &fy, &fz), "final_position in result");
	expect(fx == alien->getPosition().x && fy == alien->getPosition().y && fz == alien->getPosition().z,
		"final_position matches engine");
	expect(alien->getTimeUnits() < tuBefore, "TU spent on completed walk");
	expect(h.resultJson().find("\"destination_reached\": true") != std::string::npos, "destination_reached");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_MOVE", "result command_type MC_MOVE");
	expect(h.resultJson().find("\"final_direction\"") == std::string::npos,
		"MC_MOVE has no HWP-only final_direction field");
	logEvidence("mc-terminal-walk", h.resultJson());
	logEvidence("mc-terminal-walk-engine",
		"pos=" + std::to_string(alien->getPosition().x) + "," + std::to_string(alien->getPosition().y) + " tu="
			+ std::to_string(alien->getTimeUnits()) + " tuBefore=" + std::to_string(tuBefore) + " ticks="
			+ std::to_string(ticks));
	h.endWalkCase();
}

void certHumanMcRejected()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(2, 2, 0), 60);
	const int tu0 = alien != 0 ? alien->getTimeUnits() : 0;
	const Position pos0 = alien != 0 ? alien->getPosition() : Position();
	h.writeControl(false);
	expect(h.exportBinding(), "snapshot human MC");
	h.publish(h.mcMoveCmd("cert-mc-human", 3, 2, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "human MC rejected");
	expect(h.rejectCode() == "NOT_AI_CONTROLLED", "reclaimed/human NOT_AI_CONTROLLED");
	expect(alien->getTimeUnits() == tu0, "no TU loss on reject");
	expect(alien->getPosition() == pos0, "no position change on reject");
	h.endWalkCase();
}

void certStaleBindingRejected()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(2, 2, 0), 60);
	const int tu0 = alien->getTimeUnits();
	const Position pos0 = alien->getPosition();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot stale");
	h.publish(h.mcMoveCmd("cert-mc-stale-sv", 3, 2, 0, h.bindingStateVersion + 99));
	h.tickRelay();
	expect(h.status() == "REJECTED", "stale state_version REJECTED");
	expect(h.rejectCode() == "STALE_STATE_VERSION", "STALE_STATE_VERSION");
	expect(alien->getTimeUnits() == tu0, "no TU on stale reject");
	expect(alien->getPosition() == pos0, "no move on stale reject");
	h.endWalkCase();
}

void certWrongTurnRejected()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(2, 2, 0), 60);
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot wrong turn");
	h.battle->endTurn();
	expect(h.battle->getSide() != FACTION_PLAYER, "player turn ended");
	h.publish(h.mcMoveCmd("cert-mc-wrong-turn", 3, 2, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "wrong turn REJECTED");
	expect(h.rejectCode() == "WRONG_TURN", "WRONG_TURN");
	expect(alien->getTimeUnits() == tu0, "no TU after turn-end reject");
	h.endWalkCase();
}

void certActorIdentityRejected()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	h.addMcHostile(Position(2, 2, 0), 60);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot identity");
	const std::string goodSha = h.bindingSha256;
	std::ostringstream soldierJson;
	soldierJson << "{\n  \"schema_version\": 1,\n  \"command_id\": \"cert-mc-soldier-id\",\n"
	            << "  \"command_type\": \"MC_MOVE\",\n  \"actor_unit_id\": 1,\n"
	            << "  \"destination\": {\"x\": 3, \"y\": 2, \"z\": 0},\n"
	            << "  \"snapshot_sha256\": \"" << goodSha << "\",\n"
	            << "  \"session_id\": \"" << h.sessionId << "\",\n  \"turn\": 1,\n  \"state_version\": "
	            << h.bindingStateVersion << "\n}\n";
	h.publish(soldierJson.str());
	h.tickRelay();
	expect(h.status() == "REJECTED", "soldier id MC_MOVE rejected");
	expect(h.rejectCode() == "INVALID_ACTOR", "soldier INVALID_ACTOR");

	h.resetRelayFiles();
	expect(h.exportBinding(), "re-export for HWP negative");
	Unit *hwpRules = h.game->getMod()->getUnit("STR_TANK_CANNON", false);
	Armor *hwpArmor = h.game->getMod()->getArmor("TANK_ARMOR", false);
	expect(hwpRules != 0 && hwpArmor != 0, "tank rules for identity negative");
	if (hwpRules != 0 && hwpArmor != 0)
	{
		const int kHwpId = 1'000'001;
		StatAdjustment adj;
		BattleUnit *tank = new BattleUnit(hwpRules, FACTION_PLAYER, kHwpId, hwpArmor, &adj, 0);
		tank->load(YAML::Load(
			"status: 0\nstunlevel: 0\nhealth: 120\ntu: 80\nenergy: 60\nmorale: 100\narmor: [50,50,50,50,50]\n"
			"fatalWounds: [0,0,0,0,0,0]\n"));
		h.battle->getUnits()->push_back(tank);
		wireUnit(h.battle, tank, Position(4, 2, 0));
		expect(h.battle->assignCompanionTacticalHwpToAi(kHwpId), "HWP AI for identity negative");
		std::ostringstream hwpJson;
		hwpJson << "{\n  \"schema_version\": 1,\n  \"command_id\": \"cert-mc-hwp-id\",\n"
		        << "  \"command_type\": \"MC_MOVE\",\n  \"actor_unit_id\": " << kHwpId << ",\n"
		        << "  \"destination\": {\"x\": 5, \"y\": 2, \"z\": 0},\n"
		        << "  \"snapshot_sha256\": \"" << h.bindingSha256 << "\",\n"
		        << "  \"session_id\": \"" << h.sessionId << "\",\n  \"turn\": 1,\n  \"state_version\": "
		        << h.bindingStateVersion << "\n}\n";
		h.publish(hwpJson.str());
		h.tickRelay();
		expect(h.status() == "REJECTED", "HWP id MC_MOVE rejected");
		expect(h.rejectCode() == "INVALID_ACTOR", "HWP INVALID_ACTOR for MC");
		expect(tank->getTimeUnits() == 80, "HWP TU unchanged on MC reject");
	}
	h.endWalkCase();
}

void certAuthorityLossDuringWalk()
{
	CertHarness &h = harness();
	h.setupBattle(14, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(1, 2, 0), 80);
	expect(alien != 0, "alien for authority-loss walk");
	h.writeControl(true);
	const Position dest(9, 2, 0);
	const int tuAtStart = alien->getTimeUnits();
	const Position startPos = alien->getPosition();
	expect(h.exportBinding(), "snapshot authority-loss");
	h.publish(h.mcMoveCmd("cert-mc-auth-loss", dest.x, dest.y, dest.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "EXECUTING before reclaim");
	BattlescapeGame *bg = h.battleGame();
	int steps = 0;
	while (steps < 48 && bg->isBusy() && alien->getPosition().x < 4)
	{
		bg->handleState();
		h.tickRelay();
		++steps;
	}
	expect(alien->getPosition().x > startPos.x, "partial progress before reclaim");
	const int tuAfterPartial = alien->getTimeUnits();
	expect(tuAfterPartial <= tuAtStart, "partial walk TU not invented");
	expect(h.battle->reclaimCompanionTacticalMcHostileForHuman(MC_HOSTILE_ID), "reclaim during walk");
	h.writeControl(false);
	BattlescapeGame *bgAfterReclaim = h.battleGame();
	std::string st;
	for (int i = 0; i < 160; ++i)
	{
		if (bgAfterReclaim != 0 && bgAfterReclaim->isBusy())
		{
			bgAfterReclaim->handleState();
		}
		h.tickRelay();
		st = h.status();
		if (!st.empty() && st != "EXECUTING")
		{
			break;
		}
	}
	expect(st == "INTERRUPTED" || st == "FAILED", "authority-loss terminal status");
	expect(alien->getPosition() != dest, "no destination_reached after authority loss");
	expect(h.resultJson().find("\"destination_reached\": true") == std::string::npos,
		"destination_reached not true");
	for (int j = 0; j < 96 && bgAfterReclaim != 0 && bgAfterReclaim->isBusy(); ++j)
	{
		bgAfterReclaim->handleState();
		h.tickRelay();
	}
	const Position freezePos = alien->getPosition();
	const int freezeTu = alien->getTimeUnits();
	expect(!bgAfterReclaim->isBusy(), "idle after authority-loss settle");
	expect(!CompanionCommandRuntime::isMoveInFlight(), "relay in-flight cleared after cancel");
	for (int i = 0; i < 32; ++i)
	{
		if (bgAfterReclaim != 0 && bgAfterReclaim->isBusy())
		{
			bgAfterReclaim->handleState();
		}
		h.tickRelay();
		expect(alien->getPosition() == freezePos, "no movement during post-cancel engine updates");
		expect(alien->getTimeUnits() == freezeTu, "no TU spend during post-cancel engine updates");
	}
	expect(alien->getPosition().x >= startPos.x, "pre-cancel partial progress retained");
	expect(alien->getTimeUnits() <= tuAtStart, "pre-cancel TU never invented");
	logEvidence("mc-auth-loss", "status=" + st + " pos=" + std::to_string(alien->getPosition().x) + ","
		+ std::to_string(alien->getPosition().y) + " tu=" + std::to_string(alien->getTimeUnits()) + " tuStart="
		+ std::to_string(tuAtStart));
	h.endWalkCase();
}

void certMcKneelRejectedForStockAlien()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(3, 3, 0), 60);
	expect(alien != 0, "alien for MC_KNEEL negative");
	expect(alien->getType() != "SOLDIER", "stock alien is not SOLDIER type");
	const bool kneeledBefore = alien->isKneeled();
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_KNEEL reject");
	h.publish(h.mcActorCmd("cert-mc-kneel-reject", "MC_KNEEL", h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "MC_KNEEL rejected for alien");
	expect(h.rejectCode() == "INVALID_ACTOR", "MC_KNEEL INVALID_ACTOR");
	expect(!alien->isKneeled(), "no kneel posture change");
	expect(alien->isKneeled() == kneeledBefore, "kneel flag unchanged");
	expect(alien->getTimeUnits() == tu0, "no TU spent on MC_KNEEL reject");
	h.endWalkCase();
}

void certMcStandRejectedForStockAlien()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(3, 3, 0), 60);
	expect(alien != 0, "alien for MC_STAND negative");
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_STAND reject");
	h.publish(h.mcActorCmd("cert-mc-stand-reject", "MC_STAND", h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "MC_STAND rejected for alien");
	const std::string code = h.rejectCode();
	expect(code == "INVALID_ACTOR" || code == "ALREADY_STANDING", "MC_STAND stock posture reject");
	expect(!alien->isKneeled(), "alien remains standing");
	expect(alien->getTimeUnits() == tu0, "no TU spent on MC_STAND reject");
	h.endWalkCase();
}

void certMcTurnFaceTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position pos(4, 4, 0);
	BattleUnit *alien = h.addMcHostile(pos, 60);
	expect(alien != 0, "alien for MC_TURN_FACE");
	alien->setDirection(4);
	const int tuBefore = alien->getTimeUnits();
	const int dirBefore = alien->getDirection();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_TURN_FACE");
	h.publish(h.mcTurnFaceCmd("cert-mc-turn-face", 6, 4, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_TURN_FACE COMPLETED");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_TURN_FACE", "result command_type MC_TURN_FACE");
	expect(alien->getPosition() == pos, "MC_TURN_FACE does not move");
	expect(alien->getDirection() != dirBefore, "direction changed");
	expect(alien->getTimeUnits() < tuBefore, "TU spent on turn");
	int relayDir = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_direction", &relayDir), "final_direction");
	expect(relayDir == alien->getDirection(), "relay final_direction");
	logEvidence("mc-turn-face", "dir=" + std::to_string(alien->getDirection()) + " tu=" +
		std::to_string(alien->getTimeUnits()) + " tuBefore=" + std::to_string(tuBefore));
	h.endWalkCase();
}

} // namespace

int main(int argc, char **argv)
{
	CertHarness &h = harness();
	if (!h.initGame(argc > 0 ? argv[0] : "companion_mc_hostile_move_relay_terminal_test", defaultDataDir()))
	{
		std::cerr << "mc relay terminal cert: Options/Game init failed" << std::endl;
		return 1;
	}
	h.initRelayEnv();

	certStaleBindingRejected();
	certWrongTurnRejected();
	certActorIdentityRejected();
	certHumanMcRejected();
	certAuthorityLossDuringWalk();
	certMcKneelRejectedForStockAlien();
	certMcStandRejectedForStockAlien();
	certMcTurnFaceTerminal();
	certAdjacentTerminalWalk();

	if (g_failures != 0)
	{
		std::cerr << "companion_mc_hostile_move_relay_terminal_test: " << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_mc_hostile_move_relay_terminal_test: all tests passed." << std::endl;
	return 0;
}