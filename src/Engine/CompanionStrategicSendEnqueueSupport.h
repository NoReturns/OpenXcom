#pragma once
/*
 * Open X-Com Companion — strategic SEND export/digest after enqueue guards (M1).
 */
#include "CompanionOnDemandRelayCore.h"
#include <string>

namespace OpenXcom
{

class Mod;
class SavedGame;

namespace CompanionStrategicSendEnqueueSupport
{

/// Relay publication snapshot (F8/manual uses companion-base-intelligence.json).
constexpr const char *STRATEGIC_SEND_BI_FILENAME = "companion-base-intelligence-send.json";

/// After in-flight guards pass: export BI JSON, stage send copy, and fill marker prep (live geoscape/basescape SEND).
bool buildStrategicMarkerPrepFromLiveExport(SavedGame *save, Mod *mod,
	CompanionOnDemandRelayCore::StrategicSendMarkerPrep *outPrep, std::string *outError);

} // namespace CompanionStrategicSendEnqueueSupport

} // namespace OpenXcom
