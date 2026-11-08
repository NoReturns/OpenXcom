/*
 * M1 P4 Step 5 — direct BattlescapeGenerator stress export (TEST_FULL_REVEAL diagnostic only).
 * Console harness: loads real OpenXcom data, generates missions, writes disposable artifacts.
 */
#include "Engine/CompanionManualV5Export.h"
#include "Engine/CompanionSnapshotBuild.h"
#include "Engine/CompanionSnapshotExportCore.h"
#include "Engine/CompanionSnapshotTerrain.h"
#include "Engine/CompanionSnapshotTerrainCapture.h"
#include "Engine/CompanionSnapshotTerrainSemantics.h"
#include "Engine/Exception.h"
#include "Engine/Game.h"
#include "Engine/Options.h"
#include "Engine/RNG.h"
#include "Engine/State.h"
#include "../Battlescape/BattlescapeGenerator.h"
#include "../Battlescape/TileEngine.h"
#include "../Mod/AlienDeployment.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleAlienMission.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleUfo.h"
#include "../Savegame/Base.h"
#include "../Savegame/Craft.h"
#include "../Savegame/MissionSite.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include "../Savegame/Ufo.h"
#include "../Battlescape/Position.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#endif

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace CompanionSnapshotTerrain;
using namespace CompanionSnapshotExportCore;

namespace
{

const int SCHEMA_V5 = 5;
const int SCHEMA_V4 = 4;

struct ScenarioSpec
{
	const char *id;
	const char *deployment;
};

struct VariantSeed
{
	std::string scenarioId;
	uint64_t seed;
};

struct RunArtifacts
{
	std::string diagnosticYaml;
	std::string discoveredYaml;
	std::string automaticYaml;
	std::string asciiJson;
	int mapW;
	int mapH;
	int mapD;
	size_t gridCells;
	size_t recordsFull;
	size_t recordsDiscovered;
	size_t v5FullBytes;
	size_t v5DiscoveredBytes;
	size_t automaticBytes;
	int automaticSchema;
	bool automaticTerrainOmitted;
	size_t packedBase64Bytes;
};

static void writeUtf8File(const std::string &path, const std::string &utf8)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
	{
		throw std::runtime_error("failed to open output file: " + path);
	}
	out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
}

static size_t countTerrainRecords(SavedBattleGame *battle, Mod *mod, bool testFullReveal)
{
	const int mapW = battle->getMapSizeX();
	const int mapH = battle->getMapSizeY();
	const int mapD = battle->getMapSizeZ();
	TerrainSemanticsContext semCtx = buildSemanticsContext(battle, mod);
	TerrainCaptureOptions captureOptions;
	captureOptions.semanticsContext = &semCtx;
	captureOptions.testFullRevealGeometry = testFullReveal;
	std::vector<TerrainTileExport> records;
	captureTerrainTiles(mapW, mapH, mapD,
		[battle](int x, int y, int z) -> Tile * { return battle->getTile(Position(x, y, z)); },
		records,
		&captureOptions);
	return records.size();
}

static SavedGame *buildDisposableGeoscape(Game &game)
{
	Mod *mod = game.getMod();
	SavedGame *save = new SavedGame();
	Base *base = new Base(mod);
	const YAML::Node &starter = mod->getStartingBase();
	base->load(starter, save, true, true);
	for (std::vector<Soldier*>::iterator i = base->getSoldiers()->begin(); i != base->getSoldiers()->end(); ++i)
	{
		delete *i;
	}
	base->getSoldiers()->clear();
	for (std::vector<Craft*>::iterator i = base->getCrafts()->begin(); i != base->getCrafts()->end(); ++i)
	{
		delete *i;
	}
	base->getCrafts()->clear();
	RuleCraft *craftRules = mod->getCraft("STR_SKYRANGER", true);
	Craft *craft = new Craft(craftRules, base, 1);
	base->getCrafts()->push_back(craft);
	const int squadSize = std::min(12, craftRules->getSoldiers());
	for (int i = 0; i < squadSize; ++i)
	{
		Soldier *soldier = mod->genSoldier(save, "STR_SOLDIER");
		base->getSoldiers()->push_back(soldier);
		soldier->setCraft(craft);
	}
	save->getBases()->push_back(base);
	return save;
}

static void clearTransientBattlePointers(SavedGame *save)
{
	for (std::vector<Ufo*>::iterator i = save->getUfos()->begin(); i != save->getUfos()->end(); ++i)
	{
		delete *i;
	}
	save->getUfos()->clear();
	for (std::vector<MissionSite*>::iterator i = save->getMissionSites()->begin(); i != save->getMissionSites()->end(); ++i)
	{
		delete *i;
	}
	save->getMissionSites()->clear();
}

