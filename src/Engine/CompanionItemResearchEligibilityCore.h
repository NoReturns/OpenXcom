#pragma once
/*
 * Open X-Com Companion — item research eligibility for snapshot observation (aligns with PICKUP/DROP runtime).
 */
namespace OpenXcom
{

class SavedGame;
class RuleItem;

bool companionItemResearchUsable(SavedGame *campaign, RuleItem *rules);

} // namespace OpenXcom
