/*
 * Open X-Com Companion — engine manual v5 export (M1 P3 step 3).
 */
#include "Engine/CompanionManualV5Export.h"
#include "Engine/CompanionSnapshotExportCore.h"
#include "Engine/CompanionSnapshotTerrain.h"
#include "Engine/CompanionSha256.h"
#include "Engine/CompanionJsonLite.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <direct.h>
#define COMPANION_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define COMPANION_MKDIR(path) mkdir(path, 0755)
#endif

using namespace OpenXcom;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static std::string readFileUtf8(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

static YAML::Node buildSampleIntelligenceV4()
{
	YAML::Node root;
	root["schemaVersion"] = 4;
	root["turn"] = 3;
	root["side"] = 0;
	root["selectedUnitId"] = 1;
	YAML::Node map;
	map["width"] = 2;
	map["height"] = 1;
	map["depth"] = 2;
	root["map"] = map;
	YAML::Node eventLog;
	eventLog["sessionId"] = "0123456789abcdef0123456789abcdef";
	eventLog["oldestSeq"] = 1;
	eventLog["latestSeq"] = 1;
	eventLog["droppedCount"] = 0;
	eventLog["events"] = YAML::Node(YAML::NodeType::Sequence);
	root["eventLog"] = eventLog;
	root["units"] = YAML::Node(YAML::NodeType::Sequence);
	return root;
}

static YAML::Node buildOversizedTerrainV5(const YAML::Node &intelligenceV4)
{
	YAML::Node v5;
	v5["schemaVersion"] = 5;
	v5["turn"] = intelligenceV4["turn"];
	v5["side"] = intelligenceV4["side"];
	v5["selectedUnitId"] = intelligenceV4["selectedUnitId"];
	v5["map"] = intelligenceV4["map"];
	v5["units"] = intelligenceV4["units"];
	v5["eventLog"] = intelligenceV4["eventLog"];
	YAML::Node terrain;
	terrain["memoryNote"] = "synthetic oversized terrain for engine manual export tests";
	terrain["encoding"] = "packed-v1";
	terrain["recordCount"] = 1;
	terrain["data"] = std::string(CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES, 'x');
	v5["terrain"] = terrain;
	return v5;
}

static YAML::Node build60x60x4OversizedV5(const YAML::Node &intelligenceV4)
{
	YAML::Node v5 = buildOversizedTerrainV5(intelligenceV4);
	YAML::Node map;
	map["width"] = 60;
	map["height"] = 60;
	map["depth"] = 4;
	v5["map"] = map;
	v5["terrain"]["recordCount"] = 24000;
	v5["terrain"]["data"] = std::string(280000, 'A');
	return v5;
}

static void testOversizedV5FallbackWithManualArtifact(const std::string &root)
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const std::string v5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(v5);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok && resolved.terrainOmittedDueToSize, "oversized v5 resolves to v4");

	std::string automaticDigest;
	expect(CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &automaticDigest), "automatic digest");

	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = root;
	input.sessionId = v4["eventLog"]["sessionId"].as<std::string>();
	input.turn = v4["turn"].as<int>();
	input.v5YamlUtf8 = v5Yaml;
	input.automaticYamlUtf8 = resolved.yamlUtf8;
	input.automaticSnapshotSha256 = automaticDigest;
	input.v5AttemptedBytes = resolved.v5AttemptedBytes;
	input.v4FallbackBytes = resolved.v4FallbackBytes;
	input.exportedAtMs = 1794000001000LL;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(manual.artifactWritten, "manual artifact written");
	expect(manual.noticeWritten, "engine notice written");
	expect(manual.artifactBytes > CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES, "artifact larger than automatic cap");
	expect(manual.artifactBytes < CompanionManualV5Export::MANUAL_ARTIFACT_MAX_BYTES, "artifact within manual cap");

	const std::string artifactBody = readFileUtf8(manual.artifactPath);
	std::string artifactDigest;
	expect(CompanionSha256::digestHexLowerUtf8(artifactBody, &artifactDigest), "artifact digest");
	expect(artifactDigest == manual.artifactSha256, "artifact hash matches");

	std::string noticeBody = readFileUtf8(
		root + "/companion-manual-upload-notice.json");
	std::string producer;
	std::string reason;
	CompanionJsonLite::extractJsonStringField(noticeBody, "producer", &producer);
	CompanionJsonLite::extractJsonStringField(noticeBody, "reason_code", &reason);
	expect(producer == "engine", "notice producer engine");
	expect(reason == CompanionManualV5Export::REASON_ENGINE_V5_OVERSIZE(), "notice reason code");
	expect(artifactBody.find("tactical_snapshot") != std::string::npos, "artifact embeds tactical snapshot");
	expect(artifactBody.find("schemaVersion: 5") != std::string::npos || artifactBody.find("schemaVersion:5") != std::string::npos,
		"artifact contains full v5 yaml");
}

