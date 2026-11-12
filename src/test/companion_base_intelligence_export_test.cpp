/*
 * Open X-Com Companion — base intelligence export tests (M1-BI1).
 */
#include "Engine/CompanionBaseIntelligenceExportCore.h"
#include "Engine/CompanionBaseIntelligenceExport.h"
#include "Engine/CompanionJsonLite.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/RuleBaseFacility.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/RuleResearch.h"
#include "../Mod/RuleManufacture.h"
#include "../Mod/RuleAlienMission.h"
#include "../Mod/AlienDeployment.h"
#include "../Mod/RuleCountry.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Base.h"
#include "../Savegame/BaseFacility.h"
#include "../Savegame/ResearchProject.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Craft.h"
#include "../Savegame/Ufo.h"
#include "../Savegame/MissionSite.h"
#include "../Savegame/AlienBase.h"
#include "../Savegame/Waypoint.h"
#include "../Savegame/Transfer.h"
#include "../Savegame/Country.h"
#include "../Savegame/ItemContainer.h"
#include "../Engine/Options.h"

#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <stdlib.h>
#else
#include <sys/stat.h>
#include <stdlib.h>
#endif

#ifdef main
#undef main
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

static Mod &mod()
{
	static Mod m;
	return m;
}

namespace
{

struct TestModScope
{
	Mod *_m;
	explicit TestModScope(Mod *m) : _m(m)
	{
		if (_m != 0)
		{
			_m->enterCompanionTestModContext();
		}
	}
	~TestModScope()
	{
		if (_m != 0)
		{
			_m->leaveCompanionTestModContext();
		}
	}
};

} // namespace

