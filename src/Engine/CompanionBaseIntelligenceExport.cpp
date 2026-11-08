/*
 * Open X-Com Companion — manual Geoscape base intelligence export (M1-BI1).
 */
#include "CompanionBaseIntelligenceExport.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionBaseIntelligenceExportCore.h"
#include "CompanionSnapshotExportCore.h"
#include "Logger.h"
#include "../Mod/Mod.h"
#include "../Savegame/SavedGame.h"
#include "../version.h"

#include <chrono>
#include <cstdlib>
#include <sstream>

namespace OpenXcom
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";
const char *EXPORT_DIR_ENV = "OPENXCOM_COMPANION_BASE_INTELLIGENCE_EXPORT_DIR";

std::string engineVersionLabel()
{
	std::ostringstream o;
	o << OPENXCOM_VERSION_SHORT << OPENXCOM_VERSION_GIT;
#ifdef GIT_BUILD
	if (OPENXCOM_VERSION_ENGINE[0] != '\0')
	{
		o << " engine:" << OPENXCOM_VERSION_ENGINE;
	}
#endif
	return o.str();
}

bool resolveOutputDirectory(std::string *outDir, std::string *reason)
{
	if (outDir == 0)
	{
		return false;
	}
	const char *dirOverride = std::getenv(EXPORT_DIR_ENV);
	if (dirOverride != 0 && dirOverride[0] != '\0')
	{
		*outDir = std::string(dirOverride);
		return true;
	}
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		if (reason != 0)
		{
			*reason = std::string(SNAPSHOT_ENV) + "_not_set";
		}
		return false;
	}
	const std::string parent = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
	if (parent.empty())
	{
		if (reason != 0)
		{
			*reason = "snapshot_parent_invalid";
		}
		return false;
	}
	*outDir = parent;
	return true;
}

} // namespace

namespace CompanionBaseIntelligenceExport
{

bool tryExport(SavedGame *save, Mod *mod)
{
	if (save == 0 || mod == 0)
	{
		return false;
	}
	if (save->getDebugMode())
	{
		Log(LOG_INFO) << "Companion base intelligence: skipped (debug mode)";
		return false;
	}

	std::string outDir;
	std::string reason;
	if (!resolveOutputDirectory(&outDir, &reason))
	{
		Log(LOG_INFO) << "Companion base intelligence: skipped (" << reason << ")";
		return false;
	}

	const long long exportedAtMs = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());

	const CompanionBaseIntelligenceExportCore::BuildResult built =
		CompanionBaseIntelligenceExportCore::buildJsonUtf8(save, mod, exportedAtMs, engineVersionLabel());

	if (!built.ok)
	{
		Log(LOG_WARNING) << "Companion base intelligence: build failed (" << built.skip_reason << ")";
		return false;
	}

	const std::string path = outDir + "/companion-base-intelligence.json";
	if (!CompanionSnapshotExportCore::writeUtf8FileAtomically(path, built.jsonUtf8))
	{
		Log(LOG_WARNING) << "Companion base intelligence: atomic write failed: " << path;
		return false;
	}

	Log(LOG_INFO) << "Companion base intelligence: wrote " << path << " (" << built.jsonUtf8.size() << " bytes"
		<< (built.truncated ? ", truncated" : "") << ")";
	return true;
}

} // namespace CompanionBaseIntelligenceExport

} // namespace OpenXcom
