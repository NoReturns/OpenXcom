/*
 * Open X-Com Companion — item research eligibility (uses SavedGame::isResearched on RuleItem::getRequirements).
 */
#include "CompanionItemResearchEligibilityCore.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/SavedGame.h"

namespace OpenXcom
{

bool companionItemResearchUsable(SavedGame *campaign, RuleItem *rules)
{
	if (rules == 0 || campaign == 0)
	{
		return false;
	}
	return campaign->isResearched(rules->getRequirements());
}

} // namespace OpenXcom
