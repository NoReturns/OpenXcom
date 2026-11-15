/*
 * Open X-Com Companion — PSI_PANIC relay inbox E2E (M1-WP10 S3/S4). Included from companion_psi_panic_engine_test.cpp.
 */
#include "../Battlescape/Position.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Unit.h"
#include "../Savegame/Base.h"
#include "../Savegame/Tile.h"
#include "../Mod/RuleInventory.h"
#include "Engine/CompanionCommandRuntime.h"
#include "Engine/CompanionJsonLite.h"
#include "Engine/CompanionSnapshot.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define WP10_RELAY_MKDIR(path) _mkdir(path)
#define WP10_RELAY_PUTENV(name, value) _putenv_s(name, value)
#define WP10_RELAY_SLEEP_MS(ms) Sleep(ms)
#else
#include <sys/stat.h>
#include <unistd.h>
#define WP10_RELAY_MKDIR(path) mkdir(path, 0755)
#define WP10_RELAY_PUTENV(name, value) setenv(name, value, 1)
#define WP10_RELAY_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

namespace
{

static void wp10WriteFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static std::string wp10ReadFile(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void wp10RemoveIfPresent(const std::string &path)
{
	std::remove(path.c_str());
}

static MapData *wp10FloorMapData(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static MapDataSet *wp10HarnessFloorMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("wp10_relay_harness_floor");
	}
	return ds;
}

static RuleInventory *wp10RegisterGroundInventory(Mod &mod)
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

static void wp10WireUnitTile(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	tile->setMapData(wp10FloorMapData(wp10HarnessFloorMapDataSet()), 1, 0, O_FLOOR);
	unit->setPosition(pos);
	battle->setUnitPosition(unit, pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

static BattleUnit *wp10MakeVisibleHostile(
	SavedBattleGame &battle, int unitId, const Position &pos, int bravery, int psiStrength)
{
	static Unit alienRules("STR_SECTOID");
	static Armor alienArmor("STR_SECTOID_ARMOR");
	StatAdjustment adjustment;
	BattleUnit *hostile = new BattleUnit(&alienRules, FACTION_HOSTILE, unitId, &alienArmor, &adjustment, 0);
	hostile->setVisible(true);
	hostile->getBaseStats()->bravery = bravery;
	hostile->getBaseStats()->psiStrength = psiStrength;
	hostile->getBaseStats()->psiSkill = 0;
	battle.getUnits()->push_back(hostile);
	wireUnitTile(&battle, hostile, pos);
	return hostile;
}

static std::string wp10Join(const std::string &dir, const char *name)
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

static std::string wp10RelayExeDir()
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

struct Wp10SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	Wp10SoldierFixture(int id)
		: rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id)
	{
	}
};

struct Wp10RelayHarness
{
	Mod mod;
	SavedGame campaign;
	Base *base;
	std::vector<Wp10SoldierFixture *> soldierFixtures;
	SavedBattleGame battle;
	BattlescapeGame game;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	BattleUnit *actor;
	BattleUnit *hostile;
	BattleItem *psiAmp;
	int psiAmpBattleItemId;
	std::string bindingSha256;
	int bindingStateVersion;

