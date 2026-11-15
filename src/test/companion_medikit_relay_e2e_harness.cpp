/*
 * Open X-Com Companion — medikit relay inbox E2E (M1-WP9 S4). Included from companion_medikit_engine_test.cpp.
 */
#include "../Mod/Unit.h"
#include "../Savegame/Base.h"
#include "Engine/CompanionCommandRuntime.h"
#include "Engine/CompanionJsonLite.h"
#include "Engine/CompanionMedikitExecutionCore.h"
#include "Engine/CompanionSnapshot.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define WP9_RELAY_MKDIR(path) _mkdir(path)
#define WP9_RELAY_PUTENV(name, value) _putenv_s(name, value)
#else
#include <sys/stat.h>
#include <unistd.h>
#define WP9_RELAY_MKDIR(path) mkdir(path, 0755)
#define WP9_RELAY_PUTENV(name, value) setenv(name, value, 1)
#endif

namespace
{

static void wp9WriteFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static std::string wp9ReadFile(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void wp9RemoveIfPresent(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string wp9Join(const std::string &dir, const char *name)
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

static RuleInventory *wp9RegisterGroundInventory(Mod &mod)
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

static std::string wp9RelayExeDir()
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

struct Wp9SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	Wp9SoldierFixture(int id)
		: rules("STR_SOLDIER"),
		  armor("STR_JUMPSUIT"),
		  soldier(&rules, &armor, id)
	{
	}
};

struct Wp9RelayHarness
{
	Mod mod;
	SavedGame campaign;
	Base *base;
	std::vector<Wp9SoldierFixture *> soldierFixtures;
	SavedBattleGame battle;
	BattlescapeGame game;
	std::string relayDir;
	std::string snapshotPath;
	std::string sessionId;
	BattleUnit *medic;
	BattleUnit *patient;
	BattleItem *medikit;
	int medikitBattleItemId;
	std::string bindingSha256;
	int bindingStateVersion;

	Wp9RelayHarness()
		: base(new Base(&mod)),
		  battle(),
		  game(&battle, 0),
		  medic(0),
		  patient(0),
		  medikit(0),
		  medikitBattleItemId(0),
		  bindingStateVersion(0)
	{
		makeHandInventory(mod, "STR_RIGHT_HAND");
		makeHandInventory(mod, "STR_LEFT_HAND");
		wp9RegisterGroundInventory(mod);
		campaign.ensureCompanionCampaignId();
		campaign.getBases()->push_back(base);
		battle.setTestMod(&mod);
		battle.initMap(6, 6, 1);
		battle.initUtilities(&mod);
		*battle.getCurrentItemId() = 700;
		battle.setGeoscapeSaveHint(&campaign);
		battle.ensureCompanionTacticalOwnershipInitialized(&campaign);
		battle.getCompanionEventLog().beginSession();
		sessionId = battle.getCompanionEventLog().getSessionId();
		battle.assignCompanionTacticalSoldierToAi(1);

		relayDir = wp9Join(wp9RelayExeDir(), "wp9_medikit_relay_e2e");
		WP9_RELAY_MKDIR(relayDir.c_str());
		snapshotPath = wp9Join(relayDir, "tactical-relay-fixture.yaml");
		wp9WriteFile(snapshotPath, "tactical:\n  relay_fixture: wp9_s4\n");
		WP9_RELAY_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		WP9_RELAY_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");

		const std::string controlJson =
			"{\n"
			"  \"schema_version\": 1,\n"
			"  \"session_id\": \"" + sessionId + "\",\n"
			"  \"assignments\": { \"1\": \"ai\" }\n"
			"}\n";
		wp9WriteFile(wp9Join(relayDir, "companion-soldier-control.json"), controlJson);
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
		medic = 0;
		patient = 0;
		medikit = 0;
		medikitBattleItemId = 0;
	}

	Soldier *addRosterSoldier(int soldierId)
	{
		Wp9SoldierFixture *fixture = new Wp9SoldierFixture(soldierId);
		soldierFixtures.push_back(fixture);
		base->getSoldiers()->push_back(&fixture->soldier);
		return &fixture->soldier;
	}

	BattleUnit *addMedic(int soldierId, const Position &pos, int tu)
	{
		Soldier *soldier = addRosterSoldier(soldierId);
		BattleUnit *unit = new BattleUnit(soldier, 0);
		battle.getUnits()->push_back(unit);
		unit->setTimeUnits(tu);
		wireUnitTile(&battle, unit, pos);
		return unit;
	}

	BattleUnit *addPatientFromYaml(int soldierId, const char *yamlExtra, const Position &pos)
	{
		Soldier *soldier = addRosterSoldier(soldierId);
		BattleUnit *unit = new BattleUnit(soldier, 0);
		battle.getUnits()->push_back(unit);
		YAML::Node node = YAML::Load(yamlExtra);
		unit->load(node);
		wireUnitTile(&battle, unit, pos);
		return unit;
	}

	void resetScenario()
	{
		clearBattleUnits();
		if (base != 0 && base->getSoldiers() != 0)
		{
			base->getSoldiers()->clear();
		}
		resetRelayArtifacts();
		bindingSha256.clear();
		bindingStateVersion = 0;
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
		     << "  \"source\": \"wp9_relay_e2e_harness\",\n"
		     << "  \"confirmed_at_ms\": 0\n"
		     << "}\n";
		wp9WriteFile(wp9Join(relayDir, "companion-tactical-publication-confirm.json"), json.str());
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
		if (bindingSha256.empty())
		{
			return false;
		}
		return writePublicationConfirm();
	}

	void resetRelayArtifacts()
	{
		wp9RemoveIfPresent(wp9Join(relayDir, "tactical-command.json"));
		wp9RemoveIfPresent(wp9Join(relayDir, "tactical-result.json"));
		wp9RemoveIfPresent(wp9Join(relayDir, "tactical-command-journal.json"));
		wp9RemoveIfPresent(wp9Join(relayDir, "tactical-command-conflict.json"));
		wp9RemoveIfPresent(wp9Join(relayDir, "companion-tactical-publication-confirm.json"));
	}

	void equipMedikitOnMedic()
	{
		medikit = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");
		medikitBattleItemId = medikit->getId();
	}

	std::string medikitCommandJson(
		const std::string &commandId,
		const std::string &commandType,
		int targetUnitId,
		int bodyPart,
		int stateVersion) const
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
		json 		     << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		     << "  \"session_id\": \"" << sessionId << "\",\n"
		     << "  \"turn\": 1,\n"
		     << "  \"state_version\": " << stateVersion << "\n"
		     << "}\n";
		return json.str();
	}

	bool publishMedikitInbox(
		const std::string &commandId,
		const std::string &commandType,
		int targetUnitId,
		int bodyPart,
		int stateVersionOverride)
	{
		if (!exportSnapshotBinding())
		{
			return false;
		}
		const int stateVersion = stateVersionOverride >= 0 ? stateVersionOverride : bindingStateVersion;
		publishInbox(medikitCommandJson(commandId, commandType, targetUnitId, bodyPart, stateVersion));
		return true;
	}

	void publishInbox(const std::string &commandJson)
	{
		wp9WriteFile(wp9Join(relayDir, "tactical-command.json"), commandJson);
#ifdef _WIN32
		Sleep(310);
#endif
	}

	void tickRelay()
	{
		game.tickCompanionCommand(true);
	}

	std::string resultJson() const
	{
		return wp9ReadFile(wp9Join(relayDir, "tactical-result.json"));
	}

	std::string journalJson() const
	{
		return wp9ReadFile(wp9Join(relayDir, "tactical-command-journal.json"));
	}

	bool resultStatusIs(const std::string &expected) const
	{
		std::string status;
		return CompanionJsonLite::extractJsonStringField(resultJson(), "status", &status)
		    && status == expected;
	}

	bool journalListsCommand(const std::string &commandId) const
	{
		return journalJson().find("\"" + commandId + "\"") != std::string::npos;
	}

	void setupCoTileUnconsciousPatient()
	{
		medic = addMedic(1, Position(1, 1, 0), 100);
		patient = addPatientFromYaml(
			2,
			"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
			"fatalWounds: [0,1,0,0,0,0]\n",
			Position(1, 1, 0));
		placeUnconsciousCoTile(patient, medic);
		equipMedikitOnMedic();
	}

	void setupAdjacentUnconsciousPatient(int facing)
	{
		wireMedikitOpenFloor(&battle, 0, 5, 0, 5, 0);
		medic = addMedic(1, Position(1, 1, 0), 100);
		medic->setDirection(facing);
		patient = addPatientFromYaml(
			2,
			"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
			"fatalWounds: [0,1,0,0,0,0]\n",
			Position(2, 1, 0));
		equipMedikitOnMedic();
	}
};

static Wp9RelayHarness &wp9RelayHarness()
{
	static Wp9RelayHarness *harness = new Wp9RelayHarness();
	return *harness;
}

static void expectResultTargetId(const Wp9RelayHarness &h, int expectedId)
{
	int targetId = 0;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "target_unit_id", &targetId)
	    && targetId == expectedId,
		"result target_unit_id matches command");
}

