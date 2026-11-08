#pragma once
/*
 * Open X-Com Companion — bounded cleanup for manual-exports/ artifacts (M1 P3 step 5).
 */
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionManualExportRetention
{

/** Keep the protected set plus this many additional newest eligible artifacts. */
const int MANUAL_EXPORT_KEEP_ADDITIONAL_RECENT = 3;

struct ManualExportRetentionResult
{
	bool ok;
	int candidates;
	int deleted;
	int retained;
};

bool isEligibleManualExportFilename(const std::string &filename);

bool parseExportedAtMsFromManualFilename(const std::string &filename, long long *outMs);

void collectProtectedPathsFromActiveNotice(const std::string &snapshotDir, std::vector<std::string> *out);

/**
 * Remove stale manual export JSON files under exportDir.
 * Never deletes paths in extraProtected or an active notice artifact_path.
 * Missing exportDir is not an error (ok=true, deleted=0).
 */
ManualExportRetentionResult pruneManualExportDirectory(const std::string &snapshotDir,
	const std::string &exportDir,
	const std::vector<std::string> &extraProtected = std::vector<std::string>());

} // namespace CompanionManualExportRetention

} // namespace OpenXcom