static void preparePlayerFov(SavedBattleGame *battle)
{
	BattleUnit *firstPlayer = 0;
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *unit = *i;
		if (unit != 0 && unit->getFaction() == FACTION_PLAYER && !unit->isOut())
		{
			firstPlayer = unit;
			break;
		}
	}
	if (firstPlayer != 0)
	{
		battle->setSelectedUnit(firstPlayer);
	}
	battle->getTileEngine()->recalculateFOV();
}

static SavedBattleGame *generateBattle(Game &game, SavedGame *save, const ScenarioSpec &scenario, uint64_t seed)
{
	RNG::setSeed(seed);
	clearTransientBattlePointers(save);
	delete save->getSavedBattle();
	save->setBattleGame(0);

	SavedBattleGame *battle = new SavedBattleGame();
	save->setBattleGame(battle);
	Mod *mod = game.getMod();
	BattlescapeGenerator bgen(&game);
	Base *base = save->getBases()->front();
	Craft *craft = base->getCrafts()->front();
	craft->setSpeed(0);
	craft->setDestination(0);

	if (std::string(scenario.id) == "battleship")
	{
		battle->setMissionType("STR_BATTLESHIP");
		RuleUfo *ufoRules = mod->getUfo("STR_BATTLESHIP", true);
		Ufo *ufo = new Ufo(ufoRules);
		ufo->setId(static_cast<int>(seed & 0x7fffffff));
		// Landed assault only — crash recovery is unstable headless and not required for stress sizing.
		ufo->setStatus(Ufo::LANDED);
		battle->setMissionType("STR_UFO_GROUND_ASSAULT");
		craft->setDestination(ufo);
		save->getUfos()->push_back(ufo);
		bgen.setUfo(ufo);
	}
	else if (std::string(scenario.id) == "cydonia_final")
	{
		battle->setMissionType("STR_MARS_THE_FINAL_ASSAULT");
		const AlienDeployment *deployment = mod->getDeployment("STR_MARS_THE_FINAL_ASSAULT", true);
		const RuleAlienMission *mission = mod->getAlienMission(mod->getAlienMissionList().front());
		MissionSite *site = new MissionSite(mission, deployment);
		site->setId(static_cast<int>(seed & 0x7fffffff));
		site->setAlienRace(deployment->getRace().empty() ? "STR_SECTOID" : deployment->getRace());
		craft->setDestination(site);
		save->getMissionSites()->push_back(site);
		bgen.setMissionSite(site);
		bgen.setTerrain(mod->getTerrain("UBASE", true));
	}
	else
	{
		throw std::runtime_error("unknown scenario id");
	}

	bgen.setCraft(craft);
	bgen.setWorldShade(12);
	bgen.setAlienRace("STR_SECTOID");
	bgen.setAlienItemlevel(0);
	battle->setDepth(0);
	bgen.run();
	preparePlayerFov(battle);
	return battle;
}

