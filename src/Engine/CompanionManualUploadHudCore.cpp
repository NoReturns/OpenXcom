/*
 * Open X-Com Companion — read-only manual upload notice HUD (M1 P3 step 4).
 */
#include "CompanionManualUploadHudCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionJsonLite.h"
#include "CompanionManualV5Export.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace OpenXcom
{

namespace CompanionManualUploadHudCore
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";
const char *TACTICAL_SEND_MARKER_FILENAME = "companion-tactical-send-marker.json";
const char *REASON_RELAY_SHRINK = "RELAY_SHRINK_EXHAUSTED";

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

bool isAllowedProducer(const std::string &producer)
{
	return producer == "engine" || producer == "node";
}

} // namespace

bool companionSnapshotPathConfigured()
{
	const char *path = std::getenv(SNAPSHOT_ENV);
	return path != 0 && path[0] != '\0';
}

bool resolveManualUploadNoticePath(std::string *outPath)
{
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0' || outPath == 0)
	{
		return false;
	}
	const std::string dir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(std::string(snapshotPath));
	if (dir.empty())
	{
		return false;
	}
#ifdef _WIN32
	*outPath = dir + "\\" + CompanionManualV5Export::MANUAL_UPLOAD_NOTICE_FILENAME();
#else
	*outPath = dir + "/" + CompanionManualV5Export::MANUAL_UPLOAD_NOTICE_FILENAME();
#endif
	return true;
}

bool resolveTacticalSendMarkerPath(std::string *outPath)
{
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0' || outPath == 0)
	{
		return false;
	}
	const std::string dir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(std::string(snapshotPath));
	if (dir.empty())
	{
		return false;
	}
#ifdef _WIN32
	*outPath = dir + "\\" + TACTICAL_SEND_MARKER_FILENAME;
#else
	*outPath = dir + "/" + TACTICAL_SEND_MARKER_FILENAME;
#endif
	return true;
}

bool readSessionBinding(SessionBinding *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = SessionBinding();
	std::string markerPath;
	if (!resolveTacticalSendMarkerPath(&markerPath))
	{
		return false;
	}
	std::string markerJson;
	if (!readTextFile(markerPath, &markerJson))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(markerJson, "session_id", &out->sessionId)
	    || out->sessionId.empty())
	{
		return false;
	}
	int turn = 0;
	if (!CompanionJsonLite::extractJsonIntField(markerJson, "turn", &turn))
	{
		return false;
	}
	out->turn = turn;
	out->known = true;
	return true;
}

void formatReasonLabel(const std::string &reasonCode, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	if (reasonCode == CompanionManualV5Export::REASON_ENGINE_V5_OVERSIZE())
	{
		*out = "Optional upload (v4 auto OK)";
		return;
	}
	if (reasonCode == REASON_RELAY_SHRINK)
	{
		*out = "Relay delivery unavailable";
		return;
	}
	*out = "Manual upload";
}

void formatHeadlineForReason(const std::string &reasonCode, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	if (reasonCode == CompanionManualV5Export::REASON_ENGINE_V5_OVERSIZE())
	{
		*out = "FULL TERRAIN EXPORT AVAILABLE";
		return;
	}
	if (reasonCode == REASON_RELAY_SHRINK)
	{
		*out = "MANUAL UPLOAD REQUIRED";
		return;
	}
	*out = "MANUAL UPLOAD REQUIRED";
}

std::string basenameOnly(const std::string &path)
{
	const std::string::size_type slash = path.find_last_of("/\\");
	if (slash == std::string::npos)
	{
		return path;
	}
	return path.substr(slash + 1);
}

bool parseManualUploadNoticeJson(const std::string &noticeJson, const SessionBinding &binding,
	ManualUploadHudView *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = ManualUploadHudView();
	if (noticeJson.empty() || !CompanionManualV5Export::isActiveManualUploadNoticeJson(noticeJson))
	{
		return false;
	}
	std::string producer;
	std::string sessionId;
	std::string reasonCode;
	std::string artifactPath;
	std::string artifactFilename;
	if (!CompanionJsonLite::extractJsonStringField(noticeJson, "producer", &producer)
	    || !isAllowedProducer(producer))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(noticeJson, "session_id", &sessionId) || sessionId.empty())
	{
		return false;
	}
	if (binding.known)
	{
		if (sessionId != binding.sessionId)
		{
			return false;
		}
		int noticeTurn = 0;
		if (CompanionJsonLite::extractJsonIntField(noticeJson, "turn", &noticeTurn)
		    && noticeTurn > 0
		    && binding.turn > 0
		    && noticeTurn < binding.turn)
		{
			return false;
		}
	}
	CompanionJsonLite::extractJsonStringField(noticeJson, "reason_code", &reasonCode);
	CompanionJsonLite::extractJsonStringField(noticeJson, "artifact_path", &artifactPath);
	CompanionJsonLite::extractJsonStringField(noticeJson, "artifact_filename", &artifactFilename);
	if (artifactFilename.empty())
	{
		artifactFilename = basenameOnly(artifactPath);
	}
	if (artifactFilename.empty())
	{
		return false;
	}
	out->show = true;
	out->producer = producer;
	out->reasonCode = reasonCode;
	formatReasonLabel(reasonCode, &out->reasonLabel);
	out->artifactFilename = artifactFilename;
	if (out->artifactFilename.size() > static_cast<size_t>(MANUAL_NOTICE_MAX_FILENAME_CHARS))
	{
		out->artifactFilename = out->artifactFilename.substr(0, static_cast<size_t>(MANUAL_NOTICE_MAX_FILENAME_CHARS - 3))
			+ "...";
	}
	formatHeadlineForReason(reasonCode, &out->headline);
	out->detailLine = out->reasonLabel + " | " + out->artifactFilename;
	return true;
}

void formatManualUploadHudText(const ManualUploadHudView &view, std::string *out)
{
	if (out == 0 || !view.show)
	{
		if (out != 0)
		{
			out->clear();
		}
		return;
	}
	*out = view.headline + "\n" + view.detailLine;
}

bool readManualUploadHudView(ManualUploadHudView *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = ManualUploadHudView();
	if (!companionSnapshotPathConfigured())
	{
		return false;
	}
	std::string noticePath;
	if (!resolveManualUploadNoticePath(&noticePath))
	{
		return false;
	}
	std::string noticeJson;
	if (!readTextFile(noticePath, &noticeJson))
	{
		return false;
	}
	SessionBinding binding;
	readSessionBinding(&binding);
	if (!binding.known)
	{
		return false;
	}
	return parseManualUploadNoticeJson(noticeJson, binding, out);
}

} // namespace CompanionManualUploadHudCore

} // namespace OpenXcom
