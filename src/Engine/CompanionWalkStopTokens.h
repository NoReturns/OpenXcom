#pragma once
/*
 * Open X-Com Companion — BattleAction.result tokens for MOVE stop diagnostics (M1).
 * Stable wire values; mapped to movement_stop_reason in CompanionCommandCore.
 */
namespace OpenXcom
{

namespace CompanionWalkStopTokens
{

const char HOSTILE_SPOTTED[] = "COMPANION_HOSTILE_SPOTTED";
const char REACTION_FIRE[] = "COMPANION_REACTION_FIRE";
const char PATH_BLOCKED[] = "COMPANION_PATH_BLOCKED";
const char PROXIMITY_GRENADE[] = "COMPANION_PROXIMITY_GRENADE";

} // namespace CompanionWalkStopTokens

} // namespace OpenXcom
