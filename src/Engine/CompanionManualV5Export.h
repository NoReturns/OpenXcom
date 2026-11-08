#pragma once
/*
 * Open X-Com Companion — full schema v5 manual artifact when automatic cap forces v4 (M1 P3 step 3).
 */
#include <cstddef>
#include <string>

namespace OpenXcom
{

namespace CompanionManualV5Export
{

const size_t MANUAL_ARTIFACT_MAX_BYTES = 4u * 1024u * 1024u;
const char *REASON_ENGINE_V5_OVERSIZE();
const char *NOTICE_PRODUCER_ENGINE();
const char *MANUAL_UPLOAD_NOTICE_FILENAME();

struct ManualV5ExportInput
{
	std::string snapshotDir;
	std::string sessionId;
	int turn;
	std::string v5YamlUtf8;
	std::string automaticYamlUtf8;
	std::string automaticSnapshotSha256;
	size_t v5AttemptedBytes;
	size_t v4FallbackBytes;
	long long exportedAtMs;
	std::string briefingRequestId;
};

struct ManualV5ExportResult
{
	bool artifactWritten;
	bool noticeWritten;
	std::string artifactPath;
	std::string artifactFilename;
	std::string artifactSha256;
	size_t artifactBytes;
	std::string diagnostic;
};

std::string resolveManualExportDirectory(const std::string &snapshotDir);

std::string formatManualArtifactFilename(const std::string &sessionId, int turn, long long exportedAtMs,
	const std::string &snapshotId12);

std::string buildManualIntelligenceArtifactJson(const ManualV5ExportInput &input, const std::string &snapshotId12);

bool isActiveManualUploadNoticeJson(const std::string &noticeJson);

bool shouldReplaceEngineManualUploadNotice(const std::string &existingNoticeJson,
	const std::string &incomingNoticeJson);

std::string buildEngineManualUploadNoticeJson(const ManualV5ExportInput &input,
	const std::string &artifactPath,
	const std::string &artifactFilename,
	const std::string &artifactSha256,
	const std::string &snapshotId12,
	const std::string &noticeId);

bool writeEngineManualUploadNoticeIfAllowed(const std::string &snapshotDir, const std::string &noticeJson);

/** Deactivate an active engine notice after a successful full v5 automatic export for the same session. */
bool clearEngineManualUploadNoticeForSession(const std::string &snapshotDir, const std::string &sessionId,
	long long clearedAtMs);

/**
 * Write full v5 manual JSON artifact and notice when automatic export fell back to schema v4.
 * Never throws; returns artifactWritten=false on failure (automatic export may still have succeeded).
 */
ManualV5ExportResult tryWriteManualV5OversizeExport(const ManualV5ExportInput &input);

} // namespace CompanionManualV5Export

} // namespace OpenXcom