static void seedRules()
{
	Mod &m = mod();

	Armor *armor = new Armor("STR_TEST_ARMOR");
	YAML::Node an;
	an["type"] = "STR_TEST_ARMOR";
	armor->load(an);
	m.registerTestArmorRule(armor);

	RuleSoldier *rs = new RuleSoldier("STR_TEST_SOLDIER");
	YAML::Node sn;
	sn["type"] = "STR_TEST_SOLDIER";
	sn["armor"] = "STR_TEST_ARMOR";
	rs->load(sn, &m);
	m.registerTestSoldierRule(rs);

	RuleBaseFacility *lift = new RuleBaseFacility("STR_TEST_LIFT");
	YAML::Node ln;
	ln["type"] = "STR_TEST_LIFT";
	ln["size"] = 1;
	ln["buildCost"] = 0;
	ln["buildTime"] = 0;
	lift->load(ln, &m, 0);
	m.registerTestBaseFacilityRule(lift);

	RuleCraft *craftRules = new RuleCraft("STR_TEST_CRAFT");
	YAML::Node cn;
	cn["type"] = "STR_TEST_CRAFT";
	cn["fuelMax"] = 1000;
	cn["weapons"] = 0;
	cn["costBuy"] = 550000;
	cn["transferTime"] = 72;
	craftRules->load(cn, &m, 0);
	m.registerTestCraftRule(craftRules);

	RuleCraft *craftRulesB = new RuleCraft("STR_TEST_CRAFT_B");
	YAML::Node cnb;
	cnb["type"] = "STR_TEST_CRAFT_B";
	cnb["fuelMax"] = 800;
	cnb["weapons"] = 0;
	craftRulesB->load(cnb, &m, 0);
	m.registerTestCraftRule(craftRulesB);

	RuleUfo *ufoRules = new RuleUfo("STR_TEST_UFO");
	YAML::Node un;
	un["type"] = "STR_TEST_UFO";
	un["sprite"] = -1;
	un["marker"] = -1;
	un["markerLand"] = -1;
	un["markerCrash"] = -1;
	ufoRules->load(un, &m);
	m.registerTestUfoRule(ufoRules);

	// Item rules for storage math (Base::getUsedStores / ItemContainer::getTotalSize use getItem(..., true)).
	m.registerTestItemRule(new RuleItem("STR_RIFLE"));
	m.registerTestItemRule(new RuleItem("STR_CLIP"));
	RuleItem *electro = new RuleItem("STR_ELECTRO");
	YAML::Node en;
	en["type"] = "STR_ELECTRO";
	en["costBuy"] = 500;
	en["transferTime"] = 12;
	electro->load(en, &m, 0);
	m.registerTestItemRule(electro);

	RuleItem *buyItem = new RuleItem("STR_TEST_BUY_ITEM");
	YAML::Node bin;
	bin["type"] = "STR_TEST_BUY_ITEM";
	bin["costBuy"] = 1200;
	bin["transferTime"] = 36;
	buyItem->load(bin, &m, 0);
	m.registerTestItemRule(buyItem);

	RuleItem *lockedBuyItem = new RuleItem("STR_TEST_BUY_LOCKED");
	YAML::Node lbin;
	lbin["type"] = "STR_TEST_BUY_LOCKED";
	lbin["costBuy"] = 800;
	lbin["requires"] = YAML::Load("[STR_TEST_LOCKED]");
	lockedBuyItem->load(lbin, &m, 0);
	m.registerTestItemRule(lockedBuyItem);

	RuleResearch *starter = new RuleResearch("STR_TEST_START");
	YAML::Node ry;
	ry["name"] = "STR_TEST_START";
	ry["cost"] = 0;
	starter->load(ry, 0);
	m.registerTestResearchRule(starter);

	RuleResearch *locked = new RuleResearch("STR_TEST_LOCKED");
	YAML::Node ly;
	ly["name"] = "STR_TEST_LOCKED";
	ly["cost"] = 10;
	ly["dependencies"] = YAML::Load("[STR_NEVER_DONE]");
	locked->load(ly, 0);
	m.registerTestResearchRule(locked);

	RuleResearch *pickable = new RuleResearch("STR_TEST_PICKABLE");
	YAML::Node py2;
	py2["name"] = "STR_TEST_PICKABLE";
	py2["cost"] = 25;
	pickable->load(py2, 0);
	m.registerTestResearchRule(pickable);

	RuleResearch *requiresOnly = new RuleResearch("STR_TEST_REQUIRES_ONLY");
	YAML::Node ro;
	ro["name"] = "STR_TEST_REQUIRES_ONLY";
	ro["cost"] = 0;
	ro["requires"] = YAML::Load("[STR_TEST_START]");
	requiresOnly->load(ro, 0);
	m.registerTestResearchRule(requiresOnly);

	RuleResearch *psiGate = new RuleResearch("STR_TEST_PSI_GATE");
	YAML::Node py;
	py["name"] = "STR_TEST_PSI_GATE";
	py["cost"] = 0;
	psiGate->load(py, 0);
	m.registerTestResearchRule(psiGate);
	m.setTestPsiRequirementsForHarness(std::vector<std::string>(1, "STR_TEST_PSI_GATE"));

	RuleManufacture *man = new RuleManufacture("STR_TEST_ITEM");
	YAML::Node mn;
	mn["name"] = "STR_TEST_ITEM";
	mn["category"] = "STR_ITEMS";
	mn["requires"] = YAML::Load("[STR_TEST_START]");
	mn["space"] = 5;
	mn["time"] = 50;
	mn["cost"] = 1000;
	man->load(mn, 0);
	m.registerTestManufactureRule(man);

	RuleCountry *rc = new RuleCountry("STR_TEST_COUNTRY");
	YAML::Node cy;
	cy["funding"] = 500000;
	cy["labelLon"] = 0;
	cy["labelLat"] = 0;
	cy["areas"] = YAML::Load("[[-180,180,-90,90]]");
	rc->load(cy);
	m.registerTestCountryRule(rc);

	RuleAlienMission *am = new RuleAlienMission("STR_TEST_ALIEN_MISSION");
	YAML::Node my;
	my["type"] = "STR_TEST_ALIEN_MISSION";
	my["objective"] = 3;
	am->load(my);
	m.registerTestAlienMissionRule(am);

	AlienDeployment *depMs = new AlienDeployment("STR_TEST_MS_DEP");
	YAML::Node dmy;
	dmy["type"] = "STR_TEST_MS_DEP";
	dmy["markerName"] = "STR_TEST_MS_MARKER";
	depMs->load(dmy, &m);
	m.registerTestAlienDeploymentRule(depMs);

	AlienDeployment *depAb = new AlienDeployment("STR_TEST_AB_DEP");
	YAML::Node dab;
	dab["type"] = "STR_TEST_AB_DEP";
	dab["markerName"] = "STR_TEST_AB_MARKER";
	dab["isAlienBase"] = true;
	depAb->load(dab, &m);
	m.registerTestAlienDeploymentRule(depAb);

	(void)depAb;
	(void)depMs;
}