static void test60x60x4TerrainCase(const std::string &root)
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = build60x60x4OversizedV5(v4);
	const std::string v5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(v5);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "60x60x4 resolves to automatic v4");
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = root + "/sixty";
	COMPANION_MKDIR(input.snapshotDir.c_str());
	input.sessionId = v4["eventLog"]["sessionId"].as<std::string>();
	input.turn = 4;
	input.v5YamlUtf8 = v5Yaml;
	input.automaticYamlUtf8 = resolved.yamlUtf8;
	CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &input.automaticSnapshotSha256);
	input.v5AttemptedBytes = resolved.v5AttemptedBytes;
	input.v4FallbackBytes = resolved.v4FallbackBytes;
	input.exportedAtMs = 1794000002000LL;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(manual.artifactWritten, "60x60x4 manual artifact");
	const YAML::Node parsedV5 = YAML::Load(v5Yaml);
	expect(parsedV5["map"]["width"].as<int>() == 60, "map width preserved in v5 yaml");
	expect(parsedV5["map"]["depth"].as<int>() == 4, "map depth preserved");
}

static void testRewritesWhenActiveNoticeArtifactMissing(const std::string &root)
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const std::string v5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(v5);
	const std::string dir = root + "/missing-artifact";
	COMPANION_MKDIR(dir.c_str());
	std::remove((dir + "/companion-manual-upload-notice.json").c_str());
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = dir;
	input.sessionId = v4["eventLog"]["sessionId"].as<std::string>();
	input.turn = v4["turn"].as<int>();
	input.v5YamlUtf8 = v5Yaml;
	input.automaticSnapshotSha256 = std::string(64, 'd');
	input.v5AttemptedBytes = v5Yaml.size();
	input.exportedAtMs = 50;
	const CompanionManualV5Export::ManualV5ExportResult first =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(first.artifactWritten, "initial export writes artifact");
	std::remove(first.artifactPath.c_str());
	const CompanionManualV5Export::ManualV5ExportResult second =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(second.artifactWritten, "missing artifact forces rewrite");
	expect(!second.artifactPath.empty(), "rewrite produces path");
	std::ifstream verify(second.artifactPath.c_str());
	expect(verify.good(), "rewritten artifact readable");
}

static void testSkipsRedundantExportWhenNoticeMatches(const std::string &root)
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const std::string v5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(v5);
	const std::string dir = root + "/redundant";
	COMPANION_MKDIR(dir.c_str());
	std::remove((dir + "/companion-manual-upload-notice.json").c_str());
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = dir;
	input.sessionId = v4["eventLog"]["sessionId"].as<std::string>();
	input.turn = v4["turn"].as<int>();
	input.v5YamlUtf8 = v5Yaml;
	input.automaticSnapshotSha256 = std::string(64, 'c');
	input.v5AttemptedBytes = v5Yaml.size();
	input.exportedAtMs = 1;
	const CompanionManualV5Export::ManualV5ExportResult first =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(first.artifactWritten, "first export writes");
	const CompanionManualV5Export::ManualV5ExportResult second =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(!second.artifactWritten, "second export skipped");
	expect(second.diagnostic.find("already references") != std::string::npos, "skip diagnostic");
}

static void testNoticeOwnershipAndStaleRules(const std::string &root)
{
	const std::string nodeNotice =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"active\": true,\n"
		"  \"producer\": \"node\",\n"
		"  \"artifact_path\": \"c:/relay.json\",\n"
		"  \"artifact_sha256\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n"
		"  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
		"  \"created_at_ms\": 1000\n"
		"}\n";
	const std::string engineIncoming =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"active\": true,\n"
		"  \"producer\": \"engine\",\n"
		"  \"artifact_path\": \"c:/engine.json\",\n"
		"  \"artifact_sha256\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\n"
		"  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
		"  \"created_at_ms\": 9000\n"
		"}\n";
	expect(!CompanionManualV5Export::shouldReplaceEngineManualUploadNotice(nodeNotice, engineIncoming),
		"engine notice must not replace active node notice");

	const std::string newerEngine =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"active\": true,\n"
		"  \"producer\": \"engine\",\n"
		"  \"artifact_path\": \"c:/newer.json\",\n"
		"  \"artifact_sha256\": \"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\",\n"
		"  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
		"  \"turn\": 4,\n"
		"  \"created_at_ms\": 5000\n"
		"}\n";
	const std::string olderEngine =
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"active\": true,\n"
		"  \"producer\": \"engine\",\n"
		"  \"artifact_path\": \"c:/older.json\",\n"
		"  \"artifact_sha256\": \"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\",\n"
		"  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
		"  \"turn\": 4,\n"
		"  \"created_at_ms\": 1000\n"
		"}\n";
	expect(CompanionManualV5Export::shouldReplaceEngineManualUploadNotice(olderEngine, newerEngine),
		"newer engine supersedes older engine");
	expect(!CompanionManualV5Export::shouldReplaceEngineManualUploadNotice(newerEngine, olderEngine),
		"older engine cannot replace newer");

	const std::string dir = root + "/notice";
	COMPANION_MKDIR(dir.c_str());
	CompanionManualV5Export::writeEngineManualUploadNoticeIfAllowed(dir, newerEngine);
	const std::string onDisk = readFileUtf8(dir + "/companion-manual-upload-notice.json");
	expect(onDisk.find("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc") != std::string::npos,
		"newer notice on disk");
}