	Wp10RelayHarness()
		: base(new Base(&mod)),
		  battle(),
		  game(&battle, 0),
		  actor(0),
		  hostile(0),
		  psiAmp(0),
		  psiAmpBattleItemId(0),
		  bindingStateVersion(0)
	{
		makeHandInventory(mod, "STR_LEFT_HAND");
		makeHandInventory(mod, "STR_RIGHT_HAND");
		wp10RegisterGroundInventory(mod);
		campaign.ensureCompanionCampaignId();
		campaign.getBases()->push_back(base);
		battle.setTestMod(&mod);
		battle.initMap(6, 6, 1);
		battle.initUtilities(&mod);
		for (int y = 0; y < 6; ++y)
		{
			for (int x = 0; x < 6; ++x)
			{
				wireFloorTile(&battle, Position(x, y, 0));
			}
		}
		*battle.getCurrentItemId() = 800;
		battle.setGeoscapeSaveHint(&campaign);
		battle.ensureCompanionTacticalOwnershipInitialized(&campaign);
		battle.getCompanionEventLog().beginSession();
		sessionId = battle.getCompanionEventLog().getSessionId();
		battle.assignCompanionTacticalSoldierToAi(1);

#ifndef WP10_RELAY_E2E_DIR
#define WP10_RELAY_E2E_DIR "wp10_psi_panic_relay_e2e"
#endif
		relayDir = wp10Join(wp10RelayExeDir(), WP10_RELAY_E2E_DIR);
		WP10_RELAY_MKDIR(relayDir.c_str());
		snapshotPath = wp10Join(relayDir, "tactical-relay-fixture.yaml");
		wp10WriteFile(snapshotPath, "tactical:\n  relay_fixture: wp10_s3\n");
		WP10_RELAY_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		WP10_RELAY_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");

		const std::string controlJson =
			"{\n"
			"  \"schema_version\": 1,\n"
			"  \"session_id\": \"" + sessionId + "\",\n"
			"  \"assignments\": { \"1\": \"ai\" }\n"
			"}\n";
		wp10WriteFile(wp10Join(relayDir, "companion-soldier-control.json"), controlJson);
	}

	void clearBattleUnits()
	{
		if (battle.getUnits() == 0)
		{
			return;
		}
		for (std::vector<BattleUnit *>::iterator i = battle.getUnits()->begin();
		     i != battle.getUnits()->end(); ++i)
		{
			delete *i;
		}
		battle.getUnits()->clear();
		actor = 0;
		hostile = 0;
		psiAmp = 0;
		psiAmpBattleItemId = 0;
	}

	Soldier *addRosterSoldier(int soldierId)
	{
		Wp10SoldierFixture *fixture = new Wp10SoldierFixture(soldierId);
		soldierFixtures.push_back(fixture);
		base->getSoldiers()->push_back(&fixture->soldier);
		return &fixture->soldier;
	}

	BattleUnit *addActor(int soldierId, const Position &pos, int tu, int psiSkill)
	{
		Soldier *soldier = addRosterSoldier(soldierId);
		BattleUnit *unit = new BattleUnit(soldier, 0);
		battle.getUnits()->push_back(unit);
		unit->setTimeUnits(tu);
		unit->getBaseStats()->psiSkill = psiSkill;
		if (psiSkill > 0)
		{
			unit->getBaseStats()->psiStrength = 90;
		}
		wireUnitTile(&battle, unit, pos);
		return unit;
	}

	void resetRelayArtifacts()
	{
		wp10RemoveIfPresent(wp10Join(relayDir, "tactical-command.json"));
		wp10RemoveIfPresent(wp10Join(relayDir, "tactical-result.json"));
		wp10RemoveIfPresent(wp10Join(relayDir, "tactical-command-journal.json"));
		wp10RemoveIfPresent(wp10Join(relayDir, "tactical-command-conflict.json"));
		wp10RemoveIfPresent(wp10Join(relayDir, "companion-tactical-publication-confirm.json"));
	}

	bool writePublicationConfirm()
	{
		if (bindingSha256.empty())
		{
			return false;
		}
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": " << battle.getTurn() << ",\n"
		     << "  \"state_version\": " << bindingStateVersion << ",\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"status\": \"published\",\n"
		     << "  \"source\": \"wp10_relay_e2e_harness\",\n"
		     << "  \"confirmed_at_ms\": 0\n"
		     << "}\n";
		wp10WriteFile(wp10Join(relayDir, "companion-tactical-publication-confirm.json"), json.str());
		return true;
	}

	bool exportSnapshotBinding()
	{
		bindingSha256.clear();
		if (!CompanionSnapshot::tryExport(&battle, &bindingSha256))
		{
			return false;
		}
		bindingStateVersion = battle.getCompanionEventLog().getLatestSeq();
		return !bindingSha256.empty() && writePublicationConfirm();
	}

