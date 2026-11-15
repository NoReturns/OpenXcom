/*
 * Open X-Com Companion — battle AAR JSON builder (M1 AAR local capture).
 */
#include "CompanionBattleAarCaptureCore.h"
#include "CompanionSnapshotBuild.h"
#include "CompanionSha256.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/MissionStatistics.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleUnitStatistics.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/SoldierDiary.h"
#include "../Savegame/GameTime.h"
#include "../Mod/Unit.h"
#include "../Battlescape/DebriefingState.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace OpenXcom
{

namespace CompanionBattleAarCaptureCore
{

namespace
{

void appendEscapedJsonString(std::ostringstream &out, const std::string &s)
{
	out << '"';
	for (size_t i = 0; i < s.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c)
		{
		case '"': out << "\\\""; break;
		case '\\': out << "\\\\"; break;
		case '\b': out << "\\b"; break;
		case '\f': out << "\\f"; break;
		case '\n': out << "\\n"; break;
		case '\r': out << "\\r"; break;
		case '\t': out << "\\t"; break;
		default:
			if (c < 0x20)
			{
				out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec
				    << std::setfill(' ');
			}
			else
			{
				out << static_cast<char>(c);
			}
		}
	}
	out << '"';
}

void appendJsonIndent(std::ostringstream &out, int depth)
{
	for (int i = 0; i < depth; ++i)
	{
		out << "  ";
	}
}

bool appendYamlScalarJson(std::ostringstream &out, const YAML::Node &node)
{
	if (node.IsNull())
	{
		out << "null";
		return true;
	}
	if (node.Tag() == "!")
	{
		out << "null";
		return true;
	}
	try
	{
		switch (node.Type())
		{
		case YAML::NodeType::Scalar:
			if (node.Scalar() == "null" || node.Scalar() == "~")
			{
				out << "null";
			}
			else if (node.Scalar() == "true" || node.Scalar() == "false")
			{
				out << node.Scalar();
			}
			else
			{
				try
				{
					(void)node.as<int>();
					out << node.as<int>();
				}
				catch (...)
				{
					try
					{
						(void)node.as<double>();
						out << node.as<double>();
					}
					catch (...)
					{
						appendEscapedJsonString(out, node.as<std::string>());
					}
				}
			}
			return true;
		default:
			return false;
		}
	}
	catch (...)
	{
		appendEscapedJsonString(out, node.as<std::string>(""));
		return true;
	}
}

bool appendYamlNodeAsJsonImpl(std::ostringstream &out, const YAML::Node &node, int depth)
{
	if (!node.IsDefined() || node.IsNull())
	{
		out << "null";
		return true;
	}
	if (node.Type() == YAML::NodeType::Scalar)
	{
		return appendYamlScalarJson(out, node);
	}
	if (node.Type() == YAML::NodeType::Sequence)
	{
		out << "[";
		bool first = true;
		for (YAML::const_iterator i = node.begin(); i != node.end(); ++i)
		{
			if (!first)
			{
				out << ",";
			}
			first = false;
			out << "\n";
			appendJsonIndent(out, depth + 1);
			if (!appendYamlNodeAsJsonImpl(out, *i, depth + 1))
			{
				return false;
			}
		}
		if (!first)
		{
			out << "\n";
			appendJsonIndent(out, depth);
		}
		out << "]";
		return true;
	}
	if (node.Type() == YAML::NodeType::Map)
	{
		out << "{";
		bool first = true;
		for (YAML::const_iterator i = node.begin(); i != node.end(); ++i)
		{
			if (!first)
			{
				out << ",";
			}
			first = false;
			out << "\n";
			appendJsonIndent(out, depth + 1);
			appendEscapedJsonString(out, i->first.as<std::string>());
			out << ": ";
			if (!appendYamlNodeAsJsonImpl(out, i->second, depth + 1))
			{
				return false;
			}
		}
		if (!first)
		{
			out << "\n";
			appendJsonIndent(out, depth);
		}
		out << "}";
		return true;
	}
	out << "null";
	return true;
}

void appendUnitStatsDelta(std::ostringstream &out, const UnitStats &stats)
{
	out << "{";
	out << "\"tu\":" << stats.tu << ",";
	out << "\"stamina\":" << stats.stamina << ",";
	out << "\"health\":" << stats.health << ",";
	out << "\"bravery\":" << stats.bravery << ",";
	out << "\"reactions\":" << stats.reactions << ",";
	out << "\"firing\":" << stats.firing << ",";
	out << "\"throwing\":" << stats.throwing << ",";
	out << "\"melee\":" << stats.melee << ",";
	out << "\"strength\":" << stats.strength << ",";
	out << "\"psiStrength\":" << stats.psiStrength << ",";
	out << "\"psiSkill\":" << stats.psiSkill;
	out << "}";
}

const char *unitFactionKey(UnitFaction f)
{
	switch (f)
	{
	case FACTION_PLAYER: return "player";
	case FACTION_HOSTILE: return "hostile";
	case FACTION_NEUTRAL: return "neutral";
	default: return "unknown";
	}
}

const char *unitStatusKey(UnitStatus s)
{
	switch (s)
	{
	case STATUS_STANDING: return "standing";
	case STATUS_WALKING: return "walking";
	case STATUS_FLYING: return "flying";
	case STATUS_TURNING: return "turning";
	case STATUS_AIMING: return "aiming";
	case STATUS_COLLAPSING: return "collapsing";
	case STATUS_UNCONSCIOUS: return "unconscious";
	case STATUS_DEAD: return "dead";
	case STATUS_PANICKING: return "panicking";
	case STATUS_BERSERK: return "berserk";
	default: return "unknown";
	}
}

} // namespace

bool appendYamlNodeAsJson(std::ostringstream &out, const YAML::Node &node)
{
	return appendYamlNodeAsJsonImpl(out, node, 0);
}

std::string soldierRankKey(SoldierRank rank)
{
	switch (rank)
	{
	case RANK_ROOKIE: return "RANK_ROOKIE";
	case RANK_SQUADDIE: return "RANK_SQUADDIE";
	case RANK_SERGEANT: return "RANK_SERGEANT";
	case RANK_CAPTAIN: return "RANK_CAPTAIN";
	case RANK_COLONEL: return "RANK_COLONEL";
	case RANK_COMMANDER: return "RANK_COMMANDER";
	default: return "RANK_UNKNOWN";
	}
}

std::string computeReportId(const std::string &campaignId, const std::string &tacticalBattleId, int missionStatisticsId)
{
	std::ostringstream canonical;
	canonical << "{\"campaign_id\":\"" << campaignId << "\",\"mission_statistics_id\":" << missionStatisticsId
	          << ",\"tactical_battle_id\":\"" << tacticalBattleId << "\"}";
	std::string digest;
	if (!CompanionSha256::digestHexLowerUtf8(canonical.str(), &digest))
	{
		return "";
	}
	return digest;
}

PhaseASnapshot capturePhaseAData(SavedGame *save, SavedBattleGame *battle)
{
	PhaseASnapshot snap = {};
	snap.captured = false;
	if (save == 0 || battle == 0)
	{
		return snap;
	}
	snap.campaignId = save->getCompanionCampaignId();
	snap.tacticalBattleId = battle->getCompanionTacticalBattleId();
	snap.tacticalSessionId = battle->getCompanionEventLog().getSessionId();
	snap.missionType = battle->getMissionType();
	snap.tacticalTerminalYaml = CompanionSnapshotBuild::buildAarTacticalTerminal(battle, save);
	for (std::vector<BattleUnit *>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		Soldier *gs = (*i)->getGeoscapeSoldier();
		if (gs != 0)
		{
			snap.geoscapeSoldierIdToRank[gs->getId()] = static_cast<int>(gs->getRank());
		}
	}
	snap.captured = true;
	return snap;
}

std::vector<PromotionAppliedRow> diffPromotions(const std::map<int, int> &rankBefore, SavedBattleGame *battle)
{
	std::vector<PromotionAppliedRow> rows;
	if (battle == 0)
	{
		return rows;
	}
	for (std::vector<BattleUnit *>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		Soldier *gs = (*i)->getGeoscapeSoldier();
		if (gs == 0)
		{
			continue;
		}
		const int sid = gs->getId();
		std::map<int, int>::const_iterator prev = rankBefore.find(sid);
		const int beforeRank = prev != rankBefore.end() ? prev->second : static_cast<int>(gs->getRank());
		const int afterRank = static_cast<int>(gs->getRank());
		if (afterRank > beforeRank)
		{
			PromotionAppliedRow row = {};
			row.geoscapeSoldierId = sid;
			row.displayName = gs->getName();
			row.rankBefore = soldierRankKey(static_cast<SoldierRank>(beforeRank));
			row.rankAfter = soldierRankKey(static_cast<SoldierRank>(afterRank));
			rows.push_back(row);
		}
	}
	return rows;
}

std::vector<CommendationAwardedRow> collectNewCommendations(SavedBattleGame *battle)
{
	std::vector<CommendationAwardedRow> rows;
	if (battle == 0)
	{
		return rows;
	}
	for (std::vector<BattleUnit *>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		Soldier *gs = (*i)->getGeoscapeSoldier();
		if (gs == 0 || gs->getDiary() == 0)
		{
			continue;
		}
		std::vector<SoldierCommendations *> *commendations = gs->getDiary()->getSoldierCommendations();
		if (commendations == 0)
		{
			continue;
		}
		for (std::vector<SoldierCommendations *>::const_iterator c = commendations->begin(); c != commendations->end(); ++c)
		{
			if (*c == 0 || !(*c)->isNew())
			{
				continue;
			}
			CommendationAwardedRow row = {};
			row.geoscapeSoldierId = gs->getId();
			row.displayName = gs->getName();
			row.commendationType = (*c)->getType();
			row.noun = (*c)->getNoun();
			row.decorationLevel = (*c)->getDecorationLevelInt();
			rows.push_back(row);
		}
	}
	return rows;
}

BuildResult buildFinalizedJson(
	const PhaseASnapshot &phaseA,
	SavedGame *save,
	SavedBattleGame *battle,
	const DebriefOutcomeInput &outcome)
{
	BuildResult result = {};
	if (!phaseA.captured || outcome.missionStatistics == 0 || outcome.debriefStats == 0)
	{
		result.skipReason = "missing_phase_a_or_outcome";
		return result;
	}

	const MissionStatistics *ms = outcome.missionStatistics;
	const std::vector<PromotionAppliedRow> promotions = diffPromotions(phaseA.geoscapeSoldierIdToRank, battle);
	const std::vector<CommendationAwardedRow> commendations = collectNewCommendations(battle);

	std::ostringstream json;
	json << "{\n";
	json << "  \"schema_version\": " << AAR_SCHEMA_VERSION << ",\n";
	json << "  \"type\": \"battle_aar\",\n";
	json << "  \"status\": \"finalized\",\n";

	const std::string reportId = computeReportId(phaseA.campaignId, phaseA.tacticalBattleId, ms->id);
	json << "  \"report_id\": \"" << reportId << "\",\n";
	const long long finalizedAtMs = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	json << "  \"generation_id\": \"" << finalizedAtMs << "\",\n";
	json << "  \"finalized_at_ms\": " << finalizedAtMs << ",\n";
	json << "  \"published_at\": null,\n";

	json << "  \"identity\": {\n";
	json << "    \"campaign_id\": ";
	appendEscapedJsonString(json, phaseA.campaignId);
	json << ",\n";
	json << "    \"tactical_battle_id\": ";
	appendEscapedJsonString(json, phaseA.tacticalBattleId);
	json << ",\n";
	json << "    \"tactical_session_id\": ";
	appendEscapedJsonString(json, phaseA.tacticalSessionId);
	json << ",\n";
	json << "    \"mission_statistics_id\": " << ms->id << ",\n";
	json << "    \"mission_type\": ";
	appendEscapedJsonString(json, ms->type.empty() ? phaseA.missionType : ms->type);
	json << ",\n";
	json << "    \"region\": ";
	appendEscapedJsonString(json, ms->region);
	json << ",\n";
	json << "    \"country\": ";
	appendEscapedJsonString(json, ms->country);
	json << ",\n";
	if (!ms->markerName.empty())
	{
		json << "    \"marker_name\": ";
		appendEscapedJsonString(json, ms->markerName);
		json << ",\n";
		json << "    \"marker_id\": " << ms->markerId << ",\n";
	}
	json << "    \"game_time\": {";
	json << "\"year\":" << ms->time.getYear() << ",";
	json << "\"month\":" << ms->time.getMonth() << ",";
	json << "\"day\":" << ms->time.getDay() << ",";
	json << "\"hour\":" << ms->time.getHour() << ",";
	json << "\"minute\":" << ms->time.getMinute();
	json << "},\n";
	json << "    \"mission_start_time\": null\n";
	json << "  },\n";

	json << "  \"outcome\": {\n";
	json << "    \"aborted\": " << (outcome.missionAborted ? "true" : "false") << ",\n";
	json << "    \"success\": " << (ms->success ? "true" : "false") << ",\n";
	json << "    \"score\": " << ms->score << ",\n";
	json << "    \"rating\": ";
	appendEscapedJsonString(json, ms->rating);
	json << ",\n";
	json << "    \"valiant_crux\": " << (ms->valiantCrux ? "true" : "false") << ",\n";
	json << "    \"loot_value\": " << ms->lootValue << ",\n";
	json << "    \"score_breakdown\": [";
	bool firstBreakdown = true;
	for (std::vector<DebriefingStat *>::const_iterator i = outcome.debriefStats->begin();
	     i != outcome.debriefStats->end();
	     ++i)
	{
		const DebriefingStat *row = *i;
		if (row == 0 || row->qty == 0)
		{
			continue;
		}
		if (!firstBreakdown)
		{
			json << ",";
		}
		firstBreakdown = false;
		json << "\n      {";
		json << "\"line_id\": ";
		appendEscapedJsonString(json, row->item);
		json << ", \"quantity\": " << row->qty << ", \"points\": " << row->score;
		json << ", \"recovery\": " << (row->recovery ? "true" : "false") << "}";
	}
	if (!firstBreakdown)
	{
		json << "\n    ";
	}
	json << "],\n";
	json << "    \"provisional\": false\n";
	json << "  },\n";

	json << "  \"tactical_terminal\": {\n";
	json << "    \"captured_at_phase\": \"pre_prepare_debriefing\",\n";
	json << "    \"log_retention_note\": ";
	appendEscapedJsonString(json,
		"eventLog and perceptionLog are bounded rings; droppedCount > 0 means older events unavailable, not zero.");
	json << ",\n";
	json << "    \"terminal_payload\": ";
	appendYamlNodeAsJsonImpl(json, phaseA.tacticalTerminalYaml, 2);
	json << "\n  },\n";

	json << "  \"post_debrief\": {\n";
	json << "    \"injury_list\": {";
	bool firstInjury = true;
	for (std::map<int, int>::const_iterator i = ms->injuryList.begin(); i != ms->injuryList.end(); ++i)
	{
		if (!firstInjury)
		{
			json << ",";
		}
		firstInjury = false;
		json << "\"" << i->first << "\":" << i->second;
	}
	json << "},\n";
	json << "    \"soldier_stat_increases\": [";
	bool firstStat = true;
	if (outcome.soldierStatIncreases != 0)
	{
		for (std::vector<DebriefOutcomeInput::SoldierStatsEntry>::const_iterator i =
		         outcome.soldierStatIncreases->begin();
		     i != outcome.soldierStatIncreases->end();
		     ++i)
		{
			if (!firstStat)
			{
				json << ",";
			}
			firstStat = false;
			json << "\n      {\"display_name\": ";
			appendEscapedJsonString(json, i->first);
			json << ", \"delta\": ";
			appendUnitStatsDelta(json, i->second);
			json << "}";
		}
	}
	if (!firstStat)
	{
		json << "\n    ";
	}
	json << "],\n";

	json << "    \"soldiers\": [";
	bool firstSoldier = true;
	if (battle != 0)
	{
		for (std::vector<BattleUnit *>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
		{
			Soldier *gs = (*i)->getGeoscapeSoldier();
			if (gs == 0)
			{
				continue;
			}
			if (!firstSoldier)
			{
				json << ",";
			}
			firstSoldier = false;
			const BattleUnitStatistics *st = (*i)->getStatistics();
			json << "\n      {";
			json << "\"geoscape_soldier_id\": " << gs->getId() << ",";
			json << "\"display_name\": ";
			appendEscapedJsonString(json, gs->getName());
			json << ",\"rank\": ";
			appendEscapedJsonString(json, soldierRankKey(gs->getRank()));
			json << ",\"kia\": " << (st != 0 && st->KIA ? "true" : "false");
			json << ",\"mia\": " << (st != 0 && st->MIA ? "true" : "false");
			json << ",\"wound_recovery_days\": " << gs->getWoundRecovery();
			json << ",\"career_kills\": " << gs->getKills();
			json << ",\"engine_kills\": [";
			bool firstKill = true;
			if (st != 0)
			{
				for (std::vector<BattleUnitKills *>::const_iterator k = st->kills.begin(); k != st->kills.end(); ++k)
				{
					if (*k == 0)
					{
						continue;
					}
					if (!firstKill)
					{
						json << ",";
					}
					firstKill = false;
					json << "{";
					json << "\"type\": ";
					appendEscapedJsonString(json, (*k)->type);
					json << ",\"race\": ";
					appendEscapedJsonString(json, (*k)->race);
					json << ",\"faction\": \"" << unitFactionKey((*k)->faction) << "\"";
					json << ",\"status\": \"" << unitStatusKey((*k)->status) << "\"";
					json << ",\"turn\": " << (*k)->turn;
					json << ",\"mission_index_at_kill\": " << (*k)->mission;
					if (!(*k)->weapon.empty())
					{
						json << ",\"weapon\": ";
						appendEscapedJsonString(json, (*k)->weapon);
					}
					json << "}";
				}
			}
			json << "]";
			json << "}";
		}
	}
	if (!firstSoldier)
	{
		json << "\n    ";
	}
	json << "],\n";

	json << "    \"lost_in_service\": [";
	bool firstLost = true;
	if (battle != 0)
	{
		for (std::vector<BattleUnit *>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
		{
			const BattleUnitStatistics *st = (*i)->getStatistics();
			Soldier *gs = (*i)->getGeoscapeSoldier();
			if (gs == 0 || st == 0 || (!st->KIA && !st->MIA))
			{
				continue;
			}
			if (!firstLost)
			{
				json << ",";
			}
			firstLost = false;
			json << "\n      {";
			json << "\"display_name\": ";
			appendEscapedJsonString(json, gs->getName());
			json << ",\"rank\": ";
			appendEscapedJsonString(json, soldierRankKey(gs->getRank()));
			json << ",\"career_kills\": " << gs->getKills();
			json << ",\"kia\": " << (st->KIA ? "true" : "false");
			json << ",\"mia\": " << (st->MIA ? "true" : "false");
			json << "}";
		}
	}
	if (!firstLost)
	{
		json << "\n    ";
	}
	json << "],\n";

	json << "    \"wounded\": [";
	bool firstWounded = true;
	if (battle != 0)
	{
		for (std::vector<BattleUnit *>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
		{
			const BattleUnitStatistics *st = (*i)->getStatistics();
			Soldier *gs = (*i)->getGeoscapeSoldier();
			if (gs == 0 || st == 0 || st->KIA || st->MIA)
			{
				continue;
			}
			const int recovery = gs->getWoundRecovery();
			if (recovery <= 0)
			{
				continue;
			}
			if (!firstWounded)
			{
				json << ",";
			}
			firstWounded = false;
			json << "\n      {";
			json << "\"display_name\": ";
			appendEscapedJsonString(json, gs->getName());
			json << ",\"rank\": ";
			appendEscapedJsonString(json, soldierRankKey(gs->getRank()));
			json << ",\"recovery_days\": " << recovery;
			json << "}";
		}
	}
	if (!firstWounded)
	{
		json << "\n    ";
	}
	json << "],\n";

	json << "    \"promotions_applied\": [";
	for (size_t pi = 0; pi < promotions.size(); ++pi)
	{
		if (pi > 0)
		{
			json << ",";
		}
		json << "\n      {";
		json << "\"geoscape_soldier_id\": " << promotions[pi].geoscapeSoldierId << ",";
		json << "\"display_name\": ";
		appendEscapedJsonString(json, promotions[pi].displayName);
		json << ",\"rank_before\": ";
		appendEscapedJsonString(json, promotions[pi].rankBefore);
		json << ",\"rank_after\": ";
		appendEscapedJsonString(json, promotions[pi].rankAfter);
		json << "}";
	}
	if (!promotions.empty())
	{
		json << "\n    ";
	}
	json << "],\n";
	json << "    \"commendations_awarded_this_debrief\": [";
	for (size_t ci = 0; ci < commendations.size(); ++ci)
	{
		if (ci > 0)
		{
			json << ",";
		}
		json << "\n      {";
		json << "\"geoscape_soldier_id\": " << commendations[ci].geoscapeSoldierId << ",";
		json << "\"display_name\": ";
		appendEscapedJsonString(json, commendations[ci].displayName);
		json << ",\"commendation_type\": ";
		appendEscapedJsonString(json, commendations[ci].commendationType);
		json << ",\"noun\": ";
		appendEscapedJsonString(json, commendations[ci].noun);
		json << ",\"decoration_level\": " << commendations[ci].decorationLevel;
		json << "}";
	}
	if (!commendations.empty())
	{
		json << "\n    ";
	}
	json << "],\n";
	json << "    \"promotions_ui_scheduled\": " << (outcome.promotionsUiScheduled ? "true" : "false") << "\n";
	json << "  },\n";

	const YAML::Node &perception = phaseA.tacticalTerminalYaml["perceptionLog"];

	json << "  \"attribution_policy\": {\n";
	json << "    \"kill_credit_source\": \"engine BattleUnitStatistics + debrief stats\",\n";
	json << "    \"perception_kill_credit\": \"combat_casualty_attributed only when fair-play gates pass\",\n";
	json << "    \"neutral_civilian_fatal\": \"omitted from B4 export by policy\"\n";
	json << "  },\n";

	json << "  \"data_quality\": {\n";
	json << "    \"missing_fields\": [\"identity.mission_start_time\"],\n";
	json << "    \"unknown_vs_zero\": \"Use null for unknown; use 0 only when engine affirms zero.\",\n";
	const int dropped =
		perception["droppedCount"].IsDefined() ? perception["droppedCount"].as<int>(0) : 0;
	json << "    \"perception_log_dropped_count\": " << dropped << "\n";
	json << "  }\n";
	json << "}\n";

	result.jsonUtf8 = json.str();
	result.reportId = reportId;
	result.ok = !result.jsonUtf8.empty() && !reportId.empty();
	return result;
}

} // namespace CompanionBattleAarCaptureCore

} // namespace OpenXcom