/*
 * Open X-Com Companion — bounded cleanup for manual-exports/ artifacts (M1 P3 step 5).
 */
#include "CompanionManualExportRetention.h"
#include "CompanionJsonLite.h"
#include "CompanionManualV5Export.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace OpenXcom
{

namespace CompanionManualExportRetention
{

namespace
{

std::string pathJoin(const std::string &dir, const std::string &leaf)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	if (dir.empty())
	{
		return leaf;
	}
	if (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')
	{
		return dir + leaf;
	}
	return dir + sep + leaf;
}

#ifdef _WIN32
std::string normalizePathKey(const std::string &path)
{
	std::string key = path;
	for (std::string::size_type i = 0; i < key.size(); ++i)
	{
		if (key[i] == '/')
		{
			key[i] = '\\';
		}
		else
		{
			key[i] = static_cast<char>(tolower(static_cast<unsigned char>(key[i])));
		}
	}
	return key;
}
#else
std::string normalizePathKey(const std::string &path)
{
	return path;
}
#endif

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

bool listEligibleExportFiles(const std::string &exportDir, std::vector<std::string> *outFilenames)
{
	if (outFilenames == 0)
	{
		return false;
	}
	outFilenames->clear();
#ifdef _WIN32
	const std::string pattern = pathJoin(exportDir, "*");
	WIN32_FIND_DATAA data;
	HANDLE handle = FindFirstFileA(pattern.c_str(), &data);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	do
	{
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			continue;
		}
		const std::string name = data.cFileName;
		if (isEligibleManualExportFilename(name))
		{
			outFilenames->push_back(name);
		}
	} while (FindNextFileA(handle, &data));
	FindClose(handle);
	return true;
#else
	DIR *dir = opendir(exportDir.c_str());
	if (dir == 0)
	{
		return false;
	}
	struct dirent *entry = 0;
	while ((entry = readdir(dir)) != 0)
	{
		if (entry->d_name[0] == '.')
		{
			continue;
		}
		const std::string name = entry->d_name;
		if (isEligibleManualExportFilename(name))
		{
			outFilenames->push_back(name);
		}
	}
	closedir(dir);
	return true;
#endif
}

struct RankedFile
{
	std::string filename;
	std::string fullPath;
	long long exportedAtMs;
	bool protectedFile;
};

} // namespace

bool isEligibleManualExportFilename(const std::string &filename)
{
	if (filename.size() < 8 || filename.find(".json") == std::string::npos)
	{
		return false;
	}
	const bool engine = filename.compare(0, 10, "engine-v5_") == 0;
	const bool relay = filename.compare(0, 13, "relay-manual_") == 0;
	return engine || relay;
}

bool parseExportedAtMsFromManualFilename(const std::string &filename, long long *outMs)
{
	if (outMs == 0)
	{
		return false;
	}
	*outMs = 0;
	if (!isEligibleManualExportFilename(filename))
	{
		return false;
	}
	std::vector<std::string> parts;
	std::string part;
	for (std::string::size_type i = 0; i <= filename.size(); ++i)
	{
		if (i == filename.size() || filename[i] == '_')
		{
			if (!part.empty())
			{
				parts.push_back(part);
				part.clear();
			}
			continue;
		}
		part.push_back(filename[i]);
	}
	if (parts.size() < 4)
	{
		return false;
	}
	const std::string &turnToken = parts[parts.size() - 3];
	const std::string &msToken = parts[parts.size() - 2];
	if (turnToken.size() < 2 || turnToken[0] != 't')
	{
		return false;
	}
	char *end = 0;
	const long long ms = std::strtoll(msToken.c_str(), &end, 10);
	if (end == msToken.c_str() || ms <= 0)
	{
		return false;
	}
	*outMs = ms;
	return true;
}

void collectProtectedPathsFromActiveNotice(const std::string &snapshotDir, std::vector<std::string> *out)
{
	if (out == 0 || snapshotDir.empty())
	{
		return;
	}
	const std::string noticePath = pathJoin(snapshotDir, CompanionManualV5Export::MANUAL_UPLOAD_NOTICE_FILENAME());
	std::string noticeJson;
	if (!readTextFile(noticePath, &noticeJson))
	{
		return;
	}
	if (!CompanionManualV5Export::isActiveManualUploadNoticeJson(noticeJson))
	{
		return;
	}
	std::string artifactPath;
	if (CompanionJsonLite::extractJsonStringField(noticeJson, "artifact_path", &artifactPath) && !artifactPath.empty())
	{
		out->push_back(artifactPath);
	}
}

ManualExportRetentionResult pruneManualExportDirectory(const std::string &snapshotDir,
	const std::string &exportDir,
	const std::vector<std::string> &extraProtected)
{
	ManualExportRetentionResult result = {};
	result.ok = true;
	if (exportDir.empty())
	{
		return result;
	}
	std::vector<std::string> filenames;
	if (!listEligibleExportFiles(exportDir, &filenames))
	{
		return result;
	}
	result.candidates = static_cast<int>(filenames.size());

	std::vector<std::string> protectedPaths = extraProtected;
	collectProtectedPathsFromActiveNotice(snapshotDir, &protectedPaths);
	std::vector<std::string> protectedKeys;
	for (std::vector<std::string>::const_iterator it = protectedPaths.begin(); it != protectedPaths.end(); ++it)
	{
		if (!it->empty())
		{
			protectedKeys.push_back(normalizePathKey(*it));
		}
	}

	std::vector<RankedFile> ranked;
	for (std::vector<std::string>::const_iterator it = filenames.begin(); it != filenames.end(); ++it)
	{
		RankedFile entry;
		entry.filename = *it;
		entry.fullPath = pathJoin(exportDir, *it);
		entry.exportedAtMs = 0;
		parseExportedAtMsFromManualFilename(*it, &entry.exportedAtMs);
		const std::string key = normalizePathKey(entry.fullPath);
		entry.protectedFile = std::find(protectedKeys.begin(), protectedKeys.end(), key) != protectedKeys.end();
		ranked.push_back(entry);
	}

	std::sort(ranked.begin(), ranked.end(), [](const RankedFile &a, const RankedFile &b) {
		if (a.exportedAtMs != b.exportedAtMs)
		{
			return a.exportedAtMs > b.exportedAtMs;
		}
		return a.filename > b.filename;
	});

	int additionalKept = 0;
	for (std::vector<RankedFile>::const_iterator it = ranked.begin(); it != ranked.end(); ++it)
	{
		if (it->protectedFile)
		{
			++result.retained;
			continue;
		}
		if (additionalKept < MANUAL_EXPORT_KEEP_ADDITIONAL_RECENT)
		{
			++additionalKept;
			++result.retained;
			continue;
		}
		if (std::remove(it->fullPath.c_str()) == 0)
		{
			++result.deleted;
		}
	}
	return result;
}

} // namespace CompanionManualExportRetention

} // namespace OpenXcom
