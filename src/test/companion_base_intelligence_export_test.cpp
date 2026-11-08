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
	craftRules->load(cn, &m, 0);
	m.registerTestCraftRule(craftRules);

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
	m.registerTestItemRule(new RuleItem("STR_ELECTRO"));

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

	Craft *c = new Craft(mod().getCraft("STR_TEST_CRAFT", true), b, 501);
	c->setFuel(500);
	c->setDamage(5);
	c->setStatus("STR_READY");
	b->getCrafts()->push_back(c);

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

	std::string cid;
	expect(CompanionJsonLite::extractJsonStringField(built.jsonUtf8, "campaign_id", &cid), "campaign_id");
	expect(!cid.empty(), "campaign_id non-empty");

	expect(built.jsonUtf8.find("STR_TEST_LOCKED") == std::string::npos, "locked topic must not appear");
	expect(built.jsonUtf8.find("\"id\":8002") == std::string::npos, "undetected UFO omitted");
	expect(built.jsonUtf8.find("psi_strength") == std::string::npos, "psi hidden until psi gate is researched");
	expect(built.jsonUtf8.find("STR_TEST_CRAFT#501") != std::string::npos, "unnamed craft uses safe export label");

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
