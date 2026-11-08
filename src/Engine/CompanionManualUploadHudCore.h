#pragma once
/*
 * Open X-Com Companion — read-only manual upload notice HUD (M1 P3 step 4).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionManualUploadHudCore
{

const int MANUAL_NOTICE_TEXT_HEIGHT = 18;
const int MANUAL_NOTICE_MAX_FILENAME_CHARS = 22;

struct SessionBinding
{
	bool known;
	std::string sessionId;
	int turn;
};

struct ManualUploadHudView
{
	bool show;
	std::string producer;
	std::string reasonCode;
	std::string reasonLabel;
	std::string artifactFilename;
	std::string headline;
	std::string detailLine;
};

bool companionSnapshotPathConfigured();
bool resolveManualUploadNoticePath(std::string *outPath);
bool resolveTacticalSendMarkerPath(std::string *outPath);
bool readSessionBinding(SessionBinding *out);

bool parseManualUploadNoticeJson(const std::string &noticeJson, const SessionBinding &binding,
	ManualUploadHudView *out);

void formatReasonLabel(const std::string &reasonCode, std::string *out);

void formatHeadlineForReason(const std::string &reasonCode, std::string *out);

std::string basenameOnly(const std::string &path);

void formatManualUploadHudText(const ManualUploadHudView &view, std::string *out);

bool readManualUploadHudView(ManualUploadHudView *out);

} // namespace CompanionManualUploadHudCore

} // namespace OpenXcom
