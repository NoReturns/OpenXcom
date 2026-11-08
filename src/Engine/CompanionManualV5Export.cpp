/*
 * Open X-Com Companion — full schema v5 manual artifact when automatic cap forces v4 (M1 P3 step 3).
 */
#include "CompanionManualV5Export.h"
#include "CompanionManualExportRetention.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionJsonLite.h"
#include "CompanionSha256.h"
#include "CompanionSnapshotExportCore.h"
#include "CompanionSnapshotTerrain.h"
#include <chrono>
#include <iostream>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace OpenXcom
{

namespace CompanionManualV5Export
{

namespace
{

const char *MANUAL_ARTIFACT_KIND = "companion_manual_intelligence";

std::string normalizeArtifactPathForJson(const std::string &path)
{
	std::string normalized = path;
	for (std::string::size_type i = 0; i < normalized.size(); ++i)
	{
		if (normalized[i] == '\\')
		{
			normalized[i] = '/';
		}
	}
	return normalized;
}

std::string jsonEscapeString(const std::string &value)
{
	std::string out;
	out.reserve(value.size() + 16);
	for (std::string::size_type i = 0; i < value.size(); ++i)
	{
		const char c = value[i];
		if (c == '\\' || c == '"')
		{
			out.push_back('\\');
			out.push_back(c);
		}
		else if (c == '\n')
		{
			out += "\\n";
		}
		else if (c == '\r')
		{
			out += "\\r";
		}
		else if (static_cast<unsigned char>(c) < 0x20)
		{
			continue;
		}
		else
		{
			out.push_back(c);
		}
	}
	return out;
}

std::string pathJoin(const std::string &dir, const char *leaf)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	if (dir.empty())
	{
		return std::string(leaf);
	}
	if (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')
	{
		return dir + leaf;
	}
	return dir + sep + leaf;
}

bool ensureDirectoryExists(const std::string &dir)
{
	if (dir.empty())
	{
		return false;
	}
#ifdef _WIN32
	if (_mkdir(dir.c_str()) == 0)
	{
		return true;
	}
	const DWORD attrs = GetFileAttributesA(dir.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	return mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::string isoUtcFromMs(long long ms)
{
	const std::chrono::system_clock::time_point tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
	const std::time_t tt = std::chrono::system_clock::to_time_t(tp);
	std::tm tmUtc = {};
#ifdef _WIN32
	gmtime_s(&tmUtc, &tt);
#else
	gmtime_r(&tt, &tmUtc);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmUtc);
	std::ostringstream iso;
	iso << buf << "Z";
	return iso.str();
}

std::string snapshotId12FromYaml(const std::string &v5YamlUtf8)
{
	std::string digest;
	if (!CompanionSha256::digestHexLowerUtf8(v5YamlUtf8, &digest) || digest.size() < 12)
	{
		return "unknownsnap";
	}
	return digest.substr(0, 12);
}

bool artifactFileExists(const std::string &artifactPath)
{
	if (artifactPath.empty())
	{
		return false;
	}
#ifdef _WIN32
	const DWORD attrs = GetFileAttributesA(artifactPath.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
	std::ifstream probe(artifactPath.c_str(), std::ios::in | std::ios::binary);
	return probe.good();
#endif
}

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

bool noticeBoolField(const std::string &json, const std::string &key, bool defaultValue)
{
	int parsed = defaultValue ? 1 : 0;
	if (CompanionJsonLite::extractJsonIntField(json, key, &parsed))
	{
		return parsed != 0;
	}
	if (json.find("\"" + key + "\": true") != std::string::npos
	    || json.find("\"" + key + "\":true") != std::string::npos)
	{
		return true;
	}
	if (json.find("\"" + key + "\": false") != std::string::npos
	    || json.find("\"" + key + "\":false") != std::string::npos)
	{
		return false;
	}
	return defaultValue;
}

bool engineNoticeAlreadyCoversExport(const std::string &snapshotDir, const ManualV5ExportInput &input,
	const std::string &snapshotId12)
{
	if (snapshotDir.empty() || snapshotId12.empty())
	{
		return false;
	}
	const std::string noticePath = pathJoin(snapshotDir, MANUAL_UPLOAD_NOTICE_FILENAME());
	std::string existing;
	if (!readTextFile(noticePath, &existing))
	{
		return false;
	}
	if (!isActiveManualUploadNoticeJson(existing))
	{
		return false;
	}
	std::string producer;
	std::string sessionId;
	std::string existingSnapshotId;
	int existingTurn = 0;
	if (!CompanionJsonLite::extractJsonStringField(existing, "producer", &producer)
	    || producer != NOTICE_PRODUCER_ENGINE())
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(existing, "session_id", &sessionId) || sessionId != input.sessionId)
	{
		return false;
	}
	if (CompanionJsonLite::extractJsonIntField(existing, "turn", &existingTurn)
	    && existingTurn != input.turn)
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(existing, "snapshot_id", &existingSnapshotId)
	    || existingSnapshotId != snapshotId12)
	{
		return false;
	}
	std::string artifactPath;
	if (!CompanionJsonLite::extractJsonStringField(existing, "artifact_path", &artifactPath)
	    || artifactPath.empty()
	    || !artifactFileExists(artifactPath))
	{
		return false;
	}
	std::ifstream artifactIn(artifactPath.c_str(), std::ios::in | std::ios::binary);
	if (!artifactIn)
	{
		return false;
	}
	return true;
}

} // namespace

const char *REASON_ENGINE_V5_OVERSIZE()
{
	return "ENGINE_V5_OVERSIZE";
}

const char *NOTICE_PRODUCER_ENGINE()
{
	return "engine";
}

const char *MANUAL_UPLOAD_NOTICE_FILENAME()
{
	return "companion-manual-upload-notice.json";
}

std::string resolveManualExportDirectory(const std::string &snapshotDir)
{
	const char *configured = std::getenv("OPENXCOM_COMPANION_MANUAL_EXPORT_DIR");
	if (configured != 0 && configured[0] != '\0')
	{
		return std::string(configured);
	}
	return pathJoin(snapshotDir, "manual-exports");
}

std::string formatManualArtifactFilename(const std::string &sessionId, int turn, long long exportedAtMs,
	const std::string &snapshotId12)
{
	std::string session8 = sessionId.size() >= 8 ? sessionId.substr(0, 8) : "unknowns";
	std::ostringstream name;
	name << "engine-v5_" << session8 << "_t" << turn << "_" << exportedAtMs << "_" << snapshotId12 << ".json";
	return name.str();
}

std::string buildManualIntelligenceArtifactJson(const ManualV5ExportInput &input, const std::string &snapshotId12)
{
	const std::string createdAt = isoUtcFromMs(input.exportedAtMs);
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"kind\": \"" << MANUAL_ARTIFACT_KIND << "\",\n"
	     << "  \"created_at\": \"" << createdAt << "\",\n"
	     << "  \"read_only\": true,\n"
	     << "  \"producer\": \"" << NOTICE_PRODUCER_ENGINE() << "\",\n"
	     << "  \"reason_code\": \"" << REASON_ENGINE_V5_OVERSIZE() << "\",\n"
	     << "  \"command_authority\": {\n"
	     << "    \"schema_version\": 1,\n"
	     << "    \"authoritative\": false,\n"
	     << "    \"note\": \"Manual upload is read-only battlefield intelligence. It does not authorize tactical commands.\"\n"
	     << "  },\n"
	     << "  \"session_id\": \"" << jsonEscapeString(input.sessionId) << "\",\n"
	     << "  \"turn\": " << input.turn << ",\n"
	     << "  \"snapshot_id\": \"" << jsonEscapeString(snapshotId12) << "\",\n"
	     << "  \"source_snapshot_sha256\": \"" << jsonEscapeString(input.automaticSnapshotSha256) << "\",\n";
	if (!input.briefingRequestId.empty())
	{
		json << "  \"request_id\": \"" << jsonEscapeString(input.briefingRequestId) << "\",\n";
	}
	json << "  \"automatic_transport\": {\n"
	     << "    \"schema_version\": 1,\n"
	     << "    \"max_bytes\": " << CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES << ",\n"
	     << "    \"outcome\": \"" << REASON_ENGINE_V5_OVERSIZE() << "\",\n"
	     << "    \"attempted_v5_bytes\": " << input.v5AttemptedBytes << ",\n"
	     << "    \"fallback_v4_bytes\": " << input.v4FallbackBytes << ",\n"
	     << "    \"automatic_schema_version\": 4,\n"
	     << "    \"terrain_omitted_due_to_size\": true\n"
	     << "  },\n"
	     << "  \"tactical_snapshot\": {\n"
	     << "    \"schema_version\": 5,\n"
	     << "    \"encoding\": \"yaml-utf8\",\n"
	     << "    \"content\": \"" << jsonEscapeString(input.v5YamlUtf8) << "\"\n"
	     << "  }\n"
	     << "}\n";
	return json.str();
}

bool isActiveManualUploadNoticeJson(const std::string &noticeJson)
{
	if (noticeJson.empty())
	{
		return false;
	}
	std::string artifactPath;
	if (!CompanionJsonLite::extractJsonStringField(noticeJson, "artifact_path", &artifactPath) || artifactPath.empty())
	{
		return false;
	}
	return noticeBoolField(noticeJson, "active", true);
}

bool shouldReplaceEngineManualUploadNotice(const std::string &existingNoticeJson,
	const std::string &incomingNoticeJson)
{
	std::string incomingProducer;
	if (!CompanionJsonLite::extractJsonStringField(incomingNoticeJson, "producer", &incomingProducer)
	    || incomingProducer != NOTICE_PRODUCER_ENGINE())
	{
		return false;
	}
	if (!isActiveManualUploadNoticeJson(existingNoticeJson))
	{
		return true;
	}
	std::string existingProducer;
	CompanionJsonLite::extractJsonStringField(existingNoticeJson, "producer", &existingProducer);
	if (existingProducer == "node")
	{
		return false;
	}
	if (existingProducer != NOTICE_PRODUCER_ENGINE())
	{
		return false;
	}
	std::string incomingSession;
	std::string existingSession;
	CompanionJsonLite::extractJsonStringField(incomingNoticeJson, "session_id", &incomingSession);
	CompanionJsonLite::extractJsonStringField(existingNoticeJson, "session_id", &existingSession);
	if (!incomingSession.empty() && !existingSession.empty() && incomingSession != existingSession)
	{
		return true;
	}
	long long incomingMs = 0;
	long long existingMs = 0;
	CompanionJsonLite::extractJsonInt64Field(incomingNoticeJson, "created_at_ms", &incomingMs);
	CompanionJsonLite::extractJsonInt64Field(existingNoticeJson, "created_at_ms", &existingMs);
	if (incomingMs > 0 && existingMs > 0 && incomingMs < existingMs)
	{
		return false;
	}
	int incomingTurn = 0;
	int existingTurn = 0;
	CompanionJsonLite::extractJsonIntField(incomingNoticeJson, "turn", &incomingTurn);
	CompanionJsonLite::extractJsonIntField(existingNoticeJson, "turn", &existingTurn);
	if (incomingTurn > 0 && existingTurn > 0 && incomingTurn < existingTurn)
	{
		return false;
	}
	return true;
}

std::string buildEngineManualUploadNoticeJson(const ManualV5ExportInput &input,
	const std::string &artifactPath,
	const std::string &artifactFilename,
	const std::string &artifactSha256,
	const std::string &snapshotId12,
	const std::string &noticeId)
{
	const std::string createdAt = isoUtcFromMs(input.exportedAtMs);
	const std::string artifactPathJson = normalizeArtifactPathForJson(artifactPath);
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"active\": true,\n"
	     << "  \"notice_id\": \"" << jsonEscapeString(noticeId) << "\",\n"
	     << "  \"producer\": \"" << NOTICE_PRODUCER_ENGINE() << "\",\n"
	     << "  \"reason_code\": \"" << REASON_ENGINE_V5_OVERSIZE() << "\",\n"
	     << "  \"artifact_path\": \"" << jsonEscapeString(artifactPathJson) << "\",\n"
	     << "  \"artifact_sha256\": \"" << artifactSha256 << "\",\n"
	     << "  \"artifact_filename\": \"" << jsonEscapeString(artifactFilename) << "\",\n"
	     << "  \"session_id\": \"" << jsonEscapeString(input.sessionId) << "\",\n"
	     << "  \"snapshot_id\": \"" << jsonEscapeString(snapshotId12) << "\",\n"
	     << "  \"turn\": " << input.turn << ",\n";
	if (!input.briefingRequestId.empty())
	{
		json << "  \"request_id\": \"" << jsonEscapeString(input.briefingRequestId) << "\",\n";
	}
	json << "  \"created_at_ms\": " << input.exportedAtMs << ",\n"
	     << "  \"created_at\": \"" << createdAt << "\",\n"
	     << "  \"title\": \"COMPANION SNAPSHOT TOO LARGE\",\n"
	     << "  \"message\": \"Complete battlefield intelligence has been saved locally.\\nPlease upload this file to your ChatGPT partner.\"\n"
	     << "}\n";
	return json.str();
}

bool clearEngineManualUploadNoticeForSession(const std::string &snapshotDir, const std::string &sessionId,
	long long clearedAtMs)
{
	if (snapshotDir.empty() || sessionId.empty())
	{
		return false;
	}
	const std::string noticePath = pathJoin(snapshotDir, MANUAL_UPLOAD_NOTICE_FILENAME());
	std::string existing;
	if (!readTextFile(noticePath, &existing))
	{
		return false;
	}
	std::string producer;
	std::string existingSession;
	CompanionJsonLite::extractJsonStringField(existing, "producer", &producer);
	CompanionJsonLite::extractJsonStringField(existing, "session_id", &existingSession);
	if (producer != NOTICE_PRODUCER_ENGINE() || !isActiveManualUploadNoticeJson(existing))
	{
		return false;
	}
	if (!existingSession.empty() && existingSession != sessionId)
	{
		return false;
	}
	std::string artifactPath;
	std::string artifactSha;
	CompanionJsonLite::extractJsonStringField(existing, "artifact_path", &artifactPath);
	CompanionJsonLite::extractJsonStringField(existing, "artifact_sha256", &artifactSha);
	const std::string artifactPathJson = normalizeArtifactPathForJson(artifactPath);
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"active\": false,\n"
	     << "  \"producer\": \"" << NOTICE_PRODUCER_ENGINE() << "\",\n"
	     << "  \"artifact_path\": \"" << jsonEscapeString(artifactPathJson) << "\",\n"
	     << "  \"artifact_sha256\": \"" << artifactSha << "\",\n"
	     << "  \"session_id\": \"" << jsonEscapeString(existingSession) << "\",\n"
	     << "  \"cleared_at_ms\": " << clearedAtMs << ",\n"
	     << "  \"cleared_reason\": \"automatic_v5_export\"\n"
	     << "}\n";
	return CompanionSnapshotExportCore::writeUtf8FileAtomically(noticePath, json.str());
}

bool writeEngineManualUploadNoticeIfAllowed(const std::string &snapshotDir, const std::string &noticeJson)
{
	if (snapshotDir.empty())
	{
		return false;
	}
	const std::string noticePath = pathJoin(snapshotDir, MANUAL_UPLOAD_NOTICE_FILENAME());
	std::string existing;
	readTextFile(noticePath, &existing);
	if (!shouldReplaceEngineManualUploadNotice(existing, noticeJson))
	{
		return false;
	}
	return CompanionSnapshotExportCore::writeUtf8FileAtomically(noticePath, noticeJson);
}

ManualV5ExportResult tryWriteManualV5OversizeExport(const ManualV5ExportInput &input)
{
	ManualV5ExportResult result = {};
	if (input.snapshotDir.empty() || input.sessionId.empty() || input.turn < 0
	    || input.v5YamlUtf8.empty() || input.automaticSnapshotSha256.empty())
	{
		result.diagnostic = "manual export input incomplete";
		return result;
	}
	if (input.v5YamlUtf8.size() <= CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES)
	{
		result.diagnostic = "v5 within automatic cap";
		return result;
	}
	const std::string snapshotId12 = snapshotId12FromYaml(input.v5YamlUtf8);
	if (engineNoticeAlreadyCoversExport(input.snapshotDir, input, snapshotId12))
	{
		result.diagnostic = "active engine notice already references this v5 export";
		return result;
	}
	const std::string serialized = buildManualIntelligenceArtifactJson(input, snapshotId12);
	const size_t bytes = serialized.size();
	if (bytes > MANUAL_ARTIFACT_MAX_BYTES)
	{
		result.diagnostic = "manual artifact exceeds manual cap";
		std::cerr << "Companion manual v5 export: artifact exceeds " << MANUAL_ARTIFACT_MAX_BYTES
			<< " byte cap (" << bytes << " bytes); notice not written" << std::endl;
		return result;
	}
	const std::string exportDir = resolveManualExportDirectory(input.snapshotDir);
	if (!ensureDirectoryExists(exportDir))
	{
		result.diagnostic = "manual export directory unavailable";
		std::cerr << "Companion manual v5 export: could not create export directory" << std::endl;
		return result;
	}
	const std::string filename = formatManualArtifactFilename(
		input.sessionId, input.turn, input.exportedAtMs, snapshotId12);
	const std::string destPath = pathJoin(exportDir, filename.c_str());
	if (!CompanionSnapshotExportCore::writeUtf8FileAtomically(destPath, serialized))
	{
		result.diagnostic = "manual artifact write failed";
		std::cerr << "Companion manual v5 export: artifact write failed" << std::endl;
		return result;
	}
	std::string digest;
	if (!CompanionSha256::digestHexLowerUtf8(serialized, &digest))
	{
		result.diagnostic = "manual artifact digest failed";
		std::cerr << "Companion manual v5 export: digest failed after write; notice skipped" << std::endl;
		return result;
	}
	result.artifactWritten = true;
	result.artifactPath = destPath;
	result.artifactFilename = filename;
	result.artifactSha256 = digest;
	result.artifactBytes = bytes;

	const std::string noticeId = CompanionBriefingTestSupport::makeRequestId();
	const std::string noticeJson = buildEngineManualUploadNoticeJson(
		input, destPath, filename, digest, snapshotId12, noticeId);
	result.noticeWritten = writeEngineManualUploadNoticeIfAllowed(input.snapshotDir, noticeJson);
	if (!result.noticeWritten)
	{
		std::cerr << "Companion manual v5 export: notice unchanged (newer unrelated notice active)" << std::endl;
	}
	else
	{
		std::cerr << "Companion manual v5 export: wrote " << filename
			<< " (" << bytes << " bytes); notice active" << std::endl;
	}
	std::vector<std::string> protectOne;
	protectOne.push_back(destPath);
	CompanionManualExportRetention::pruneManualExportDirectory(input.snapshotDir, exportDir, protectOne);
	return result;
}

} // namespace CompanionManualV5Export

} // namespace OpenXcom
