/*
 * Open X-Com Companion — player-visible mission context for tactical snapshots (M1-WP1).
 */
#include "CompanionSnapshotMissionContext.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleTerrain.h"
#include "../Mod/MapDataSet.h"
#include "../Savegame/Node.h"
#include <algorithm>
#include <cctype>

namespace OpenXcom
{

namespace CompanionSnapshotMissionContext
{

namespace
{

std::string toLowerAscii(std::string value)
{
	for (size_t i = 0; i < value.size(); ++i)
	{
		value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
	}
	return value;
}

std::string deriveMissionClass(const std::string &missionType)
{
	const std::string lower = toLowerAscii(missionType);
	if (lower.find("base_defense") != std::string::npos)
	{
		return "base_defense";
	}
	if (lower.find("terror") != std::string::npos)
	{
		return "terror";
	}
	if (lower.find("cydonia") != std::string::npos)
	{
		return "cydonia";
	}
	if (lower.find("alien_base") != std::string::npos || lower.find("alienbase") != std::string::npos)
	{
		return "alien_base";
	}
	if (lower.find("ufo_crash") != std::string::npos)
	{
		return "ufo_crash";
	}
	if (lower.find("ufo_landed") != std::string::npos || lower.find("ufo_ground") != std::string::npos)
	{
		return "ufo_ground";
	}
	if (lower.find("ufo") != std::string::npos)
	{
		return "ufo_mission";
	}
	return "other";
}

} // namespace

void appendMissionContext(YAML::Node &root, SavedBattleGame *battle, Mod *mod)
{
	(void)mod;
	if (battle == 0)
	{
		return;
	}

	YAML::Node ctx;
	ctx["schemaVersion"] = 1;
	ctx["missionType"] = battle->getMissionType();
	const std::string missionClass = deriveMissionClass(battle->getMissionType());
	ctx["missionClass"] = missionClass;

	// Chrono turn limit only when the battle exposes a positive limit (player-visible countdown).
	const int turnLimit = battle->getTurnLimit();
	if (turnLimit > 0)
	{
		ctx["turnLimit"] = turnLimit;
	}

	if (missionClass == "base_defense")
	{
		ctx["siteKind"] = "xcom_base";
	}
	else if (missionClass == "ufo_crash" || missionClass == "ufo_ground" || missionClass == "ufo_mission")
	{
		ctx["siteKind"] = "ufo_site";
	}

	root["missionContext"] = ctx;
}

} // namespace CompanionSnapshotMissionContext

} // namespace OpenXcom
