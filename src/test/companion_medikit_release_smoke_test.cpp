/*
 * M1-WP9 — one-shot Release smoke: production ruleset relay (co-tile HEAL + adjacent mis-aim reject).
 * Manual / CI optional target — NOT part of companion_medikit_engine_test default gates.
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/TileEngine.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../Engine/Exception.h"
#include "../Engine/CompanionJsonLite.h"
#include "../Engine/CompanionMedikitExecutionCore.h"
#include "../Engine/CompanionSnapshot.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/State.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleResearch.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/Base.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define SMOKE_MKDIR(path) _mkdir(path)
#define SMOKE_PUTENV(name, value) _putenv_s(name, value)
#else
#include <sys/stat.h>
#include <unistd.h>
#define SMOKE_MKDIR(path) mkdir(path, 0755)
#define SMOKE_PUTENV(name, value) setenv(name, value, 1)
#endif

#ifdef main
#undef main
#endif

using namespace OpenXcom;

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

static std::string defaultDataDir(const char *argv0)
{
	(void)argv0;
	const char *candidates[] = {
		"E:/Open X-Com Companion/runtime/m0/bin",
		"../../../../runtime/m0/bin",
		"../../../runtime/m0/bin",
		"standard",
		"../standard",
		"../../standard",
	};
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
	{
		const std::string pck = std::string(candidates[i]) + "/UFO/TERRAIN/BLANKS.PCK";
		const std::string rules = std::string(candidates[i]) + "/standard/xcom1/items.rul";
		std::ifstream pckIn(pck.c_str());
		std::ifstream rulesIn(rules.c_str());
		if (pckIn && rulesIn)
		{
			return candidates[i];
		}
	}
	return "standard";
}

static void smokeWriteFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static std::string smokeReadFile(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void smokeRemoveIfPresent(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string smokeJoin(const std::string &dir, const char *name)
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
	if (dir.back() == '/' || dir.back() == '\\')
	{
		return dir + name;
	}
	return dir + sep + name;
}

#ifdef _WIN32
static std::string smokeExeDir()
{
	char path[MAX_PATH];
	const DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return ".";
	}
	std::string exePath(path, length);
	const std::string::size_type slash = exePath.find_last_of("\\/");
	return slash == std::string::npos ? std::string(".") : exePath.substr(0, slash);
}
#else
static std::string smokeExeDir()
{
	return ".";
}
#endif

static const int kSmokeMedicX = 1;
static const int kSmokeMedicY = 1;
static const int kSmokePatientX = 2;
static const int kSmokePatientY = 1;
static const int kSmokeFloorZ = 0;

static MapData *makeReleaseSmokeOpenFloor(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static MapDataSet *releaseSmokeOpenFloorMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("wp9_release_smoke_open_floor");
	}
	return ds;
}

/** Guided-launch parity: flat open floor when homogenous ruleset MCD grids lack medikit melee LOS. */
static void wireReleaseSmokeOpenFloor(Mod *mod, SavedBattleGame *battle, int x0, int y0, int x1, int y1, int z)
{
	MapDataSet *ds = releaseSmokeOpenFloorMapDataSet();
	std::vector<MapDataSet *> *sets = battle->getMapDataSets();
	sets->clear();
	sets->push_back(ds);
	const int mdsId = 0;
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
			tile->setMapData(makeReleaseSmokeOpenFloor(ds), 1, mdsId, O_FLOOR);
		}
	}
	battle->initUtilities(mod);
	if (battle->getTileEngine() != 0)
	{
		battle->getTileEngine()->recalculateFOV();
	}
}

/** Informational — homogenous DESERT grids are not used as the release smoke fixture. */
static void logDesertHomogeneousGridAdjacentNote()
{
	std::cerr << "wp9 release smoke: co-tile HEAL uses open-floor at (" << kSmokeMedicX << "," << kSmokeMedicY
	          << "); adjacent mis-aim at (" << kSmokeMedicX << "," << kSmokeMedicY << ")/(" << kSmokePatientX
	          << "," << kSmokePatientY << ") — homogenous DESERT MCD pads excluded" << std::endl;
}

static void wireUnitOnTile(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "unit tile exists");
	if (tile == 0)
	{
		return;
	}
	unit->setPosition(pos);
	battle->setUnitPosition(unit, pos);
	Tile *below = pos.z > 0 ? battle->getTile(Position(pos.x, pos.y, pos.z - 1)) : 0;
	unit->setTile(tile, below);
}

