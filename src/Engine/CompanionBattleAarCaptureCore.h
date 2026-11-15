#pragma once
/*
 * Open X-Com Companion — battle AAR JSON builder (M1 AAR local capture).
 */
#include <map>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class BattleUnit;
class SavedBattleGame;
class SavedGame;
class MissionStatistics;
struct DebriefingStat;
struct UnitStats;
class Soldier;
enum SoldierRank;

namespace CompanionBattleAarCaptureCore
{

static const int AAR_SCHEMA_VERSION = 1;

struct PhaseASnapshot
{
	bool captured;
	std::string campaignId;
	std::string tacticalBattleId;
	std::string tacticalSessionId;
	std::string missionType;
	YAML::Node tacticalTerminalYaml;
	std::map<int, int> geoscapeSoldierIdToRank;
};

struct DebriefOutcomeInput
{
	const MissionStatistics *missionStatistics;
	const std::vector<DebriefingStat *> *debriefStats;
	typedef std::pair<std::string, UnitStats> SoldierStatsEntry;
	const std::vector<SoldierStatsEntry> *soldierStatIncreases;
	bool missionAborted;
	bool promotionsUiScheduled;
};

struct PromotionAppliedRow
{
	int geoscapeSoldierId;
	std::string displayName;
	std::string rankBefore;
	std::string rankAfter;
};

struct CommendationAwardedRow
{
	int geoscapeSoldierId;
	std::string displayName;
	std::string commendationType;
	std::string noun;
	int decorationLevel;
};

struct BuildResult
{
	bool ok;
	std::string jsonUtf8;
	std::string reportId;
	std::string skipReason;
};

/// Collect phase-A data before prepareDebriefing mutates the battle.
PhaseASnapshot capturePhaseAData(SavedGame *save, SavedBattleGame *battle);

/// Final JSON from phase A + debrief inputs (authoritative stats only).
BuildResult buildFinalizedJson(
	const PhaseASnapshot &phaseA,
	SavedGame *save,
	SavedBattleGame *battle,
	const DebriefOutcomeInput &outcome);

/// Serialize YAML subtree to JSON (bounded; for tactical_terminal embedding).
bool appendYamlNodeAsJson(std::ostringstream &out, const YAML::Node &node);

std::string soldierRankKey(SoldierRank rank);

std::vector<PromotionAppliedRow> diffPromotions(
	const std::map<int, int> &rankBefore,
	SavedBattleGame *battle);

std::vector<CommendationAwardedRow> collectNewCommendations(SavedBattleGame *battle);

std::string computeReportId(const std::string &campaignId, const std::string &tacticalBattleId, int missionStatisticsId);

} // namespace CompanionBattleAarCaptureCore

} // namespace OpenXcom