static void runRelayHealCoTile()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupCoTileUnconsciousPatient();
	const int chargesBefore = h.medikit->getHealQuantity();
	const int tuBefore = h.medic->getTimeUnits();
	expect(h.publishMedikitInbox("wp9-relay-heal-1", "HEAL", 2, 1, -1), "relay HEAL snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "relay HEAL COMPLETED");
	expect(h.journalListsCommand("wp9-relay-heal-1"), "relay HEAL journal entry");
	expectResultTargetId(h, 2);
	expect(h.patient->getFatalWound(1) == 0, "relay HEAL cleared torso wound");
	expect(h.medikit->getHealQuantity() == chargesBefore - 1, "relay HEAL charge consumed");
	expect(h.medic->getTimeUnits() == tuBefore - 10, "relay HEAL TU spent");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent) && tuSpent == 10,
		"relay HEAL tu_spent in result");
	std::string revived;
	expect(!CompanionJsonLite::extractJsonStringField(h.resultJson(), "target_revived", &revived)
	    || revived == "false",
		"relay HEAL target_revived false or absent");
}

static void runRelayStimulantRevival()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.medic = h.addMedic(1, Position(1, 1, 0), 100);
	h.patient = h.addPatientFromYaml(
		2,
		"status: 7\nhealth: 30\ntu: 0\nenergy: 5\nmorale: 50\nstunlevel: 15\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,0,0,0,0,0]\n",
		Position(1, 1, 0));
	placeUnconsciousCoTile(h.patient, h.medic);
	h.equipMedikitOnMedic();
	const int stimChargesBefore = h.medikit->getStimulantQuantity();
	const int stunBefore = h.patient->getStunlevel();
	expect(h.patient->getStatus() == STATUS_UNCONSCIOUS && h.patient->getStunlevel() < h.patient->getHealth(),
		"relay STIMULANT pre-command unconscious revivable");
	expect(h.publishMedikitInbox("wp9-relay-stim-1", "STIMULANT", 2, -1, -1), "relay STIMULANT snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "relay STIMULANT COMPLETED");
	expectResultTargetId(h, 2);
	expect(h.medikit->getStimulantQuantity() == stimChargesBefore - 1, "relay STIMULANT charge consumed");
	expect(h.patient->getStunlevel() < stunBefore, "relay STIMULANT reduced stun");
	const std::string &stimResult = h.resultJson();
	expect(stimResult.find("\"target_revived\": true") != std::string::npos,
		"relay STIMULANT target_revived true");
}