static SavedGame *makeEarlyScenario()
{
	seedRules();

	SavedGame *save = new SavedGame();
	save->setName("harness-early");
	save->setFunds(750000);
	save->setDifficulty(DIFF_EXPERIENCED);
	save->ensureCompanionCampaignId();

	Base *b = new Base(&mod());
	b->setName("Test Base");
	b->setLongitude(0.25);
	b->setLatitude(-0.40);
	save->getBases()->push_back(b);

	b->getFacilities()->push_back(new BaseFacility(mod().getBaseFacility("STR_TEST_LIFT", true), b));

	Armor *ar = mod().getArmor("STR_TEST_ARMOR", true);
	RuleSoldier *sr = mod().getSoldier("STR_TEST_SOLDIER", true);
	Soldier *s = new Soldier(sr, ar, 1001);
	s->setName("Test Trooper");
	s->getCurrentStats()->psiStrength = 90;
	s->getCurrentStats()->psiSkill = 65;
	b->getSoldiers()->push_back(s);

	b->getStorageItems()->addItem("STR_RIFLE", 2);
	b->getStorageItems()->addItem("STR_CLIP", 40);
	b->getStorageItems()->addItem("STR_ELECTRO", 3);

	Craft *c = new Craft(mod().getCraft("STR_TEST_CRAFT", true), b, 501);
	c->setName("Custom Alpha Name");
	c->setFuel(500);
	c->setDamage(5);
	c->setStatus("STR_READY");
	b->getCrafts()->push_back(c);

	Craft *cB = new Craft(mod().getCraft("STR_TEST_CRAFT_B", true), b, 501);
	cB->setFuel(300);
	cB->setStatus("STR_OUT");
	b->getCrafts()->push_back(cB);

	// Second base — both leave Target id at 0 (typical geoscape flow); export uses stable slot ids 1 and 2.
	Base *b2 = new Base(&mod());
	b2->setName("Second Base");
	b2->setLongitude(0.35);
	b2->setLatitude(0.45);
	save->getBases()->push_back(b2);
	b2->getFacilities()->push_back(new BaseFacility(mod().getBaseFacility("STR_TEST_LIFT", true), b2));
	b2->setScientists(5);
	RuleResearch *laser = new RuleResearch("STR_TEST_LASER");
	YAML::Node lr;
	lr["name"] = "STR_TEST_LASER";
	lr["cost"] = 100;
	laser->load(lr, 0);
	mod().registerTestResearchRule(laser);
	ResearchProject *laserProj = new ResearchProject(laser, laser->getCost());
	laserProj->setAssigned(10);
	b2->addResearch(laserProj);
	b2->getSoldiers()->push_back(new Soldier(sr, ar, 1002));
	Craft *c2 = new Craft(mod().getCraft("STR_TEST_CRAFT", true), b2, 502);
	c2->setFuel(400);
	c2->setStatus("STR_READY");
	c2->setMissionComplete(true);
	b2->getCrafts()->push_back(c2);

	save->addFinishedResearchSimple(mod().getResearch("STR_TEST_START", true));

	std::vector<RuleResearch *> avail;
	save->getAvailableResearchProjects(avail, &mod(), b, false);
	bool lockedListed = false;
	for (size_t i = 0; i < avail.size(); ++i)
	{
		if (avail[i]->getName() == "STR_TEST_LOCKED")
		{
			lockedListed = true;
			break;
		}
	}
	expect(!lockedListed, "STR_TEST_LOCKED must not appear in available research");

	Transfer *tx = new Transfer(10);
	tx->setItems("STR_ELECTRO", 2);
	b->getTransfers()->push_back(tx);

	// gen=true: Country must have non-empty funding history — engine uses getFunding().back() (empty is UB).
	save->getCountries()->push_back(new Country(mod().getCountry("STR_TEST_COUNTRY", true), true));

	Ufo *uVis = new Ufo(mod().getUfo("STR_TEST_UFO", true));
	uVis->setId(8001);
	uVis->setDetected(true);
	uVis->setLongitude(0.1);
	uVis->setLatitude(0.2);
	save->getUfos()->push_back(uVis);

	Ufo *uHid = new Ufo(mod().getUfo("STR_TEST_UFO", true));
	uHid->setId(8002);
	uHid->setDetected(false);
	uHid->setHyperDetected(false);
	save->getUfos()->push_back(uHid);

	MissionSite *ms = new MissionSite(mod().getAlienMission("STR_TEST_ALIEN_MISSION", true),
		mod().getDeployment("STR_TEST_MS_DEP", true));
	ms->setDetected(true);
	ms->setAlienRace("STR_SECTOID");
	ms->setLongitude(0.3);
	ms->setLatitude(0.4);
	save->getMissionSites()->push_back(ms);

	MissionSite *msH = new MissionSite(mod().getAlienMission("STR_TEST_ALIEN_MISSION", true),
		mod().getDeployment("STR_TEST_MS_DEP", true));
	msH->setDetected(false);
	save->getMissionSites()->push_back(msH);

	AlienBase *abVis = new AlienBase(mod().getDeployment("STR_TEST_AB_DEP", true));
	abVis->setDiscovered(true);
	abVis->setAlienRace("STR_MUTON");
	abVis->setLongitude(0.5);
	abVis->setLatitude(-0.6);
	save->getAlienBases()->push_back(abVis);

	AlienBase *abHid = new AlienBase(mod().getDeployment("STR_TEST_AB_DEP", true));
	abHid->setDiscovered(false);
	save->getAlienBases()->push_back(abHid);

	Waypoint *wp = new Waypoint();
	wp->setId(9901);
	wp->setLongitude(0.7);
	wp->setLatitude(0.8);
	save->getWaypoints()->push_back(wp);

	return save;
}

