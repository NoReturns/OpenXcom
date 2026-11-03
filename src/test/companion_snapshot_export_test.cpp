/*
 * Open X-Com Companion — regression tests for snapshot size-cap fallback (M1-17H).
 */
#include "Engine/CompanionSnapshotExportCore.h"
#include "Engine/CompanionSnapshotTerrain.h"
#include "Engine/CompanionSha256.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

	YAML::Node unit;
	unit["id"] = 1;
	unit["faction"] = 0;
	unit["originalFaction"] = 0;
	unit["position"] = YAML::Load("[0, 0, 0]");
	root["units"] = YAML::Node(YAML::NodeType::Sequence);
	root["units"].push_back(unit);

	YAML::Node eventLog;
	eventLog["sessionId"] = "abc123";
	eventLog["oldestSeq"] = 1;
	eventLog["latestSeq"] = 1;
	eventLog["droppedCount"] = 0;
	YAML::Node ev;
	ev["seq"] = 1;
	ev["type"] = "weapon_fired";
	ev["turn"] = 3;
	ev["actorUnitId"] = 1;
	ev["weaponItemType"] = "STR_RIFLE";
	ev["actionType"] = "snap";
	ev["target"] = YAML::Load("{x: 1, y: 0, z: 0}");
	eventLog["events"] = YAML::Node(YAML::NodeType::Sequence);
	eventLog["events"].push_back(ev);
	root["eventLog"] = eventLog;
	return root;
}

static YAML::Node buildSampleV5WithSmallTerrain(const YAML::Node &intelligenceV4)
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
	terrain["memoryNote"] = "terrain note for export fallback tests";
	terrain["encoding"] = "packed-v1";
	terrain["recordCount"] = 1;
	// map 2x1x2, one body tile at (0,0,0): disc 4, inLos, walkF 4 elev 0
	terrain["data"] = "T1hUMQEAAAACAAEAAgAAAQAAAAAAAAQBBAAE";
	v5["terrain"] = terrain;
	return v5;
}

static YAML::Node buildOversizedTerrainV5(const YAML::Node &intelligenceV4)
{
	YAML::Node v5 = buildSampleV5WithSmallTerrain(intelligenceV4);
	v5["terrain"]["memoryNote"] = std::string(CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES, 'x');
	return v5;
}

static YAML::Node buildOversizedV4()
{
	YAML::Node v4 = buildSampleIntelligenceV4();
	YAML::Node events = YAML::Node(YAML::NodeType::Sequence);
	for (int i = 0; i < 8000; ++i)
	{
		YAML::Node ev;
		ev["seq"] = i;
		ev["type"] = "weapon_fired";
		ev["turn"] = 3;
		ev["actorUnitId"] = 1;
		ev["weaponItemType"] = "STR_RIFLE";
		ev["actionType"] = "snap";
		ev["target"] = YAML::Load("{x: 1, y: 0, z: 0}");
		events.push_back(ev);
	}
	v4["eventLog"]["events"] = events;
	return v4;
}

static std::string readFileUtf8(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

static void testSmallSnapshotRemainsV5WithTerrain()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildSampleV5WithSmallTerrain(v4);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "small v5 export must succeed");
	expect(resolved.schemaVersion == 5, "small snapshot must remain schemaVersion 5");
	expect(!resolved.terrainOmittedDueToSize, "terrain must not be omitted when v5 fits");
	const YAML::Node parsed = YAML::Load(resolved.yamlUtf8);
	expect(parsed["schemaVersion"].as<int>() == 5, "serialized schemaVersion must be 5");
	expect(parsed["terrain"]["encoding"].as<std::string>() == "packed-v1", "packed terrain expected");
	expect(parsed["terrain"]["recordCount"].as<unsigned int>() == 1, "recordCount expected");
}

