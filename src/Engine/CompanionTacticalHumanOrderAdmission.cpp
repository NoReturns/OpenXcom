/*
 * Open X-Com Companion — F4A human movement admission (Battlescape + campaign context).
 */
#include "CompanionTacticalAuthorityCore.h"
#include "CompanionBriefingTestSupport.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Soldier.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace OpenXcom
{

namespace CompanionTacticalAuthorityCore
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";

bool readTextFile(const std::string &path, std::string *out)
{
	if (out == 0)
	{
		return false;
	}
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	*out = ss.str();
	return true;
}

} // namespace

bool isLiveCampaignPlayerSoldierUnit(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0 || campaign == 0 || unit->isOut())
	{
		return false;
	}
	if (unit->getOriginalFaction() != FACTION_PLAYER || unit->getFaction() != FACTION_PLAYER)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0 || soldier->getId() != unit->getId())
	{
		return false;
	}
	if (unit->getId() <= 0 || unit->getId() >= BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	return campaign->isLiveRosterSoldier(unit->getId());
}

bool shouldApplyCompanionHumanOrderPolicy(const BattleUnit *unit, const SavedGame *campaign)
{
	return campaign != 0 && isLiveCampaignPlayerSoldierUnit(unit, campaign);
}

bool tryReadRelayControlJsonFromSnapshotEnvironment(std::string *controlJsonOut)
{
	if (controlJsonOut == 0)
	{
		return false;
	}
	controlJsonOut->clear();
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return false;
	}
	const std::string dir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
#ifdef _WIN32
	const std::string controlPath = dir + "\\companion-soldier-control.json";
#else
	const std::string controlPath = dir + "/companion-soldier-control.json";
#endif
	return readTextFile(controlPath, controlJsonOut);
}

bool humanPlayerOrderPermitted(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattleUnit *unit,
	const std::string &controlFileJson)
{
	if (!shouldApplyCompanionHumanOrderPolicy(unit, campaign))
	{
		return true;
	}
	if (battle == 0)
	{
		return true;
	}
	const TacticalAuthorityInput authorityInput = buildTacticalAuthorityInput(
		true,
		battle->getCompanionTacticalAiSoldierIds(),
		true,
		unit->getId(),
		controlFileJson,
		battle->getCompanionEventLog().getSessionId(),
		campaign->getCompanionCampaignId(),
		battle->getCompanionTacticalBattleId());
	return evaluateTacticalAuthority(authorityInput).humanOrderPermitted;
}

} // namespace CompanionTacticalAuthorityCore

} // namespace OpenXcom
