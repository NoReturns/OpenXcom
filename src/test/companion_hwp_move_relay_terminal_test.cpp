/*
 * ASTER-M1-HWP-S3B-03A — HWP_MOVE relay terminal walk certification.
 * Real BattlescapeState + UnitWalkBState + production CompanionCommandRuntime inbox.
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

const int HWP_ID = 1'000'000;
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
		ds = new MapDataSet("hwp_relay_terminal_floor");
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

	Map *map() const
	{
		return battlescape != 0 ? battlescape->getMap() : 0;
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
		game = new Game("HWP relay terminal cert");
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
		relayDir = certJoin(certExeDir(), "hwp_relay_terminal_cert");
		CERT_MKDIR(relayDir.c_str());
		snapshotPath = certJoin(relayDir, "tactical-relay-fixture.yaml");
		certWrite(snapshotPath, "tactical:\n  relay_fixture: hwp_terminal_s3b03a\n");
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

	void writeControl(bool hwpAi, bool soldierAi)
	{
		std::set<int> aiSoldiers;
		std::vector<int> liveSoldiers;
		std::set<int> aiHwps;
		std::vector<int> liveHwps;
		if (soldierAi)
		{
			aiSoldiers.insert(1);
			liveSoldiers.push_back(1);
			battle->assignCompanionTacticalSoldierToAi(1);
		}
		else
		{
			battle->reclaimCompanionTacticalSoldierForHuman(1);
		}
		if (hwpAi)
		{
			aiHwps.insert(HWP_ID);
			liveHwps.push_back(HWP_ID);
			battle->assignCompanionTacticalHwpToAi(HWP_ID);
		}
		else
		{
			battle->reclaimCompanionTacticalHwpForHuman(HWP_ID);
		}
		const ControlPublicationPlan plan = buildControlPublicationJson(
			sessionId,
			campaign->getCompanionCampaignId(),
			battle->getCompanionTacticalBattleId(),
			aiSoldiers,
			liveSoldiers,
			aiHwps,
			liveHwps);
		expect(plan.ok, "control json");
		if (plan.ok)
		{
			certWrite(certJoin(relayDir, "companion-soldier-control.json"), plan.jsonUtf8);
		}
	}

	BattleUnit *addTank(const Position &pos, int tu)
	{
		Unit *rules = game->getMod()->getUnit("STR_TANK_CANNON", false);
		Armor *armor = game->getMod()->getArmor("TANK_ARMOR", false);
		expect(rules != 0 && armor != 0, "stock 2x2 HWP rules");
		expect(armor != 0 && armor->getSize() == 2, "2x2 HWP armor");
		if (rules == 0 || armor == 0)
		{
			return 0;
		}
		StatAdjustment adj;
		BattleUnit *unit = new BattleUnit(rules, FACTION_PLAYER, HWP_ID, armor, &adj, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n";
		unit->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(unit);
		wireUnit(battle, unit, pos);
		return unit;
	}

	BattleUnit *addSoldier(const Position &pos, int tu)
	{
		RuleSoldier *rs = game->getMod()->getSoldier("STR_SOLDIER", false);
		Armor *ar = game->getMod()->getArmor("STR_POWER_SUIT_UC", false);
		if (ar == 0)
		{
			ar = game->getMod()->getArmor("STR_JUMPSUIT", false);
		}
		if (rs == 0 || ar == 0)
		{
			return 0;
		}
		Soldier *soldier = new Soldier(rs, ar, 1);
		base->getSoldiers()->push_back(soldier);
		BattleUnit *unit = new BattleUnit(soldier, 0);
		unit->setTimeUnits(tu);
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

	std::string hwpMoveCmd(const char *id, int dx, int dy, int dz, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"HWP_MOVE\",\n  \"actor_unit_id\": " << HWP_ID << ",\n"
		  << "  \"destination\": {\"x\": " << dx << ", \"y\": " << dy << ", \"z\": " << dz << "},\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": 1,\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string soldierMoveCmd(const char *id, int dx, int dy, int dz, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MOVE\",\n  \"actor_unit_id\": 1,\n"
		  << "  \"destination\": {\"x\": " << dx << ", \"y\": " << dy << ", \"z\": " << dz << "},\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": 1,\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
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

void certTerminalWalk()
{
	CertHarness &h = harness();
	h.setupBattle(10, 10);
	h.resetRelayFiles();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80);
	h.writeControl(true, false);
	const Position dest(3, 1, 0);
	const int tuBefore = tank->getTimeUnits();
	const int dirBefore = tank->getDirection();
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpMoveCmd("cert-terminal-walk", dest.x, dest.y, dest.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "accepted EXECUTING");
	expect(h.battleGame()->isBusy(), "busy while walking");
	h.tickRelay();
	expect(h.status() != "COMPLETED", "no premature COMPLETED");
	const int ticks = h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "terminal COMPLETED");
	expect(!h.battleGame()->isBusy(), "idle after walk");
	expect(!CompanionCommandRuntime::isMoveInFlight(), "active command cleared");
	expect(tank->getPosition() == dest, "engine at destination");
	int rem = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &rem), "remaining_tu");
	expect(rem == tank->getTimeUnits(), "remaining_tu matches engine");
	int fx = 0, fy = 0, fz = 0;
	expect(h.parseFinalPos(h.resultJson(), &fx, &fy, &fz), "final_position in result");
	expect(fx == tank->getPosition().x && fy == tank->getPosition().y && fz == tank->getPosition().z,
		"final_position matches engine");
	expect(tank->getTimeUnits() < tuBefore, "TU spent");
	expect(h.resultJson().find("\"destination_reached\": true") != std::string::npos, "destination_reached");
	int relayDir = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_direction", &relayDir),
		"final_direction in COMPLETED");
	expect(relayDir == tank->getDirection(), "final_direction matches engine");
	expect(relayDir == 2, "completed walk final_direction 2");
	logEvidence("terminal-walk", h.resultJson());
	logEvidence("terminal-walk-engine", "pos=" + std::to_string(tank->getPosition().x) + "," +
		std::to_string(tank->getPosition().y) + " tu=" + std::to_string(tank->getTimeUnits()) +
		" dir=" + std::to_string(tank->getDirection()) + " dirBefore=" + std::to_string(dirBefore) +
		" ticks=" + std::to_string(ticks));
	h.endWalkCase();
}

void certInterruptedWalk()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80);
	h.writeControl(true, false);
	const Position dest(9, 1, 0);
	const int tuBefore = tank->getTimeUnits();
	const Position startPos = tank->getPosition();
	expect(h.exportBinding(), "snapshot interrupt");
	h.publish(h.hwpMoveCmd("cert-interrupt", dest.x, dest.y, dest.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "EXECUTING before interrupt");
	BattlescapeGame *bg = h.battleGame();
	int steps = 0;
	while (steps < 80 && bg->isBusy() && tank->getPosition().x < 4)
	{
		bg->handleState();
		h.tickRelay();
		++steps;
	}
	expect(tank->getPosition() != startPos, "partial movement before cancel");
	bg->cancelCurrentAction();
	h.advanceUntilTerminal(true);
	const std::string st = h.status();
	expect(st == "INTERRUPTED" || st == "FAILED", "interrupt terminal status");
	expect(tank->getPosition() != dest, "no false arrival");
	expect(h.resultJson().find("\"destination_reached\": true") == std::string::npos,
		"destination_reached not true");
	expect(tank->getTimeUnits() <= tuBefore, "TU not invented");
	int rem = -1;
	if (CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &rem))
	{
		expect(rem == tank->getTimeUnits(), "remaining_tu on interrupt");
	}
	int relayDir = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_direction", &relayDir),
		"final_direction on INTERRUPTED");
	expect(relayDir == tank->getDirection(), "interrupt final_direction matches engine");
	logEvidence("interrupt", h.resultJson());
	logEvidence("interrupt-engine", "pos=" + std::to_string(tank->getPosition().x) + "," +
		std::to_string(tank->getPosition().y) + " tu=" + std::to_string(tank->getTimeUnits()));
	h.endWalkCase();
}

void certRelayHygieneAfterTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(10, 10);
	h.resetRelayFiles();
	h.addTank(Position(1, 1, 0), 80);
	h.writeControl(true, false);
	expect(h.exportBinding(), "snapshot hygiene");
	h.publish(h.hwpMoveCmd("cert-hygiene-1", 2, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	h.advanceUntilTerminal(true);
	const std::string terminal = h.resultJson();
	expect(!CompanionCommandRuntime::isMoveInFlight(), "cleared after terminal");
	for (int i = 0; i < 8; ++i)
	{
		h.tickRelay();
	}
	expect(h.resultJson() == terminal, "single terminal publication");
	expect(h.exportBinding(), "re-export binding for second command");
	h.publish(h.hwpMoveCmd("cert-hygiene-2", 3, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING" || h.status() == "COMPLETED", "second command accepted");
	logEvidence("hygiene-second", "status=" + h.status());
	h.endWalkCase();
}

void certDuplicateAndStale()
{
	CertHarness &h = harness();
	h.setupBattle(10, 10);
	h.resetRelayFiles();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80);
	h.writeControl(true, false);
	expect(h.exportBinding(), "snapshot dup");
	const std::string cmd = h.hwpMoveCmd("cert-dup", 2, 1, 0, h.bindingStateVersion);
	h.publish(cmd);
	h.tickRelay();
	h.advanceUntilTerminal(true);
	const int tuAfter = tank->getTimeUnits();
	const Position posAfter = tank->getPosition();
	const std::string r0 = h.resultJson();
	CERT_SLEEP_MS(320);
	h.publish(cmd);
	h.tickRelay();
	expect(tank->getTimeUnits() == tuAfter, "duplicate no second TU");
	expect(tank->getPosition() == posAfter, "duplicate no second move");
	expect(h.resultJson() == r0, "duplicate preserves result");

	h.resetRelayFiles();
	h.publish(h.hwpMoveCmd("cert-stale", 2, 1, 0, h.bindingStateVersion + 99));
	h.tickRelay();
	expect(h.status() == "REJECTED", "stale rejected");
	h.endWalkCase();
}

void certHumanAndUnsupported()
{
	CertHarness &h = harness();
	h.setupBattle(10, 10);
	h.resetRelayFiles();
	BattleUnit *tank = h.addTank(Position(1, 1, 0), 80);
	const int tu0 = tank != 0 ? tank->getTimeUnits() : 0;
	h.writeControl(false, false);
	expect(h.exportBinding(), "snapshot human");
	h.publish(h.hwpMoveCmd("cert-human", 2, 1, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "human HWP rejected");
	expect(tank->getTimeUnits() == tu0, "no TU loss on reject");

	for (const char *type : { "HWP_FIRE", "HWP_TURN" })
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"cert-" << type << "\",\n"
		  << "  \"command_type\": \"" << type << "\",\n  \"actor_unit_id\": " << HWP_ID << ",\n"
		  << "  \"snapshot_sha256\": \"" << h.bindingSha256 << "\",\n  \"session_id\": \"" << h.sessionId
		  << "\",\n  \"turn\": 1,\n  \"state_version\": " << h.bindingStateVersion << "\n}\n";
		h.publish(j.str());
		h.tickRelay();
		std::string code;
		CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &code);
		if (std::string(type) == "HWP_FIRE")
		{
			expect(h.status() == "REJECTED" && code == "HWP_EXECUTION_NOT_SUPPORTED", "HWP_FIRE gate off");
		}
		else
		{
			expect(h.status() == "REJECTED" && code == "INVALID_TARGET", "HWP_TURN malformed target");
		}
	}
}

void certSoldierMoveRegression()
{
	CertHarness &h = harness();
	h.setupBattle(10, 10);
	h.resetRelayFiles();
	BattleUnit *soldier = h.addSoldier(Position(2, 2, 0), 60);
	expect(soldier != 0, "soldier exists");
	h.writeControl(false, true);
	expect(h.exportBinding(), "snapshot soldier");
	h.publish(h.soldierMoveCmd("cert-soldier-move", 3, 2, 0, h.bindingStateVersion));
	h.tickRelay();
	std::string ct;
	CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &ct);
	expect(h.status() == "EXECUTING" && ct == "MOVE", "soldier MOVE EXECUTING");
	h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "soldier MOVE COMPLETED");
	expect(soldier->getPosition() == Position(3, 2, 0), "soldier destination");
	expect(h.resultJson().find("\"final_direction\"") == std::string::npos,
		"soldier MOVE has no final_direction");
	h.endWalkCase();
}

} // namespace

int main(int argc, char **argv)
{
	CertHarness &h = harness();
	if (!h.initGame(argc > 0 ? argv[0] : "companion_hwp_move_relay_terminal_test", defaultDataDir()))
	{
		std::cerr << "hwp relay terminal cert: Options/Game init failed" << std::endl;
		return 1;
	}
	h.initRelayEnv();

	certHumanAndUnsupported();
	certDuplicateAndStale();
	certTerminalWalk();
	certInterruptedWalk();
	certRelayHygieneAfterTerminal();
	certSoldierMoveRegression();

	if (g_failures != 0)
	{
		std::cerr << "companion_hwp_move_relay_terminal_test: " << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_hwp_move_relay_terminal_test: all tests passed." << std::endl;
	return 0;
}