	void resetScenario()
	{
		clearBattleUnits();
		if (base != 0 && base->getSoldiers() != 0)
		{
			base->getSoldiers()->clear();
		}
		for (std::vector<Wp10SoldierFixture *>::iterator i = soldierFixtures.begin();
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

	void setupPanicScenario(int actorTu, int actorPsiSkill, int hostileBravery, int hostilePsiStr)
	{
		clearBattleUnits();
		const Position actorPos(1, 1, 0);
		const Position hostilePos(3, 1, 0);
		actor = addActor(1, actorPos, actorTu, actorPsiSkill);
		hostile = wp10MakeVisibleHostile(battle, 100042, hostilePos, hostileBravery, hostilePsiStr);
		wireUnitTile(&battle, actor, actorPos);
		wireUnitTile(&battle, hostile, hostilePos);
		battle.setSelectedUnit(actor);
		psiAmp = equipPsiAmp(battle, mod, actor, "STR_LEFT_HAND");
		psiAmpBattleItemId = psiAmp->getId();
	}

	std::string psiPanicCommandJson(
		const std::string &commandId,
		int targetUnitId,
		int stateVersion,
		int actorUnitId = 1,
		int psiAmpItemId = -1,
		const char *activeHand = "STR_LEFT_HAND") const
	{
		const int ampId = psiAmpItemId >= 0 ? psiAmpItemId : psiAmpBattleItemId;
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"PSI_PANIC\",\n"
		     << "  \"actor_unit_id\": " << actorUnitId << ",\n"
		     << "  \"target_unit_id\": " << targetUnitId << ",\n"
		     << "  \"psi_amp_battle_item_id\": " << ampId << ",\n"
		     << "  \"active_hand\": \"" << activeHand << "\",\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << stateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	bool publishPanicInbox(const std::string &commandId, int targetUnitId, int stateVersionOverride)
	{
		if (!exportSnapshotBinding())
		{
			return false;
		}
		const int stateVersion = stateVersionOverride >= 0 ? stateVersionOverride : bindingStateVersion;
		publishInbox(psiPanicCommandJson(commandId, targetUnitId, stateVersion));
		return true;
	}

	std::string psiMindControlCommandJson(
		const std::string &commandId,
		int targetUnitId,
		int stateVersion,
		int actorUnitId = 1,
		int psiAmpItemId = -1,
		const char *activeHand = "STR_LEFT_HAND") const
	{
		const int ampId = psiAmpItemId >= 0 ? psiAmpItemId : psiAmpBattleItemId;
		std::ostringstream json;
		json << "{\n"
		     << "  \"schema_version\": 1,\n"
		     << "  \"command_id\": \"" << commandId << "\",\n"
		     << "  \"command_type\": \"PSI_MIND_CONTROL\",\n"
		     << "  \"actor_unit_id\": " << actorUnitId << ",\n"
		     << "  \"target_unit_id\": " << targetUnitId << ",\n"
		     << "  \"psi_amp_battle_item_id\": " << ampId << ",\n"
		     << "  \"active_hand\": \"" << activeHand << "\",\n"
		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << stateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	bool publishMindControlInbox(const std::string &commandId, int targetUnitId, int stateVersionOverride)
	{
		if (!exportSnapshotBinding())
		{
			return false;
		}
		const int stateVersion = stateVersionOverride >= 0 ? stateVersionOverride : bindingStateVersion;
		publishInbox(psiMindControlCommandJson(commandId, targetUnitId, stateVersion));
		return true;
	}

	void publishInbox(const std::string &commandJson)
	{
		wp10WriteFile(wp10Join(relayDir, "tactical-command.json"), commandJson);
		WP10_RELAY_SLEEP_MS(350);
	}

	void prepareRelayExecution()
	{
		if (actor != 0)
		{
			wireUnitTile(&battle, actor, actor->getPosition());
		}
		if (hostile != 0)
		{
			wireUnitTile(&battle, hostile, hostile->getPosition());
		}
	}

	void tickRelay()
	{
		prepareRelayExecution();
		game.tickCompanionCommand(true);
	}

	std::string resultJson() const
	{
		return wp10ReadFile(wp10Join(relayDir, "tactical-result.json"));
	}

	bool resultStatusIs(const std::string &expected) const
	{
		std::string status;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "status", &status)
		    && status == expected;
	}

	bool journalListsCommand(const std::string &commandId) const
	{
		return wp10ReadFile(wp10Join(relayDir, "tactical-command-journal.json")).find("\"" + commandId + "\"") !=
			std::string::npos;
	}

	bool resultHasNoPsychicLeak() const
	{
		const std::string &json = resultJson();
		return json.find("psychic") == std::string::npos
		    && json.find("Psychic") == std::string::npos
		    && json.find("morale") == std::string::npos
		    && json.find("psiStrength") == std::string::npos
		    && json.find("succeeded") == std::string::npos;
	}

	bool resultPublicReasonContains(const std::string &fragment) const
	{
		std::string reason;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "public_reason", &reason)
		    && reason.find(fragment) != std::string::npos;
	}
};

static Wp10RelayHarness &wp10RelayHarness()
{
	static Wp10RelayHarness *harness = new Wp10RelayHarness();
	return *harness;
}

static void runPsiRelayCompletedSuccess()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 90, 10, 5);
	RNG::setSeed(0xA11CE001ULL);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishPanicInbox("wp10-relay-panic-ok", 100042, -1), "panic success snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "panic success COMPLETED");
	expect(h.resultHasNoPsychicLeak(), "panic success result has no psychic leak");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent) && tuSpent == 25,
		"panic success tu_spent 25");
	expect(h.actor->getTimeUnits() == tuBefore - 25, "panic success TU deducted once");
}