static BattleItem *equipProductionMedikit(Mod *mod, SavedBattleGame *battle, BattleUnit *owner)
{
	RuleItem *rules = mod->getItem("STR_MEDI_KIT", true);
	RuleInventory *hand = mod->getInventory("STR_RIGHT_HAND", true);
	expect(rules != 0 && hand != 0, "STR_MEDI_KIT and STR_RIGHT_HAND in ruleset");
	if (rules == 0 || hand == 0)
	{
		return 0;
	}
	BattleItem *mk = new BattleItem(rules, battle->getCurrentItemId());
	mk->setSlot(hand);
	mk->setSlotX(0);
	mk->setSlotY(0);
	mk->moveToOwner(owner);
	battle->getItems()->push_back(mk);
	return mk;
}

static void configureUnconsciousPatient(BattleUnit *patient)
{
	YAML::Node node = YAML::Load(
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\n"
		"armor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	patient->load(node);
}

struct ProductionSmokeHarness
{
	std::unique_ptr<Game> game;
	SavedGame *campaign;
	Base *base;
	SavedBattleGame *battle;
	std::unique_ptr<BattlescapeGame> bg;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	BattleUnit *medic;
	BattleUnit *patient;
	BattleItem *medikit;
	int medikitBattleItemId;
	std::string bindingSha256;
	int bindingStateVersion;

	ProductionSmokeHarness()
		: campaign(0),
		  base(0),
		  battle(0),
		  medic(0),
		  patient(0),
		  medikit(0),
		  medikitBattleItemId(0),
		  bindingStateVersion(0)
	{
	}

	void resetRelayArtifacts()
	{
		smokeRemoveIfPresent(smokeJoin(relayDir, "tactical-command.json"));
		smokeRemoveIfPresent(smokeJoin(relayDir, "tactical-result.json"));
		smokeRemoveIfPresent(smokeJoin(relayDir, "tactical-command-journal.json"));
		smokeRemoveIfPresent(smokeJoin(relayDir, "tactical-command-conflict.json"));
		smokeRemoveIfPresent(smokeJoin(relayDir, "companion-tactical-publication-confirm.json"));
	}

	void initRelayEnv()
	{
		relayDir = smokeJoin(smokeExeDir(), "wp9_medikit_release_smoke");
		SMOKE_MKDIR(relayDir.c_str());
		resetRelayArtifacts();
		snapshotPath = smokeJoin(relayDir, "tactical-relay-fixture.yaml");
		smokeWriteFile(snapshotPath, "tactical:\n  relay_fixture: wp9_release_smoke\n");
		SMOKE_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		SMOKE_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");
		sessionId = battle->getCompanionEventLog().getSessionId();
		const std::string controlJson =
			"{\n"
			"  \"schema_version\": 1,\n"
			"  \"session_id\": \"" + sessionId + "\",\n"
			"  \"assignments\": { \"1\": \"ai\" }\n"
			"}\n";
		smokeWriteFile(smokeJoin(relayDir, "companion-soldier-control.json"), controlJson);
		bg.reset(new BattlescapeGame(battle, 0));
	}

	bool exportSnapshotBinding()
	{
		bindingSha256.clear();
		if (!CompanionSnapshot::tryExport(battle, &bindingSha256))
		{
			return false;
		}
		bindingStateVersion = battle->getCompanionEventLog().getLatestSeq();
		if (bindingSha256.empty())
		{
			return false;
		}
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": " << battle->getTurn() << ",\n"
		     << "  \"state_version\": " << bindingStateVersion << ",\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"status\": \"published\",\n"
		     << "  \"source\": \"wp9_release_smoke\",\n"
		     << "  \"confirmed_at_ms\": 0\n"
		     << "}\n";
		smokeWriteFile(smokeJoin(relayDir, "companion-tactical-publication-confirm.json"), json.str());
		return true;
	}

	std::string medikitCommandJson(
		const std::string &commandId,
		const std::string &commandType,
		int targetUnitId,
		int bodyPart) const
	{
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"" << commandType << "\",\n"
		     << "  \"actor_unit_id\": 1,\n"
		     << "  \"target_unit_id\": " << targetUnitId << ",\n"
		     << "  \"medikit_battle_item_id\": " << medikitBattleItemId << ",\n"
		     << "  \"active_hand\": \"STR_RIGHT_HAND\",\n";
		if (commandType == "HEAL" && bodyPart >= 0)
		{
			json << "  \"body_part\": " << bodyPart << ",\n";
		}
		json << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << bindingStateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	void publishInbox(const std::string &commandJson)
	{
		smokeWriteFile(smokeJoin(relayDir, "tactical-command.json"), commandJson);
#ifdef _WIN32
		Sleep(310);
#endif
	}

	bool publishMedikit(const std::string &commandId, const std::string &type, int targetId, int bodyPart)
	{
		if (!exportSnapshotBinding())
		{
			return false;
		}
		publishInbox(medikitCommandJson(commandId, type, targetId, bodyPart));
		return true;
	}

	void tickRelay()
	{
		bg->tickCompanionCommand(true);
	}

	std::string resultJson() const
	{
		return smokeReadFile(smokeJoin(relayDir, "tactical-result.json"));
	}

	std::string journalJson() const
	{
		return smokeReadFile(smokeJoin(relayDir, "tactical-command-journal.json"));
	}

	bool resultStatusIs(const std::string &expected) const
	{
		std::string status;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "status", &status)
		    && status == expected;
	}
};

static bool initProductionGame(const char *argv0, const std::string &dataDir, ProductionSmokeHarness *h)
{
	Options::mute = true;
	std::vector<char *> fakeArgv;
	fakeArgv.push_back(const_cast<char *>(argv0));
	fakeArgv.push_back(const_cast<char *>("-data"));
	std::string dataArg = dataDir;
	fakeArgv.push_back(const_cast<char *>(dataArg.c_str()));
	const int fakeArgc = static_cast<int>(fakeArgv.size());
	if (!Options::init(fakeArgc, fakeArgv.data()))
	{
		std::cerr << "Options::init failed" << std::endl;
		return false;
	}
	h->game.reset(new Game("WP9 medikit release smoke"));
	State::setGamePtr(h->game.get());
	Options::updateMods();
	h->game->loadMods();
	Mod *mod = h->game->getMod();

	h->campaign = new SavedGame();
	h->campaign->ensureCompanionCampaignId();
	h->game->setSavedGame(h->campaign);
	h->base = new Base(mod);
	h->campaign->getBases()->push_back(h->base);

	RuleSoldier *rs = mod->getSoldier("STR_SOLDIER", true);
	Armor *ar = mod->getArmor("STR_PERSONAL_ARMOR_UC", false);
	if (ar == 0)
	{
		ar = mod->getArmor("STR_POWER_SUIT_UC", false);
	}
	if (ar == 0)
	{
		ar = mod->getArmor("STR_JUMPSUIT", false);
	}
	expect(rs != 0 && ar != 0, "production soldier armor rules");
	if (rs == 0 || ar == 0)
	{
		return false;
	}

	RuleResearch *mediKit = mod->getResearch("STR_MEDI_KIT", false);
	if (mediKit != 0)
	{
		h->campaign->addFinishedResearchSimple(mediKit);
	}

	Soldier *medicSoldier = new Soldier(rs, ar, 1);
	Soldier *patientSoldier = new Soldier(rs, ar, 2);
	h->base->getSoldiers()->push_back(medicSoldier);
	h->base->getSoldiers()->push_back(patientSoldier);

	h->battle = new SavedBattleGame();
	h->campaign->setBattleGame(h->battle);
	h->battle->setTestMod(mod);
	h->battle->initMap(6, 6, 1);
	h->battle->initUtilities(mod);
	h->battle->setGeoscapeSaveHint(h->campaign);
	h->battle->ensureCompanionTacticalOwnershipInitialized(h->campaign);
	h->battle->getCompanionEventLog().beginSession();
	h->battle->assignCompanionTacticalSoldierToAi(1);
	*h->battle->getCurrentItemId() = 700;

	h->medic = new BattleUnit(medicSoldier, 0);
	h->patient = new BattleUnit(patientSoldier, 0);
	h->battle->getUnits()->push_back(h->medic);
	h->battle->getUnits()->push_back(h->patient);
	h->medic->setTimeUnits(100);
	configureUnconsciousPatient(h->patient);
	logDesertHomogeneousGridAdjacentNote();
	wireReleaseSmokeOpenFloor(mod, h->battle, 0, 5, 0, 5, kSmokeFloorZ);
	const Position coTile(kSmokeMedicX, kSmokeMedicY, kSmokeFloorZ);
	h->medic->setDirection(2);
	wireUnitOnTile(h->battle, h->medic, coTile);
	wireUnitOnTile(h->battle, h->patient, coTile);

	h->medikit = equipProductionMedikit(mod, h->battle, h->medic);
	if (h->medikit != 0)
	{
		h->medikitBattleItemId = h->medikit->getId();
	}

	h->initRelayEnv();
	return true;
}

static void runProductionCoTileHealSmoke(ProductionSmokeHarness *h)
{
	BattleUnit *nativeTarget = CompanionMedikitExecutionCore::resolveNativeMedikitTarget(h->battle, h->medic);
	expect(nativeTarget != 0 && nativeTarget->getId() == 2, "production co-tile native target id 2");

	const int chargesBefore = h->medikit->getHealQuantity();
	const int tuBefore = h->medic->getTimeUnits();
	const int woundBefore = h->patient->getFatalWound(1);
	expect(chargesBefore > 0, "medikit has heal charges");
	expect(woundBefore > 0, "patient has torso wound");

	expect(h->publishMedikit("wp9-smoke-adj-east", "HEAL", 2, 1), "release smoke snapshot export");
	h->tickRelay();

	expect(h->resultStatusIs("COMPLETED"), "release smoke co-tile HEAL COMPLETED");
	expect(h->journalJson().find("wp9-smoke-adj-east") != std::string::npos, "journal lists smoke command");
	int targetId = 0;
	expect(CompanionJsonLite::extractJsonIntField(h->resultJson(), "target_unit_id", &targetId)
	    && targetId == 2,
		"terminal target_unit_id 2");
	expect(h->patient->getFatalWound(1) == 0, "wound healed");
	expect(h->medikit->getHealQuantity() == chargesBefore - 1, "charge spent");
	expect(h->medic->getTimeUnits() == tuBefore - 10, "TU spent");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h->resultJson(), "tu_spent", &tuSpent) && tuSpent == 10,
		"tu_spent in terminal json");
}