static void testManualWriteFailurePreservesAutomatic(const std::string & /*root*/)
{
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = "";
	input.sessionId = "0123456789abcdef0123456789abcdef";
	input.turn = 1;
	input.v5YamlUtf8 = std::string(300000, 'y');
	input.automaticYamlUtf8 = "schemaVersion: 4\n";
	input.automaticSnapshotSha256 = std::string(64, 'f');
	input.v5AttemptedBytes = input.v5YamlUtf8.size();
	input.exportedAtMs = 1;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(!manual.artifactWritten, "invalid export dir prevents artifact");
	expect(!manual.noticeWritten, "no notice without artifact");
}

static YAML::Node buildSmallV5WithTerrain(const YAML::Node &intelligenceV4)
{
	YAML::Node v5;
	v5["schemaVersion"] = 5;
	v5["turn"] = intelligenceV4["turn"];
	v5["side"] = intelligenceV4["side"];
	v5["selectedUnitId"] = intelligenceV4["selectedUnitId"];
	v5["map"] = intelligenceV4["map"];
	v5["units"] = intelligenceV4["units"];
	v5["eventLog"] = intelligenceV4["eventLog"];
	YAML::Node terrain;
	terrain["memoryNote"] = "small terrain";
	terrain["encoding"] = "packed-v1";
	terrain["recordCount"] = 1;
	terrain["data"] = "T1hUMQEAAAACAAEAAgAAAQAAAAAAAAQBBAAE";
	v5["terrain"] = terrain;
	return v5;
}

static void testUnderCapSkipsManualExport(const std::string &root)
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node smallV5 = buildSmallV5WithTerrain(v4);
	const std::string v5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(smallV5);
	expect(v5Yaml.size() <= CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES, "fixture v5 under automatic cap");
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = root + "/small";
	COMPANION_MKDIR(input.snapshotDir.c_str());
	input.sessionId = v4["eventLog"]["sessionId"].as<std::string>();
	input.turn = 1;
	input.v5YamlUtf8 = v5Yaml;
	input.automaticSnapshotSha256 = std::string(64, 'a');
	input.exportedAtMs = 100;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(!manual.artifactWritten, "under-cap v5 skips manual export");
}

static void testAtomicWriteLeavesNoTemp(const std::string &root)
{
	const std::string dir = root + "/atomic";
	COMPANION_MKDIR(dir.c_str());
	const std::string v5Yaml = std::string(CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES + 1024, 'z');
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = dir;
	input.sessionId = "0123456789abcdef0123456789abcdef";
	input.turn = 2;
	input.v5YamlUtf8 = v5Yaml;
	input.automaticSnapshotSha256 = std::string(64, 'e');
	input.v5AttemptedBytes = v5Yaml.size();
	input.exportedAtMs = 42;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(manual.artifactWritten, "atomic write succeeds");
	expect(manual.artifactPath.find(".companion.tmp") == std::string::npos, "final artifact path not temp");
	const std::string tmpProbe = manual.artifactPath + ".companion.tmp";
	std::ifstream tmp(tmpProbe.c_str());
	expect(!tmp.good(), "tmp artifact not left behind");
}

#ifdef main
#undef main
#endif
static void removeIfExists(const std::string &path)
{
	std::remove(path.c_str());
}

int main()
{
	const std::string root = "companion_manual_v5_export_test_tmp";
	COMPANION_MKDIR(root.c_str());
	removeIfExists(root + "/companion-manual-upload-notice.json");
	testOversizedV5FallbackWithManualArtifact(root);
	testSkipsRedundantExportWhenNoticeMatches(root);
	testRewritesWhenActiveNoticeArtifactMissing(root);
	test60x60x4TerrainCase(root);
	testNoticeOwnershipAndStaleRules(root);
	testManualWriteFailurePreservesAutomatic(root);
	testUnderCapSkipsManualExport(root);
	testAtomicWriteLeavesNoTemp(root);
	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_manual_v5_export_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