static void runPsiRelayCompletedFailedRoll()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 50, 100, 100);
	h.actor->getBaseStats()->psiStrength = 1;
	h.actor->getBaseStats()->psiSkill = 1;
	RNG::setSeed(0ULL);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishPanicInbox("wp10-relay-panic-fail", 100042, -1), "panic failed roll snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "failed psychic roll still COMPLETED");
	expect(h.resultHasNoPsychicLeak(), "failed roll result has no psychic leak");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent) && tuSpent == 25,
		"failed roll still tu_spent");
	expect(h.actor->getTimeUnits() == tuBefore - 25, "failed roll TU consumed");
}

static void runPsiRelayInsufficientTuRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(20, 80, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishPanicInbox("wp10-relay-panic-tu", 100042, -1), "insufficient TU snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "insufficient TU REJECTED");
	expect(h.actor->getTimeUnits() == tuBefore, "insufficient TU no spend");
	expect(h.resultHasNoPsychicLeak(), "reject result has no psychic leak");
}

static void runPsiRelayWrongHandRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	expect(h.exportSnapshotBinding(), "wrong hand snapshot export");
	const int tuBefore = h.actor->getTimeUnits();
	h.publishInbox(h.psiPanicCommandJson("wp10-relay-hand", 100042, h.bindingStateVersion, 1, -1, "STR_RIGHT_HAND"));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "wrong hand REJECTED");
	expect(h.resultPublicReasonContains("specified hand"), "wrong hand public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "wrong hand no TU spend");
	expect(h.resultHasNoPsychicLeak(), "wrong hand no psychic leak");
}

static void runPsiRelayInvalidTargetRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishPanicInbox("wp10-relay-panic-bad-target", 999999, -1), "invalid target snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "invalid target REJECTED");
	expect(h.resultPublicReasonContains("not found"), "invalid target public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "invalid target no TU");
	expect(h.resultHasNoPsychicLeak(), "invalid target no psychic leak");
}

static void runPsiRelayInvalidActorRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	expect(h.exportSnapshotBinding(), "invalid actor snapshot export");
	const int tuBefore = h.actor->getTimeUnits();
	h.publishInbox(h.psiPanicCommandJson("wp10-relay-panic-bad-actor", 100042, h.bindingStateVersion, 99999));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "invalid actor REJECTED");
	expect(h.resultPublicReasonContains("Actor is not"), "invalid actor public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "invalid actor no TU");
}

static void runPsiRelayZeroPsiSkillRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 0, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishPanicInbox("wp10-relay-panic-zero-skill", 100042, -1), "zero psi skill snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "zero psi skill REJECTED");
	expect(h.resultPublicReasonContains("no psionic skill"), "zero psi skill public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "zero psi skill no TU");
}

static void runPsiRelayInvalidPsiAmpRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	expect(h.exportSnapshotBinding(), "invalid psi amp snapshot export");
	const int tuBefore = h.actor->getTimeUnits();
	h.publishInbox(h.psiPanicCommandJson("wp10-relay-panic-bad-amp", 100042, h.bindingStateVersion, 1, 1));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "invalid psi amp REJECTED");
	expect(h.resultPublicReasonContains("Psi-Amp battle item not found"), "invalid psi amp public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "invalid psi amp no TU");
}

static void runPsiRelayUnauthorizedActorRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	h.addActor(2, Position(2, 1, 0), 80, 80);
	expect(h.exportSnapshotBinding(), "unauthorized actor snapshot export");
	const int tuBefore = h.actor->getTimeUnits();
	h.publishInbox(h.psiPanicCommandJson("wp10-relay-panic-human", 100042, h.bindingStateVersion, 2));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "human actor REJECTED");
	expect(h.resultPublicReasonContains("human-controlled"), "unauthorized actor public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "unauthorized actor no TU on AI soldier");
}

static void runPsiRelayStaleBindingRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.exportSnapshotBinding(), "stale snapshot export");
	h.publishInbox(h.psiPanicCommandJson("wp10-relay-stale", 100042, 99));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "stale binding REJECTED");
	expect(h.actor->getTimeUnits() == tuBefore, "stale no TU spend");
	expect(h.journalListsCommand("wp10-relay-stale"), "stale journals command");
}

static void runPsiRelayHiddenHostileRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	h.hostile->setVisible(false);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishPanicInbox("wp10-relay-hidden", 100042, -1), "hidden hostile snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "hidden hostile REJECTED");
	expect(h.actor->getTimeUnits() == tuBefore, "hidden hostile no TU");
}

} // namespace

static void runPsiRelayDuplicateIdempotent()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 90, 10, 5);
	RNG::setSeed(0xA11CE001ULL);
	expect(h.publishPanicInbox("wp10-relay-dup", 100042, -1), "duplicate first snapshot export");
	const std::string firstCommandJson = wp10ReadFile(wp10Join(h.relayDir, "tactical-command.json"));
	const int tuBefore = h.actor->getTimeUnits();
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "duplicate first pass COMPLETED");
	const int tuAfterFirst = h.actor->getTimeUnits();
	const std::string firstResult = h.resultJson();
	WP10_RELAY_SLEEP_MS(310);
	h.publishInbox(firstCommandJson);
	h.tickRelay();
	expect(h.actor->getTimeUnits() == tuAfterFirst, "duplicate replay leaves actor TU");
	expect(h.actor->getTimeUnits() == tuBefore - 25, "duplicate still net one panic TU spend");
	expect(h.resultJson() == firstResult, "duplicate preserves terminal result json");
}

static void runPsiPanicRelayE2eSuite()
{
	runPsiRelayCompletedSuccess();
	runPsiRelayCompletedFailedRoll();
	runPsiRelayInsufficientTuRejected();
	runPsiRelayDuplicateIdempotent();
	runPsiRelayWrongHandRejected();
	runPsiRelayStaleBindingRejected();
	runPsiRelayHiddenHostileRejected();
	runPsiRelayInvalidTargetRejected();
	runPsiRelayInvalidActorRejected();
	runPsiRelayZeroPsiSkillRejected();
	runPsiRelayInvalidPsiAmpRejected();
	runPsiRelayUnauthorizedActorRejected();
}

static void runMcRelayCompletedSuccess()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 90, 10, 5);
	RNG::setSeed(0xA11CE001ULL);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishMindControlInbox("wp10-relay-mc-ok", 100042, -1), "mc success snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "mc success COMPLETED");
	expect(h.resultHasNoPsychicLeak(), "mc success result has no psychic leak");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent) && tuSpent == 25,
		"mc success tu_spent 25");
	expect(h.actor->getTimeUnits() == tuBefore - 25, "mc success TU deducted once");
	expect(h.hostile->getFaction() == FACTION_PLAYER, "mc success faction conversion");
	expect(h.hostile->getOriginalFaction() == FACTION_HOSTILE, "mc preserves original faction");
	expect(h.battle.getCompanionTacticalAiSoldierIds().find(100042) ==
		h.battle.getCompanionTacticalAiSoldierIds().end(), "mc alien not in companion AI soldiers");
}

