#pragma once
/*
 * Open X-Com Companion — Mind Probe fair-play observation stats (M1 MIND-PROBE-18).
 * Mirrors UnitInfoState::init for mindProbe=true only.
 */
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class BattleUnit;
class SavedGame;
class Mod;

namespace CompanionMindProbeObservationCore
{

void appendAuthorizedMindProbeStats(YAML::Node &parent, BattleUnit *unit, SavedGame *campaign, Mod *mod);

} // namespace CompanionMindProbeObservationCore

} // namespace OpenXcom