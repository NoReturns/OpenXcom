/*
 * Open X-Com Companion — strategic SEND export/digest after enqueue guards (M1).
 */
#include "CompanionStrategicSendEnqueueSupport.h"
#include "CompanionBaseIntelligenceExport.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionSha256.h"
#include "../Mod/Mod.h"
#include "../Savegame/SavedGame.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace OpenXcom
{

namespace
{

constexpr const char *CANONICAL_BI_FILENAME = "companion-base-intelligence.json";

bool writeBytesAtomically(const std::string &destPath, const std::string &content)
{
	const std::string tmpPath = destPath + ".companion.tmp";
	{
		std::ofstream out(tmpPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
		if (!out)
		{
			return false;
		}
		out << content;
		out.flush();
		if (!out)
		{
#ifdef _WIN32
			DeleteFileA(tmpPath.c_str());
#else
			std::remove(tmpPath.c_str());
#endif
			return false;
		}
	}
#ifdef _WIN32
	if (!MoveFileExA(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		DeleteFileA(tmpPath.c_str());
		return false;
	}
#else
	if (std::rename(tmpPath.c_str(), destPath.c_str()) != 0)
	{
		std::remove(tmpPath.c_str());
		return false;
	}
#endif
	return true;
}

bool readFileUtf8(const std::string &path, std::string *out, std::string *outError)
{
	if (out == 0)
	{
		if (outError != 0)
		{
			*outError = "read_invalid";
		}
		return false;
	}
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		if (outError != 0)
		{
			*outError = "strategic_export_missing";
		}
		return false;
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	*out = buf.str();
	return true;
}

} // namespace

namespace CompanionStrategicSendEnqueueSupport
{

bool buildStrategicMarkerPrepFromLiveExport(SavedGame *save, Mod *mod,
	CompanionOnDemandRelayCore::StrategicSendMarkerPrep *outPrep, std::string *outError)
{
#ifdef COMPANION_RELAY_TEST_STUB_STRATEGIC_EXPORT
	(void)save;
	(void)mod;
	(void)outPrep;
	if (outError != 0)
	{
		*outError = "strategic_export_stub";
	}
	return false;
#else
	if (save == 0 || mod == 0 || outPrep == 0)
	{
		if (outError != 0)
		{
			*outError = "strategic_inputs_invalid";
		}
		return false;
	}
	if (!CompanionBaseIntelligenceExport::tryExport(save, mod))
	{
		if (outError != 0)
		{
			*outError = "strategic_export_failed";
		}
		return false;
	}
	const char *snapshotPath = std::getenv("OPENXCOM_COMPANION_SNAPSHOT_PATH");
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		if (outError != 0)
		{
			*outError = "snapshot_not_configured";
		}
		return false;
	}
	const std::string snapshotDir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
	const std::string biPath = snapshotDir + "/" + CANONICAL_BI_FILENAME;
	const std::string sendPath = snapshotDir + "/" + STRATEGIC_SEND_BI_FILENAME;
	std::string biUtf8;
	if (!readFileUtf8(biPath, &biUtf8, outError))
	{
		return false;
	}
	if (!writeBytesAtomically(sendPath, biUtf8))
	{
		if (outError != 0)
		{
			*outError = "strategic_send_stage_failed";
		}
		return false;
	}
	std::string digest;
	if (!CompanionSha256::digestHexLowerUtf8(biUtf8, &digest))
	{
		if (outError != 0)
		{
			*outError = "strategic_digest_failed";
		}
		return false;
	}
	outPrep->content_sha256 = digest;
	outPrep->campaign_id = save->getCompanionCampaignId();
	outPrep->exported_at_ms = CompanionOnDemandRelayCore::companionWallClockMs();
	return true;
#endif
}

} // namespace CompanionStrategicSendEnqueueSupport

} // namespace OpenXcom
