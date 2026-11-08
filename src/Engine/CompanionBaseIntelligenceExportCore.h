#pragma once
/*
 * Open X-Com Companion — manual Geoscape base intelligence JSON (M1-BI1).
 * Pure serialization + size management; no filesystem I/O here.
 */
#include <cstddef>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace OpenXcom
{

class Mod;
class SavedGame;

namespace CompanionBaseIntelligenceExportCore
{

static const size_t EXPORT_JSON_MAX_BYTES = 524288; // 512 KiB hard cap (UTF-8)

struct BuildResult
{
	bool ok;
	std::string jsonUtf8;
	bool truncated;
	std::vector<std::string> truncated_sections;
	std::string skip_reason;
};

/// Serializes @p save using UI-parity research/production lists and conservative geoscape filtering.
BuildResult buildJsonUtf8(SavedGame *save, Mod *mod,
	long long exportedAtMs, const std::string &engineVersionLabel);

} // namespace CompanionBaseIntelligenceExportCore

} // namespace OpenXcom
