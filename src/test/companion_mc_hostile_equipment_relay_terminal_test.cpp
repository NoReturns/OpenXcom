/*
 * M1 MC-04 — MC equipment relay terminal certification (MC_PRIME, MC_THROW).
 * Real BattlescapeState + UnitWalkBState + production CompanionCommandRuntime inbox.
 * Acceptance: adjacent arrival, TU spent, relay COMPLETED (not queued-state only).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/TileEngine.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../Engine/CompanionJsonLite.h"
#include "../Engine/CompanionSnapshot.h"
#include "../Engine/CompanionSoldierControlPublishCore.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/State.h"
#include "../Mod/Armor.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "../Battlescape/UnitTurnBState.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/Unit.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleResearch.h"
#include "../Mod/RuleInventory.h"
#include "../Savegame/Base.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Tile.h"
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
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
const int kMaxFireTicks = 200000;
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

static bool mapDataHasRenderableSurfaces(MapData *md)
{
	return md != 0 && md->getDataset() != 0 && md->getDataset()->getSurfaceset() != 0;
}

// Mod-backed floor tiles (parity with companion_hwp_fire_relay_test S5-1CH / MC-03A-FIX-01).
static MapDataSet *loadModMapDataSet(Mod *mod, const char *name)
{
	if (mod == 0 || name == 0)
	{
		return 0;
	}
	MapDataSet *set = mod->getMapDataSet(name);
	if (set == 0)
	{
		return 0;
	}
	if (set->getSurfaceset() == 0)
	{
		set->loadData(mod->getMCDPatch(set->getName()));
	}
	return set->getSurfaceset() != 0 ? set : 0;
}

static MapData *renderableFloorTile(Mod *mod)
{
	if (mod != 0)
	{
		const char *setNames[] = { "URBAN", "UFO1", "PLAINS", "JUNGLE", "BLANKS" };
		for (size_t s = 0; s < sizeof(setNames) / sizeof(setNames[0]); ++s)
		{
			MapDataSet *set = loadModMapDataSet(mod, setNames[s]);
			if (set == 0)
			{
				continue;
			}
			for (size_t i = 0; i < set->getSize(); ++i)
			{
				MapData *md = set->getObject(i);
				if (md != 0 && md->getObjectType() == O_FLOOR && mapDataHasRenderableSurfaces(md))
				{
					return md;
				}
			}
		}
	}
	MapData *scorched = MapDataSet::getScorchedEarthTile();
	if (mapDataHasRenderableSurfaces(scorched))
	{
		return scorched;
	}
	if (mod != 0)
	{
		MapDataSet *blanks = loadModMapDataSet(mod, "BLANKS");
		if (blanks != 0)
		{
			MapData *blank = MapDataSet::getBlankFloorTile();
			if (mapDataHasRenderableSurfaces(blank))
			{
				return blank;
			}
		}
	}
	return 0;
}

static void wireFloor(SavedBattleGame *battle, Mod *mod, int w, int h)
{
	MapData *floor = renderableFloorTile(mod);
	expect(floor != 0 && mapDataHasRenderableSurfaces(floor),
		"wireFloor: mod-backed floor tile must have loaded SurfaceSet (MC-03A-FIX-01)");
	if (floor == 0)
	{
		return;
	}
	if (floor->getDataset() != 0)
	{
		logEvidence("wireFloor",
			std::string("floorTile dataset=") + floor->getDataset()->getName()
			+ " surfaceset=" + (floor->getDataset()->getSurfaceset() != 0 ? "yes" : "no"));
	}
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
			t->setMapData(floor, 1, 0, O_FLOOR);
		}
	}
}

static void logHarnessFloorAudit(SavedBattleGame *battle, const Position &pos)
{
	Tile *tile = battle != 0 ? battle->getTile(pos) : 0;
	if (tile == 0)
	{
		logEvidence("floor-audit", "tile missing at " + std::to_string(pos.x) + "," + std::to_string(pos.y));
		return;
	}
	MapData *floor = tile->getMapData(O_FLOOR);
	const bool renderable = mapDataHasRenderableSurfaces(floor);
	std::ostringstream line;
	line << "pos=" << pos.x << "," << pos.y << " floorMapData=" << (floor != 0 ? "yes" : "no")
	     << " renderableSurfaceset=" << (renderable ? "yes" : "no");
	if (floor != 0 && floor->getDataset() != 0)
	{
		line << " dataset=" << floor->getDataset()->getName()
		     << " surfaceset=" << (floor->getDataset()->getSurfaceset() != 0 ? "yes" : "no");
	}
	logEvidence("floor-audit", line.str());
}

static bool unitCarriesItemId(BattleUnit *unit, int itemId)
{
	if (unit == 0 || itemId <= 0)
	{
		return false;
	}
	for (std::vector<BattleItem *>::iterator it = unit->getInventory()->begin();
	     it != unit->getInventory()->end(); ++it)
	{
		if (*it != 0 && (*it)->getId() == itemId)
		{
			return true;
		}
	}
	return false;
}

static void syncRelayInventoryVector(BattleUnit *unit, BattleItem *item)
{
	if (unit == 0 || item == 0 || unitCarriesItemId(unit, item->getId()))
	{
		return;
	}
	for (std::vector<BattleItem *>::iterator it = unit->getInventory()->begin();
	     it != unit->getInventory()->end(); ++it)
	{
		if (*it == item)
		{
			return;
		}
	}
	unit->getInventory()->push_back(item);
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
		if (game != 0)
		{
			const char *researchTopics[] = {
				"STR_GRENADE", "STR_ALIEN_GRENADE", "STR_PISTOL", "STR_PISTOL_CLIP", "STR_RIFLE", "STR_RIFLE_CLIP", 0
			};
			for (size_t ri = 0; researchTopics[ri] != 0; ++ri)
			{
				RuleResearch *topic = game->getMod()->getResearch(researchTopics[ri], false);
				if (topic != 0)
				{
					campaign->addFinishedResearchSimple(topic);
				}
			}
		}
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
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
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
		Mod *rules = game != 0 ? game->getMod() : &fixtureMod;
		battle->setTestMod(rules);
		battle->initMap(w, h, 1);
		battle->initUtilities(rules);
		battle->loadMapResources(rules);
		battle->setTUReserved(BA_NONE);
		battle->setKneelReserved(false);
		battle->setGeoscapeSaveHint(campaign);
		battle->ensureCompanionTacticalOwnershipInitialized(campaign);
		battle->getCompanionEventLog().beginSession();
		sessionId = battle->getCompanionEventLog().getSessionId();
		wireFloor(battle, rules, w, h);
		battlescape = new BattlescapeState();
		battle->setBattleState(battlescape);
		battlescape->init();
		bindingSha256.clear();
		bindingStateVersion = 0;
		// Relay JSON parsers reject item_battle_item_id <= 0; first engine id may be 0.
		RuleItem *idBurnRules = game != 0 ? game->getMod()->getItem("STR_PISTOL_CLIP", false) : 0;
		if (idBurnRules != 0)
		{
			BattleItem *idBurn = new BattleItem(idBurnRules, battle->getCurrentItemId());
			battle->getItems()->push_back(idBurn);
		}
	}

	int advanceUntilTerminal()
	{
		BattlescapeGame *bg = battleGame();
		expect(bg != 0, "battle game for fire terminal");
		int ticks = 0;
		while (ticks < kMaxFireTicks)
		{
			if (battlescape != 0)
			{
				battlescape->think();
			}
			else if (bg != 0 && bg->isBusy())
			{
				bg->handleState();
			}
			else if (bg != 0)
			{
				bg->think();
			}
			tickRelay();
			const std::string st = status();
			if (!st.empty() && st != "EXECUTING")
			{
				break;
			}
			++ticks;
		}
		expect(ticks < kMaxFireTicks, "fire terminal within tick budget");
		return ticks;
	}

	void initRelayEnv()
	{
		relayDir = certJoin(certExeDir(), "mc_equipment_relay_terminal_cert");
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

	BattleUnit *addMcHostile(const Position &pos, int tu, const char *armorOverride = 0)
	{
		Mod *rules = game->getMod();
		Unit *unitRules = rules != 0 ? rules->getUnit("STR_SECTOID_SOLDIER", false) : 0;
		if (unitRules == 0 && rules != 0)
		{
			unitRules = rules->getUnit("STR_SECTOID", false);
		}
		Armor *armor = 0;
		if (armorOverride != 0 && rules != 0)
		{
			armor = rules->getArmor(armorOverride, false);
		}
		if (armor == 0 && rules != 0)
		{
			armor = rules->getArmor("SECTOID_ARMOR0", false);
		}
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
		pistol->setSlotX(0);
		pistol->setSlotY(0);
		pistol->moveToOwner(actor);
		battle->getItems()->push_back(pistol);
		battle->getItems()->push_back(clip);
		actor->setActiveHand("STR_RIGHT_HAND");
		return pistol;
	}

	BattleItem *mountStockPistolEmpty(BattleUnit *actor)
	{
		RuleItem *pistolRules = game->getMod()->getItem("STR_PISTOL", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(pistolRules != 0 && hand != 0, "stock empty pistol rules");
		if (pistolRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *pistol = new BattleItem(pistolRules, battle->getCurrentItemId());
		pistol->setSlot(hand);
		pistol->setSlotX(0);
		pistol->setSlotY(0);
		pistol->moveToOwner(actor);
		battle->getItems()->push_back(pistol);
		syncRelayInventoryVector(actor, pistol);
		actor->setActiveHand("STR_RIGHT_HAND");
		return pistol;
	}

	BattleItem *mountStockGrenade(BattleUnit *actor)
	{
		RuleItem *grenadeRules = game->getMod()->getItem("STR_ALIEN_GRENADE", false);
		if (grenadeRules == 0)
		{
			grenadeRules = game->getMod()->getItem("STR_GRENADE", false);
		}
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(grenadeRules != 0 && hand != 0, "stock grenade rules");
		if (grenadeRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *grenade = new BattleItem(grenadeRules, battle->getCurrentItemId());
		grenade->setSlot(hand);
		grenade->setSlotX(0);
		grenade->setSlotY(0);
		grenade->moveToOwner(actor);
		battle->getItems()->push_back(grenade);
		actor->setActiveHand("STR_RIGHT_HAND");
		return grenade;
	}

	BattleItem *mountPistolClipOnBelt(BattleUnit *actor, int beltX = 0, int beltY = 0)
	{
		RuleItem *clipRules = game->getMod()->getItem("STR_PISTOL_CLIP", false);
		RuleInventory *belt = game->getMod()->getInventory("STR_BELT", true);
		RuleInventory *back = game->getMod()->getInventory("STR_BACK_PACK", true);
		RuleInventory *slot = belt != 0 ? belt : back;
		expect(clipRules != 0 && slot != 0, "stock pistol clip + carried slot");
		if (clipRules == 0 || slot == 0)
		{
			return 0;
		}
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		clip->setSlot(slot);
		clip->setSlotX(beltX);
		clip->setSlotY(beltY);
		clip->moveToOwner(actor);
		battle->getItems()->push_back(clip);
		return clip;
	}

	BattleItem *mountRifleClipOnBelt(BattleUnit *actor, int beltX = 0, int beltY = 0)
	{
		RuleItem *clipRules = game->getMod()->getItem("STR_RIFLE_CLIP", false);
		RuleInventory *belt = game->getMod()->getInventory("STR_BELT", true);
		RuleInventory *back = game->getMod()->getInventory("STR_BACK_PACK", true);
		RuleInventory *slot = belt != 0 ? belt : back;
		expect(clipRules != 0 && slot != 0, "stock rifle clip + carried slot");
		if (clipRules == 0 || slot == 0)
		{
			return 0;
		}
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		clip->setAmmoQuantity(clipRules->getClipSize());
		clip->setSlot(slot);
		clip->setSlotX(beltX);
		clip->setSlotY(beltY);
		clip->moveToOwner(actor);
		battle->getItems()->push_back(clip);
		return clip;
	}

	BattleItem *mountMcCarriedPistolClip(BattleUnit *actor)
	{
		BattleItem *clip = mountPistolClipOnBelt(actor, 0, 0);
		if (clip == 0 || actor->getItem("STR_BELT", 0, 0) != clip)
		{
			clip = mountPistolClipInLeftHand(actor);
		}
		return clip;
	}

	BattleItem *mountPistolClipInLeftHand(BattleUnit *actor)
	{
		RuleItem *clipRules = game->getMod()->getItem("STR_PISTOL_CLIP", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_LEFT_HAND", true);
		expect(clipRules != 0 && hand != 0, "stock pistol clip + left hand");
		if (clipRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		clip->setSlot(hand);
		clip->setSlotX(0);
		clip->setSlotY(0);
		clip->moveToOwner(actor);
		battle->getItems()->push_back(clip);
		syncRelayInventoryVector(actor, clip);
		return clip;
	}

	BattleItem *mountRifleClipInLeftHand(BattleUnit *actor)
	{
		RuleItem *clipRules = game->getMod()->getItem("STR_RIFLE_CLIP", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_LEFT_HAND", true);
		expect(clipRules != 0 && hand != 0, "stock rifle clip + left hand");
		if (clipRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		clip->setAmmoQuantity(clipRules->getClipSize());
		clip->setSlot(hand);
		clip->setSlotX(0);
		clip->setSlotY(0);
		clip->moveToOwner(actor);
		battle->getItems()->push_back(clip);
		return clip;
	}

	BattleItem *mountStockRifleEmpty(BattleUnit *actor)
	{
		RuleItem *rifleRules = game->getMod()->getItem("STR_RIFLE", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(rifleRules != 0 && hand != 0, "stock empty rifle rules");
		if (rifleRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *rifle = new BattleItem(rifleRules, battle->getCurrentItemId());
		rifle->setSlot(hand);
		rifle->setSlotX(0);
		rifle->setSlotY(0);
		rifle->moveToOwner(actor);
		battle->getItems()->push_back(rifle);
		actor->setActiveHand("STR_RIGHT_HAND");
		return rifle;
	}

	BattleItem *mountStockRifleLoaded(BattleUnit *actor, int clipRounds = 25)
	{
		return mountStockRifle(actor, clipRounds);
	}

	BattleItem *placePistolClipOnGroundAt(const Position &pos, int groundX = 0, int groundY = 0)
	{
		RuleItem *clipRules = game->getMod()->getItem("STR_PISTOL_CLIP", false);
		RuleInventory *ground = game->getMod()->getInventory("STR_GROUND", true);
		Tile *tile = battle != 0 ? battle->getTile(pos) : 0;
		expect(clipRules != 0 && ground != 0 && tile != 0, "ground pistol clip placement");
		if (clipRules == 0 || ground == 0 || tile == 0)
		{
			return 0;
		}
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		clip->setSlot(ground);
		clip->setSlotX(groundX);
		clip->setSlotY(groundY);
		clip->setTile(tile);
		tile->getInventory()->push_back(clip);
		battle->getItems()->push_back(clip);
		return clip;
	}

	static bool battleItemExists(SavedBattleGame *bgame, int itemId)
	{
		if (bgame == 0 || itemId <= 0)
		{
			return false;
		}
		for (std::vector<BattleItem *>::iterator it = bgame->getItems()->begin();
		     it != bgame->getItems()->end(); ++it)
		{
			if (*it != 0 && (*it)->getId() == itemId)
			{
				return true;
			}
		}
		return false;
	}

	static BattleItem *findItemOnTile(Tile *tile, int itemId)
	{
		if (tile == 0)
		{
			return 0;
		}
		for (std::vector<BattleItem *>::iterator it = tile->getInventory()->begin();
		     it != tile->getInventory()->end(); ++it)
		{
			if (*it != 0 && (*it)->getId() == itemId)
			{
				return *it;
			}
		}
		return 0;
	}

	std::string mcPrimeCmd(const char *id, int fuseTimer, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_PRIME\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"active_hand\": \"STR_RIGHT_HAND\",\n  \"fuse_timer\": " << fuseTimer << ",\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcThrowCmd(const char *id, int tx, int ty, int tz, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_THROW\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"active_hand\": \"STR_RIGHT_HAND\",\n"
		  << "  \"target\": {\"x\": " << tx << ", \"y\": " << ty << ", \"z\": " << tz << "},\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcDropCmd(const char *id, int itemId, const char *sourceSlot, int sx, int sy, bool beltCoords,
		int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_DROP\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"item_battle_item_id\": " << itemId << ",\n"
		  << "  \"source_slot\": \"" << sourceSlot << "\",\n";
		if (beltCoords)
		{
			j << "  \"source_x\": " << sx << ",\n  \"source_y\": " << sy << ",\n";
		}
		j << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcPickupCmd(const char *id, int itemId, const char *destSlot, int destX, int destY,
		int groundX, int groundY, int groundZ, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_PICKUP\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"item_battle_item_id\": " << itemId << ",\n"
		  << "  \"ground_position\": {\"x\": " << groundX << ", \"y\": " << groundY << ", \"z\": " << groundZ << "},\n"
		  << "  \"destination_slot\": \"" << destSlot << "\",\n"
		  << "  \"destination_x\": " << destX << ",\n  \"destination_y\": " << destY << ",\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcReloadCmd(const char *id, int weaponId, int ammoId, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_RELOAD\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"weapon_battle_item_id\": " << weaponId << ",\n"
		  << "  \"ammo_battle_item_id\": " << ammoId << ",\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcUnloadCmd(const char *id, int weaponId, int loadedAmmoId, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_UNLOAD\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"weapon_battle_item_id\": " << weaponId << ",\n"
		  << "  \"loaded_ammo_battle_item_id\": " << loadedAmmoId << ",\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	std::string mcInventoryMoveCmd(const char *id, int itemId, const char *sourceSlot, const char *destSlot,
		int sx, int sy, int dx, int dy, bool sourceCoords, int sv) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"MC_INVENTORY_MOVE\",\n  \"actor_unit_id\": " << MC_HOSTILE_ID << ",\n"
		  << "  \"item_battle_item_id\": " << itemId << ",\n"
		  << "  \"source_slot\": \"" << sourceSlot << "\",\n"
		  << "  \"destination_slot\": \"" << destSlot << "\",\n";
		if (sourceCoords)
		{
			j << "  \"source_x\": " << sx << ",\n  \"source_y\": " << sy << ",\n";
		}
		j << "  \"destination_x\": " << dx << ",\n  \"destination_y\": " << dy << ",\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": " << battle->getTurn()
		  << ",\n  \"state_version\": " << sv << "\n}\n";
		return j.str();
	}

	bool tickBattleUntilItemAbsent(int itemId, int maxTicks = 8000)
	{
		BattlescapeGame *bg = battleGame();
		if (bg == 0 || itemId <= 0)
		{
			return true;
		}
		int ticks = 0;
		while (ticks < maxTicks && battleItemExists(battle, itemId))
		{
			if (bg->isBusy())
			{
				bg->handleState();
			}
			else if (battlescape != 0)
			{
				battlescape->think();
			}
			else
			{
				bg->think();
			}
			tickRelay();
			++ticks;
		}
		return !battleItemExists(battle, itemId);
	}

	// Mirror BattlescapeGame::endTurn fuse handling without UI states (private endTurn).
	void applyStockPrimedGrenadeTurnSlice()
	{
		if (battle == 0 || battle->getSide() == FACTION_NEUTRAL)
		{
			return;
		}
		const int mapSize = battle->getMapSizeXYZ();
		for (int i = 0; i < mapSize; ++i)
		{
			Tile *tile = battle->getTiles()[i];
			if (tile == 0)
			{
				continue;
			}
			bool rescanned = true;
			while (rescanned)
			{
				rescanned = false;
				for (std::vector<BattleItem *>::iterator it = tile->getInventory()->begin();
				     it != tile->getInventory()->end(); ++it)
				{
					BattleItem *item = *it;
					if (item != 0 && item->getRules() != 0
					    && item->getRules()->getBattleType() == BT_GRENADE
					    && item->getFuseTimer() == 0)
					{
						battle->removeItem(item);
						rescanned = true;
						break;
					}
				}
			}
		}
		for (std::vector<BattleItem *>::iterator it = battle->getItems()->begin();
		     it != battle->getItems()->end(); ++it)
		{
			BattleItem *item = *it;
			if (item != 0 && item->getRules() != 0)
			{
				const BattleType bt = item->getRules()->getBattleType();
				if ((bt == BT_GRENADE || bt == BT_PROXIMITYGRENADE) && item->getFuseTimer() > 0)
				{
					item->setFuseTimer(item->getFuseTimer() - 1);
				}
			}
		}
		battle->endTurn();
	}

	BattleItem *mountStockRifle(BattleUnit *actor, int clipRounds = 25)
	{
		RuleItem *rifleRules = game->getMod()->getItem("STR_RIFLE", false);
		RuleItem *clipRules = game->getMod()->getItem("STR_RIFLE_CLIP", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(rifleRules != 0 && clipRules != 0 && hand != 0, "stock rifle rules");
		if (rifleRules == 0 || clipRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *rifle = new BattleItem(rifleRules, battle->getCurrentItemId());
		BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
		if (clipRounds > 0)
		{
			clip->setAmmoQuantity(clipRounds);
		}
		expect(rifle->setAmmoItem(clip) == 0, "rifle loaded");
		rifle->setSlot(hand);
		rifle->setSlotX(0);
		rifle->setSlotY(0);
		rifle->moveToOwner(actor);
		battle->getItems()->push_back(rifle);
		battle->getItems()->push_back(clip);
		actor->setActiveHand("STR_RIGHT_HAND");
		return rifle;
	}

	MapData *ruleLosBlockerTile()
	{
		if (game == 0)
		{
			return 0;
		}
		const char *setNames[] = { "UFO1", "URBAN", "JUNGLE" };
		for (size_t s = 0; s < sizeof(setNames) / sizeof(setNames[0]); ++s)
		{
			MapDataSet *set = game->getMod()->getMapDataSet(setNames[s]);
			if (set == 0)
			{
				continue;
			}
			for (size_t i = 0; i < set->getSize(); ++i)
			{
				MapData *md = set->getObject(i);
				if (md != 0 && md->getBlock(DT_NONE) >= 200)
				{
					return md;
				}
			}
		}
		return 0;
	}

	void placeTerrainWall(const Position &pos)
	{
		Tile *tile = battle != 0 ? battle->getTile(pos) : 0;
		if (tile == 0)
		{
			return;
		}
		MapData *rule = ruleLosBlockerTile();
		if (rule != 0)
		{
			tile->setMapData(rule, 0, 0, O_OBJECT);
			logEvidence("terrain-wall", "pos=" + std::to_string(pos.x) + "," + std::to_string(pos.y) + " rulesetObject=yes");
			return;
		}
		MapData *floor = renderableFloorTile(game != 0 ? game->getMod() : 0);
		expect(floor != 0 && floor->getDataset() != 0, "synthetic wall floorSet");
		if (floor == 0 || floor->getDataset() == 0)
		{
			return;
		}
		MapData *wall = new MapData(floor->getDataset());
		wall->setObjectType(O_OBJECT);
		wall->setTUCosts(255, 255, 255);
		wall->setStopLOS(true);
		wall->setFlags(false, true, false, 0, false, false, true, true, false);
		wall->setBlockValue(255, 255, 255, 255, 255, 255);
		tile->setMapData(wall, 0, 0, O_OBJECT);
		logEvidence("terrain-wall", "pos=" + std::to_string(pos.x) + "," + std::to_string(pos.y) + " syntheticObject=yes");
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

#ifdef _WIN32
static unsigned int invokeHandleStateSeh(BattlescapeGame *bg)
{
	if (bg == 0)
	{
		return 0;
	}
	__try
	{
		bg->handleState();
		return 0;
	}
	__except (GetExceptionCode())
	{
		return GetExceptionCode();
	}
}
#endif

void diagMcSnapCrashForensics(bool skipBattleSetup = false)
{
	logEvidence("crash-diag", "MC-03A-CRASH-01 forensics: bare directed fire (no relay)");
	CertHarness &h = harness();
	if (!skipBattleSetup)
	{
		h.setupBattle(12, 12);
		logHarnessFloorAudit(h.battle, Position(4, 4, 0));
	}
	BattleUnit *alien = h.addMcHostile(Position(4, 4, 0), 80);
	alien->setVisible(true);
	BattleItem *pistol = h.mountStockPistol(alien);
	const Position target(8, 4, 0);
	BattlescapeGame *bg = h.battleGame();
	expect(bg != 0 && pistol != 0, "battle context for bare fire");
	if (bg == 0 || pistol == 0)
	{
		return;
	}
	std::string reject;
	const bool started = bg->tryExecuteCompanionDirectedFire(alien, target, pistol, BA_SNAPSHOT, &reject);
	logEvidence("crash-diag", std::string("tryExecuteCompanionDirectedFire=") + (started ? "true" : "false")
		+ (reject.empty() ? "" : " reject=" + reject));
	expect(started, "bare companion directed fire starts");
	expect(bg->isBusy(), "battle busy after bare fire accept");
#ifdef _WIN32
	int bareSteps = 0;
	unsigned int firstFault = 0;
	while (bg->isBusy() && bareSteps < 512)
	{
		const unsigned int ex = invokeHandleStateSeh(bg);
		if (ex != 0)
		{
			firstFault = ex;
			std::ostringstream hex;
			hex << std::hex << ex;
			logEvidence("crash-diag", std::string("bare handleState fault step=") + std::to_string(bareSteps)
				+ " SEH=0x" + hex.str());
			break;
		}
		++bareSteps;
	}
	logEvidence("crash-diag", std::string("bare fire state ticks=") + std::to_string(bareSteps)
		+ " busyAfter=" + (bg->isBusy() ? "yes" : "no"));
	if (firstFault != 0)
	{
		logEvidence("crash-diag", "failure reproduced without MC relay or MC command type");
	}
#else
	for (int bareSteps = 0; bareSteps < 512 && bg->isBusy(); ++bareSteps)
	{
		bg->handleState();
	}
#endif
	h.endWalkCase();
}

void certMcSnapFireTerminal(bool skipBattleSetup = false)
{
	CertHarness &h = harness();
	if (!skipBattleSetup)
	{
		h.setupBattle(12, 12);
		logHarnessFloorAudit(h.battle, Position(4, 4, 0));
	}
	h.resetRelayFiles();
	const Position shooterPos(4, 4, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	expect(alien != 0, "MC shooter");
	expect(alien->getId() == MC_HOSTILE_ID, "MC shooter id");
	alien->setVisible(true);
	BattleItem *pistol = h.mountStockPistol(alien);
	expect(pistol != 0, "pistol mounted");
	const int ammo0 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	expect(ammo0 > 0, "pistol starts loaded");
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot for MC_SNAP_SHOT");
	if (h.battle != 0 && h.battle->getTileEngine() != 0)
	{
		h.battle->getTileEngine()->calculateFOV(alien);
		h.battle->getTileEngine()->recalculateFOV();
	}
	const Position target(8, 4, 0);
	h.publish(h.mcFireCmd("cert-mc-snap-fire", "MC_SNAP_SHOT", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "MC_SNAP_SHOT EXECUTING");
	expect(h.battleGame()->isBusy(), "battle busy after MC_SNAP_SHOT accept");
	logEvidence("mc-snap-fire-executing", h.resultJson());
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_SNAP_SHOT", "EXECUTING echoes MC_SNAP_SHOT");
	const int ticks = h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "MC_SNAP_SHOT COMPLETED after projectile execution");
	expect(!h.battleGame()->isBusy(), "battle idle after MC_SNAP_SHOT terminal");
	expect(alien->getPosition() == shooterPos, "MC snap does not move shooter");
	expect(h.resultJson().find("\"final_position\"") != std::string::npos, "terminal final_position");
	expect(h.resultJson().find("\"x\": 4") != std::string::npos, "relay final_position x");
	expect(alien->getId() == MC_HOSTILE_ID, "actor id unchanged");
	expect(alien->getTimeUnits() < tu0, "TU spent");
	const int ammo1 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	expect(ammo0 >= 0 && ammo1 == ammo0 - 1, "exactly one pistol round consumed");
	int remTu = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &remTu), "remaining_tu in result");
	expect(remTu == alien->getTimeUnits(), "remaining_tu matches engine");
	int actorJson = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "actor_unit_id", &actorJson), "actor_unit_id");
	expect(actorJson == MC_HOSTILE_ID, "relay actor_unit_id");
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "terminal command_type");
	expect(cmdType == "MC_SNAP_SHOT", "terminal echoes MC_SNAP_SHOT");
	logEvidence("mc-snap-fire-terminal", h.resultJson());
	logEvidence("mc-snap-fire-engine",
		"tu=" + std::to_string(alien->getTimeUnits()) + " ammo="
			+ std::to_string(ammo1) + " ticks=" + std::to_string(ticks));
	h.endWalkCase();
}

void certMcAutoFireTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position shooterPos(5, 5, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 120);
	expect(alien != 0, "MC auto shooter");
	alien->setVisible(true);
	BattleItem *rifle = h.mountStockRifle(alien);
	expect(rifle != 0, "rifle mounted");
	expect(rifle->getRules()->getAccuracyAuto() != 0, "rifle supports auto");
	const int stockAutoShots = rifle->getRules()->getAutoShots();
	expect(stockAutoShots > 0, "stock autoShots configured");
	const int ammo0 = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
	const int tu0 = alien->getTimeUnits();
	const int tuAuto = alien->getActionTUs(BA_AUTOSHOT, rifle);
	expect(tuAuto > 0, "auto TU computable");
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot for MC_AUTO_SHOT");
	if (h.battle != 0 && h.battle->getTileEngine() != 0)
	{
		h.battle->getTileEngine()->calculateFOV(alien);
		h.battle->getTileEngine()->recalculateFOV();
	}
	const Position target(9, 5, 0);
	h.publish(h.mcFireCmd("cert-mc-auto-fire", "MC_AUTO_SHOT", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "MC_AUTO_SHOT EXECUTING");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_AUTO_SHOT", "EXECUTING echoes MC_AUTO_SHOT");
	const int ticks = h.advanceUntilTerminal();
	const std::string terminal = h.status();
	expect(terminal == "COMPLETED", "MC_AUTO_SHOT COMPLETED");
	expect(!h.battleGame()->isBusy(), "battle idle after MC_AUTO_SHOT");
	expect(alien->getPosition() == shooterPos, "auto does not move shooter");
	const int ammo1 = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
	const int roundsFired = ammo0 - ammo1;
	expect(roundsFired >= 1, "auto consumed at least one round");
	expect(roundsFired <= stockAutoShots, "auto rounds bounded by stock autoShots");
	expect(roundsFired == stockAutoShots, "full stock auto burst on COMPLETED");
	expect(alien->getTimeUnits() < tu0, "auto TU spent");
	int remTu = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &remTu), "remaining_tu");
	expect(remTu == alien->getTimeUnits(), "remaining_tu matches engine");
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "terminal command_type");
	expect(cmdType == "MC_AUTO_SHOT", "terminal echoes MC_AUTO_SHOT");
	logEvidence("mc-auto-fire-terminal", h.resultJson());
	logEvidence("mc-auto-fire-engine",
		"autoShots=" + std::to_string(stockAutoShots) + " roundsFired=" + std::to_string(roundsFired)
			+ " tu=" + std::to_string(alien->getTimeUnits()) + " tuAuto=" + std::to_string(tuAuto)
			+ " ticks=" + std::to_string(ticks));
	h.endWalkCase();
}

void certMcAimedFireTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position shooterPos(4, 4, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	expect(alien != 0, "MC aimed shooter");
	alien->setVisible(true);
	BattleItem *pistol = h.mountStockPistol(alien);
	expect(pistol != 0, "pistol for aimed");
	expect(pistol->getRules()->getAccuracyAimed() != 0, "pistol supports aimed");
	const int tuAimed = alien->getActionTUs(BA_AIMEDSHOT, pistol);
	expect(tuAimed > 0, "aimed TU computable");
	const int ammo0 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot for MC_AIMED_SHOT");
	if (h.battle != 0 && h.battle->getTileEngine() != 0)
	{
		h.battle->getTileEngine()->calculateFOV(alien);
		h.battle->getTileEngine()->recalculateFOV();
	}
	const Position target(8, 4, 0);
	h.publish(h.mcFireCmd("cert-mc-aimed-fire", "MC_AIMED_SHOT", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "MC_AIMED_SHOT EXECUTING");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_AIMED_SHOT", "EXECUTING echoes MC_AIMED_SHOT");
	const int ticks = h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "MC_AIMED_SHOT COMPLETED");
	expect(!h.battleGame()->isBusy(), "battle idle after MC_AIMED_SHOT");
	expect(alien->getPosition() == shooterPos, "aimed does not move shooter");
	const int ammo1 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	expect(ammo0 >= 0 && ammo1 == ammo0 - 1, "exactly one aimed round consumed");
	expect(alien->getTimeUnits() <= tu0 - tuAimed + 4, "aimed TU within stock aimed cost + facing");
	expect(alien->getTimeUnits() >= tu0 - tuAimed - 4, "aimed TU not under-reported");
	int remTu = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &remTu), "remaining_tu");
	expect(remTu == alien->getTimeUnits(), "remaining_tu matches engine");
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "terminal command_type");
	expect(cmdType == "MC_AIMED_SHOT", "terminal echoes MC_AIMED_SHOT");
	logEvidence("mc-aimed-fire-terminal", h.resultJson());
	logEvidence("mc-aimed-fire-engine",
		"tu=" + std::to_string(alien->getTimeUnits()) + " tuAimed=" + std::to_string(tuAimed)
			+ " ammo=" + std::to_string(ammo1) + " ticks=" + std::to_string(ticks));
	h.endWalkCase();
}

void certMcAuthorityLossDuringFire()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position shooterPos(4, 4, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	BattleItem *pistol = h.mountStockPistol(alien);
	expect(alien != 0 && pistol != 0, "MC auth-loss fire setup");
	alien->setVisible(true);
	const int ammoAtStart = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	const int tuAtStart = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot auth-loss fire");
	if (h.battle != 0 && h.battle->getTileEngine() != 0)
	{
		h.battle->getTileEngine()->calculateFOV(alien);
		h.battle->getTileEngine()->recalculateFOV();
	}
	const Position target(8, 4, 0);
	h.publish(h.mcFireCmd("cert-mc-fire-auth-loss", "MC_SNAP_SHOT", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "EXECUTING before fire authority reclaim");
	BattlescapeGame *bg = h.battleGame();
	bool sawProjectile = false;
	bool sawAmmoSpend = false;
	int preTicks = 0;
	for (; preTicks < kMaxFireTicks && bg != 0 && bg->isBusy() && h.status() == "EXECUTING"; ++preTicks)
	{
		if (bg->getMap() != 0 && bg->getMap()->getProjectile() != 0)
		{
			sawProjectile = true;
		}
		const int ammoNow = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
		if (ammoNow >= 0 && ammoNow < ammoAtStart)
		{
			sawAmmoSpend = true;
		}
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else
		{
			bg->handleState();
		}
		h.tickRelay();
		if ((sawProjectile || sawAmmoSpend) && preTicks >= 4)
		{
			break;
		}
	}
	logEvidence("mc-fire-auth-pre", std::string("sawProjectile=") + (sawProjectile ? "true" : "false")
		+ " sawAmmoSpend=" + (sawAmmoSpend ? "true" : "false") + " preTicks=" + std::to_string(preTicks)
		+ " ammo=" + std::to_string(pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1));
	expect(sawProjectile || sawAmmoSpend, "native fire sequence started before reclaim");
	expect(h.battle->reclaimCompanionTacticalMcHostileForHuman(MC_HOSTILE_ID), "reclaim during MC fire");
	h.writeControl(false);
	const int ammoAfterReclaim = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	std::string st;
	for (int i = 0; i < kMaxFireTicks; ++i)
	{
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else if (bg != 0 && bg->isBusy())
		{
			bg->handleState();
		}
		h.tickRelay();
		st = h.status();
		if (!st.empty() && st != "EXECUTING")
		{
			break;
		}
	}
	expect(st == "INTERRUPTED" || st == "FAILED", "authority-loss fire terminal");
	const int ammoTerminal = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	const int roundsCommitted = ammoAtStart - ammoTerminal;
	expect(roundsCommitted >= 0 && roundsCommitted <= 1,
		"snap interrupt: at most one committed round on pistol snap");
	for (int j = 0; j < kMaxFireTicks && bg != 0 && bg->isBusy(); ++j)
	{
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else
		{
			bg->handleState();
		}
		h.tickRelay();
	}
	expect(bg == 0 || !bg->isBusy(), "battle idle before follow-up reject probe");
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	h.tickRelay();
	logEvidence("mc-fire-auth-loss",
		"status=" + st + " roundsCommitted=" + std::to_string(roundsCommitted)
			+ " ammoStart=" + std::to_string(ammoAtStart) + " afterReclaim=" + std::to_string(ammoAfterReclaim)
			+ " terminal=" + std::to_string(ammoTerminal) + " tu=" + std::to_string(alien->getTimeUnits())
			+ " tuStart=" + std::to_string(tuAtStart)
			+ " note=committed rounds are not rolled back by authority loss");
	h.resetRelayFiles();
	expect(h.exportBinding(), "re-export after interrupt");
	const int tuBeforeSecond = alien->getTimeUnits();
	h.writeControl(false);
	const int sv2 = h.bindingStateVersion;
	h.publish(h.mcFireCmd("cert-mc-fire-auth-denied", "MC_SNAP_SHOT", target.x, target.y, target.z, sv2));
	h.tickRelay();
	logEvidence("mc-fire-auth-denied", h.resultJson());
	expect(h.status() == "REJECTED", "human MC fire rejected after reclaim");
	std::string code;
	CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &code);
	expect(code == "NOT_AI_CONTROLLED", "no new authorized MC fire after human reclaim");
	expect(alien->getTimeUnits() == tuBeforeSecond, "no TU on rejected post-reclaim fire");
	h.endWalkCase();
}

void certMcAuthorityLossDuringAutoFire()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position shooterPos(5, 5, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 120);
	BattleItem *rifle = h.mountStockRifle(alien);
	expect(alien != 0 && rifle != 0, "MC auto auth-loss setup");
	alien->setVisible(true);
	const int stockAutoShots = rifle->getRules()->getAutoShots();
	expect(stockAutoShots > 1, "multi-round auto for partial interrupt");
	const int ammoAtStart = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
	const int tuAtStart = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot auto auth-loss");
	if (h.battle != 0 && h.battle->getTileEngine() != 0)
	{
		h.battle->getTileEngine()->calculateFOV(alien);
		h.battle->getTileEngine()->recalculateFOV();
	}
	const Position target(9, 5, 0);
	h.publish(h.mcFireCmd("cert-mc-auto-auth-loss", "MC_AUTO_SHOT", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "EXECUTING before auto authority reclaim");
	BattlescapeGame *bg = h.battleGame();
	int preTicks = 0;
	int roundsBeforeReclaim = 0;
	for (; preTicks < kMaxFireTicks && bg != 0 && bg->isBusy() && h.status() == "EXECUTING"; ++preTicks)
	{
		const int ammoNow = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
		if (ammoNow >= 0 && ammoAtStart > ammoNow)
		{
			roundsBeforeReclaim = ammoAtStart - ammoNow;
			if (roundsBeforeReclaim >= 1 && roundsBeforeReclaim < stockAutoShots)
			{
				break;
			}
		}
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else
		{
			bg->handleState();
		}
		h.tickRelay();
	}
	expect(roundsBeforeReclaim >= 1, "at least one auto round committed before reclaim");
	expect(roundsBeforeReclaim < stockAutoShots, "reclaim before full stock auto burst");
	logEvidence("mc-auto-auth-pre",
		"roundsCommittedBeforeReclaim=" + std::to_string(roundsBeforeReclaim) + " preTicks=" + std::to_string(preTicks)
			+ " ammo=" + std::to_string(rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1));
	expect(h.battle->reclaimCompanionTacticalMcHostileForHuman(MC_HOSTILE_ID), "reclaim during MC auto fire");
	h.writeControl(false);
	const int ammoAfterReclaim = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
	std::string st;
	for (int i = 0; i < kMaxFireTicks; ++i)
	{
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else if (bg != 0 && bg->isBusy())
		{
			bg->handleState();
		}
		h.tickRelay();
		st = h.status();
		if (!st.empty() && st != "EXECUTING")
		{
			break;
		}
	}
	expect(st == "INTERRUPTED" || st == "FAILED", "auto authority-loss terminal");
	const int ammoAfterIdle = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
	for (int j = 0; j < kMaxFireTicks && bg != 0 && bg->isBusy(); ++j)
	{
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else
		{
			bg->handleState();
		}
		h.tickRelay();
	}
	expect(bg == 0 || !bg->isBusy(), "battle idle after auto interrupt");
	const int ammoTerminal = rifle->getAmmoItem() != 0 ? rifle->getAmmoItem()->getAmmoQuantity() : -1;
	const int totalRoundsSpent = ammoAtStart - ammoTerminal;
	const int roundsAfterReclaim = ammoAfterReclaim - ammoTerminal;
	expect(totalRoundsSpent >= roundsBeforeReclaim, "ammo accounting includes pre-reclaim rounds");
	expect(totalRoundsSpent <= stockAutoShots, "auto interrupt: native burst bounded by stock autoShots");
	expect(roundsAfterReclaim <= stockAutoShots - roundsBeforeReclaim,
		"post-reclaim ammo delta only from in-flight native auto (no new relay burst)");
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	h.tickRelay();
	logEvidence("mc-auto-auth-loss",
		"status=" + st + " roundsBeforeReclaim=" + std::to_string(roundsBeforeReclaim)
			+ " totalRoundsSpent=" + std::to_string(totalRoundsSpent) + " ammoStart=" + std::to_string(ammoAtStart)
			+ " afterReclaim=" + std::to_string(ammoAfterReclaim) + " afterIdle=" + std::to_string(ammoAfterIdle)
			+ " terminal=" + std::to_string(ammoTerminal) + " tu=" + std::to_string(alien->getTimeUnits())
			+ " tuStart=" + std::to_string(tuAtStart));
	h.resetRelayFiles();
	expect(h.exportBinding(), "re-export after auto interrupt");
	const int tuBeforeSecond = alien->getTimeUnits();
	h.writeControl(false);
	const int sv2 = h.bindingStateVersion;
	h.publish(h.mcFireCmd("cert-mc-auto-auth-denied", "MC_AUTO_SHOT", target.x, target.y, target.z, sv2));
	h.tickRelay();
	logEvidence("mc-auto-auth-denied", h.resultJson());
	expect(h.status() == "REJECTED", "human MC auto fire rejected after reclaim");
	std::string code;
	CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &code);
	expect(code == "NOT_AI_CONTROLLED", "no new authorized MC auto after human reclaim");
	expect(alien->getTimeUnits() == tuBeforeSecond, "no TU on rejected post-reclaim auto");
	h.endWalkCase();
}

void certMcTerrainWallSnapFire()
{
	CertHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position shooterPos(3, 3, 0);
	const Position wallPos(8, 3, 0);
	h.placeTerrainWall(wallPos);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	BattleItem *pistol = h.mountStockPistol(alien);
	expect(alien != 0 && pistol != 0, "MC terrain wall snap setup");
	alien->setVisible(true);
	const int ammo0 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot terrain wall snap");
	if (h.battle != 0 && h.battle->getTileEngine() != 0)
	{
		h.battle->getTileEngine()->calculateFOV(alien);
		h.battle->getTileEngine()->recalculateFOV();
	}
	h.publish(h.mcFireCmd("cert-mc-terrain-wall-snap", "MC_SNAP_SHOT", wallPos.x, wallPos.y, wallPos.z,
		h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "terrain wall snap EXECUTING");
	const int ticks = h.advanceUntilTerminal();
	std::string terminal = h.status();
	expect(terminal == "COMPLETED", "terrain wall MC_SNAP_SHOT COMPLETED");
	expect(!h.battleGame()->isBusy(), "battle idle after terrain wall snap");
	const int ammo1 = pistol->getAmmoItem() != 0 ? pistol->getAmmoItem()->getAmmoQuantity() : -1;
	expect(ammo0 > 0 && ammo1 == ammo0 - 1, "terrain wall snap spent one round");
	expect(alien->getPosition() == shooterPos, "terrain snap does not move shooter");
	logEvidence("mc-terrain-wall-snap",
		"terminal=" + terminal + " target=" + std::to_string(wallPos.x) + "," + std::to_string(wallPos.y)
			+ " ammo=" + std::to_string(ammo0) + "->" + std::to_string(ammo1) + " tu=" + std::to_string(tu0)
			+ "->" + std::to_string(alien->getTimeUnits()) + " ticks=" + std::to_string(ticks));
	logEvidence("mc-terrain-wall-snap-relay", h.resultJson());
	h.endWalkCase();
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
	expect(h.exportBinding(), "snapshot human MC fire");
	h.publish(h.mcFireCmd("cert-mc-fire-human", "MC_SNAP_SHOT", 5, 2, 0, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "human MC fire rejected");
	std::string code;
	CompanionJsonLite::extractJsonStringField(h.resultJson(), "reject_code", &code);
	expect(code == "NOT_AI_CONTROLLED", "NOT_AI_CONTROLLED");
	expect(alien->getTimeUnits() == tu0, "no TU on fire reject");
	h.endWalkCase();
}

void certMcDropTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position pos(3, 3, 0);
	BattleUnit *alien = h.addMcHostile(pos, 80);
	BattleItem *clip = h.mountPistolClipInLeftHand(alien);
	expect(clip != 0, "carried clip for MC_DROP");
	expect(alien->getItem("STR_LEFT_HAND", 0, 0) == clip, "clip visible in left hand before export");
	const char *sourceSlot = "STR_LEFT_HAND";
	const bool sourceCoords = false;
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_DROP");
	expect(alien->getItem("STR_LEFT_HAND", 0, 0) == clip, "clip survives snapshot export");
	syncRelayInventoryVector(alien, clip);
	BattleItem *clipRelay = alien->getItem(sourceSlot, 0, 0);
	const int clipId = clipRelay != 0 ? clipRelay->getId() : clip->getId();
	h.publish(h.mcDropCmd("cert-mc-drop", clipId, sourceSlot, 0, 0, sourceCoords, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_DROP COMPLETED");
	expect(alien->getTimeUnits() < tu0, "TU spent on drop");
	Tile *tile = h.battle->getTile(pos);
	BattleItem *onGround = h.findItemOnTile(tile, clip->getId());
	expect(onGround != 0, "dropped item on actor tile ground");
	expect(onGround->getOwner() == 0, "ground item has no owner");
	logEvidence("mc-drop-terminal", h.resultJson());
	h.endWalkCase();
}

void certMcPickupTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position pos(4, 4, 0);
	BattleUnit *alien = h.addMcHostile(pos, 80);
	BattleItem *clip = h.mountMcCarriedPistolClip(alien);
	const char *dropSource = "STR_BELT";
	bool dropCoords = true;
	if (alien->getItem("STR_LEFT_HAND", 0, 0) == clip)
	{
		dropSource = "STR_LEFT_HAND";
		dropCoords = false;
	}
	expect(clip != 0, "carried clip before drop->pickup");
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_PICKUP drop");
	const int clipId = clip->getId();
	syncRelayInventoryVector(alien, clip);
	h.publish(h.mcDropCmd("cert-mc-pickup-drop", clipId, dropSource, 0, 0, dropCoords, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "setup drop before pickup");
	expect(h.findItemOnTile(h.battle->getTile(pos), clipId) != 0, "clip on ground before pickup");
	expect(h.exportBinding(), "snapshot MC_PICKUP");
	const int tu0 = alien->getTimeUnits();
	syncRelayInventoryVector(alien, clip);
	h.publish(h.mcPickupCmd("cert-mc-pickup", clipId, "STR_LEFT_HAND", 0, 0,
		pos.x, pos.y, pos.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_PICKUP COMPLETED");
	expect(alien->getTimeUnits() < tu0, "TU spent on pickup");
	expect(h.findItemOnTile(alien->getTile(), clipId) == 0, "item off ground");
	expect(alien->getItem("STR_LEFT_HAND", 0, 0) != 0 && alien->getItem("STR_LEFT_HAND", 0, 0)->getId() == clipId,
		"clip in left hand after pickup");
	logEvidence("mc-pickup-terminal", h.resultJson());
	h.endWalkCase();
}

void certMcInventoryMoveTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(2, 2, 0), 80);
	BattleItem *clip = h.mountMcCarriedPistolClip(alien);
	const char *invSource = alien->getItem("STR_LEFT_HAND", 0, 0) == clip ? "STR_LEFT_HAND" : "STR_BELT";
	const bool invSourceCoords = invSource[0] != 0 && std::strcmp(invSource, "STR_LEFT_HAND") != 0;
	expect(clip != 0, "carried clip for inventory move");
	expect(alien->getItem("STR_RIGHT_HAND", 0, 0) == 0, "right hand empty before move");
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_INVENTORY_MOVE");
	syncRelayInventoryVector(alien, clip);
	h.publish(h.mcInventoryMoveCmd("cert-mc-inventory", clip->getId(), invSource, "STR_RIGHT_HAND",
		0, 0, 0, 0, invSourceCoords, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_INVENTORY_MOVE COMPLETED");
	expect(alien->getTimeUnits() < tu0, "TU spent on inventory move");
	expect(alien->getItem("STR_RIGHT_HAND", 0, 0) == clip, "clip in right hand");
	expect(alien->getItem("STR_LEFT_HAND", 0, 0) == 0, "left hand empty after move");
	logEvidence("mc-inventory-move-terminal", h.resultJson());
	h.endWalkCase();
}

void certMcReloadTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(3, 3, 0), 80);
	BattleItem *pistol = h.mountStockPistolEmpty(alien);
	BattleItem *clip = h.mountMcCarriedPistolClip(alien);
	expect(pistol != 0 && clip != 0, "empty pistol + carried clip");
	expect(pistol->getAmmoItem() == 0, "pistol chamber empty");
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_RELOAD");
	syncRelayInventoryVector(alien, pistol);
	syncRelayInventoryVector(alien, clip);
	h.publish(h.mcReloadCmd("cert-mc-reload", pistol->getId(), clip->getId(), h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_RELOAD COMPLETED");
	expect(alien->getTimeUnits() < tu0, "TU spent on reload");
	expect(pistol->getAmmoItem() != 0, "magazine inserted after reload");
	expect(pistol->getAmmoItem()->getId() == clip->getId(), "same ammo battle item id");
	logEvidence("mc-reload-terminal", h.resultJson());
	h.endWalkCase();
}

void certMcUnloadTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(3, 3, 0), 80);
	BattleItem *rifle = h.mountStockRifleLoaded(alien);
	expect(rifle != 0 && rifle->getAmmoItem() != 0, "loaded rifle for unload");
	const int ammoId = rifle->getAmmoItem()->getId();
	const int tu0 = alien->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_UNLOAD");
	h.publish(h.mcUnloadCmd("cert-mc-unload", rifle->getId(), ammoId, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_UNLOAD COMPLETED");
	expect(alien->getTimeUnits() < tu0, "TU spent on unload");
	expect(rifle->getAmmoItem() == 0, "weapon unloaded");
	expect(unitCarriesItemId(alien, ammoId), "unloaded magazine remains on actor inventory");
	logEvidence("mc-unload-terminal", h.resultJson());
	h.endWalkCase();
}

void certMcGrenadeThrowInventoryState()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position shooterPos(2, 2, 0);
	const Position target(3, 2, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	BattleItem *grenade = h.mountStockGrenade(alien);
	const int grenadeId = grenade != 0 ? grenade->getId() : -1;
	expect(grenade != 0, "grenade for throw inventory state");
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot throw inventory");
	h.publish(h.mcThrowCmd("cert-mc-throw-inv", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "throw EXECUTING");
	h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "throw COMPLETED for inventory state");
	expect(alien->getItem("STR_RIGHT_HAND", 0, 0) == 0, "hand empty after MC_THROW");
	expect(!h.battleItemExists(h.battle, grenadeId) || h.findItemOnTile(h.battle->getTile(target), grenadeId) != 0,
		"grenade left hand (ground or consumed item record)");
	logEvidence("mc-throw-inventory", h.resultJson());
	h.endWalkCase();
}

void certMcGrenadeDetonationAfterShortFuse()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position shooterPos(2, 2, 0);
	const Position target(2, 3, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	BattleItem *grenade = h.mountStockGrenade(alien);
	expect(grenade != 0, "grenade for short fuse");
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot short fuse prime");
	h.publish(h.mcPrimeCmd("cert-mc-prime-fuse1", 1, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "fuse-1 prime COMPLETED");
	expect(grenade->getFuseTimer() == 1, "fuse set to 1");
	expect(h.exportBinding(), "re-export before throw");
	h.publish(h.mcThrowCmd("cert-mc-throw-fuse1", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "short fuse throw EXECUTING");
	h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "short fuse throw COMPLETED");
	Tile *targetTile = h.battle->getTile(target);
	BattleItem *groundGrenade = h.findItemOnTile(targetTile, grenade != 0 ? grenade->getId() : -1);
	if (groundGrenade == 0)
	{
		for (std::vector<BattleItem *>::iterator it = targetTile->getInventory()->begin();
		     it != targetTile->getInventory()->end(); ++it)
		{
			if (*it != 0 && (*it)->getRules() != 0
			    && ((*it)->getRules()->getBattleType() == BT_GRENADE
				|| (*it)->getRules()->getBattleType() == BT_PROXIMITYGRENADE))
			{
				groundGrenade = *it;
				break;
			}
		}
	}
	expect(groundGrenade != 0, "thrown grenade on target tile before detonation");
	const int groundGrenadeId = groundGrenade != 0 ? groundGrenade->getId() : -1;
	const int fuseBeforeAdvance = groundGrenade != 0 ? groundGrenade->getFuseTimer() : -1;
	expect(fuseBeforeAdvance >= 0, "ground grenade still fused");
	auto grenadeStillAtTarget = [&]() {
		return h.findItemOnTile(targetTile, groundGrenadeId) != 0;
	};
	for (int turnStep = 0; turnStep < 4 && (grenadeStillAtTarget() || h.battleItemExists(h.battle, groundGrenadeId)); ++turnStep)
	{
		h.applyStockPrimedGrenadeTurnSlice();
		h.tickBattleUntilItemAbsent(groundGrenadeId);
	}
	const bool removed = !grenadeStillAtTarget() || !h.battleItemExists(h.battle, groundGrenadeId);
	BattleItem *groundAfter = h.findItemOnTile(targetTile, groundGrenadeId);
	const int fuseAfter = groundAfter != 0 ? groundAfter->getFuseTimer()
				: (removed ? -1 : (groundGrenade != 0 ? groundGrenade->getFuseTimer() : -1));
	expect(removed || fuseAfter < fuseBeforeAdvance, "fuse advanced or grenade removed by stock turn/tick");
	logEvidence("mc-grenade-detonation",
		"groundId=" + std::to_string(groundGrenadeId) + " removed=" + (removed ? "yes" : "no")
			+ " fuseBefore=" + std::to_string(fuseBeforeAdvance) + " fuseAfter=" + std::to_string(fuseAfter));
	h.endWalkCase();
}

void certMcDropPrimedGrenadeTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position pos(4, 4, 0);
	BattleUnit *alien = h.addMcHostile(pos, 80);
	BattleItem *grenade = h.mountStockGrenade(alien);
	expect(grenade != 0, "grenade for primed drop");
	const int grenadeId = grenade->getId();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_PRIME before drop");
	h.publish(h.mcPrimeCmd("cert-mc-prime-drop", 5, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "prime before drop");
	expect(grenade->getFuseTimer() == 5, "fuse primed");
	expect(h.exportBinding(), "snapshot MC_DROP primed");
	h.publish(h.mcDropCmd("cert-mc-drop-primed", grenadeId, "STR_RIGHT_HAND", 0, 0, false, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_DROP primed COMPLETED");
	expect(alien->getItem("STR_RIGHT_HAND", 0, 0) == 0, "hand empty after primed drop");
	BattleItem *ground = h.findItemOnTile(h.battle->getTile(pos), grenadeId);
	expect(ground != 0, "primed grenade on ground");
	expect(ground->getFuseTimer() == 5, "fuse preserved on ground drop");
	logEvidence("mc-drop-primed", h.resultJson());
	h.endWalkCase();
}

void certMcReloadHumanRejected()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(2, 2, 0), 80);
	BattleItem *pistol = h.mountStockPistolEmpty(alien);
	BattleItem *clip = h.mountPistolClipInLeftHand(alien);
	expect(pistol != 0 && clip != 0, "reload human reject setup");
	const int tu0 = alien->getTimeUnits();
	h.writeControl(false);
	expect(h.exportBinding(), "snapshot human MC reload");
	h.publish(h.mcReloadCmd("cert-mc-reload-human", pistol->getId(), clip->getId(), h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "REJECTED", "human MC reload rejected");
	expect(h.rejectCode() == "NOT_AI_CONTROLLED", "NOT_AI_CONTROLLED");
	expect(pistol->getAmmoItem() == 0, "no reload on reject");
	expect(alien->getTimeUnits() == tu0, "no TU on reload reject");
	h.endWalkCase();
}

void certMcPrimeTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	BattleUnit *alien = h.addMcHostile(Position(3, 3, 0), 80);
	BattleItem *grenade = h.mountStockGrenade(alien);
	expect(grenade != 0, "alien grenade in hand");
	expect(grenade->getFuseTimer() < 0, "grenade unprimed");
	h.writeControl(true);
	const int tuBefore = alien->getTimeUnits();
	expect(h.exportBinding(), "snapshot MC_PRIME");
	h.publish(h.mcPrimeCmd("cert-mc-prime", 3, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "COMPLETED", "MC_PRIME COMPLETED");
	std::string cmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &cmdType), "command_type");
	expect(cmdType == "MC_PRIME", "result echoes MC_PRIME");
	expect(grenade->getFuseTimer() == 3, "fuse primed via relay");
	expect(alien->getTimeUnits() < tuBefore, "TU spent on prime");
	h.endWalkCase();
}

void certMcThrowTerminal()
{
	CertHarness &h = harness();
	h.setupBattle(8, 8);
	h.resetRelayFiles();
	const Position shooterPos(2, 2, 0);
	const Position target(3, 3, 0);
	BattleUnit *alien = h.addMcHostile(shooterPos, 80);
	BattleItem *grenade = h.mountStockGrenade(alien);
	expect(grenade != 0, "throw grenade");
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot MC_THROW");
	h.publish(h.mcThrowCmd("cert-mc-throw", target.x, target.y, target.z, h.bindingStateVersion));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "MC_THROW EXECUTING");
	expect(h.battleGame()->isBusy(), "busy after MC_THROW accept");
	h.advanceUntilTerminal(true);
	expect(h.status() == "COMPLETED", "MC_THROW COMPLETED");
	std::string throwCmdType;
	expect(CompanionJsonLite::extractJsonStringField(h.resultJson(), "command_type", &throwCmdType), "command_type");
	expect(throwCmdType == "MC_THROW", "result echoes MC_THROW");
	expect(!h.battleGame()->isBusy(), "idle after throw");
	expect(alien->getItem("STR_RIGHT_HAND", 0, 0) == 0, "throw leaves hand empty");
	const int grenadeId = grenade != 0 ? grenade->getId() : -1;
	if (grenadeId > 0)
	{
		expect(h.findItemOnTile(h.battle->getTile(target), grenadeId) != 0,
			"thrown grenade rests on target tile");
	}
	logEvidence("mc-throw-terminal", h.resultJson());
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
	if (!h.initGame(argc > 0 ? argv[0] : "companion_mc_hostile_equipment_relay_terminal_test", defaultDataDir()))
	{
		std::cerr << "mc equipment relay terminal cert: Options/Game init failed" << std::endl;
		return 1;
	}
	h.initRelayEnv();
	certMcPrimeTerminal();
	certMcThrowTerminal();
	certMcDropTerminal();
	certMcPickupTerminal();
	certMcInventoryMoveTerminal();
	certMcReloadTerminal();
	certMcUnloadTerminal();
	certMcGrenadeThrowInventoryState();
	certMcGrenadeDetonationAfterShortFuse();
	certMcDropPrimedGrenadeTerminal();
	certMcReloadHumanRejected();

	if (g_failures != 0)
	{
		std::cerr << "companion_mc_hostile_equipment_relay_terminal_test: " << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_mc_hostile_equipment_relay_terminal_test: all tests passed." << std::endl;
	return 0;
}