static void runMcRelayCompletedFailedRoll()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 50, 100, 100);
	h.actor->getBaseStats()->psiStrength = 1;
	h.actor->getBaseStats()->psiSkill = 1;
	RNG::setSeed(0ULL);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishMindControlInbox("wp10-relay-mc-fail", 100042, -1), "mc failed roll snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "mc failed roll still COMPLETED");
	expect(h.resultHasNoPsychicLeak(), "mc failed roll no psychic leak");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent) && tuSpent == 25,
		"mc failed roll tu_spent");
	const UnitFaction hostileBefore = h.hostile->getFaction();
	expect(h.actor->getTimeUnits() == tuBefore - 25, "mc failed roll TU consumed");
	expect(h.hostile->getFaction() == hostileBefore, "mc failed roll leaves faction unchanged");
}

static void runMcRelayInsufficientTuRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(20, 80, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishMindControlInbox("wp10-relay-mc-tu", 100042, -1), "mc insufficient TU snapshot");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc insufficient TU REJECTED");
	expect(h.actor->getTimeUnits() == tuBefore, "mc insufficient TU no spend");
	expect(h.resultHasNoPsychicLeak(), "mc reject no psychic leak");
	expect(h.hostile->getFaction() == FACTION_HOSTILE, "mc insufficient TU leaves hostile faction");
}

static void runMcRelayWrongHandRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	expect(h.exportSnapshotBinding(), "mc wrong hand snapshot export");
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	h.publishInbox(h.psiMindControlCommandJson("wp10-relay-mc-hand", 100042, h.bindingStateVersion, 1, -1, "STR_RIGHT_HAND"));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc wrong hand REJECTED");
	expect(h.resultPublicReasonContains("specified hand"), "mc wrong hand public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc wrong hand no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc wrong hand no faction change");
	expect(h.resultHasNoPsychicLeak(), "mc wrong hand no psychic leak");
}

static void runMcRelayStaleBindingRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	expect(h.exportSnapshotBinding(), "mc stale snapshot export");
	h.publishInbox(h.psiMindControlCommandJson("wp10-relay-mc-stale", 100042, 99));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc stale binding REJECTED");
	expect(h.resultPublicReasonContains("State version"), "mc stale public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc stale no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc stale no faction change");
	expect(h.journalListsCommand("wp10-relay-mc-stale"), "mc stale journals command");
}

static void runMcRelayHiddenHostileRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	h.hostile->setVisible(false);
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	expect(h.publishMindControlInbox("wp10-relay-mc-hidden", 100042, -1), "mc hidden hostile snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc hidden hostile REJECTED");
	expect(h.resultPublicReasonContains("not visible"), "mc hidden public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc hidden hostile no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc hidden no faction change");
}

static void runMcRelayInvalidTargetRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	expect(h.publishMindControlInbox("wp10-relay-mc-bad-target", 999999, -1), "mc invalid target snapshot");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc invalid target REJECTED");
	expect(h.resultPublicReasonContains("not found"), "mc invalid target public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc invalid target no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc invalid target no faction change");
}

static void runMcRelayInvalidActorRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	expect(h.exportSnapshotBinding(), "mc invalid actor snapshot");
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	h.publishInbox(h.psiMindControlCommandJson("wp10-relay-mc-bad-actor", 100042, h.bindingStateVersion, 99999));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc invalid actor REJECTED");
	expect(h.resultPublicReasonContains("Actor is not"), "mc invalid actor public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc invalid actor no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc invalid actor no faction change");
}

static void runMcRelayZeroPsiSkillRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 0, 50, 20);
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	expect(h.publishMindControlInbox("wp10-relay-mc-zero-skill", 100042, -1), "mc zero skill snapshot");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc zero psi skill REJECTED");
	expect(h.resultPublicReasonContains("no psionic skill"), "mc zero skill public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc zero skill no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc zero skill no faction change");
}

static void runMcRelayInvalidPsiAmpRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	expect(h.exportSnapshotBinding(), "mc invalid psi amp snapshot");
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	h.publishInbox(h.psiMindControlCommandJson("wp10-relay-mc-bad-amp", 100042, h.bindingStateVersion, 1, 1));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc invalid psi amp REJECTED");
	expect(h.resultPublicReasonContains("Psi-Amp battle item not found"), "mc invalid psi amp public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc invalid psi amp no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc invalid psi amp no faction change");
}

static void runMcRelayUnauthorizedActorRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	h.addActor(2, Position(2, 1, 0), 80, 80);
	expect(h.exportSnapshotBinding(), "mc unauthorized actor snapshot");
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	h.publishInbox(h.psiMindControlCommandJson("wp10-relay-mc-human", 100042, h.bindingStateVersion, 2));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc human actor REJECTED");
	expect(h.resultPublicReasonContains("human-controlled"), "mc unauthorized public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc unauthorized no TU on AI actor");
	expect(h.hostile->getFaction() == hostileBefore, "mc unauthorized no faction change");
}

static void runMcRelayFriendlyTargetRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	BattleUnit *ally = h.addActor(2, Position(5, 1, 0), 80, 0);
	ally->setVisible(true);
	const int tuBefore = h.actor->getTimeUnits();
	const UnitFaction hostileBefore = h.hostile->getFaction();
	expect(h.publishMindControlInbox("wp10-relay-mc-friendly", 2, -1), "mc friendly target snapshot");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc friendly target REJECTED");
	expect(h.resultPublicReasonContains("not eligible"), "mc friendly target public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc friendly target no TU");
	expect(h.hostile->getFaction() == hostileBefore, "mc friendly target leaves hostile");
	expect(ally->getFaction() == FACTION_PLAYER, "mc friendly target unchanged");
}

static void runMcRelayProhibitedAlreadyControlledRejected()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 80, 50, 20);
	h.hostile->convertToFaction(FACTION_PLAYER);
	const int tuBefore = h.actor->getTimeUnits();
	expect(h.publishMindControlInbox("wp10-relay-mc-prohibited", 100042, -1), "mc prohibited snapshot");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "mc already controlled REJECTED");
	expect(h.resultPublicReasonContains("genuine hostile"), "mc prohibited target public_reason");
	expect(h.actor->getTimeUnits() == tuBefore, "mc prohibited no TU");
	expect(h.hostile->getFaction() == FACTION_PLAYER, "mc prohibited leaves converted faction");
}

static void runMcRelayDuplicateIdempotent()
{
	Wp10RelayHarness &h = wp10RelayHarness();
	h.resetScenario();
	h.setupPanicScenario(80, 90, 10, 5);
	RNG::setSeed(0xA11CE001ULL);
	expect(h.publishMindControlInbox("wp10-relay-mc-dup", 100042, -1), "mc duplicate first snapshot");
	const std::string firstCommandJson = wp10ReadFile(wp10Join(h.relayDir, "tactical-command.json"));
	const int tuBefore = h.actor->getTimeUnits();
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "mc duplicate first pass COMPLETED");
	const int tuAfterFirst = h.actor->getTimeUnits();
	const std::string firstResult = h.resultJson();
	WP10_RELAY_SLEEP_MS(310);
	h.publishInbox(firstCommandJson);
	h.tickRelay();
	expect(h.actor->getTimeUnits() == tuAfterFirst, "mc duplicate replay leaves actor TU");
	expect(h.actor->getTimeUnits() == tuBefore - 25, "mc duplicate net one MC TU spend");
	expect(h.resultJson() == firstResult, "mc duplicate preserves terminal result json");
}

static void runPsiMindControlRelayE2eSuite()
{
	runMcRelayCompletedSuccess();
	runMcRelayCompletedFailedRoll();
	runMcRelayInsufficientTuRejected();
	runMcRelayDuplicateIdempotent();
	runMcRelayWrongHandRejected();
	runMcRelayStaleBindingRejected();
	runMcRelayHiddenHostileRejected();
	runMcRelayInvalidTargetRejected();
	runMcRelayInvalidActorRejected();
	runMcRelayZeroPsiSkillRejected();
	runMcRelayInvalidPsiAmpRejected();
	runMcRelayUnauthorizedActorRejected();
	runMcRelayFriendlyTargetRejected();
	runMcRelayProhibitedAlreadyControlledRejected();
}

static void runPsiMindControlRelayE2eSuiteExtended()
{
	runPsiMindControlRelayE2eSuite();
}