static RunArtifacts exportBattleArtifacts(Game &game, SavedGame *save, SavedBattleGame *battle,
	const ScenarioSpec &scenario, uint64_t seed, const std::string &outputDir)
{
	RunArtifacts out;
	Mod *mod = game.getMod();
	out.mapW = battle->getMapSizeX();
	out.mapH = battle->getMapSizeY();
	out.mapD = battle->getMapSizeZ();
	out.gridCells = static_cast<size_t>(out.mapW) * static_cast<size_t>(out.mapH) * static_cast<size_t>(out.mapD);
	out.recordsFull = countTerrainRecords(battle, mod, true);
	out.recordsDiscovered = countTerrainRecords(battle, mod, false);

	YAML::Node diagnostic = CompanionSnapshotBuild::buildSnapshot(battle, save, SCHEMA_V5, true, true);
	YAML::Node companionExport;
	companionExport["mode"] = "TEST_FULL_REVEAL";
	companionExport["authoritative"] = false;
	companionExport["rngSeed"] = static_cast<unsigned long long>(seed);
	companionExport["scenario"] = scenario.id;
	companionExport["deployment"] = scenario.deployment;
	companionExport["generator"] = "BattlescapeGenerator";
	diagnostic["companionExport"] = companionExport;
	const std::string diagnosticSessionId = "00000000000000000000000000000001";
	if (diagnostic["eventLog"])
	{
		diagnostic["eventLog"]["sessionId"] = diagnosticSessionId;
	}
	if (diagnostic["perceptionLog"])
	{
		diagnostic["perceptionLog"]["sessionId"] = diagnosticSessionId;
	}
	if (diagnostic["groundItems"])
	{
		diagnostic["groundItems"]["sessionId"] = diagnosticSessionId;
	}

	YAML::Node discoveredOnly = CompanionSnapshotBuild::buildSnapshot(battle, save, SCHEMA_V5, true, false);
	const std::string v5FullYaml = CompanionSnapshotBuild::serializeSnapshotYaml(diagnostic);
	const std::string v5DiscoveredYaml = CompanionSnapshotBuild::serializeSnapshotYaml(discoveredOnly);
	out.v5FullBytes = v5FullYaml.size();
	out.v5DiscoveredBytes = v5DiscoveredYaml.size();

	ResolvedSnapshot resolved = resolveSnapshotForSizeCap(diagnostic,
		[&]() { return CompanionSnapshotBuild::buildSnapshot(battle, save, SCHEMA_V4, false, false); });
	out.automaticSchema = resolved.schemaVersion;
	out.automaticBytes = resolved.ok ? resolved.yamlUtf8.size() : 0;
	out.automaticTerrainOmitted = resolved.terrainOmittedDueToSize;

	std::ostringstream fname;
	fname << scenario.id << "-seed" << seed;
	const std::string stem = fname.str();
	out.diagnosticYaml = outputDir + "/" + stem + "-diagnostic-v5-full.yaml";
	out.discoveredYaml = outputDir + "/" + stem + "-discovered-v5-baseline.yaml";
	out.automaticYaml = outputDir + "/" + stem + "-production-automatic-sim.yaml";
	out.asciiJson = outputDir + "/" + stem + "-full-ascii.json";

	writeUtf8File(out.diagnosticYaml, v5FullYaml);
	writeUtf8File(out.discoveredYaml, v5DiscoveredYaml);
	if (resolved.ok)
	{
		writeUtf8File(out.automaticYaml, resolved.yamlUtf8);
	}

	if (diagnostic["terrain"] && diagnostic["terrain"]["data"])
	{
		out.packedBase64Bytes = diagnostic["terrain"]["data"].as<std::string>().size();
	}
	return out;
}

static std::string jsonEscape(const std::string &s)
{
	std::ostringstream o;
	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		if (c == '"' || c == '\\')
		{
			o << '\\' << c;
		}
		else if (c == '\n')
		{
			o << "\\n";
		}
		else if (c == '\r')
		{
			o << "\\r";
		}
		else
		{
			o << c;
		}
	}
	return o.str();
}

static void appendGenerationReport(const std::string &path, const RunArtifacts &run, const ScenarioSpec &scenario,
	uint64_t seed)
{
	const bool exists = std::ifstream(path.c_str()).good();
	std::ofstream out(path.c_str(), std::ios::out | std::ios::app);
	if (!exists)
	{
		out << "{\n  \"schema\": \"m1-p4-step5-generation-report-v1\",\n  \"runs\": [\n";
	}
	else
	{
		out << ",\n";
	}
	out << "    {\n"
	    << "      \"scenario\": \"" << jsonEscape(scenario.id) << "\",\n"
	    << "      \"deployment\": \"" << jsonEscape(scenario.deployment) << "\",\n"
	    << "      \"rngSeed\": " << seed << ",\n"
	    << "      \"map\": { \"width\": " << run.mapW << ", \"height\": " << run.mapH << ", \"depth\": " << run.mapD
	    << " },\n"
	    << "      \"gridCells\": " << run.gridCells << ",\n"
	    << "      \"terrainRecords\": { \"testFullReveal\": " << run.recordsFull
	    << ", \"discoveredOnly\": " << run.recordsDiscovered << " },\n"
	    << "      \"bytes\": {\n"
	    << "        \"v5FullYaml\": " << run.v5FullBytes << ",\n"
	    << "        \"v5DiscoveredYaml\": " << run.v5DiscoveredBytes << ",\n"
	    << "        \"packedV1Base64Chars\": " << run.packedBase64Bytes << ",\n"
	    << "        \"productionAutomaticSim\": " << run.automaticBytes << "\n"
	    << "      },\n"
	    << "      \"productionAutomatic\": { \"schemaVersion\": " << run.automaticSchema << ", \"terrainOmitted\": "
	    << (run.automaticTerrainOmitted ? "true" : "false") << " },\n"
	    << "      \"paths\": {\n"
	    << "        \"diagnosticYaml\": \"" << jsonEscape(run.diagnosticYaml) << "\",\n"
	    << "        \"discoveredYaml\": \"" << jsonEscape(run.discoveredYaml) << "\",\n"
	    << "        \"automaticYaml\": \"" << jsonEscape(run.automaticYaml) << "\"\n"
	    << "      }\n"
	    << "    }";
}