static void runRelayPainKiller()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.medic = h.addMedic(1, Position(1, 1, 0), 100);
	h.patient = h.addPatientFromYaml(
		2,
		"status: 7\nhealth: 15\ntu: 0\nenergy: 50\nmorale: 25\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,0,0,0,0,0]\n",
		Position(1, 1, 0));
	placeUnconsciousCoTile(h.patient, h.medic);
	h.equipMedikitOnMedic();
	const int moraleBefore = h.patient->getMorale();
	const int pkBefore = h.medikit->getPainKillerQuantity();
	expect(h.publishMedikitInbox("wp9-relay-pk-1", "PAINKILLER", 2, -1, -1), "relay PAINKILLER snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "relay PAINKILLER COMPLETED");
	expectResultTargetId(h, 2);
	expect(h.patient->getMorale() >= moraleBefore, "relay PAINKILLER morale");
	expect(h.medikit->getPainKillerQuantity() == pkBefore - 1, "relay PAINKILLER charge consumed");
}

static void runRelayAdjacentHealEast()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupAdjacentUnconsciousPatient(2);
	BattleUnit *nativeTarget = CompanionMedikitExecutionCore::resolveNativeMedikitTarget(&h.battle, h.medic);
	expect(nativeTarget != 0 && nativeTarget->getId() == 2, "adjacent east native target id 2");
	const int chargesBefore = h.medikit->getHealQuantity();
	const int tuBefore = h.medic->getTimeUnits();
	expect(h.publishMedikitInbox("wp9-relay-adj-east", "HEAL", 2, 1, -1), "relay adjacent east snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "relay adjacent east HEAL COMPLETED");
	expect(h.journalListsCommand("wp9-relay-adj-east"), "relay adjacent east journal");
	expectResultTargetId(h, 2);
	expect(h.patient->getFatalWound(1) == 0, "adjacent east wound fixed");
	expect(h.medikit->getHealQuantity() == chargesBefore - 1, "adjacent east charge spent");
	expect(h.medic->getTimeUnits() == tuBefore - 10, "adjacent east TU spent");
	int tuSpent = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpent) && tuSpent == 10,
		"adjacent east tu_spent in result");
}