static void testOversizedV5FallsBackToValidV4()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "oversized v5 must fall back to v4");
	expect(resolved.schemaVersion == 4, "fallback must be schemaVersion 4");
	expect(resolved.terrainOmittedDueToSize, "terrainOmittedDueToSize flag expected");
	const YAML::Node parsed = YAML::Load(resolved.yamlUtf8);
	expect(parsed["schemaVersion"].as<int>() == 4, "serialized schemaVersion must be 4");
}

static void testFallbackContainsNoTerrainRoot()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "fallback export must succeed");
	expect(resolved.yamlUtf8.find("terrain:") == std::string::npos, "fallback YAML must not contain terrain root");
	const YAML::Node parsed = YAML::Load(resolved.yamlUtf8);
	expect(!parsed["terrain"], "parsed fallback must have no terrain node");
}

static void testOriginalNonTerrainIntelligenceRemains()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "fallback export must succeed");
	const YAML::Node parsed = YAML::Load(resolved.yamlUtf8);
	expect(parsed["turn"].as<int>() == v4["turn"].as<int>(), "turn preserved");
	expect(parsed["units"].size() == v4["units"].size(), "units count preserved");
	expect(parsed["eventLog"]["sessionId"].as<std::string>() == v4["eventLog"]["sessionId"].as<std::string>(),
		"eventLog session preserved");
	expect(CompanionSnapshotExportCore::serializeSnapshotYaml(v4) == resolved.yamlUtf8,
		"fallback YAML must match full v4 intelligence snapshot");
}

static void testFinalBriefingShaMatchesActualYamlBytes()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "fallback export must succeed");
	std::string digest;
	expect(CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &digest), "digest must succeed");
	expect(CompanionSha256::isValidHexDigest(digest), "digest format must be valid");
	std::string redigest;
	expect(CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &redigest), "redigest must succeed");
	expect(digest == redigest, "digest must be deterministic over final YAML bytes");
	std::string v5Digest;
	const std::string oversizedV5Yaml = CompanionSnapshotExportCore::serializeSnapshotYaml(v5);
	expect(CompanionSha256::digestHexLowerUtf8(oversizedV5Yaml, &v5Digest), "v5 digest must succeed");
	expect(digest != v5Digest, "briefing SHA must bind to final v4 bytes not rejected v5");
}

static void testOversizedV4StillFailsExplicitly()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const YAML::Node hugeV4 = buildOversizedV4();
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&hugeV4]() { return hugeV4; });
	expect(!resolved.ok, "oversized v4 must fail when v5 also exceeds cap");
	expect(resolved.yamlUtf8.empty(), "failed resolution must not return YAML payload");
}

static void testNoStaleV5FileAfterFallback()
{
	const YAML::Node v4 = buildSampleIntelligenceV4();
	const YAML::Node v5 = buildOversizedTerrainV5(v4);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(v5, [&v4]() { return v4; });
	expect(resolved.ok, "fallback export must succeed");

	const std::string path = "companion_export_fallback_test.yaml";
	{
		std::ofstream seed(path.c_str(), std::ios::out | std::ios::trunc);
		seed << "schemaVersion: 5\nterrain:\n  stale: true\n";
	}
	expect(CompanionSnapshotExportCore::writeSnapshotAtomically(path, resolved.yamlUtf8),
		"atomic write of final fallback YAML must succeed");
	const std::string onDisk = readFileUtf8(path);
	expect(onDisk.find("stale") == std::string::npos, "stale v5 marker must not remain on disk");
	expect(onDisk.find("schemaVersion: 4") != std::string::npos, "on-disk snapshot must be schemaVersion 4");
	expect(onDisk.find("terrain:") == std::string::npos, "on-disk snapshot must not contain terrain");
	std::remove(path.c_str());
}

#ifdef main
#undef main
#endif
int main()
{
	testSmallSnapshotRemainsV5WithTerrain();
	testOversizedV5FallsBackToValidV4();
	testFallbackContainsNoTerrainRoot();
	testOriginalNonTerrainIntelligenceRemains();
	testFinalBriefingShaMatchesActualYamlBytes();
	testOversizedV4StillFailsExplicitly();
	testNoStaleV5FileAfterFallback();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_snapshot_export_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