static void finalizeGenerationReport(const std::string &path)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::app);
	out << "\n  ]\n}\n";
}

static void usage(const char *argv0)
{
	std::cerr
		<< "Usage: " << argv0
		<< " --output-dir <dir> [--data <path>] [--user <path>] [--seeds-per-scenario N]\n"
		<< "Generates STR_BATTLESHIP and STR_MARS_THE_FINAL_ASSAULT maps via BattlescapeGenerator.\n";
}

} // namespace

int main(int argc, char *argv[])
{
	std::string outputDir;
	std::string dataDir;
	std::string userDir;
	int seedsPerScenario = 2;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--output-dir" && i + 1 < argc)
		{
			outputDir = argv[++i];
		}
		else if (arg == "--data" && i + 1 < argc)
		{
			dataDir = argv[++i];
		}
		else if (arg == "--user" && i + 1 < argc)
		{
			userDir = argv[++i];
		}
		else if (arg == "--seeds-per-scenario" && i + 1 < argc)
		{
			seedsPerScenario = std::max(1, std::atoi(argv[++i]));
		}
		else if (arg == "--help" || arg == "-h")
		{
			usage(argv[0]);
			return 0;
		}
	}

	if (outputDir.empty())
	{
		usage(argv[0]);
		return 2;
	}

#ifdef _WIN32
	_mkdir(outputDir.c_str());
#else
	mkdir(outputDir.c_str(), 0755);
#endif

	std::vector<char *> fakeArgv;
	fakeArgv.push_back(argv[0]);
	if (!dataDir.empty())
	{
		fakeArgv.push_back(const_cast<char *>("-data"));
		fakeArgv.push_back(const_cast<char *>(dataDir.c_str()));
	}
	if (!userDir.empty())
	{
		fakeArgv.push_back(const_cast<char *>("-user"));
		fakeArgv.push_back(const_cast<char *>(userDir.c_str()));
		fakeArgv.push_back(const_cast<char *>("-config"));
		fakeArgv.push_back(const_cast<char *>(userDir.c_str()));
	}
	const int fakeArgc = static_cast<int>(fakeArgv.size());
	if (!Options::init(fakeArgc, fakeArgv.data()))
	{
		std::cerr << "Options::init failed or --help invoked." << std::endl;
		return 2;
	}

	try
	{
		Game game("Companion P5 stress harness");
		State::setGamePtr(&game);
		Options::updateMods();
		game.loadMods();
		game.loadLanguages();

		const ScenarioSpec scenarios[] = {
			{"battleship", "STR_BATTLESHIP"},
			{"cydonia_final", "STR_MARS_THE_FINAL_ASSAULT"},
		};
		const std::string reportPath = outputDir + "/generation-report.partial.json";

		for (size_t s = 0; s < sizeof(scenarios) / sizeof(scenarios[0]); ++s)
		{
			const ScenarioSpec &scenario = scenarios[s];
			for (int variant = 0; variant < seedsPerScenario; ++variant)
			{
				const uint64_t baseSeed = (std::string(scenario.id) == "battleship") ? 42001u : 43001u;
				const uint64_t seed = baseSeed + static_cast<uint64_t>(variant);
				std::cerr << "Generating " << scenario.id << " seed=" << seed << "..." << std::endl;
				SavedGame *save = buildDisposableGeoscape(game);
				game.setSavedGame(save);
				SavedBattleGame *battle = generateBattle(game, save, scenario, seed);
				RunArtifacts artifacts = exportBattleArtifacts(game, save, battle, scenario, seed, outputDir);
				appendGenerationReport(reportPath, artifacts, scenario, seed);
				std::cerr << "  map " << artifacts.mapW << "x" << artifacts.mapH << "x" << artifacts.mapD
				          << " records full=" << artifacts.recordsFull << " discovered=" << artifacts.recordsDiscovered
				          << " v5=" << artifacts.v5FullBytes << " bytes automaticSim=" << artifacts.automaticBytes
				          << " schema=" << artifacts.automaticSchema << std::endl;
			}
		}

		finalizeGenerationReport(reportPath);
		const std::string finalReport = outputDir + "/generation-report.json";
		{
			std::ifstream in(reportPath.c_str(), std::ios::in | std::ios::binary);
			std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			writeUtf8File(finalReport, body);
		}
		std::cerr << "Wrote " << finalReport << std::endl;
	}
	catch (const Exception &e)
	{
		std::cerr << "Harness failed (OpenXcom): " << e.what() << std::endl;
		return 1;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Harness failed: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Harness failed: unknown exception" << std::endl;
		return 1;
	}

	return 0;
}
