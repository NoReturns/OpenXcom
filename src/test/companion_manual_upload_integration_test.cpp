/*
 * Open X-Com Companion — manual upload P3 step 6 isolated integration (engine side).
 */
#include "Engine/CompanionManualExportRetention.h"
#include "Engine/CompanionManualUploadHudCore.h"
#include "Engine/CompanionManualV5Export.h"
#include "Engine/CompanionSnapshotExportCore.h"
#include "Engine/CompanionSnapshotTerrain.h"
#include "Engine/CompanionSha256.h"
#include <yaml-cpp/yaml.h>
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

static void writeFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static void removeIfExists(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string readFileUtf8(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

static YAML::Node buildIntelligenceV4()
{
	YAML::Node root;
	root["schemaVersion"] = 4;
	root["turn"] = 2;
	root["side"] = 0;
	root["selectedUnitId"] = 1;
	YAML::Node map;
	map["width"] = 10;
	map["height"] = 10;
	map["depth"] = 2;
	root["map"] = map;
	YAML::Node eventLog;
	eventLog["sessionId"] = "fedcba9876543210fedcba9876543210";
	eventLog["oldestSeq"] = 1;
	eventLog["latestSeq"] = 1;
	eventLog["droppedCount"] = 0;
	eventLog["events"] = YAML::Node(YAML::NodeType::Sequence);
	root["eventLog"] = eventLog;
	root["units"] = YAML::Node(YAML::NodeType::Sequence);
	return root;
}

static YAML::Node buildOversizedV5(const YAML::Node &v4)
{
	YAML::Node v5;
	v5["schemaVersion"] = 5;
	v5["turn"] = v4["turn"];
	v5["side"] = v4["side"];
	v5["selectedUnitId"] = v4["selectedUnitId"];
	v5["map"] = v4["map"];
	v5["units"] = v4["units"];
	v5["eventLog"] = v4["eventLog"];
	YAML::Node terrain;
	terrain["encoding"] = "packed-v1";
	terrain["recordCount"] = 1;
	terrain["data"] = std::string(CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES + 4096, 'Z');
	v5["terrain"] = terrain;
	return v5;
}

static void testEngineV4BindingHudRetention(const std::string &root)
{
	const std::string snapshotDir = root + "/engine-flow";
	removeIfExists(snapshotDir + "/companion-manual-upload-notice.json");
	COMPANION_MKDIR(snapshotDir.c_str());
	const YAML::Node v4 = buildIntelligenceV4();
	const YAML::Node v5 = buildOversizedV5(v4);
	const std::string v5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(v5);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok && resolved.terrainOmittedDueToSize, "v5 oversize falls back to v4");
	std::string automaticDigest;
	expect(CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &automaticDigest), "v4 digest");

	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = snapshotDir;
	input.sessionId = v4["eventLog"]["sessionId"].as<std::string>();
	input.turn = v4["turn"].as<int>();
	input.v5YamlUtf8 = v5Yaml;
	input.automaticYamlUtf8 = resolved.yamlUtf8;
	input.automaticSnapshotSha256 = automaticDigest;
	input.v5AttemptedBytes = resolved.v5AttemptedBytes;
	input.v4FallbackBytes = resolved.v4FallbackBytes;
	input.exportedAtMs = 1730000000000LL;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(manual.artifactWritten && manual.noticeWritten, "engine manual artifact and notice");
	const std::string artifactBody = readFileUtf8(manual.artifactPath);
	expect(artifactBody.find(automaticDigest) != std::string::npos,
		"artifact binds source_snapshot_sha256 to v4 automatic export");
	expect(artifactBody.find("\"authoritative\": false") != std::string::npos, "manual artifact not command authority");

	const std::string noticePath = snapshotDir + "/" + CompanionManualV5Export::MANUAL_UPLOAD_NOTICE_FILENAME();
	const std::string goodNotice = readFileUtf8(noticePath);
	const std::string markerPath = snapshotDir + "/companion-tactical-send-marker.json";
	writeFile(markerPath,
		"{\"session_id\":\"fedcba9876543210fedcba9876543210\",\"turn\":2}\n");
	const std::string snapshotPath = snapshotDir + "/tactical-live.yaml";
	writeFile(snapshotPath, resolved.yamlUtf8);
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif

	CompanionManualUploadHudCore::ManualUploadHudView hudView;
	expect(CompanionManualUploadHudCore::readManualUploadHudView(&hudView), "hud reads engine notice");
	expect(hudView.headline == "FULL TERRAIN EXPORT AVAILABLE", "hud optional headline");

	writeFile(noticePath, "{not-json");
	expect(!CompanionManualUploadHudCore::readManualUploadHudView(&hudView), "malformed notice hidden");
	writeFile(noticePath, goodNotice);

	const CompanionManualV5Export::ManualV5ExportResult duplicate =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(!duplicate.artifactWritten, "duplicate export skipped");

	CompanionManualUploadHudCore::SessionBinding binding = {};
	binding.known = true;
	binding.sessionId = input.sessionId;
	binding.turn = input.turn;
	const std::string nodeNotice =
		"{\"active\":true,\"producer\":\"node\",\"reason_code\":\"RELAY_SHRINK_EXHAUSTED\","
		"\"artifact_path\":\"" + snapshotDir + "/manual-exports/relay-manual_test.json\","
		"\"artifact_filename\":\"relay-manual_test.json\","
		"\"session_id\":\"" + input.sessionId + "\",\"turn\":2,\"created_at_ms\":1}\n";
	CompanionManualUploadHudCore::ManualUploadHudView nodeHud;
	expect(CompanionManualUploadHudCore::parseManualUploadNoticeJson(nodeNotice, binding, &nodeHud),
		"node notice parses");
	expect(nodeHud.headline == "MANUAL UPLOAD REQUIRED", "hud required headline for relay path");

	CompanionManualExportRetention::ManualExportRetentionResult retention =
		CompanionManualExportRetention::pruneManualExportDirectory(snapshotDir,
			snapshotDir + "/manual-exports",
			{ manual.artifactPath });
	expect(retention.ok, "retention ok");
	std::ifstream protectedArtifact(manual.artifactPath.c_str());
	expect(protectedArtifact.good(), "active engine artifact survives retention");
}