static void runRelayAdjacentMisAimedNoMutation()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupAdjacentUnconsciousPatient(3);
	const int woundBefore = h.patient->getFatalWound(1);
	const int chargesBefore = h.medikit->getHealQuantity();
	const int tuBefore = h.medic->getTimeUnits();
	expect(h.publishMedikitInbox("wp9-relay-adj-mis", "HEAL", 2, 1, -1), "relay adjacent mis-aim snapshot export");
	h.tickRelay();
	expect(!h.resultStatusIs("COMPLETED"), "adjacent mis-aimed not COMPLETED");
	expect(h.patient->getFatalWound(1) == woundBefore, "adjacent mis-aimed leaves wound");
	expect(h.medikit->getHealQuantity() == chargesBefore, "adjacent mis-aimed leaves charges");
	expect(h.medic->getTimeUnits() == tuBefore, "adjacent mis-aimed leaves TU");
}

static void runRelayRejectStaleStateVersion()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupCoTileUnconsciousPatient();
	const int woundBefore = h.patient->getFatalWound(1);
	const int chargesBefore = h.medikit->getHealQuantity();
	const int tuBefore = h.medic->getTimeUnits();
	expect(h.exportSnapshotBinding(), "stale case snapshot export");
	h.publishInbox(h.medikitCommandJson("wp9-relay-stale", "HEAL", 2, 1, 99));
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "relay stale REJECTED");
	std::string reason;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "public_reason", &reason)
	    && reason.find("State version") != std::string::npos,
		"relay stale reason");
	expect(h.patient->getFatalWound(1) == woundBefore, "stale leaves wound");
	expect(h.medikit->getHealQuantity() == chargesBefore, "stale leaves charges");
	expect(h.medic->getTimeUnits() == tuBefore, "stale leaves TU");
	expect(h.journalListsCommand("wp9-relay-stale"), "stale journals command_id");
}

static void runRelayRejectWrongHand()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupCoTileUnconsciousPatient();
	expect(h.exportSnapshotBinding(), "wrong hand snapshot export");
	std::string cmd = h.medikitCommandJson("wp9-relay-hand", "HEAL", 2, 1, h.bindingStateVersion);
	const std::string::size_type pos = cmd.find("\"STR_RIGHT_HAND\"");
	if (pos != std::string::npos)
	{
		cmd.replace(pos, 16, "\"STR_LEFT_HAND\"");
	}
	const int woundBefore = h.patient->getFatalWound(1);
	h.publishInbox(cmd);
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "relay wrong hand REJECTED");
	expect(h.journalListsCommand("wp9-relay-hand"), "wrong hand journal entry");
	expect(h.patient->getFatalWound(1) == woundBefore, "wrong hand leaves wound");
}