static bool mkdirOne(const std::string &path)
{
#ifdef _WIN32
	return _mkdir(path.c_str()) == 0;
#else
	return mkdir(path.c_str(), 0755) == 0;
#endif
}

int main()
{
	const bool prevPsiEval = Options::psiStrengthEval;
	Options::psiStrengthEval = true;

	// Mod::getOffset requires _modCurrent; keep synthetic context for full harness.
	TestModScope modCtx(&mod());

	SavedGame *save = makeEarlyScenario();

	CompanionBaseIntelligenceExportCore::BuildResult built =
		CompanionBaseIntelligenceExportCore::buildJsonUtf8(save, &mod(), 1700000000000LL, "test");
	expect(built.ok, "export must succeed");
	expect(built.jsonUtf8.find("\"schema_version\":2") != std::string::npos, "schema version");
	expect(built.jsonUtf8.size() <= CompanionBaseIntelligenceExportCore::EXPORT_JSON_MAX_BYTES, "under 512 KiB cap");

	// Distinct base slots (not Target::getId), stable cross-refs
	expect(built.jsonUtf8.find("\"bases\":[{\"id\":1,") != std::string::npos, "first base id is slot 1");
	expect(built.jsonUtf8.find(",{\"id\":2,") != std::string::npos, "second base id is slot 2");
	expect(built.jsonUtf8.find("\"base_id\":1") != std::string::npos, "reference uses base_id 1");
	expect(built.jsonUtf8.find("\"base_id\":2") != std::string::npos, "reference uses base_id 2");
	expect(built.jsonUtf8.find("\"scientists\":5") != std::string::npos, "b2 unassigned scientist pool");
	expect(built.jsonUtf8.find("\"assigned_scientists\":10") != std::string::npos, "active research assignment");

	Base *b2 = save->getBases()->at(1);
	b2->setName("Renamed Second");
	const CompanionBaseIntelligenceExportCore::BuildResult renamed =
		CompanionBaseIntelligenceExportCore::buildJsonUtf8(save, &mod(), 1700000000002LL, "test");
	expect(renamed.ok, "export after rename must succeed");
	expect(renamed.jsonUtf8.find(",{\"id\":2,") != std::string::npos, "slot id unchanged after rename");
	expect(renamed.jsonUtf8.find("Renamed Second") != std::string::npos, "renamed label present");
	expect(renamed.jsonUtf8.find("\"unique_key\":\"STR_TEST_CRAFT#501\"") != std::string::npos,
		"unique_key stable after base rename");

	std::string cid;
	expect(CompanionJsonLite::extractJsonStringField(built.jsonUtf8, "campaign_id", &cid), "campaign_id");
	expect(!cid.empty(), "campaign_id non-empty");

	expect(built.jsonUtf8.find("STR_TEST_LOCKED") == std::string::npos, "locked topic must not appear");
	expect(built.jsonUtf8.find("\"id\":8002") == std::string::npos, "undetected UFO omitted");
	expect(built.jsonUtf8.find("psi_strength") == std::string::npos, "psi hidden until psi gate is researched");
	expect(built.jsonUtf8.find("STR_TEST_CRAFT#501") != std::string::npos, "unnamed craft uses safe export label");

	expect(built.jsonUtf8.find("\"unique_key\":\"STR_TEST_CRAFT#501\"") != std::string::npos,
		"unique_key uses type and id not custom display name");
	expect(built.jsonUtf8.find("\"unique_key\":\"STR_TEST_CRAFT_B#501\"") != std::string::npos,
		"cross-type same numeric id has distinct unique_key");
	expect(built.jsonUtf8.find("\"unique_key\":\"STR_TEST_CRAFT#502\"") != std::string::npos,
		"second craft of same type has distinct unique_key");
	expect(built.jsonUtf8.find("\"Custom Alpha Name\"") != std::string::npos,
		"display name preserved separately from unique_key");
	expect(built.jsonUtf8.find("\"status\":\"STR_READY\"") != std::string::npos, "ready craft status");
	expect(built.jsonUtf8.find("\"status\":\"STR_OUT\"") != std::string::npos, "airborne craft status");
	expect(built.jsonUtf8.find("\"away_from_base\":true") != std::string::npos, "away_from_base when STR_OUT");
	expect(built.jsonUtf8.find("\"returning_from_ground_mission\":true") != std::string::npos,
		"returning_from_ground_mission when mission complete flag set");
	expect(built.jsonUtf8.find("\"on_mission\":true") != std::string::npos,
		"legacy on_mission still mirrors getMissionComplete");

	expect(built.jsonUtf8.find("STR_TEST_PICKABLE") != std::string::npos, "pickable research in export");
	expect(built.jsonUtf8.find("STR_TEST_REQUIRES_ONLY") == std::string::npos,
		"requires-gated topic excluded from available_by_base");
	expect(built.jsonUtf8.find("\"completed\"") != std::string::npos, "completed research section present");
	expect(built.jsonUtf8.find("STR_TEST_START") != std::string::npos, "completed topic preserved");

	expect(built.jsonUtf8.find("\"available_by_base\"") != std::string::npos, "purchasing available_by_base");
	expect(built.jsonUtf8.find("Purchase catalog not exported") == std::string::npos, "purchasing stub removed");
	expect(built.jsonUtf8.find("\"kind\":\"scientist\"") != std::string::npos, "scientist catalog row");
	expect(built.jsonUtf8.find("STR_TEST_BUY_ITEM") != std::string::npos, "buyable item in catalog");
	expect(built.jsonUtf8.find("\"unit_cost\":1200") != std::string::npos, "item buy cost");
	expect(built.jsonUtf8.find("\"delivery_hours\":36") != std::string::npos, "item delivery hours");
	expect(built.jsonUtf8.find("STR_TEST_BUY_LOCKED") == std::string::npos, "unresearched buy item excluded");
	expect(built.jsonUtf8.find("STR_TEST_CRAFT") != std::string::npos, "buyable craft in catalog");
	expect(built.jsonUtf8.find("\"delivery_hours\":72") != std::string::npos, "craft delivery hours");
	expect(built.jsonUtf8.find("living_quarters_free") != std::string::npos, "purchase capacity quarters");
	const size_t electroPos = built.jsonUtf8.find("\"rule_id\":\"STR_ELECTRO\"");
	expect(electroPos != std::string::npos, "electro catalog row present");
	expect(built.jsonUtf8.find("\"quantity_at_base\":3", electroPos) != std::string::npos &&
			built.jsonUtf8.find("\"quantity_at_base\":3", electroPos) < electroPos + 120,
		"item qty is on-base stores only (electro 3, transfer separate)");

	save->setFunds(0);
	const CompanionBaseIntelligenceExportCore::BuildResult broke =
		CompanionBaseIntelligenceExportCore::buildJsonUtf8(save, &mod(), 1700000000003LL, "test");
	expect(broke.ok, "export at zero funds must succeed");
	expect(broke.jsonUtf8.find("STR_TEST_BUY_ITEM") != std::string::npos,
		"catalog visible at zero funds (availability != affordability)");

	long long funds = 0;
	expect(CompanionJsonLite::extractJsonInt64Field(built.jsonUtf8, "funds", &funds), "overview funds");
	expect(funds == 750000, "fund total matches");

	save->addFinishedResearchSimple(mod().getResearch("STR_TEST_PSI_GATE", true));
	const CompanionBaseIntelligenceExportCore::BuildResult withPsi =
		CompanionBaseIntelligenceExportCore::buildJsonUtf8(save, &mod(), 1700000000001LL, "test");
	expect(withPsi.ok, "export with psi gate must succeed");
	expect(withPsi.jsonUtf8.find("psi_strength") != std::string::npos, "psi stats appear after psi gate research");

	Options::psiStrengthEval = prevPsiEval;

	std::ostringstream dir;
	dir << "companion_bi1_test_" << static_cast<unsigned long long>(std::time(0));
	const std::string d = dir.str();
	(void)mkdirOne(d);
	const std::string snap = d + "/tactical-live.yaml";
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snap.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snap.c_str(), 1);
#endif
	expect(CompanionBaseIntelligenceExport::tryExport(save, &mod()), "atomic write");

	const std::string outPath = d + "/companion-base-intelligence.json";
	std::ifstream f(outPath.c_str());
	expect(f.good(), "output file exists");

	delete save;
	return g_failures > 0 ? 1 : 0;
}
