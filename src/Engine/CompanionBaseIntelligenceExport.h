#pragma once
/*
 * Open X-Com Companion — manual Geoscape base intelligence export (M1-BI1).
 */
#include <string>

namespace OpenXcom
{

class Mod;
class SavedGame;

namespace CompanionBaseIntelligenceExport
{

/// Writes companion-base-intelligence.json beside OPENXCOM_COMPANION_SNAPSHOT_PATH (or optional override env).
bool tryExport(SavedGame *save, Mod *mod);

} // namespace CompanionBaseIntelligenceExport

} // namespace OpenXcom
