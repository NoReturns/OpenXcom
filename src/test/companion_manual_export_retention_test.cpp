/*
 * Open X-Com Companion — manual export retention (M1 P3 step 5).
 */
#include "Engine/CompanionManualExportRetention.h"
#include "Engine/CompanionManualV5Export.h"
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

static std::string activeNotice(const std::string &artifactPath)
{
	return std::string("{\n")
		+ "  \"active\": true,\n"
		+ "  \"producer\": \"engine\",\n"
		+ "  \"reason_code\": \"ENGINE_V5_OVERSIZE\",\n"
		+ "  \"artifact_path\": \"" + artifactPath + "\",\n"
		+ "  \"session_id\": \"aa\",\n"
		+ "  \"turn\": 1\n"
		+ "}\n";
}

static std::string filenameForMs(long long ms)
{
	std::ostringstream name;
	name << "engine-v5_abcd1234_t1_" << ms << "_snap00000001.json";
	return name.str();
}

static void testPruneRetainsProtectedAndRecent(const std::string &root)
{
	const std::string exportDir = root + "/manual-exports";
	COMPANION_MKDIR(exportDir.c_str());
	const std::string protectedPath = exportDir + "/" + filenameForMs(1000);
	writeFile(protectedPath, "{}");
	for (long long ms = 2000; ms <= 7000; ms += 1000)
	{
		writeFile(exportDir + "/" + filenameForMs(ms), "{}");
	}
	writeFile(root + "/" + CompanionManualV5Export::MANUAL_UPLOAD_NOTICE_FILENAME(), activeNotice(protectedPath));
	const CompanionManualExportRetention::ManualExportRetentionResult pruned =
		CompanionManualExportRetention::pruneManualExportDirectory(root, exportDir, { protectedPath });
	expect(pruned.ok, "prune ok");
	expect(pruned.deleted >= 2, "old exports removed");
	std::ifstream still(protectedPath.c_str());
	expect(still.good(), "protected active artifact remains");
}

static void testMissingExportDir(const std::string &root)
{
	const CompanionManualExportRetention::ManualExportRetentionResult pruned =
		CompanionManualExportRetention::pruneManualExportDirectory(root, root + "/no-such-dir", {});
	expect(pruned.ok, "missing dir non-fatal");
	expect(pruned.deleted == 0, "no deletes for missing dir");
}

static void testMalformedNoticeNoCrash(const std::string &root)
{
	const std::string exportDir = root + "/malformed-exports";
	COMPANION_MKDIR(exportDir.c_str());
	writeFile(root + "/" + CompanionManualV5Export::MANUAL_UPLOAD_NOTICE_FILENAME(), "{not-json");
	writeFile(exportDir + "/" + filenameForMs(9000), "{}");
	const CompanionManualExportRetention::ManualExportRetentionResult pruned =
		CompanionManualExportRetention::pruneManualExportDirectory(root, exportDir, {});
	expect(pruned.ok, "malformed notice tolerated");
}

static void testFilenameParsing()
{
	long long ms = 0;
	expect(CompanionManualExportRetention::isEligibleManualExportFilename("relay-manual_ab_t2_42_deadbeef.json"),
		"relay prefix eligible");
	expect(CompanionManualExportRetention::parseExportedAtMsFromManualFilename("engine-v5_abcd1234_t1_5555_snap.json", &ms),
		"parse ms");
	expect(ms == 5555, "parsed value");
}

#ifdef main
#undef main
#endif
int main()
{
	const std::string root = "companion_manual_export_retention_test_tmp";
	COMPANION_MKDIR(root.c_str());
	testFilenameParsing();
	testMissingExportDir(root);
	testMalformedNoticeNoCrash(root);
	testPruneRetainsProtectedAndRecent(root);
	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_manual_export_retention_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
