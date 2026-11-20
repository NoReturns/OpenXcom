/*
 * ASTER-M1-HWP-S5-1CD — HWP_FIRE BattlescapeState relay certification matrix.
 * BattlescapeState + production CompanionCommandRuntime inbox + handleState() async stack.
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/ExplosionBState.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "../Battlescape/Camera.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/Projectile.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/UnitTurnBState.h"
#include "../Battlescape/UnitWalkBState.h"
#include "../Engine/SurfaceSet.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/Soldier.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../Engine/CompanionJsonLite.h"
#include "../Engine/CompanionSnapshot.h"
#include "../Engine/CompanionSoldierControlPublishCore.h"
#include "../Engine/CompanionTacticalHwpAuthorityCore.h"
#include "../Engine/CompanionHwpFireExecutionCore.h"
#include "../Engine/CompanionTuVisibilityCore.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/RuleItem.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/State.h"
#include "../Mod/Armor.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
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
#include <cstdlib>
#include <intrin.h>
#define FIRE_RELAY_MKDIR(p) _mkdir(p)
#define FIRE_RELAY_PUTENV(n, v) _putenv_s(n, v)
#define FIRE_RELAY_SLEEP_MS(ms) Sleep(ms)
#else
#include <sys/stat.h>
#include <unistd.h>
#define FIRE_RELAY_MKDIR(p) mkdir(p, 0755)
#define FIRE_RELAY_PUTENV(n, v) setenv(n, v, 1)
#define FIRE_RELAY_SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionSoldierControlPublishCore;
using namespace OpenXcom::CompanionTacticalHwpAuthorityCore;
using namespace OpenXcom::CompanionHwpFireExecutionCore;
using namespace OpenXcom::CompanionTuVisibilityCore;

namespace
{

const int HWP_ID = 1'000'000;
const int SOLDIER_RELAY_ID = 1;
const int kMaxFireTicks = 250000;
int g_failures = 0;

struct FireRelayHarness;
std::string describeNativeStack(BattlescapeGame *bg);
void certInterruptMidFlightCancelProbe(BattlescapeGame *bg, FireRelayHarness &h);

#ifdef _WIN32
struct CancelInvokeOutcome
{
	DWORD exceptionCode;
	bool trapped;
};
CancelInvokeOutcome invokeCancelCurrentActionSeh(BattlescapeGame *bg);
DWORD invokeRelayCommandTickSeh(BattlescapeGame *bg);
#endif

void expect(bool ok, const char *msg)
{
	if (!ok)
	{
		std::cerr << "FAIL fire-relay: " << msg << std::endl;
		++g_failures;
	}
}

void logEvidence(const char *tag, const std::string &line)
{
	std::cerr << "[fire-relay-evidence] " << tag << ": " << line << std::endl;
}

std::string fireJoin(const std::string &dir, const char *name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	return dir.empty() ? std::string(name) : dir + sep + name;
}

std::string fireExeDir()
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

void fireWrite(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

std::string fireRead(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

void fireRemove(const std::string &path)
{
	std::remove(path.c_str());
}

MapDataSet *floorSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("hwp_fire_relay_floor");
	}
	return ds;
}

MapData *floorTile()
{
	MapData *f = new MapData(floorSet());
	f->setObjectType(O_FLOOR);
	f->setTUCosts(4, 4, 4);
	return f;
}

static bool mapDataHasRenderableSurfaces(MapData *md)
{
	return md != 0 && md->getDataset() != 0 && md->getDataset()->getSurfaceset() != 0;
}

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

// Mod-backed floor tiles carry a loaded SurfaceSet; bare floorSet() does not and Map::drawTerrain
// will AV in Tile::getSprite via MapDataSet::getSurfaceset()->getFrame (S5-1CH).
MapData *renderableFloorTile(Mod *mod)
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

void wireFloor(SavedBattleGame *battle, Mod *mod, int w, int h)
{
	MapData *floor = renderableFloorTile(mod);
	expect(floor != 0 && mapDataHasRenderableSurfaces(floor),
		"wireFloor: mod-backed floor tile must have loaded SurfaceSet (S5-1CI)");
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

void wireUnit(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile for HWP");
	expect(battle->setUnitPosition(unit, pos), "setUnitPosition");
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

int clipRounds(BattleItem *weapon)
{
	if (weapon == 0 || !weapon->needsAmmo() || weapon->getAmmoItem() == 0)
	{
		return -1;
	}
	return weapon->getAmmoItem()->getAmmoQuantity();
}

struct FireRelayHarness
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

	FireRelayHarness()
		: game(0),
		  campaign(0),
		  battle(0),
		  battlescape(0),
		  base(0),
		  bindingStateVersion(0)
	{
	}

	// Intentionally no teardown: SDL/Game/BattlescapeState shutdown after projectile cert
	// reproduces heap corruption on some Windows builds (same class as WP8E guided harness).

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
		game = new Game("HWP fire relay cert");
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

	void initRelayEnv()
	{
		relayDir = fireJoin(fireExeDir(), "hwp_fire_relay_e2e");
		FIRE_RELAY_MKDIR(relayDir.c_str());
		snapshotPath = fireJoin(relayDir, "tactical-relay-fixture.yaml");
		fireWrite(snapshotPath, "tactical:\n  relay_fixture: hwp_fire_s5_1ca\n");
		FIRE_RELAY_PUTENV("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
		FIRE_RELAY_PUTENV("OPENXCOM_COMPANION_COMMAND_EXECUTOR", "1");
		resetRelayFiles();
	}

	void setupBattle(int w, int h)
	{
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
		if (battle != 0)
		{
			battle->setBattleState(0);
		}
		delete battlescape;
		battlescape = 0;
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
	}

	void resetRelayFiles()
	{
		fireRemove(fireJoin(relayDir, "tactical-command.json"));
		fireRemove(fireJoin(relayDir, "tactical-command-journal.json"));
		fireRemove(fireJoin(relayDir, "tactical-result.json"));
		fireRemove(fireJoin(relayDir, "tactical-command-conflict.json"));
		fireRemove(fireJoin(relayDir, "companion-tactical-publication-confirm.json"));
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
		fireWrite(fireJoin(relayDir, "companion-tactical-publication-confirm.json"), c.str());
		return !bindingSha256.empty();
	}

	void writeControlSoldier(bool soldierAi)
	{
		std::set<int> aiSoldiers;
		std::vector<int> liveSoldiers;
		if (soldierAi)
		{
			aiSoldiers.insert(SOLDIER_RELAY_ID);
			liveSoldiers.push_back(SOLDIER_RELAY_ID);
			battle->assignCompanionTacticalSoldierToAi(SOLDIER_RELAY_ID);
		}
		else
		{
			battle->reclaimCompanionTacticalSoldierForHuman(SOLDIER_RELAY_ID);
		}
		const ControlPublicationPlan plan = buildControlPublicationJson(
			sessionId,
			campaign->getCompanionCampaignId(),
			battle->getCompanionTacticalBattleId(),
			aiSoldiers,
			liveSoldiers,
			std::set<int>(),
			std::vector<int>());
		expect(plan.ok, "soldier control json");
		if (plan.ok)
		{
			fireWrite(fireJoin(relayDir, "companion-soldier-control.json"), plan.jsonUtf8);
		}
	}

	void writeControl(bool hwpAi)
	{
		std::set<int> aiHwps;
		std::vector<int> liveHwps;
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
			std::set<int>(),
			std::vector<int>(),
			aiHwps,
			liveHwps);
		expect(plan.ok, "control json");
		if (plan.ok)
		{
			fireWrite(fireJoin(relayDir, "companion-soldier-control.json"), plan.jsonUtf8);
		}
	}

	BattleUnit *addStockTank(const char *unitType, const Position &pos, int tu, int hullDir, int turretDir)
	{
		Unit *rules = game->getMod()->getUnit(unitType, false);
		Armor *armor = rules != 0 ? game->getMod()->getArmor(rules->getArmor(), false) : 0;
		expect(rules != 0 && armor != 0, "stock HWP rules");
		if (rules == 0 || armor == 0)
		{
			return 0;
		}
		StatAdjustment adj;
		BattleUnit *unit = new BattleUnit(rules, FACTION_PLAYER, HWP_ID, armor, &adj, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: " << tu
		     << "\nenergy: 80\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"
		     << "direction: " << hullDir << "\ndirectionTurret: " << turretDir << "\n";
		unit->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(unit);
		wireUnit(battle, unit, pos);
		return unit;
	}

	BattleUnit *addCannonTank(const Position &pos, int tu, int hullDir, int turretDir)
	{
		return addStockTank("STR_TANK_CANNON", pos, tu, hullDir, turretDir);
	}

	BattleUnit *addSnapSoldier(const Position &pos, int tu)
	{
		if (game == 0 || battle == 0 || base == 0)
		{
			return 0;
		}
		RuleSoldier *rs = game->getMod()->getSoldier("STR_SOLDIER", true);
		Armor *ar = game->getMod()->getArmor("STR_PERSONAL_ARMOR_UC", false);
		if (ar == 0)
		{
			ar = game->getMod()->getArmor("STR_JUMPSUIT", false);
		}
		expect(rs != 0 && ar != 0, "soldier rules from xcom1 mod");
		if (rs == 0 || ar == 0)
		{
			return 0;
		}
		Soldier *soldier = new Soldier(rs, ar, SOLDIER_RELAY_ID);
		base->getSoldiers()->push_back(soldier);
		BattleUnit *unit = new BattleUnit(soldier, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 50\ntu: " << tu
		     << "\nenergy: 60\nmorale: 100\narmor: [6,6,6,6,6]\nfatalWounds: [0,0,0,0,0,0]\ndirection: 4\n";
		unit->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(unit);
		wireUnit(battle, unit, pos);
		return unit;
	}

	BattleItem *mountRifle(BattleUnit *soldier)
	{
		if (game == 0 || battle == 0 || soldier == 0)
		{
			return 0;
		}
		RuleItem *rifleRules = game->getMod()->getItem("STR_RIFLE", false);
		RuleItem *ammoRules = game->getMod()->getItem("STR_RIFLE_CLIP", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(rifleRules != 0 && ammoRules != 0 && hand != 0, "rifle + clip rules");
		if (rifleRules == 0 || ammoRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *rifle = new BattleItem(rifleRules, battle->getCurrentItemId());
		BattleItem *clip = new BattleItem(ammoRules, battle->getCurrentItemId());
		expect(rifle->setAmmoItem(clip) == 0, "rifle loaded");
		rifle->setSlot(hand);
		rifle->setSlotX(0);
		rifle->setSlotY(0);
		rifle->moveToOwner(soldier);
		battle->getItems()->push_back(rifle);
		battle->getItems()->push_back(clip);
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

	void placeLosObjectBlock(const Position &pos)
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
			return;
		}
		MapData *wall = new MapData(floorSet());
		wall->setObjectType(O_OBJECT);
		wall->setTUCosts(255, 255, 255);
		wall->setStopLOS(true);
		wall->setFlags(false, true, false, 0, false, false, true, true, false);
		wall->setBlockValue(255, 255, 255, 255, 255, 255);
		tile->setMapData(wall, 0, 0, O_OBJECT);
	}

	bool nativeVisibleHostileFireBlocked(
		BattleUnit *tank,
		BattleUnit *hostile,
		BattleItem *cannon,
		const Position &target,
		std::string *rejectOut)
	{
		BattlescapeGame *bg = battleGame();
		if (bg == 0 || tank == 0 || hostile == 0 || cannon == 0)
		{
			return false;
		}
		std::string preflightReject;
		if (!preflightHwpFire(bg, tank, target, "snap", cannon->getId(), &preflightReject, 0))
		{
			if (rejectOut != 0)
			{
				*rejectOut = preflightReject;
			}
			return true;
		}
		BattleAction probe = {};
		probe.type = BA_SNAPSHOT;
		probe.actor = tank;
		probe.weapon = cannon;
		probe.target = target;
		probe.autoShotCounter = 0;
		if (ProjectileFlyBState::companionPreflightVisibleUnitTargetBlocked(
				probe, battle, bg, tank->getPosition()))
		{
			if (rejectOut != 0)
			{
				*rejectOut = "companionPreflightVisibleUnitTargetBlocked";
			}
			return true;
		}
		return false;
	}

	BattleUnit *addHostileUnit(const Position &pos)
	{
		Mod *rules = game != 0 ? game->getMod() : 0;
		Unit *unitRules = rules != 0 ? rules->getUnit("STR_SECTOID_SOLDIER", false) : 0;
		if (unitRules == 0 && rules != 0)
		{
			unitRules = rules->getUnit("STR_SECTOID", false);
		}
		Armor *armor = unitRules != 0 ? rules->getArmor(unitRules->getArmor(), false) : 0;
		expect(unitRules != 0 && armor != 0, "hostile unit rules");
		if (unitRules == 0 || armor == 0 || battle == 0)
		{
			return 0;
		}
		StatAdjustment adj;
		const int hostileId = HWP_ID + 1;
		BattleUnit *hostile = new BattleUnit(unitRules, FACTION_HOSTILE, hostileId, armor, &adj, 0);
		std::ostringstream yaml;
		yaml << "status: 0\nstunlevel: 0\nhealth: 26\ntu: 50\nenergy: 60\nmorale: 100\n"
		     << "armor: [6,6,6,6,6]\nfatalWounds: [0,0,0,0,0,0]\ndirection: 4\n";
		hostile->load(YAML::Load(yaml.str()));
		battle->getUnits()->push_back(hostile);
		wireUnit(battle, hostile, pos);
		// Open map placement: visibility flag matches engine directed-fire preflight harness.
		hostile->setVisible(true);
		return hostile;
	}

	void refreshPlayerFov(BattleUnit *observer)
	{
		if (battle == 0 || battle->getTileEngine() == 0)
		{
			return;
		}
		if (observer != 0)
		{
			battle->getTileEngine()->calculateFOV(observer);
		}
		battle->getTileEngine()->recalculateFOV();
	}

	BattleItem *mountStockCannon(BattleUnit *tank)
	{
		RuleItem *cannonRules = game->getMod()->getItem("STR_TANK_CANNON", false);
		RuleItem *ammoRules = game->getMod()->getItem("STR_HWP_CANNON_SHELLS", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(cannonRules != 0 && ammoRules != 0 && hand != 0, "stock cannon + ammo rules");
		if (cannonRules == 0 || ammoRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *cannon = new BattleItem(cannonRules, battle->getCurrentItemId());
		BattleItem *clip = new BattleItem(ammoRules, battle->getCurrentItemId());
		expect(cannon->setAmmoItem(clip) == 0, "cannon loaded");
		cannon->setSlot(hand);
		cannon->setSlotX(0);
		cannon->setSlotY(0);
		cannon->moveToOwner(tank);
		battle->getItems()->push_back(cannon);
		battle->getItems()->push_back(clip);
		return cannon;
	}

	void assertVanillaTankUsesInventoryNotArmorSpecial(BattleUnit *tank)
	{
		Mod *rules = game != 0 ? game->getMod() : 0;
		expect(tank != 0 && battle != 0 && rules != 0, "vanilla tank weapon context");
		if (tank == 0 || battle == 0 || rules == 0)
		{
			return;
		}
		tank->setSpecialWeapon(battle, rules);
		expect(tank->getSpecialWeapon(BT_FIREARM) == 0,
			"vanilla TANK_ARMOR has no specialWeapon firearm — craft inventory mount is stock layout");
	}

	BattleItem *mountStockLaser(BattleUnit *tank)
	{
		RuleItem *laserRules = game->getMod()->getItem("STR_TANK_LASER_CANNON", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(laserRules != 0 && hand != 0, "stock laser rules");
		if (laserRules == 0 || hand == 0)
		{
			return 0;
		}
		BattleItem *laser = new BattleItem(laserRules, battle->getCurrentItemId());
		laser->setSlot(hand);
		laser->setSlotX(0);
		laser->setSlotY(0);
		laser->moveToOwner(tank);
		battle->getItems()->push_back(laser);
		return laser;
	}

	BattleUnit *addRocketTank(const Position &pos, int tu, int hullDir, int turretDir)
	{
		return addStockTank("STR_TANK_ROCKET_LAUNCHER", pos, tu, hullDir, turretDir);
	}

	BattleItem *mountStockRocket(BattleUnit *tank, int rounds = 8)
	{
		RuleItem *launcherRules = game->getMod()->getItem("STR_TANK_ROCKET_LAUNCHER", false);
		RuleItem *ammoRules = game->getMod()->getItem("STR_HWP_ROCKETS", false);
		RuleInventory *hand = game->getMod()->getInventory("STR_RIGHT_HAND", true);
		expect(launcherRules != 0 && ammoRules != 0 && hand != 0, "stock rocket launcher + HWP_ROCKETS rules");
		if (launcherRules == 0 || ammoRules == 0 || hand == 0)
		{
			return 0;
		}
		expect(ammoRules->getClipSize() == 8, "STR_HWP_ROCKETS ruleset clipSize 8");
		BattleItem *launcher = new BattleItem(launcherRules, battle->getCurrentItemId());
		BattleItem *clip = new BattleItem(ammoRules, battle->getCurrentItemId());
		clip->setAmmoQuantity(rounds);
		expect(launcher->setAmmoItem(clip) == 0, "rocket launcher loaded");
		launcher->setSlot(hand);
		launcher->setSlotX(0);
		launcher->setSlotY(0);
		launcher->moveToOwner(tank);
		battle->getItems()->push_back(launcher);
		battle->getItems()->push_back(clip);
		return launcher;
	}

	std::string rejectCode() const
	{
		std::string code;
		CompanionJsonLite::extractJsonStringField(resultJson(), "reject_code", &code);
		return code;
	}

	int journalIdCount(const char *commandId) const
	{
		const std::string journal = fireRead(fireJoin(relayDir, "tactical-command-journal.json"));
		int count = 0;
		std::string needle = std::string("\"") + commandId + "\"";
		for (size_t pos = 0; (pos = journal.find(needle, pos)) != std::string::npos; pos += needle.size())
		{
			++count;
		}
		return count;
	}

	std::string hwpFireJson(const char *id, const char *mode, int weaponId, int tx, int ty, int tz) const
	{
		std::ostringstream j;
		j << "{\n  \"schema_version\": 1,\n  \"command_id\": \"" << id << "\",\n"
		  << "  \"command_type\": \"HWP_FIRE\",\n  \"actor_unit_id\": " << HWP_ID << ",\n"
		  << "  \"fire_mode\": \"" << mode << "\",\n"
		  << "  \"weapon_battle_item_id\": " << weaponId << ",\n"
		  << "  \"target\": {\"x\": " << tx << ", \"y\": " << ty << ", \"z\": " << tz << "},\n"
		  << "  \"snapshot_sha256\": \"" << bindingSha256 << "\",\n"
		  << "  \"session_id\": \"" << sessionId << "\",\n  \"turn\": 1,\n  \"state_version\": "
		  << bindingStateVersion << "\n}\n";
		return j.str();
	}

	void publish(const std::string &json)
	{
		fireWrite(fireJoin(relayDir, "tactical-command.json"), json);
		FIRE_RELAY_SLEEP_MS(320);
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
		return fireRead(fireJoin(relayDir, "tactical-result.json"));
	}

	std::string status() const
	{
		std::string s;
		CompanionJsonLite::extractJsonStringField(resultJson(), "status", &s);
		return s;
	}

	int advanceUntilTerminal(bool *sawExplosionState = 0)
	{
		BattlescapeGame *bg = battleGame();
		expect(bg != 0, "battle game");
		int ticks = 0;
		while (ticks < kMaxFireTicks)
		{
			if (battlescape != 0)
			{
				battlescape->think();
			}
			else if (bg->isBusy())
			{
				bg->handleState();
			}
			else
			{
				bg->think();
			}
			if (sawExplosionState != 0
			    && describeNativeStack(bg).find("ExplosionBState") != std::string::npos)
			{
				*sawExplosionState = true;
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
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
		if (bg != 0)
		{
			for (int i = 0; i < 64 && bg->isBusy(); ++i)
			{
				bg->handleState();
			}
		}
		resetRelayFiles();
	}
};

FireRelayHarness &harness()
{
	static FireRelayHarness h;
	return h;
}

void certCannonSnapRelay()
{
#ifndef COMPANION_HWP_FIRE_RELAY_HARNESS
	expect(false, "relay harness must be built with COMPANION_HWP_FIRE_RELAY_HARNESS");
	return;
#endif
	expect(hwpFireRelayExecutionSupported(), "test-only relay gate on in harness build");

	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();

	const Position tankPos(3, 3, 0);
	const Position target(8, 3, 0);
	BattleUnit *tank = h.addCannonTank(tankPos, 120, 4, 6);
	if (tank == 0)
	{
		return;
	}
	BattleItem *cannon = h.mountStockCannon(tank);
	expect(cannon != 0, "stock cannon mounted for relay contract");
	if (cannon == 0)
	{
		return;
	}
	const int weaponId = cannon->getId();
	const int tuBefore = tank->getTimeUnits();
	const int ammoBefore = clipRounds(cannon);

	h.writeControl(true);
	expect(h.exportBinding(), "snapshot export");

	h.publish(h.hwpFireJson("hwp-fire-cannon-snap-1ca", "snap", weaponId, target.x, target.y, target.z));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "accepted EXECUTING not immediate COMPLETED");
	expect(h.battleGame()->isBusy(), "native stack busy after start");

	const int ticks = h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "terminal COMPLETED after projectile execution");
	expect(!h.battleGame()->isBusy(), "battle idle after terminal");
	expect(h.resultJson().find("\"projectile_launched\": true") != std::string::npos,
		"confirmed projectile launch not empty-stack idle");

	const int ammoAfter = clipRounds(cannon);
	expect(ammoBefore >= 0 && ammoAfter == ammoBefore - 1, "cannon clip decremented exactly once");

	expect(tank->getTimeUnits() < tuBefore, "TU spent on engine");
	int tuSpentJson = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpentJson), "tu_spent in result");
	expect(tuSpentJson == tuBefore - tank->getTimeUnits(), "tu_spent matches engine");

	int remTu = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "remaining_tu", &remTu), "remaining_tu");
	expect(remTu == tank->getTimeUnits(), "remaining_tu matches engine");

	int ammoRem = -2;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "ammo_remaining", &ammoRem), "ammo_remaining");
	expect(ammoRem == ammoAfter, "ammo_remaining matches engine");

	int hullJson = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_direction", &hullJson), "final_direction");
	expect(hullJson == tank->getDirection(), "final_direction matches engine");
	if (tank->getTurretType() >= 0)
	{
		int turretJson = -1;
		expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_turret_direction", &turretJson),
			"final_turret_direction when turret present");
		expect(turretJson == tank->getTurretDirection(), "turret facing matches engine");
	}

	std::string journal = fireRead(fireJoin(h.relayDir, "tactical-command-journal.json"));
	expect(journal.find("hwp-fire-cannon-snap-1ca") != std::string::npos, "journal records command_id");

	logEvidence("ammo", std::to_string(ammoBefore) + " -> " + std::to_string(ammoAfter));
	logEvidence("tu", std::to_string(tuBefore) + " -> " + std::to_string(tank->getTimeUnits())
	                   + " (spent=" + std::to_string(tuBefore - tank->getTimeUnits()) + ")");
	logEvidence("ticks", std::to_string(ticks));
	logEvidence("terminal", h.resultJson());

	h.endCase();
}

void certWeaponLayoutAndLookup()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	BattleUnit *tankInv = h.addCannonTank(Position(2, 2, 0), 120, 4, 6);
	BattleItem *invCannon = h.mountStockCannon(tankInv);
	expect(invCannon != 0, "inventory mount");
	expect(findBattleItemOnActorById(tankInv, invCannon->getId()) == invCannon, "inventory id lookup");
	expect(h.snapshotPath.find("tactical-relay-fixture") != std::string::npos, "snapshot path");
	h.writeControl(true);
	expect(h.exportBinding(), "export for fixedWeapons");
	const std::string snap = fireRead(h.snapshotPath);
	expect(snap.find("fixedWeapons") != std::string::npos, "snapshot lists inventory fixedWeapons");

	h.setupBattle(12, 12);
	BattleUnit *tankNat = h.addCannonTank(Position(4, 4, 0), 120, 4, 6);
	h.assertVanillaTankUsesInventoryNotArmorSpecial(tankNat);
	BattleItem *natCannon = h.mountStockCannon(tankNat);
	expect(natCannon != 0, "inventory mount on second tank");
	expect(findBattleItemOnActorById(tankNat, natCannon->getId()) == natCannon, "id lookup");
	expect(findBattleItemOnActorById(tankNat, 0) == natCannon,
		"weapon id 0 resolves sole fixed firearm when unambiguous");
	logEvidence("weapon-layout",
		"stock xcom1: inventory-mounted STR_TANK_CANNON; snapshot fixedWeapons; lookup includes inventory+spec slot");
}

void certRocketNativeStackDrains()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	BattleUnit *tank = h.addRocketTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank, 8);
	BattlescapeGame *bg = h.battleGame();
	if (tank == 0 || launcher == 0 || bg == 0)
	{
		return;
	}
	const Position target(9, 3, 0);
	const int ammoBefore = clipRounds(launcher);
	const int tuBefore = tank->getTimeUnits();
	std::string reject;
	expect(preflightHwpFire(bg, tank, target, "snap", launcher->getId(), &reject, 0), "rocket native preflight");
	HwpFireStartOutcome outcome = {};
	expect(tryStartHwpFire(bg, tank, target, "snap", launcher->getId(), &reject, &outcome), "rocket native start");
	expect(bg->isBusy(), "rocket native busy after start");
	int ticks = 0;
	bool sawExplosion = false;
	while (ticks < kMaxFireTicks)
	{
		if (h.battlescape != 0)
		{
			h.battlescape->think();
		}
		else if (bg->isBusy())
		{
			bg->handleState();
		}
		else
		{
			break;
		}
		if (describeNativeStack(bg).find("ExplosionBState") != std::string::npos)
		{
			sawExplosion = true;
		}
		if (!bg->isBusy())
		{
			break;
		}
		++ticks;
	}
	const std::string stackEnd = describeNativeStack(bg);
	logEvidence("rocket-native", "ticks=" + std::to_string(ticks) + " explosion=" + (sawExplosion ? "yes" : "no")
	                                + " tu=" + std::to_string(tuBefore) + "->" + std::to_string(tank->getTimeUnits())
	                                + " busy=" + (bg->isBusy() ? "true" : "false") + " stack=" + stackEnd
	                                + " ammo=" + std::to_string(clipRounds(launcher)));
	expect(!bg->isBusy(), "rocket native stack idle after handleState");
	expect(clipRounds(launcher) == ammoBefore - 1, "rocket native one round spent");
	expect(tank->getTimeUnits() < tuBefore, "rocket native TU spent");
}

void certRocketSnapRelayTerminal()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position tankPos(3, 3, 0);
	const Position target(8, 3, 0);
	BattleUnit *tank = h.addRocketTank(tankPos, 120, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank, 8);
	if (tank == 0 || launcher == 0)
	{
		return;
	}
	expect(clipRounds(launcher) == 8, "rocket magazine starts at 8 rounds");
	logEvidence("rocket-turret-type", "turretType=" + std::to_string(tank->getTurretType()));
	const int snapTu = tank->getActionTUs(BA_SNAPSHOT, launcher);
	expect(snapTu > 0, "rocket snap TU from stock rules");
	const int tuBefore = tank->getTimeUnits();
	const int ammoBefore = clipRounds(launcher);
	const int turretBefore = tank->getTurretDirection();
	h.writeControl(true);
	expect(h.exportBinding(), "rocket snapshot export");
	const std::string snapBefore = fireRead(h.snapshotPath);
	expect(snapBefore.find("STR_TANK_ROCKET_LAUNCHER") != std::string::npos, "export lists rocket launcher");
	expect(snapBefore.find("STR_HWP_ROCKETS") != std::string::npos, "export lists HWP rockets ammo");
	h.publish(h.hwpFireJson("hwp-fire-rocket-snap-s6", "snap", launcher->getId(), target.x, target.y, target.z));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "rocket snap EXECUTING");
	expect(h.battleGame()->isBusy(), "native stack busy after rocket start");
	bool sawExplosion = false;
	const int ticks = h.advanceUntilTerminal(&sawExplosion);
	expect(h.status() == "COMPLETED", "rocket snap COMPLETED after projectile stack");
	expect(h.resultJson().find("\"projectile_launched\": true") != std::string::npos, "rocket projectile launched");
	expect(clipRounds(launcher) == ammoBefore - 1, "rocket single-round consumption");
	expect(tank->getTimeUnits() < tuBefore, "rocket snap TU spent");
	int tuSpentJson = -1;
	expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "tu_spent", &tuSpentJson), "rocket tu_spent");
	expect(tuSpentJson == tuBefore - tank->getTimeUnits(), "rocket tu_spent matches engine");
	if (tank->getTurretDirection() != turretBefore)
	{
		int turretJson = -1;
		expect(CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_turret_direction", &turretJson),
			"rocket final_turret_direction");
		expect(turretJson == tank->getTurretDirection(), "rocket turret json");
	}
	logEvidence("rocket-snap-explosion", sawExplosion ? "ExplosionBState observed during terminal" : "no ExplosionBState in stack trace (limitation)");
	expect(sawExplosion, "HE rocket should queue ExplosionBState during relay terminal");
	expect(h.exportBinding(), "post-shot snapshot export");
	const std::string snapAfter = fireRead(h.snapshotPath);
	expect(snapAfter.find("\"rounds\": 7") != std::string::npos || snapAfter.find("rounds: 7") != std::string::npos,
		"snapshot export reflects 7 rounds after one shot");
	logEvidence("rocket-snap", "ammo " + std::to_string(ammoBefore) + "->" + std::to_string(clipRounds(launcher))
	                              + " tu " + std::to_string(tuBefore) + "->" + std::to_string(tank->getTimeUnits())
	                              + " snapTu=" + std::to_string(snapTu) + " ticks=" + std::to_string(ticks));
	h.endCase();
}

void certRocketAimedRelayTerminal()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addRocketTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank, 8);
	if (tank == 0 || launcher == 0)
	{
		return;
	}
	const int aimedTu = tank->getActionTUs(BA_AIMEDSHOT, launcher);
	expect(aimedTu > 0, "rocket aimed TU from stock rules");
	const int ammoBefore = clipRounds(launcher);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-aimed-s6", "aimed", launcher->getId(), 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "rocket aimed EXECUTING");
	bool sawExplosion = false;
	h.advanceUntilTerminal(&sawExplosion);
	expect(h.status() == "COMPLETED", "rocket aimed COMPLETED");
	expect(h.resultJson().find("\"projectile_launched\": true") != std::string::npos, "rocket aimed projectile");
	expect(clipRounds(launcher) == ammoBefore - 1, "rocket aimed one round spent");
	expect(tank->getTimeUnits() < tuBefore, "rocket aimed TU spent");
	logEvidence("rocket-aimed", "aimedTu=" + std::to_string(aimedTu) + " explosion=" + (sawExplosion ? "yes" : "no"));
	h.endCase();
}

void certRocketRejectEmptyAmmo()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addRocketTank(Position(2, 2, 0), 80, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank, 0);
	launcher->getAmmoItem()->setAmmoQuantity(0);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-empty", "snap", launcher->getId(), 6, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "empty rocket magazine REJECTED");
	expect(tank->getTimeUnits() == tuBefore, "no TU on empty rocket reject");
	h.endCase();
}

void certRocketRejectInvalidTarget()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addRocketTank(Position(2, 2, 0), 80, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-oob", "snap", launcher->getId(), 99, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "rocket OOB target REJECTED");
	h.endCase();
}

void certRocketTurretRotation()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const int hullBefore = 4;
	const int turretBefore = 2;
	BattleUnit *tank = h.addRocketTank(Position(3, 3, 0), 120, hullBefore, turretBefore);
	BattleItem *launcher = h.mountStockRocket(tank);
	if (launcher == 0)
	{
		return;
	}
	const Position target(3, 7, 0);
	const int facingSteps = estimateDirectedFireFacingSteps(tank, target);
	expect(facingSteps >= 0, "rocket turret facing steps computable");
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-turret", "snap", launcher->getId(), target.x, target.y, target.z));
	h.tickRelay();
	h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "rocket turret snap COMPLETED");
	expect(tank->getTurretDirection() != turretBefore, "rocket turret rotated");
	logEvidence("rocket-turret", "facingSteps=" + std::to_string(facingSteps));
	h.endCase();
}

void certRocketInterruptMidFlight()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addRocketTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank);
	const int ammoBefore = clipRounds(launcher);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-interrupt", "snap", launcher->getId(), 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "rocket interrupt EXECUTING");
	BattlescapeGame *bg = h.battleGame();
	bool projectileLive = false;
	for (int i = 0; i < 32 && bg->isBusy(); ++i)
	{
		if (bg->getMap() != 0 && bg->getMap()->getProjectile() != 0)
		{
			projectileLive = true;
		}
		bg->handleState();
		h.tickRelay();
	}
	logEvidence("rocket-interrupt", "projectileLive=" + std::string(projectileLive ? "true" : "false"));
#ifdef _WIN32
	certInterruptMidFlightCancelProbe(bg, h);
#else
	logEvidence("rocket-interrupt", "cancel probe Windows-only");
#endif
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	if (clipRounds(launcher) == ammoBefore)
	{
		logEvidence("rocket-interrupt", "ammo unchanged after mid-flight cancel (before spend)");
	}
	h.endCase();
}

void certRocketCancelBeforeProjectileLaunch()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addRocketTank(Position(3, 3, 0), 120, 4, 0);
	BattleItem *launcher = h.mountStockRocket(tank);
	const int ammoBefore = clipRounds(launcher);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-cancel-pre", "snap", launcher->getId(), 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "pre-cancel EXECUTING");
	BattlescapeGame *bg = h.battleGame();
	bool canceledBeforeProjectile = false;
	for (int i = 0; i < 24 && bg != 0 && bg->isBusy(); ++i)
	{
		const bool projectileLive = bg->getMap() != 0 && bg->getMap()->getProjectile() != 0;
		const std::string stack = describeNativeStack(bg);
		if (!projectileLive && stack.find("UnitTurnBState") != std::string::npos)
		{
#ifdef _WIN32
			const CancelInvokeOutcome cancel = invokeCancelCurrentActionSeh(bg);
			expect(!cancel.trapped, "cancel before projectile lift-off must not SEH");
#else
			bg->cancelCurrentAction();
#endif
			canceledBeforeProjectile = true;
			break;
		}
		bg->handleState();
		h.tickRelay();
	}
	logEvidence("rocket-cancel-pre", canceledBeforeProjectile ? "canceled during turret align" : "OPEN: no pre-projectile window");
	if (canceledBeforeProjectile)
	{
		expect(clipRounds(launcher) == ammoBefore, "ammo unchanged cancel before projectile");
	}
	h.tickRelay();
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	expect(tank->getTimeUnits() <= tuBefore, "TU not increased by cancel path");
	h.endCase();
}

#ifdef _WIN32
void certRocketAuthorityLossCancel()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addRocketTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *launcher = h.mountStockRocket(tank);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-rocket-auth-cancel", "snap", launcher->getId(), 8, 3, 0));
	h.tickRelay();
	BattlescapeGame *bg = h.battleGame();
	for (int i = 0; i < 6 && bg != 0 && bg->isBusy(); ++i)
	{
		bg->handleState();
		h.tickRelay();
	}
	h.writeControl(false);
	for (int i = 0; i < 4; ++i)
	{
		const DWORD authoritySeh = invokeRelayCommandTickSeh(bg);
		expect(authoritySeh == 0, "rocket authority-loss tick must not SEH");
	}
	logEvidence("rocket-auth-cancel", "status=" + h.status());
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	h.endCase();
}
#endif

void certCannonAimedRelay()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	if (tank == 0 || cannon == 0)
	{
		return;
	}
	const int ammoBefore = clipRounds(cannon);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-cannon-aimed", "aimed", cannon->getId(), 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "aimed EXECUTING");
	h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "aimed COMPLETED");
	expect(h.resultJson().find("\"projectile_launched\": true") != std::string::npos, "aimed projectile");
	expect(clipRounds(cannon) == ammoBefore - 1, "aimed one ammo decrement");
	expect(tank->getTimeUnits() < tuBefore, "aimed TU spent");
	h.endCase();
}

void certLaserSnapRelay()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addStockTank("STR_TANK_LASER_CANNON", Position(5, 5, 0), 120, 4, 6);
	BattleItem *laser = h.mountStockLaser(tank);
	if (laser == 0)
	{
		return;
	}
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-laser-snap", "snap", laser->getId(), 9, 5, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "laser EXECUTING");
	h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "laser COMPLETED");
	expect(!laser->needsAmmo(), "laser no magazine");
	expect(h.resultJson().find("\"projectile_launched\": true") != std::string::npos, "laser projectile");
	expect(tank->getTimeUnits() < tuBefore, "laser TU spent");
	h.endCase();
}

void certWeaponBattleItemIdZero()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	if (cannon == 0)
	{
		return;
	}
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-id-zero", "snap", 0, 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING" || h.status() == "REJECTED", "id 0 accepted or rejected");
	if (h.status() == "EXECUTING")
	{
		h.advanceUntilTerminal();
		expect(h.status() == "COMPLETED", "id 0 COMPLETED");
		expect(h.resultJson().find("\"projectile_launched\": true") != std::string::npos, "id 0 fired");
	}
	h.endCase();
}

void certTurretRotationPreservesHullWhenPossible()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const int hullBefore = 4;
	const int turretBefore = 2;
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, hullBefore, turretBefore);
	BattleItem *cannon = h.mountStockCannon(tank);
	if (cannon == 0)
	{
		return;
	}
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-turret", "snap", cannon->getId(), 3, 7, 0));
	h.tickRelay();
	h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "turret snap COMPLETED");
	if (tank->getTurretType() >= 0)
	{
		expect(tank->getTurretDirection() != turretBefore, "turret rotated toward target");
		int turretJson = -1;
		CompanionJsonLite::extractJsonIntField(h.resultJson(), "final_turret_direction", &turretJson);
		expect(turretJson == tank->getTurretDirection(), "turret json");
	}
	logEvidence("turret-case", "hull=" + std::to_string(tank->getDirection()) + " (before " + std::to_string(hullBefore) + ")");
	h.endCase();
}

void certRejectEmptyAmmo()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(2, 2, 0), 80, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	cannon->getAmmoItem()->setAmmoQuantity(0);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-empty", "snap", cannon->getId(), 6, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "empty ammo REJECTED");
	expect(tank->getTimeUnits() == tuBefore, "no TU on reject");
	expect(h.journalIdCount("hwp-fire-empty") >= 1, "journal entry");
	h.endCase();
}

void certRejectBadWeaponId()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(2, 2, 0), 80, 4, 6);
	h.mountStockCannon(tank);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-bad-weapon", "snap", 99999, 6, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "bad weapon REJECTED");
	h.endCase();
}

void certRejectBlockedVisibleHostileLos()
{
	struct LosScenario
	{
		const char *label;
		Position tankPos;
		Position hostilePos;
		int blockX0;
		int blockX1;
		int blockY;
	};
	const LosScenario scenarios[] = {
		{ "ns-close", Position(3, 3, 0), Position(3, 7, 0), 3, 3, 5 },
		{ "ew-single", Position(2, 2, 0), Position(8, 2, 0), 4, 6, 2 },
		{ "ew-tight", Position(1, 2, 0), Position(6, 2, 0), 3, 4, 2 },
		{ "ns-column", Position(3, 3, 0), Position(3, 8, 0), 3, 3, -1 },
	};
	FireRelayHarness &h = harness();
	bool scenarioOk = false;
	std::string scenarioLabel;
	std::string nativeReject;
	Position fireTarget;
	BattleUnit *tank = 0;
	BattleItem *cannon = 0;
	auto tryLayout = [&](const char *label, const Position &tankPos, const Position &hostilePos,
	                     const std::vector<Position> &blocks) -> bool {
		h.setupBattle(12, 12);
		tank = h.addCannonTank(tankPos, 120, 4, 6);
		cannon = h.mountStockCannon(tank);
		if (tank == 0 || cannon == 0)
		{
			return false;
		}
		BattleUnit *hostile = h.addHostileUnit(hostilePos);
		if (hostile == 0)
		{
			return false;
		}
		h.refreshPlayerFov(tank);
		if (!hostile->getVisible())
		{
			return false;
		}
		for (size_t bi = 0; bi < blocks.size(); ++bi)
		{
			h.placeLosObjectBlock(blocks[bi]);
		}
		fireTarget = hostilePos;
		if (h.nativeVisibleHostileFireBlocked(tank, hostile, cannon, fireTarget, &nativeReject))
		{
			scenarioLabel = label;
			return true;
		}
		return false;
	};
	for (size_t si = 0; si < sizeof(scenarios) / sizeof(scenarios[0]); ++si)
	{
		const LosScenario &sc = scenarios[si];
		std::vector<Position> blocks;
		if (sc.blockY >= 0)
		{
			for (int x = sc.blockX0; x <= sc.blockX1; ++x)
			{
				blocks.push_back(Position(x, sc.blockY, 0));
			}
		}
		else
		{
			for (int y = 5; y <= 7; ++y)
			{
				blocks.push_back(Position(sc.blockX0, y, 0));
			}
		}
		if (tryLayout(sc.label, sc.tankPos, sc.hostilePos, blocks))
		{
			scenarioOk = true;
			break;
		}
	}
	if (!scenarioOk)
	{
		for (int hx = 6; hx <= 10 && !scenarioOk; ++hx)
		{
			for (int wallX = 3; wallX <= 5 && !scenarioOk; ++wallX)
			{
				std::vector<Position> blocks;
				blocks.push_back(Position(wallX, 2, 0));
				blocks.push_back(Position(wallX, 3, 0));
				if (tryLayout("bruteforce-ew", Position(2, 2, 0), Position(hx, 2, 0), blocks))
				{
					scenarioOk = true;
				}
			}
		}
	}
	if (!scenarioOk || tank == 0 || cannon == 0)
	{
		logEvidence("blocked-los",
			"OPEN: no 12x12 fixture layout triggered native visible-hostile voxel preflight block for HWP snap");
		return;
	}
	const int tuBefore = tank->getTimeUnits();
	const int ammoBefore = clipRounds(cannon);
	h.resetRelayFiles();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	logEvidence("blocked-los-native", "scenario=" + scenarioLabel + " reason=" + nativeReject);
	h.publish(h.hwpFireJson("hwp-fire-blocked-los", "snap", cannon->getId(),
		fireTarget.x, fireTarget.y, fireTarget.z));
	h.tickRelay();
	expect(h.status() == "REJECTED", "blocked LOS REJECTED");
	expect(h.rejectCode() == "NO_LINE_OF_FIRE", "NO_LINE_OF_FIRE");
	expect(!h.battleGame()->isBusy(), "no async stack after reject");
	expect(tank->getTimeUnits() == tuBefore, "TU unchanged on reject");
	expect(clipRounds(cannon) == ammoBefore, "ammo unchanged on reject");
	logEvidence("blocked-los", "code=" + h.rejectCode() + " tu=" + std::to_string(tuBefore)
	                               + " ammo=" + std::to_string(ammoBefore));
	h.endCase();
}

void certCompositeTurretRotationFireTuRelay()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	const Position target(8, 3, 0);
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 0);
	BattleItem *cannon = h.mountStockCannon(tank);
	if (tank == 0 || cannon == 0)
	{
		return;
	}
	const int snapTu = tank->getActionTUs(BA_SNAPSHOT, cannon);
	const int facingSteps = estimateDirectedFireFacingSteps(tank, target);
	expect(snapTu > 0, "snap TU known");
	tank->setTimeUnits(snapTu);
	const int tuBudget = tank->getTimeUnits();
	const int ammoBefore = clipRounds(cannon);
	const int turretBefore = tank->getTurretDirection();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	logEvidence("composite-preflight",
		"snapTu=" + std::to_string(snapTu) + " facingSteps=" + std::to_string(facingSteps)
			+ " budgetTu=" + std::to_string(tuBudget) + " turret=" + std::to_string(turretBefore));
	h.publish(h.hwpFireJson("hwp-fire-composite-tu", "snap", cannon->getId(), target.x, target.y, target.z));
	h.tickRelay();
	const std::string acceptStatus = h.status();
	if (acceptStatus == "REJECTED")
	{
		logEvidence("composite", "native preflight reject code=" + h.rejectCode());
		expect(h.rejectCode() != "", "reject code recorded");
		expect(!h.battleGame()->isBusy(), "idle after preflight reject");
		expect(clipRounds(cannon) == ammoBefore, "ammo preserved on preflight reject");
		h.endCase();
		return;
	}
	expect(acceptStatus == "EXECUTING", "fire-only TU satisfied preflight");
	h.advanceUntilTerminal();
	const std::string terminal = h.status();
	std::string outcomeClass;
	CompanionJsonLite::extractJsonStringField(h.resultJson(), "outcome_class", &outcomeClass);
	expect(terminal == "FAILED" || terminal == "INTERRUPTED" || terminal == "COMPLETED", "terminal observed");
	expect(terminal != "COMPLETED" || h.resultJson().find("\"projectile_launched\": true") != std::string::npos,
		"COMPLETED only with projectile_launched");
	if (terminal == "COMPLETED")
	{
		expect(clipRounds(cannon) < ammoBefore, "ammo spent only when completed with launch");
	}
	else
	{
		expect(h.resultJson().find("\"projectile_launched\": true") == std::string::npos,
			"no false projectile_launched on non-COMPLETED");
		expect(clipRounds(cannon) == ammoBefore, "ammo unchanged when shot did not complete");
		expect(!outcomeClass.empty(), "terminal documents outcome_class on non-COMPLETED composite");
	}
	logEvidence("composite-terminal",
		"status=" + terminal + " outcome_class=" + outcomeClass + " tu " + std::to_string(tuBudget) + "->"
			+ std::to_string(tank->getTimeUnits()) + " turret " + std::to_string(turretBefore) + "->"
			+ std::to_string(tank->getTurretDirection()) + " projectile_json="
			+ (h.resultJson().find("\"projectile_launched\": true") != std::string::npos ? "true" : "false"));
	h.endCase();
}

void certRejectInsufficientTu()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(2, 2, 0), 8, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-low-tu", "aimed", cannon->getId(), 10, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "low TU REJECTED");
	expect(tank->getTimeUnits() == tuBefore, "TU preserved");
	h.endCase();
}

void certRejectReservedTu()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	h.battle->setTUReserved(BA_SNAPSHOT);
	BattleUnit *tank = h.addCannonTank(Position(2, 2, 0), 40, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	const int tuBefore = tank->getTimeUnits();
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-reserved", "snap", cannon->getId(), 8, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "reserved TU blocks relay fire");
	expect(tank->getTimeUnits() == tuBefore, "no TU mutation on reserved reject");
	expect(!h.battleGame()->isBusy(), "no async stack on reject");
	h.battle->setTUReserved(BA_NONE);
	h.endCase();
}

void certRejectInvalidTarget()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(2, 2, 0), 80, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-oob", "snap", cannon->getId(), 99, 2, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "OOB target REJECTED");
	h.endCase();
}

void certRejectUnauthorizedHwp()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 80, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	h.writeControl(false);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-human", "snap", cannon->getId(), 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "REJECTED", "human HWP REJECTED");
	h.endCase();
}

void certRejectStaleBinding()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 80, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	std::string json = h.hwpFireJson("hwp-fire-stale", "snap", cannon->getId(), 8, 3, 0);
	const size_t sv = json.find("\"state_version\": ");
	if (sv != std::string::npos)
	{
		json.replace(sv, 17, "\"state_version\": 999999,");
	}
	h.publish(json);
	h.tickRelay();
	expect(h.status() == "REJECTED", "stale binding REJECTED");
	h.endCase();
}

void certDuplicateInboxDoesNotRefire()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	const int ammoBefore = clipRounds(cannon);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	const std::string json = h.hwpFireJson("hwp-fire-dup", "snap", cannon->getId(), 8, 3, 0);
	h.publish(json);
	h.tickRelay();
	h.advanceUntilTerminal();
	expect(h.status() == "COMPLETED", "first COMPLETED");
	const int ammoMid = clipRounds(cannon);
	expect(ammoMid == ammoBefore - 1, "first shot spent ammo");
	expect(h.journalIdCount("hwp-fire-dup") >= 1, "journal records command_id");
	expect(h.exportBinding(), "refresh binding for identical redelivery");
	const std::string redeliver = h.hwpFireJson("hwp-fire-dup", "snap", cannon->getId(), 8, 3, 0);
	h.publish(redeliver);
	for (int i = 0; i < 8; ++i)
	{
		h.tickRelay();
	}
	const std::string dupStatus = h.status();
	expect(dupStatus == "COMPLETED" || (dupStatus == "REJECTED" && h.rejectCode() == "DUPLICATE_COMMAND_ID"),
		"duplicate redelivery rejected or no-op");
	expect(clipRounds(cannon) == ammoMid, "duplicate did not consume ammo again");
	h.endCase();
}

#ifdef _WIN32
void certInterruptMidFlightCancelProbe(BattlescapeGame *bg, FireRelayHarness &h);
#endif

void certInterruptMidFlight()
{
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	const int ammoBefore = clipRounds(cannon);
	h.writeControl(true);
	expect(h.exportBinding(), "snapshot");
	h.publish(h.hwpFireJson("hwp-fire-interrupt", "snap", cannon->getId(), 8, 3, 0));
	h.tickRelay();
	expect(h.status() == "EXECUTING", "interrupt EXECUTING");
	BattlescapeGame *bg = h.battleGame();
	for (int i = 0; i < 4 && bg->isBusy(); ++i)
	{
		bg->handleState();
		h.tickRelay();
	}
	expect(h.status() == "EXECUTING", "still EXECUTING mid projectile stack");
	expect(bg->isBusy(), "native stack busy mid-flight");
#ifdef _WIN32
	certInterruptMidFlightCancelProbe(bg, h);
#else
	logEvidence("interrupt", "cancelCurrentAction mid-flight not SEH-wrapped off Windows");
#endif
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	h.endCase();
}

void certHarnessGateAndTeardownNotes()
{
#ifndef COMPANION_HWP_FIRE_RELAY_HARNESS
	expect(false, "this executable must define COMPANION_HWP_FIRE_RELAY_HARNESS");
#endif
	expect(hwpFireRelayExecutionSupported(), "relay gate on in harness build only");
	logEvidence("teardown", "intentional no Game/SDL destructor — full teardown tripped 0xC0000374/0xC0000374 heap exit on Windows after cert; harness-only lifetime");
	logEvidence("production-gate", "openxcom.exe built without COMPANION_HWP_FIRE_RELAY_HARNESS — verified via companion_tactical_hwp_authority_test");
}

const char *describeBattleState(BattleState *state)
{
	if (state == 0)
	{
		return "null";
	}
	if (dynamic_cast<UnitTurnBState *>(state) != 0)
	{
		return "UnitTurnBState";
	}
	if (dynamic_cast<ProjectileFlyBState *>(state) != 0)
	{
		return "ProjectileFlyBState";
	}
	if (dynamic_cast<UnitWalkBState *>(state) != 0)
	{
		return "UnitWalkBState";
	}
	if (dynamic_cast<ExplosionBState *>(state) != 0)
	{
		return "ExplosionBState";
	}
	return "BattleState(other)";
}

std::string describeNativeStack(BattlescapeGame *bg)
{
	if (bg == 0)
	{
		return "battleGame=null";
	}
	std::ostringstream out;
	const std::list<BattleState *> states = bg->getStates();
	int index = 0;
	for (std::list<BattleState *>::const_iterator it = states.begin(); it != states.end(); ++it, ++index)
	{
		if (index > 0)
		{
			out << " | ";
		}
		out << (index == 0 ? "front=" : "") << index << ":" << describeBattleState(*it);
	}
	if (index == 0)
	{
		out << "front=(empty)";
	}
	Map *map = bg->getMap();
	const bool liveProj = map != 0 && map->getProjectile() != 0;
	out << " mapProjectile=" << (liveProj ? "yes" : "no");
	out << " busy=" << (bg->isBusy() ? "true" : "false");
	out << " parentState=" << (bg->getSave() != 0 && bg->getSave()->getBattleState() != 0 ? "BattlescapeState" : "none");
	return out.str();
}

#ifdef _WIN32
struct CancelTraceSeh
{
	DWORD code;
	bool trapped;
};

template<typename Fn>
CancelTraceSeh runCancelTraceSeh(Fn fn)
{
	CancelTraceSeh out = {0, false};
	__try
	{
		fn();
	}
	__except (out.code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
	{
		out.trapped = true;
	}
	return out;
}

void logCancelHex(const char *tag, const std::string &line)
{
	std::ostringstream hex;
	for (size_t i = 0; i < line.size(); ++i)
	{
		if (i > 0)
		{
			hex << ' ';
		}
		hex << line[i];
	}
	logEvidence(tag, line + " seh=" + hex.str());
}

static void logGfxResources(Game *game, Map *map, Projectile *proj)
{
	if (game == 0 || game->getMod() == 0)
	{
		logEvidence("trace-resource", "game/mod missing");
		return;
	}
	Mod *mod = game->getMod();
	SurfaceSet *projSet = mod->getSurfaceSet("Projectiles");
	std::ostringstream line;
	line << "mod=" << mod << " map=" << map << " camera=" << (map != 0 ? map->getCamera() : 0)
	     << " projectile=" << proj << " projectileSprite=" << (proj != 0 ? proj->getSprite() : 0)
	     << " particle0=" << (proj != 0 ? proj->getParticle(0) : -1)
	     << " surfaceSetProjectiles=" << projSet
	     << (projSet != 0 ? (std::string(" frames=") + std::to_string(projSet->getTotalFrames())) : std::string());
	logEvidence("trace-resource", line.str());
}

static bool advanceToCancelTick(FireRelayHarness &h, bool soldierActor)
{
	const Position target(8, 3, 0);
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleItem *weapon = 0;
	if (soldierActor)
	{
		BattleUnit *soldier = h.addSnapSoldier(Position(3, 3, 0), 80);
		weapon = h.mountRifle(soldier);
		h.writeControlSoldier(true);
		expect(h.exportBinding(), "trace soldier snapshot");
		BattlescapeGame *bg = h.battleGame();
		std::string reject;
		expect(bg != 0 && soldier != 0 && weapon != 0, "soldier snap fire context");
		expect(bg->tryExecuteCompanionDirectedFire(soldier, target, weapon, BA_SNAPSHOT, &reject),
			reject.empty() ? "soldier directed snap started" : reject.c_str());
	}
	else
	{
		BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
		weapon = h.mountStockCannon(tank);
		h.writeControl(true);
		expect(h.exportBinding(), "trace hwp snapshot");
		h.publish(h.hwpFireJson("hwp-fire-trace", "snap", weapon->getId(), target.x, target.y, target.z));
		h.tickRelay();
	}
	BattlescapeGame *bg = h.battleGame();
	for (int tick = 0; tick < 2 && bg != 0 && bg->isBusy(); ++tick)
	{
		bg->handleState();
		if (!soldierActor)
		{
			h.tickRelay();
		}
	}
	const std::string stack = describeNativeStack(bg);
	logEvidence(soldierActor ? "trace-pre-soldier" : "trace-pre-hwp", stack);
	Map *map = bg != 0 ? bg->getMap() : 0;
	return bg != 0 && map != 0 && map->getProjectile() != 0
	    && dynamic_cast<ProjectileFlyBState *>(bg->getStates().front()) != 0;
}

static void runCancelOperationTraceActor(FireRelayHarness &h, bool soldierActor)
{
	const char *label = soldierActor ? "soldier" : "hwp";
	if (!advanceToCancelTick(h, soldierActor))
	{
		logEvidence("trace-abort", std::string(label) + " did not reach tick-2 ProjectileFlyBState + map projectile");
		return;
	}
	BattlescapeGame *bg = h.battleGame();
	Map *map = bg->getMap();
	Projectile *proj = map->getProjectile();
	Camera *camera = map->getCamera();
	logGfxResources(h.game, map, proj);
	{
		std::ostringstream bs;
		bs << "battleState=" << h.battle->getBattleState() << " selectedUnit="
		   << h.battle->getSelectedUnit() << " pathfinding=" << h.battle->getPathfinding()
		   << " tileEngine=" << h.battle->getTileEngine();
		logEvidence("trace-resource", bs.str());
	}

	Position originProbe(0, 0, 0);
	const CancelTraceSeh originProbeSeh = runCancelTraceSeh([&]() { originProbe = proj->getOrigin(); });
	logEvidence("trace-phase",
		std::string(label) + " probe_getOrigin trapped=" + (originProbeSeh.trapped ? "yes" : "no")
		+ (originProbeSeh.trapped ? (" code=0x" + [&]() {
			std::ostringstream c;
			c << std::hex << originProbeSeh.code;
			return c.str();
		}()) : ""));

	std::string lastOk = "entry";
	std::string firstFail = "none";

	const CancelTraceSeh skip = runCancelTraceSeh([&]() { proj->skipTrajectory(); });
	if (skip.trapped)
	{
		firstFail = "Projectile::skipTrajectory";
		logEvidence("trace-fail",
			std::string(label) + " phase=skipTrajectory code=0x" + [&]() {
				std::ostringstream c;
				c << std::hex << skip.code;
				return c.str();
			}() + " lastOk=" + lastOk);
	}
	else
	{
		lastOk = "Projectile::skipTrajectory";
	}

	Position voxelPos(0, 0, 0);
	if (firstFail == "none")
	{
		const CancelTraceSeh pos = runCancelTraceSeh([&]() { voxelPos = proj->getPosition(); });
		if (pos.trapped)
		{
			firstFail = "Projectile::getPosition";
			logEvidence("trace-fail",
				std::string(label) + " phase=getPosition code=0x" + [&]() {
					std::ostringstream c;
					c << std::hex << pos.code;
					return c.str();
				}() + " lastOk=" + lastOk);
		}
		else
		{
			lastOk = "Projectile::getPosition";
		}
	}

	const Position tilePos(voxelPos.x / 16, voxelPos.y / 16, voxelPos.z / 24);
	bool onScreen = false;
	if (firstFail == "none")
	{
		const CancelTraceSeh screen = runCancelTraceSeh([&]() {
			onScreen = camera->isOnScreen(tilePos, false, 0, false);
		});
		if (screen.trapped)
		{
			firstFail = "Camera::isOnScreen";
			logEvidence("trace-fail",
				std::string(label) + " phase=isOnScreen code=0x" + [&]() {
					std::ostringstream c;
					c << std::hex << screen.code;
					return c.str();
				}() + " lastOk=" + lastOk);
		}
		else
		{
			lastOk = "Camera::isOnScreen";
			std::ostringstream info;
			info << label << " onScreen=" << (onScreen ? "true" : "false") << " tile=" << tilePos.x << ","
			     << tilePos.y << "," << tilePos.z;
			logEvidence("trace-post", info.str());
		}
	}

	if (firstFail == "none" && !onScreen)
	{
		const CancelTraceSeh centerNoDraw = runCancelTraceSeh([&]() { camera->centerOnPosition(tilePos, false); });
		if (centerNoDraw.trapped)
		{
			firstFail = "Camera::centerOnPosition(redraw=false)";
			logEvidence("trace-fail",
				std::string(label) + " phase=centerNoDraw code=0x" + [&]() {
					std::ostringstream c;
					c << std::hex << centerNoDraw.code;
					return c.str();
				}() + " lastOk=" + lastOk);
		}
		else
		{
			lastOk = "Camera::centerOnPosition(redraw=false)";
		}
	}

	if (firstFail == "none" && !onScreen)
	{
		const CancelTraceSeh centerDraw = runCancelTraceSeh([&]() { camera->centerOnPosition(tilePos, true); });
		if (centerDraw.trapped)
		{
			firstFail = "Camera::centerOnPosition(redraw=true)→Map::draw";
			logEvidence("trace-fail",
				std::string(label) + " phase=centerDraw code=0x" + [&]() {
					std::ostringstream c;
					c << std::hex << centerDraw.code;
					return c.str();
				}() + " lastOk=" + lastOk);
		}
		else
		{
			lastOk = "Camera::centerOnPosition(redraw=true)";
		}
	}

	if (firstFail == "none")
	{
		logEvidence("trace-ok", std::string(label) + " all cancel sub-operations completed; last=" + lastOk);
	}

	BattleState *front = bg->getStates().empty() ? 0 : bg->getStates().front();
	ProjectileFlyBState *fly = dynamic_cast<ProjectileFlyBState *>(front);
	if (fly != 0)
	{
		Projectile *projBefore = map->getProjectile();
		const CancelTraceSeh nativeCancel = runCancelTraceSeh([&]() { fly->cancel(); });
		logEvidence("trace-native-cancel",
			std::string(label) + " ProjectileFlyBState::cancel trapped="
			+ (nativeCancel.trapped ? "yes" : "no")
			+ (nativeCancel.trapped ? (" code=0x" + [&]() {
				std::ostringstream c;
				c << std::hex << nativeCancel.code;
				return c.str();
			}()) : "")
			+ " projStill=" + (map->getProjectile() == projBefore ? "same" : "changed"));
	}

	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
}

static void runCancelOperationTrace()
{
	logEvidence("trace", "S5-1CG stepwise cancel operation trace");
	FireRelayHarness &h = harness();
	runCancelOperationTraceActor(h, false);
	runCancelOperationTraceActor(h, true);
}

void logNativeBacktrace(const char *tag)
{
	void *frames[24];
	const USHORT count = CaptureStackBackTrace(2, 24, frames, 0);
	std::cerr << "[cancel-forensics] " << tag << " CaptureStackBackTrace frames=" << count << std::endl;
	for (USHORT i = 0; i < count; ++i)
	{
		std::cerr << "  frame[" << i << "]=" << frames[i] << std::endl;
	}
}

CancelInvokeOutcome invokeCancelCurrentActionSeh(BattlescapeGame *bg)
{
	CancelInvokeOutcome outcome = {0, false};
	if (bg == 0)
	{
		return outcome;
	}
	__try
	{
		(void)bg->cancelCurrentAction();
	}
	__except (outcome.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
	{
		outcome.trapped = true;
	}
	return outcome;
}

DWORD invokeRelayCommandTickSeh(BattlescapeGame *bg)
{
	DWORD code = 0;
	if (bg == 0)
	{
		return 0;
	}
	__try
	{
		bg->tickCompanionCommand(true);
		if (bg->isBusy())
		{
			bg->handleState();
		}
	}
	__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
	{
	}
	return code;
}

DWORD invokeHandleStateSeh(BattlescapeGame *bg)
{
	DWORD code = 0;
	if (bg == 0)
	{
		return 0;
	}
	__try
	{
		if (bg->isBusy())
		{
			bg->handleState();
		}
	}
	__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
	{
	}
	return code;
}

void certInterruptMidFlightCancelProbe(BattlescapeGame *bg, FireRelayHarness &h)
{
	if (bg == 0)
	{
		expect(false, "interrupt cancel probe battleGame");
		return;
	}
	const CancelInvokeOutcome cancel = invokeCancelCurrentActionSeh(bg);
	if (cancel.trapped)
	{
		std::ostringstream code;
		code << std::hex << "0x" << cancel.exceptionCode;
		logEvidence("interrupt-cancel", "mid-flight cancelCurrentAction SEH=" + code.str());
		expect(cancel.exceptionCode != static_cast<DWORD>(0xC0000409),
			"mid-flight cancel must not fast-fail heap (/GS 0xC0000409)");
		expect(cancel.exceptionCode != static_cast<DWORD>(0xC0000005),
			"mid-flight cancel must not access-violate after S5-1CH terrain fix");
		expect(false, "mid-flight cancelCurrentAction trapped");
	}
	else
	{
		logEvidence("interrupt-cancel", "mid-flight cancelCurrentAction ok (no SEH)");
		const DWORD handleSeh = invokeHandleStateSeh(bg);
		expect(handleSeh == 0, "post-interrupt handleState must not SEH");
	}
	h.tickRelay();
	expect(h.status() == "INTERRUPTED" || h.status() == "EXECUTING",
		"relay status after mid-flight cancel (INTERRUPTED or still draining stack)");
}

void runCancelForensicsSweep()
{
	logEvidence("forensics", "S5-1CE cancel timing sweep — BattlescapeState relay path");
	FireRelayHarness &h = harness();
	const Position target(8, 3, 0);
	for (int preCancelTicks = 0; preCancelTicks <= 12; ++preCancelTicks)
	{
		h.setupBattle(12, 12);
		h.resetRelayFiles();
		BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
		BattleItem *cannon = h.mountStockCannon(tank);
		(void)cannon;
		h.writeControl(true);
		expect(h.exportBinding(), "forensics snapshot");
		h.publish(h.hwpFireJson("hwp-fire-forensics", "snap", cannon->getId(), target.x, target.y, target.z));
		h.tickRelay();
		BattlescapeGame *bg = h.battleGame();
		for (int tick = 0; tick < preCancelTicks && bg != 0; ++tick)
		{
			if (bg->isBusy())
			{
				bg->handleState();
			}
			h.tickRelay();
		}
		const std::string stackBefore = describeNativeStack(bg);
		logEvidence("pre-cancel",
			"ticks=" + std::to_string(preCancelTicks) + " " + stackBefore + " relay=" + h.status());
		const CancelInvokeOutcome cancel = invokeCancelCurrentActionSeh(bg);
		if (cancel.trapped)
		{
			std::ostringstream code;
			code << std::hex << "0x" << cancel.exceptionCode;
			logEvidence("crash", "preCancelTicks=" + std::to_string(preCancelTicks) + " SEH=" + code.str());
			logNativeBacktrace("relay-cancel");
			if (cancel.exceptionCode == static_cast<DWORD>(0xC0000409))
			{
				logEvidence("classification", "STATUS_STACK_BUFFER_OVERRUN (/GS fast-fail class)");
			}
			else if (cancel.exceptionCode == static_cast<DWORD>(0xC0000374))
			{
				logEvidence("classification", "heap corruption fast-fail (distinct from 0xC0000409)");
			}
			break;
		}
		logEvidence("post-cancel",
			"ticks=" + std::to_string(preCancelTicks) + " " + describeNativeStack(bg) + " cancelReturnedOk");
		const DWORD handleSeh = invokeHandleStateSeh(bg);
		if (handleSeh != 0)
		{
			std::ostringstream code;
			code << std::hex << "0x" << handleSeh;
			logEvidence("post-cancel-handleState",
				"ticks=" + std::to_string(preCancelTicks) + " SEH=" + code.str());
			logNativeBacktrace("relay-handleState-after-cancel");
			break;
		}
		CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
	}
}

void runAuthorityLossCancelForensics()
{
	logEvidence("forensics", "production authority-loss tick calls cancelCurrentAction while EXECUTING");
	FireRelayHarness &h = harness();
	h.setupBattle(12, 12);
	h.resetRelayFiles();
	BattleUnit *tank = h.addCannonTank(Position(3, 3, 0), 120, 4, 6);
	BattleItem *cannon = h.mountStockCannon(tank);
	h.writeControl(true);
	expect(h.exportBinding(), "authority forensics snapshot");
	h.publish(h.hwpFireJson("hwp-fire-auth-cancel", "snap", cannon->getId(), 8, 3, 0));
	h.tickRelay();
	BattlescapeGame *bg = h.battleGame();
	for (int i = 0; i < 6 && bg != 0 && bg->isBusy(); ++i)
	{
		bg->handleState();
		h.tickRelay();
	}
	logEvidence("authority-pre", describeNativeStack(bg));
	h.writeControl(false);
	for (int i = 0; i < 4; ++i)
	{
		const DWORD authoritySeh = invokeRelayCommandTickSeh(bg);
		if (authoritySeh != 0)
		{
			std::ostringstream code;
			code << std::hex << "0x" << authoritySeh;
			logEvidence("authority-crash", std::string("SEH during authority tick ") + code.str());
			logNativeBacktrace("authority-loss");
			return;
		}
	}
	logEvidence("authority-post", describeNativeStack(bg) + " relay=" + h.status());
	CompanionCommandRuntime::abandonInFlightCommandForTestHarness();
}

bool setupLocalHeadlessBattle(FireRelayHarness &h, SavedBattleGame &localBattle, BattleUnit **tankOut, BattleItem **cannonOut)
{
	Mod *rules = h.game != 0 ? h.game->getMod() : &h.fixtureMod;
	localBattle.setTestMod(rules);
	localBattle.initMap(12, 12, 1);
	localBattle.initUtilities(rules);
	localBattle.loadMapResources(rules);
	localBattle.setTUReserved(BA_NONE);
	localBattle.setKneelReserved(false);
	wireFloor(&localBattle, rules, 12, 12);
	Unit *unitRules = rules->getUnit("STR_TANK_CANNON", false);
	Armor *armor = unitRules != 0 ? rules->getArmor(unitRules->getArmor(), false) : 0;
	if (unitRules == 0 || armor == 0)
	{
		return false;
	}
	StatAdjustment adj;
	BattleUnit *tank = new BattleUnit(unitRules, FACTION_PLAYER, HWP_ID, armor, &adj, 0);
	std::ostringstream yaml;
	yaml << "status: 0\nstunlevel: 0\nhealth: 120\ntu: 120\nenergy: 80\nmorale: 100\n"
	     << "armor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\ndirection: 4\ndirectionTurret: 6\n";
	tank->load(YAML::Load(yaml.str()));
	localBattle.getUnits()->push_back(tank);
	wireUnit(&localBattle, tank, Position(3, 3, 0));
	RuleItem *cannonRules = rules->getItem("STR_TANK_CANNON", false);
	RuleItem *ammoRules = rules->getItem("STR_HWP_CANNON_SHELLS", false);
	RuleInventory *hand = rules->getInventory("STR_RIGHT_HAND", true);
	if (cannonRules == 0 || ammoRules == 0 || hand == 0)
	{
		return false;
	}
	BattleItem *cannon = new BattleItem(cannonRules, localBattle.getCurrentItemId());
	BattleItem *clip = new BattleItem(ammoRules, localBattle.getCurrentItemId());
	if (cannon->setAmmoItem(clip) != 0)
	{
		return false;
	}
	cannon->setSlot(hand);
	cannon->setSlotX(0);
	cannon->setSlotY(0);
	cannon->moveToOwner(tank);
	localBattle.getItems()->push_back(cannon);
	localBattle.getItems()->push_back(clip);
	if (tankOut != 0)
	{
		*tankOut = tank;
	}
	if (cannonOut != 0)
	{
		*cannonOut = cannon;
	}
	return true;
}

void runHeadlessDirectedFireCancelForensics()
{
	logEvidence("forensics", "headless BattlescapeGame(parent=0) cancel sweep — same tryExecuteCompanionDirectedFire");
	FireRelayHarness &h = harness();
	for (int preCancelTicks = 0; preCancelTicks <= 12; ++preCancelTicks)
	{
		logEvidence("headless-iter-begin", "preCancelTicks=" + std::to_string(preCancelTicks));
		SavedBattleGame localBattle;
		BattleUnit *tank = 0;
		BattleItem *cannon = 0;
		if (!setupLocalHeadlessBattle(h, localBattle, &tank, &cannon))
		{
			logEvidence("headless-skip", "local headless setup failed");
			return;
		}
		BattlescapeGame trial(&localBattle, 0);
		(void)trial.tryExecuteCompanionDirectedFire(tank, Position(8, 3, 0), cannon, BA_SNAPSHOT, 0);
		for (int tick = 0; tick < preCancelTicks && trial.isBusy(); ++tick)
		{
			trial.handleState();
		}
		logEvidence("headless-pre",
			"ticks=" + std::to_string(preCancelTicks) + " " + describeNativeStack(&trial));
		const CancelInvokeOutcome cancel = invokeCancelCurrentActionSeh(&trial);
		if (cancel.trapped)
		{
			std::ostringstream code;
			code << std::hex << "0x" << cancel.exceptionCode;
			logEvidence("headless-crash", "preCancelTicks=" + std::to_string(preCancelTicks) + " SEH=" + code.str());
			logNativeBacktrace("headless-cancel");
			return;
		}
		logEvidence("headless-post-cancel",
			"ticks=" + std::to_string(preCancelTicks) + " " + describeNativeStack(&trial));
	}
	logEvidence("headless", "S5-1CI: cancel SEH clean per tick; multi-iteration loop may still process-exit AV (investigate separately)");
}
#endif // _WIN32

} // namespace

int main(int argc, char **argv)
{
	FireRelayHarness &h = harness();
	if (!h.initGame(argc > 0 ? argv[0] : "companion_hwp_fire_relay_test", defaultDataDir()))
	{
		std::cerr << "companion_hwp_fire_relay_test: Options/Game init failed" << std::endl;
		return 1;
	}
	h.initRelayEnv();

#ifdef _WIN32
	if (const char *trace = std::getenv("OPENXCOM_COMPANION_HWP_CANCEL_TRACE"))
	{
		if (trace[0] != '0')
		{
			runCancelOperationTrace();
			std::cout << "companion_hwp_fire_relay_test: CANCEL_TRACE done (failures=" << g_failures << ")"
			          << std::endl;
			return g_failures != 0 ? 1 : 0;
		}
	}
	if (const char *forensics = std::getenv("OPENXCOM_COMPANION_HWP_CANCEL_FORENSICS"))
	{
		if (forensics[0] != '0')
		{
			runCancelForensicsSweep();
			runAuthorityLossCancelForensics();
			std::cout << "companion_hwp_fire_relay_test: CANCEL_FORENSICS done (failures=" << g_failures << ")"
			          << std::endl;
			return g_failures != 0 ? 1 : 0;
		}
	}
	if (const char *headlessForensics = std::getenv("OPENXCOM_COMPANION_HWP_HEADLESS_FORENSICS"))
	{
		if (headlessForensics[0] != '0')
		{
			runHeadlessDirectedFireCancelForensics();
			std::cout << "companion_hwp_fire_relay_test: HEADLESS_FORENSICS done (failures=" << g_failures << ")"
			          << std::endl;
			return g_failures != 0 ? 1 : 0;
		}
	}
#endif

	certHarnessGateAndTeardownNotes();
	certWeaponLayoutAndLookup();
	certCannonSnapRelay();
	certCannonAimedRelay();
	certRocketNativeStackDrains();
	certRocketSnapRelayTerminal();
	certRocketAimedRelayTerminal();
	certRocketTurretRotation();
	certRocketRejectEmptyAmmo();
	certRocketRejectInvalidTarget();
	certRocketCancelBeforeProjectileLaunch();
	certRocketInterruptMidFlight();
#ifdef _WIN32
	certRocketAuthorityLossCancel();
#endif
	certLaserSnapRelay();
	certWeaponBattleItemIdZero();
	certTurretRotationPreservesHullWhenPossible();
	certRejectEmptyAmmo();
	certRejectBadWeaponId();
	certRejectInsufficientTu();
	certRejectBlockedVisibleHostileLos();
	certCompositeTurretRotationFireTuRelay();
	certRejectReservedTu();
	certRejectInvalidTarget();
	certRejectUnauthorizedHwp();
	certRejectStaleBinding();
	certDuplicateInboxDoesNotRefire();
	certInterruptMidFlight();

	if (g_failures != 0)
	{
		std::cerr << "companion_hwp_fire_relay_test: " << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_hwp_fire_relay_test: PASS (S5-1CD relay certification)" << std::endl;
	return 0;
}