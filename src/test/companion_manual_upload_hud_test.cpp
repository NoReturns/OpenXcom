/*
 * Open X-Com Companion — manual upload notice HUD (M1 P3 step 4).
 */
#include "Engine/CompanionManualUploadHudCore.h"
#include "Engine/CompanionManualV5Export.h"
#include "Engine/CompanionOnDemandRelayLayout.h"
#include "Engine/CompanionOnDemandRelayCore.h"
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

static std::string activeEngineNotice()
{
	return "{\n"
	       "  \"schema_version\": 1,\n"
	       "  \"active\": true,\n"
	       "  \"producer\": \"engine\",\n"
	       "  \"reason_code\": \"ENGINE_V5_OVERSIZE\",\n"
	       "  \"artifact_path\": \"C:/snap/manual-exports/engine-v5_abc.json\",\n"
	       "  \"artifact_filename\": \"engine-v5_abc.json\",\n"
	       "  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
	       "  \"turn\": 3,\n"
	       "  \"created_at_ms\": 1000\n"
	       "}\n";
}

static std::string activeNodeNotice()
{
	return "{\n"
	       "  \"schema_version\": 1,\n"
	       "  \"active\": true,\n"
	       "  \"producer\": \"node\",\n"
	       "  \"reason_code\": \"RELAY_SHRINK_EXHAUSTED\",\n"
	       "  \"artifact_path\": \"C:/snap/manual-exports/relay-manual.json\",\n"
	       "  \"artifact_filename\": \"relay-manual.json\",\n"
	       "  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
	       "  \"turn\": 3,\n"
	       "  \"created_at_ms\": 2000\n"
	       "}\n";
}

static void testEngineNoticeParsing()
{
	CompanionManualUploadHudCore::SessionBinding binding = {};
	binding.known = true;
	binding.sessionId = "0123456789abcdef0123456789abcdef";
	binding.turn = 3;
	CompanionManualUploadHudCore::ManualUploadHudView view;
	expect(CompanionManualUploadHudCore::parseManualUploadNoticeJson(activeEngineNotice(), binding, &view),
		"engine notice parses");
	expect(view.show, "engine notice shows");
	expect(view.reasonLabel == "Optional upload (v4 auto OK)", "engine reason label");
	expect(view.headline == "FULL TERRAIN EXPORT AVAILABLE", "engine headline");
	std::string text;
	CompanionManualUploadHudCore::formatManualUploadHudText(view, &text);
	expect(text.find("FULL TERRAIN EXPORT AVAILABLE") != std::string::npos, "engine headline present");
	expect(text.find("engine-v5_abc.json") != std::string::npos, "filename hint");
}

static void testNodeNoticeParsing()
{
	CompanionManualUploadHudCore::SessionBinding binding = {};
	binding.known = true;
	binding.sessionId = "0123456789abcdef0123456789abcdef";
	binding.turn = 3;
	CompanionManualUploadHudCore::ManualUploadHudView view;
	expect(CompanionManualUploadHudCore::parseManualUploadNoticeJson(activeNodeNotice(), binding, &view),
		"node notice parses");
	expect(view.reasonLabel == "Relay delivery unavailable", "node reason label");
	expect(view.headline == "MANUAL UPLOAD REQUIRED", "node headline");
}