static void runAdjacentMisAimSmoke(ProductionSmokeHarness *h)
{
	h->resetRelayArtifacts();
	configureUnconsciousPatient(h->patient);
	wireUnitOnTile(h->battle, h->medic, Position(kSmokeMedicX, kSmokeMedicY, kSmokeFloorZ));
	wireUnitOnTile(h->battle, h->patient, Position(kSmokePatientX, kSmokePatientY, kSmokeFloorZ));
	h->medic->setDirection(3);
	const int woundBefore = h->patient->getFatalWound(1);
	const int chargesBefore = h->medikit->getHealQuantity();
	const int tuBefore = h->medic->getTimeUnits();
	expect(h->publishMedikit("wp9-smoke-adj-mis", "HEAL", 2, 1), "mis-aim smoke export");
	h->tickRelay();
	expect(!h->resultStatusIs("COMPLETED"), "mis-aim not COMPLETED");
	expect(h->patient->getFatalWound(1) == woundBefore, "mis-aim leaves wound");
	expect(h->medikit->getHealQuantity() == chargesBefore, "mis-aim leaves charges");
	expect(h->medic->getTimeUnits() == tuBefore, "mis-aim leaves TU");
}

static int runSmokeBody(int argc, char **argv)
{
	const std::string dataDir = argc > 1 ? argv[1] : defaultDataDir(argv[0]);
	std::cerr << "wp9 release smoke: data dir " << dataDir << std::endl;
	ProductionSmokeHarness harness;
	if (!initProductionGame(argv[0], dataDir, &harness))
	{
		return 2;
	}
	runProductionCoTileHealSmoke(&harness);
	runAdjacentMisAimSmoke(&harness);

	if (g_failures == 0)
	{
		std::cout << "WP9 medikit release smoke: PASS" << std::endl;
		return 0;
	}
	std::cerr << g_failures << " smoke failure(s)." << std::endl;
	return 1;
}

} // namespace

int main(int argc, char **argv)
{
	try
	{
		return runSmokeBody(argc, argv);
	}
	catch (const Exception &e)
	{
		std::cerr << "OpenXcom exception: " << e.what() << std::endl;
		return 99;
	}
	catch (const std::exception &e)
	{
		std::cerr << "std::exception: " << e.what() << std::endl;
		return 99;
	}
	catch (...)
	{
		std::cerr << "unknown exception" << std::endl;
		return 99;
	}
}