static void testManualCapExceededGraceful(const std::string &root)
{
	const std::string dir = root + "/over-cap";
	removeIfExists(dir + "/companion-manual-upload-notice.json");
	COMPANION_MKDIR(dir.c_str());
	CompanionManualV5Export::ManualV5ExportInput input = {};
	input.snapshotDir = dir;
	input.sessionId = "0123456789abcdef0123456789abcdef";
	input.turn = 1;
	input.v5YamlUtf8 = std::string(CompanionManualV5Export::MANUAL_ARTIFACT_MAX_BYTES + 50000, 'q');
	input.automaticSnapshotSha256 = std::string(64, 'a');
	input.v5AttemptedBytes = input.v5YamlUtf8.size();
	input.exportedAtMs = 1;
	const CompanionManualV5Export::ManualV5ExportResult manual =
		CompanionManualV5Export::tryWriteManualV5OversizeExport(input);
	expect(!manual.artifactWritten, "over 4MiB manual cap blocks artifact");
	expect(manual.diagnostic.find("manual cap") != std::string::npos, "cap diagnostic");
}

#ifdef main
#undef main
#endif
int main()
{
	const std::string root = "companion_manual_upload_integration_test_tmp";
	COMPANION_MKDIR(root.c_str());
	testEngineV4BindingHudRetention(root);
	testManualCapExceededGraceful(root);
	if (g_failures != 0)
	{
		std::cerr << g_failures << " integration test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_manual_upload_integration_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