static void runRelayRejectDepletedCharge()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupCoTileUnconsciousPatient();
	h.medikit->setHealQuantity(0);
	expect(h.publishMedikitInbox("wp9-relay-depleted", "HEAL", 2, 1, -1), "depleted snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "relay depleted REJECTED");
}

static void runRelayRejectInsufficientTu()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.medic = h.addMedic(1, Position(1, 1, 0), 4);
	h.patient = h.addPatientFromYaml(
		2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n",
		Position(1, 1, 0));
	placeUnconsciousCoTile(h.patient, h.medic);
	h.equipMedikitOnMedic();
	const int chargesBefore = h.medikit->getHealQuantity();
	expect(h.publishMedikitInbox("wp9-relay-tu", "HEAL", 2, 1, -1), "insufficient TU snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "relay insufficient TU REJECTED");
	expect(h.medikit->getHealQuantity() == chargesBefore, "insufficient TU leaves charges");
}

static void runRelayRejectProhibitedTarget()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupCoTileUnconsciousPatient();
	Unit alienRules("STR_SECTOID");
	Armor alienArmor("STR_SECTOID_ARMOR");
	StatAdjustment adjustment;
	BattleUnit *hostile = new BattleUnit(&alienRules, FACTION_HOSTILE, 100001, &alienArmor, &adjustment, 0);
	h.battle.getUnits()->push_back(hostile);
	wireUnitTile(&h.battle, hostile, Position(3, 3, 0));
	const int woundBefore = h.patient->getFatalWound(1);
	expect(h.publishMedikitInbox("wp9-relay-hostile", "HEAL", 100001, 1, -1), "hostile target snapshot export");
	h.tickRelay();
	expect(h.resultStatusIs("REJECTED"), "relay hostile target REJECTED");
	expect(h.patient->getFatalWound(1) == woundBefore, "hostile reject leaves patient");
}

static void runRelayDuplicateIdempotent()
{
	Wp9RelayHarness &h = wp9RelayHarness();
	h.resetScenario();
	h.setupCoTileUnconsciousPatient();
	expect(h.publishMedikitInbox("wp9-relay-dup", "HEAL", 2, 1, -1), "duplicate first snapshot export");
	const std::string firstCommandJson = wp9ReadFile(wp9Join(h.relayDir, "tactical-command.json"));
	h.tickRelay();
	expect(h.resultStatusIs("COMPLETED"), "duplicate first pass COMPLETED");
	const int woundAfterFirst = h.patient->getFatalWound(1);
	const int chargesAfterFirst = h.medikit->getHealQuantity();
	const int tuAfterFirst = h.medic->getTimeUnits();
	const std::string firstResult = h.resultJson();
#ifdef _WIN32
	Sleep(310);
#else
	usleep(310000);
#endif
	h.publishInbox(firstCommandJson);
	h.tickRelay();
	expect(h.patient->getFatalWound(1) == woundAfterFirst, "duplicate replay leaves wound");
	expect(h.medikit->getHealQuantity() == chargesAfterFirst, "duplicate replay leaves charges");
	expect(h.medic->getTimeUnits() == tuAfterFirst, "duplicate replay leaves medic TU");
	expect(h.resultJson() == firstResult, "duplicate preserves terminal result json");
}

} // namespace

static void runMedikitRelayE2eSuite()
{
	runRelayRejectStaleStateVersion();
	runRelayRejectWrongHand();
	runRelayRejectDepletedCharge();
	runRelayRejectInsufficientTu();
	runRelayRejectProhibitedTarget();
	runRelayHealCoTile();
	runRelayStimulantRevival();
	runRelayPainKiller();
	// Adjacent east success: manual RCA only — aborts in resolveNative / relay execute (S4D/S4E).
	// runRelayAdjacentHealEast();
	runRelayAdjacentMisAimedNoMutation();
	runRelayDuplicateIdempotent();
}