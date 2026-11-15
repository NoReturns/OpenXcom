/*
 * Open X-Com Companion — battle AAR capture tests (M1 AAR).
 */
#include "Engine/CompanionBattleAarCaptureCore.h"
#include "Engine/CompanionJsonLite.h"
#include "Engine/CompanionSha256.h"
#include "../Savegame/MissionStatistics.h"
#include "../Savegame/GameTime.h"
#include "../Savegame/Soldier.h"
#include "../Battlescape/DebriefingState.h"
#include "../Mod/Unit.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

static void testYamlToJsonRoundTrip()
{
	YAML::Node root;
	root["seq"] = 7;
	root["ok"] = true;
	root["name"] = "STR_ALIENS_KILLED";
	YAML::Node child;
	child["droppedCount"] = 3;
	root["nested"] = child;

	std::ostringstream out;
	expect(CompanionBattleAarCaptureCore::appendYamlNodeAsJson(out, root), "yaml to json");
	const std::string json = out.str();
	int dropped = -1;
	expect(CompanionJsonLite::extractJsonIntField(json, "droppedCount", &dropped), "nested droppedCount");
	expect(dropped == 3, "droppedCount value");
	int seq = -1;
	expect(CompanionJsonLite::extractJsonIntField(json, "seq", &seq), "seq field");
	expect(seq == 7, "seq value");
}

static void testReportId()
{
	const std::string id1 = CompanionBattleAarCaptureCore::computeReportId("camp-a", "battle-b", 12);
	const std::string id2 = CompanionBattleAarCaptureCore::computeReportId("camp-a", "battle-b", 12);
	expect(!id1.empty() && id1 == id2, "report_id stable");
	expect(CompanionSha256::isValidHexDigest(id1), "report_id hex digest");
	const std::string id3 = CompanionBattleAarCaptureCore::computeReportId("camp-a", "battle-b", 13);
	expect(id1 != id3, "report_id changes with mission statistics id");
}

static void testBuildFinalizedMatchesDebriefStats()
{
	CompanionBattleAarCaptureCore::PhaseASnapshot phaseA = {};
	phaseA.captured = true;
	phaseA.campaignId = "test-campaign";
	phaseA.tacticalBattleId = "test-battle";
	phaseA.tacticalSessionId = "sess-1";
	phaseA.missionType = "STR_TERROR_MISSION";
	phaseA.tacticalTerminalYaml["turn"] = 5;
	YAML::Node perception;
	perception["droppedCount"] = 2;
	perception["events"] = YAML::Node(YAML::NodeType::Sequence);
	phaseA.tacticalTerminalYaml["perceptionLog"] = perception;

	MissionStatistics ms;
	ms.id = 4;
	ms.type = "STR_TERROR_MISSION";
	ms.region = "STR_NORTH_AMERICA";
	ms.country = "STR_USA";
	ms.success = true;
	ms.score = 350;
	ms.rating = "STR_RATING_GOOD";
	ms.valiantCrux = false;
	ms.lootValue = 0;
	ms.time = GameTime(0, 10, 3, 1999, 14, 30, 0);
	ms.injuryList[101] = 5;

	DebriefingStat aliens("STR_ALIENS_KILLED", false);
	aliens.qty = 8;
	aliens.score = 400;
	DebriefingStat recovery("STR_ELERIUM_115", true);
	recovery.qty = 2;
	recovery.score = -50;

	std::vector<DebriefingStat *> stats;
	stats.push_back(&aliens);
	stats.push_back(&recovery);

	std::vector<CompanionBattleAarCaptureCore::DebriefOutcomeInput::SoldierStatsEntry> statRows;
	UnitStats delta;
	delta.firing = 2;
	delta.reactions = 1;
	statRows.push_back(std::make_pair(std::string("Test Operative"), delta));

	CompanionBattleAarCaptureCore::DebriefOutcomeInput outcome = {};
	outcome.missionStatistics = &ms;
	outcome.debriefStats = &stats;
	outcome.soldierStatIncreases = &statRows;
	outcome.missionAborted = false;
	outcome.promotionsUiScheduled = true;

	const CompanionBattleAarCaptureCore::BuildResult built =
		CompanionBattleAarCaptureCore::buildFinalizedJson(phaseA, 0, 0, outcome);
	expect(built.ok, "buildFinalizedJson ok");

	int score = -1;
	expect(CompanionJsonLite::extractJsonIntField(built.jsonUtf8, "score", &score), "outcome score field");
	expect(score == 350, "outcome score matches mission statistics");

	std::string status;
	expect(CompanionJsonLite::extractJsonStringField(built.jsonUtf8, "status", &status), "status field");
	expect(status == "finalized", "status finalized");

	int dropped = -1;
	expect(CompanionJsonLite::extractJsonIntField(built.jsonUtf8, "perception_log_dropped_count", &dropped), "dropped count");
	expect(dropped == 2, "dropped count preserved not zero-filled");

	expect(built.jsonUtf8.find("\"lost_in_service\"") != std::string::npos, "lost_in_service section present");
	expect(built.jsonUtf8.find("\"wounded\"") != std::string::npos, "wounded section present");
	expect(built.jsonUtf8.find("\"mission_summary\"") == std::string::npos, "no invented mission_summary");
	expect(built.jsonUtf8.find("\"tactical_timeline\"") == std::string::npos, "no perception tactical_timeline");

	expect(built.reportId == CompanionBattleAarCaptureCore::computeReportId("test-campaign", "test-battle", 4),
		"embedded report_id");
}

static void testRankKeys()
{
	expect(CompanionBattleAarCaptureCore::soldierRankKey(RANK_COMMANDER) == "RANK_COMMANDER", "commander rank key");
	expect(CompanionBattleAarCaptureCore::soldierRankKey(RANK_ROOKIE) == "RANK_ROOKIE", "rookie rank key");
}

int main()
{
	testYamlToJsonRoundTrip();
	testReportId();
	testBuildFinalizedMatchesDebriefStats();
	testRankKeys();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return 1;
	}
	std::cout << "companion_battle_aar_capture_test: all passed." << std::endl;
	return 0;
}