static void testStaleAndInvalidNotices()
{
	CompanionManualUploadHudCore::SessionBinding binding = {};
	binding.known = true;
	binding.sessionId = "0123456789abcdef0123456789abcdef";
	binding.turn = 5;
	CompanionManualUploadHudCore::ManualUploadHudView view;
	expect(!CompanionManualUploadHudCore::parseManualUploadNoticeJson(activeEngineNotice(), binding, &view),
		"stale turn suppressed");
	const std::string wrongSession = activeEngineNotice();
	CompanionManualUploadHudCore::SessionBinding mismatch = {};
	mismatch.known = true;
	mismatch.sessionId = "ffffffffffffffffffffffffffffffff";
	mismatch.turn = 3;
	expect(!CompanionManualUploadHudCore::parseManualUploadNoticeJson(wrongSession, mismatch, &view),
		"wrong session suppressed");
	expect(!CompanionManualUploadHudCore::parseManualUploadNoticeJson("{not-json", binding, &view),
		"malformed json rejected");
	const std::string inactive =
		"{\"active\":false,\"producer\":\"engine\",\"artifact_path\":\"x\",\"session_id\":\"0123456789abcdef0123456789abcdef\"}\n";
	expect(!CompanionManualUploadHudCore::parseManualUploadNoticeJson(inactive, binding, &view),
		"inactive notice hidden");
}

static void testReadIntegrationWithMarker(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live.yaml";
	writeFile(snapshotPath, "schemaVersion: 4\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	writeFile(root + "/companion-tactical-send-marker.json",
		"{\"session_id\":\"0123456789abcdef0123456789abcdef\",\"turn\":3}\n");
	writeFile(root + "/companion-manual-upload-notice.json", activeEngineNotice());
	CompanionManualUploadHudCore::ManualUploadHudView view;
	expect(CompanionManualUploadHudCore::readManualUploadHudView(&view), "integrated read shows notice");
	expect(view.artifactFilename == "engine-v5_abc.json", "integrated filename");
	std::remove((root + "/companion-manual-upload-notice.json").c_str());
	expect(!CompanionManualUploadHudCore::readManualUploadHudView(&view), "missing notice file hidden");
}

static void testLayoutDoesNotOverlapRelayControls()
{
	CompanionOnDemandRelayLayout::CompactBarLayout layout;
	expect(CompanionOnDemandRelayLayout::computeBattlescapeTopBarLayout(320, 200, &layout), "layout at 320");
	expect(layout.manualNoticeY == layout.statusY + CompanionOnDemandRelayCore::RELAY_STATUS_HEIGHT + 1,
		"manual notice below relay status");
	expect(layout.manualNoticeY >= layout.statusY + CompanionOnDemandRelayCore::RELAY_STATUS_HEIGHT,
		"manual notice clears status strip");
	expect(layout.manualNoticeY >= layout.sendY + layout.buttonHeight,
		"manual notice below relay buttons");
}

static void testRelayStatusIndependence()
{
	CompanionManualUploadHudCore::ManualUploadHudView manualView;
	CompanionManualUploadHudCore::SessionBinding binding = {};
	binding.known = true;
	binding.sessionId = "0123456789abcdef0123456789abcdef";
	binding.turn = 3;
	expect(CompanionManualUploadHudCore::parseManualUploadNoticeJson(activeNodeNotice(), binding, &manualView),
		"notice hud independent of relay phase");
	std::string relayLabel;
	OpenXcom::CompanionOnDemandRelayCore::RelayStatusView statusView;
	statusView.parsed = true;
	statusView.phase = "manual_upload_required";
	statusView.outcome = "manual_upload_required";
	OpenXcom::CompanionOnDemandRelayCore::formatRelayStatusLabel(statusView, &relayLabel);
	expect(relayLabel == "Manual upload required", "relay status phase separate label");
	std::string hudText;
	CompanionManualUploadHudCore::formatManualUploadHudText(manualView, &hudText);
	expect(hudText.find("MANUAL UPLOAD REQUIRED") != std::string::npos, "notice hud uses headline");
	expect(hudText.find("Relay delivery") != std::string::npos, "notice hud carries reason");
}

#ifdef main
#undef main
#endif
int main()
{
	const std::string root = "companion_manual_upload_hud_test_tmp";
	COMPANION_MKDIR(root.c_str());
	testEngineNoticeParsing();
	testNodeNoticeParsing();
	testStaleAndInvalidNotices();
	testReadIntegrationWithMarker(root);
	testLayoutDoesNotOverlapRelayControls();
	testRelayStatusIndependence();
	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_manual_upload_hud_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
