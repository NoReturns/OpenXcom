/*
 * M1 MC-03A — MC directed fire relay terminal certification.
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/ProjectileFlyBState.h"
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
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/Unit.h"
#include "../Savegame/Base.h"
#include "../Savegame/BattleItem.h"
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
const int kMaxFireTicks = 250000;
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

std::string defaultDataDir()
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
		ds = new MapDataSet("mc_fire_relay_floor");
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
	unit->setDirection(4);
	expect(battle->setUnitPosition(unit, pos), "setUnitPosition");
	Tile *tile = battle->getTile(pos);
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
		game = new Game("MC fire relay terminal cert");
		State::setGamePtr(game);
		Options::updateMods();
		game->loadMods();
		campaign = new SavedGame();
		game->setSavedGame(campaign);
		campaign->ensureCompanionCampaignId();
		base = new Base(game->getMod());
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

	BattlescapeGame *battleGame() const
	{
		return battlescape != 0 ? battlescape->getBattleGame() : 0;
	}

	void initRelayEnv()
	{
		relayDir = certJoin(certExeDir(), "mc_fire_relay_terminal_cert");
		CERT_MKDIR(relayDir.c_str());
		snapshotPath = certJoin(relayDir, "tactical-relay-fixture.yaml");
		certWrite(snapshotPath, "tactical:\n  relay_fixture: mc_terminal_fire\n");
		CERT_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		CERT_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");
		resetRelayFiles();
	}

	void resetRelayFiles()
	{
		certRemove(certJoin(relayDir, "tactical-command.json"));
		certRemove(certJoin(relayDir, "tactical-result.json"));
		certRemove(certJoin(relayDir, "tactical-command-journal.json"));
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
		     << "\nenergy: 50\nmorale: 80\n";
		unit->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(unit);
		wireUnit(battle, unit, pos);
		unit->setActiveHand("STR_RIGHT_HAND");
		return unit;
	}

	BattleItem *mountStockPistol(BattleUnit *actor)
	{
		RuleItem *pistolRules = game->getMod()->getItem("STR_PISTOL", false);
		RuleItem *clipRules = game->getMod()->getItem("STR_PISTOL_CLIP", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(pistolRules != 0 && clipRules != 0 && hand != 0, "stock pistol rules");
		if (pistolRules == 0 || clipRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *pistol = new BattleItem(pistolRules, battle->getCurrentItemId());
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		expect(pistol->setAmmoItem(clip) == 0, "pistol loaded");
		pistol->setSlot(hand);
		pistol->moveToOwner(actor);
		battle->getItems()->push_back(pistol);
		battle->getItems()->push_back(clip);
		return pistol;
	}

	std::string mcFireCmd(const char *id, const char *commandType, int tx, int ty, int tz, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"" << commandType << "\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		  << "  \"target\": {\"x\": " << tx << ", \"y\": " << ty << ", \"z\": " << tz << "},\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
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

	int advanceUntilTerminal(bool stepBattle)
	{
		BattlescapeGame *bg = battleGame();
		int ticks = 0;
		while (ticks < kMaxFireTicks)
		{
			if (stepBattle && bg != 0 && bg->isBusy())
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
		expect(ticks < kMaxFireTicks, "terminal within tick budget");
		return ticks;
	}

	void endCase()
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
};

CertHarness &harness()
{
	static CertHarness h;
	return h;
}

void certMcSnapFireTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(4, 4, 0), 80);
	expect(alien != 0, "MC shooter");
	BattleItem *pistol = h.mountStockPistol(alien);
	expect(pistol != 0, "pistol mounted");
	const int ammo0 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot for MC_SNAP_SHOT");
	const Position target(8, 4, 0);
	h.publish(h.mcFireCmd("cert-mc-snap-fire", "MC_SNAP_SHOT", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "MC_SNAP_SHOT EXECUTING");
	const int ticks = h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "MC_SNAP_SHOT COMPLETED");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_SNAP_SHOT", "result echoes MC_SNAP_SHOT");
	expect(alien->getTimeUnits() < tu0, "TU spent");
	if (pistol->getAmmoItem() != 0 && ammo0 >= 0)
	{
		expect(pistol->getAmmoItem()->getAmmoQuantity() < ammo0, "ammo consumed");
	}
	logEvidence("mc-snap-terminal", h.resultJson());
	logEvidence("mc-snap-engine", "tu=" + std::to_string(alien->getTimeUnits()) + " ammo="
		+ std::to_string(pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1)
		+ " ticks=" + std::to_string(ticks));
	h.endCase();
}

void certMcFireHumanRejected()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(2, 2, 0), 80);
	h.mountStockPistol(alien);
	const int tu0 = alien->getTimeUnits();
	h.writeControl(false);
	expect(h.exportBinding(), "snapshot human MC");
	h.publish(h.mcFireCmd("cert-mc-fire-human", "MC_SNAP_SHOT", 5, 2, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "human MC fire rejected");
	std::string code;
	CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &code);
	expect(code == "NOT_AI_CONTROLLED", "NOT_AI_CONTROLLED");
	expect(alien->getTimeUnits() == tu0, "no TU on reject");
	h.endCase();
}

} // namespace

int main(int argc, char **argv)
{
	CertHarness &h = harness();
	if (!h.initGame(argc > 0 ? argv[0] : "companion_mc_hostile_fire_relay_terminal_test", defaultDataDir()))
	{
		std::cerr << "mc fire relay terminal cert: init failed" << std::endl;
		return 1;
	}
	h.initRelayEnv();
	certMcFireHumanRejected();
	certMcSnapFireTerminal();
	if (g_failures != 0)
	{
		std::cerr << "companion_mc_hostile_fire_relay_terminal_test: " << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_mc_hostile_fire_relay_terminal_test: all tests passed." << std::endl;
	return 0;
}