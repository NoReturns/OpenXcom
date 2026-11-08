/*
 * Open X-Com Companion — on-demand relay queue (M1-25E).
 */
#include "Engine/CompanionAutoRetrieveCore.h"
#include "Engine/CompanionOnDemandRelayCore.h"
#include "Engine/CompanionRelayPublicationFreshnessCore.h"
#include "Engine/CompanionOnDemandRelayLayout.h"
#include "Engine/CompanionJsonLite.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define COMPANION_MKDIR(path) _mkdir(path)
#else
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#define COMPANION_MKDIR(path) mkdir(path, 0755)
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionAutoRetrieveCore;
using namespace OpenXcom::CompanionOnDemandRelayCore;
using namespace OpenXcom::CompanionOnDemandRelayLayout;

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

static void removeFileIfPresent(const std::string &path)
{
	std::remove(path.c_str());
}

static std::string pathJoin(const std::string &dir, const std::string &name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	if (dir.empty())
	{
		return name;
	}
	if (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')
	{
		return dir + name;
	}
	return dir + sep + name;
}

static std::string executableDirectory()
{
#ifdef _WIN32
	char path[MAX_PATH];
	const DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return std::string();
	}
	std::string exePath(path, length);
	const std::string::size_type slash = exePath.find_last_of("\\/");
	if (slash == std::string::npos)
	{
		return std::string();
	}
	return exePath.substr(0, slash);
#else
	char path[PATH_MAX];
	const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (length <= 0)
	{
		return std::string();
	}
	path[length] = '\0';
	std::string exePath(path, static_cast<std::string::size_type>(length));
	const std::string::size_type slash = exePath.find_last_of('/');
	if (slash == std::string::npos)
	{
		return std::string();
	}
	return exePath.substr(0, slash);
#endif
}

static void testCompletedRequestMarkerAllowsEnqueue(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-completed-marker.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string statusPath = root + "/companion-on-demand-status.json";
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"d1db250d10568804887d98b01d8dbfbe\",\n"
		"  \"action\": \"send\",\n"
		"  \"status\": \"completed\",\n"
		"  \"created_at_ms\": 1794053391194,\n"
		"  \"send_scope\": \"tactical\",\n"
		"  \"briefing_request_id\": \"19f6175b853fc88b5e90bc2357704bfb\",\n"
		"  \"completed_at_ms\": 1794053392710\n"
		"}\n");
	writeFile(statusPath,
		"{\n"
		"  \"request_id\": \"d1db250d10568804887d98b01d8dbfbe\",\n"
		"  \"action\": \"send\",\n"
		"  \"phase\": \"sent\",\n"
		"  \"message\": \"GitHub state.json updated.\",\n"
		"  \"updated_at_ms\": 1794053392709,\n"
		"  \"terminal\": true\n"
		"}\n");
	std::string requestId;
	std::string error;
	expect(tryEnqueueSend("tactical", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &requestId, &error),
		"completed request marker does not block new send");
	expect(!requestId.empty(), "new request id after completed marker");
	std::string requestBody;
	std::ifstream requestIn(requestPath.c_str());
	std::ostringstream reqSs;
	reqSs << requestIn.rdbuf();
	requestBody = reqSs.str();
	std::string pendingStatus;
	expect(CompanionJsonLite::extractJsonStringField(requestBody, "status", &pendingStatus)
	    && pendingStatus == "pending",
		"replaced request file is pending");
	std::remove(requestPath.c_str());
	std::remove(statusPath.c_str());
}

static void testCompletedMarkerAllowsRetrieveAfterReload(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-reload-marker.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string statusPath = root + "/companion-on-demand-status.json";
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"cccccccccccccccccccccccccccccccc\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"completed\",\n"
		"  \"created_at_ms\": 1000,\n"
		"  \"completed_at_ms\": 2000\n"
		"}\n");
	writeFile(statusPath,
		"{\n"
		"  \"request_id\": \"cccccccccccccccccccccccccccccccc\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"phase\": \"completed\",\n"
		"  \"message\": \"Retrieve complete.\",\n"
		"  \"updated_at_ms\": 2000,\n"
		"  \"terminal\": true\n"
		"}\n");
	std::string requestId;
	std::string error;
	expect(tryEnqueueAction("retrieve", &requestId, &error),
		"completed retrieve marker allows new retrieve after reload");
	expect(requestId != "cccccccccccccccccccccccccccccccc", "new retrieve request id");
	std::remove(requestPath.c_str());
	std::remove(statusPath.c_str());
}

static void testEnqueueAndDuplicateGuard(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif

	std::string requestId;
	std::string error;
	expect(tryEnqueueSend("tactical", "0123456789abcdef0123456789abcdef", &requestId, &error), "tactical send enqueue succeeds");
	expect(!requestId.empty(), "request id assigned");

	std::string requestId2;
	expect(!tryEnqueueSend("tactical", "fedcba9876543210fedcba9876543210", &requestId2, &error), "duplicate enqueue blocked while pending");
	expect(error == "request_in_flight", "duplicate error reason");

	std::string requestPath;
	std::string statusPath;
	resolveRequestPath(&requestPath);
	resolveStatusPath(&statusPath);
	std::remove(requestPath.c_str());
	writeFile(statusPath, "{\n  \"action\": \"send\",\n  \"phase\": \"sending\",\n  \"terminal\": false\n}\n");
	expect(!tryEnqueueAction("retrieve", &requestId2, &error), "non-terminal status blocks enqueue");
	std::remove(statusPath.c_str());
}

static void testStatusPhaseMapping()
{
	expect(mapPhaseString("no_new_orders", "retrieve") == RELAY_UI_NO_NEW_ORDERS, "no_new_orders maps");
	expect(mapPhaseString("manual_upload_required", "send") == RELAY_UI_MANUAL_UPLOAD_REQUIRED, "manual_upload_required maps");
	expect(mapPhaseString("mystery_phase", "send") == RELAY_UI_UNKNOWN, "unknown phase maps safely");
	expect(isRelayBusyPhase(RELAY_UI_SENDING), "sending is busy");
	expect(!isRelayBusyPhase(RELAY_UI_SENT), "sent is not busy");
	std::string label;
	formatStatusLabel(RELAY_UI_NO_NEW_ORDERS, &label);
	expect(label == "No new orders", "status label");
	formatStatusLabel(RELAY_UI_SENT, &label);
	expect(label == "Published", "sent label uses Published");
}

static std::string labelFromStatusJson(const std::string &json)
{
	RelayStatusView view;
	std::string label;
	parseRelayStatusJson(json, &view);
	formatRelayStatusLabel(view, &label);
	return label;
}

static void testRelayStatusLabels()
{
	expect(labelFromStatusJson(
		"{\"phase\":\"sent\",\"terminal\":true,\"outcome\":\"published\"}") == "Published",
		"published outcome");
	expect(labelFromStatusJson(
		"{\"phase\":\"sent\",\"terminal\":true,\"outcome\":\"skipped_unchanged\"}") == "Unchanged",
		"skipped unchanged");
	expect(labelFromStatusJson(
		"{\"phase\":\"failed\",\"terminal\":true,\"error_code\":\"SNAPSHOT_VALIDATION\"}") == "Failed: VALIDATION",
		"validation failure");
	expect(labelFromStatusJson(
		"{\"phase\":\"failed\",\"terminal\":true,\"error_code\":\"AUTH_FAILED\"}") == "Failed: AUTH",
		"auth failure");
	expect(labelFromStatusJson(
		"{\"phase\":\"sending\",\"terminal\":false,\"outcome\":\"deferred\",\"error_code\":\"RATE_LIMIT\"}")
	    == "Deferred: RATE LIMIT",
		"rate limit deferral");
	expect(labelFromStatusJson(
		"{\"phase\":\"manual_upload_required\",\"terminal\":true,\"error_code\":\"RELAY_SHRINK_EXHAUSTED\"}")
	    == "Manual upload required",
		"manual upload required");
	expect(labelFromStatusJson(
		"{\"phase\":\"not_ready\",\"terminal\":true}") == "Not ready",
		"not ready");
	expect(labelFromStatusJson(
		"{\"phase\":\"unknown_future_phase\",\"terminal\":true}") == "Status update",
		"unknown future phase fallback");
	expect(labelFromStatusJson(
		"{\"action\":\"retrieve\",\"phase\":\"completed\",\"terminal\":true,"
		"\"last_command_status\":\"COMPLETED\"}") == "Order completed",
		"retrieve command completed");
	expect(labelFromStatusJson(
		"{\"action\":\"retrieve\",\"phase\":\"completed\",\"terminal\":true,"
		"\"last_command_status\":\"FAILED\"}") == "Order failed",
		"retrieve command failed");
	expect(labelFromStatusJson(
		"{\"action\":\"retrieve\",\"phase\":\"failed\",\"terminal\":true,"
		"\"error_code\":\"NETWORK_FAILED\"}") == "Retrieve error",
		"retrieve network failure");
	expect(labelFromStatusJson(
		"{\"action\":\"retrieve\",\"phase\":\"failed\",\"terminal\":true,"
		"\"error_code\":\"RETRIEVE_INCOMPLETE\"}") == "Retrieve incomplete",
		"retrieve incomplete");
}

static void testLegacyStatusJsonWithoutOptionalFields()
{
	expect(labelFromStatusJson(
		"{\"phase\":\"sending\",\"terminal\":false}") == "Sending",
		"legacy sending");
	expect(labelFromStatusJson(
		"{\"phase\":\"sent\",\"terminal\":true}") == "Published",
		"legacy sent without outcome");
	RelayStatusView view;
	expect(parseRelayStatusJson("{\"phase\":\"failed\",\"terminal\":true}", &view), "legacy parse");
	expect(view.outcome.empty() && view.error_code.empty(), "optional fields absent");
	expect(labelFromStatusJson("{\"phase\":\"failed\",\"terminal\":true}") == "Failed", "legacy generic failed");
}

static void testRelayStatusStaleAndPersistence()
{
	RelayStatusView newerFailure;
	parseRelayStatusJson(
		"{\"request_id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"phase\":\"failed\","
		"\"terminal\":true,\"error_code\":\"AUTH_FAILED\",\"updated_at_ms\":2000}",
		&newerFailure);
	RelayStatusView olderSuccess;
	parseRelayStatusJson(
		"{\"request_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"phase\":\"sent\","
		"\"terminal\":true,\"updated_at_ms\":1000}",
		&olderSuccess);
	expect(!shouldAcceptRelayStatusUpdate(newerFailure, olderSuccess, true),
		"older success cannot replace newer failure");
	expect(shouldAcceptRelayStatusUpdate(olderSuccess, newerFailure, true),
		"newer failure replaces older success");
	RelayStatusView sameRequestDefer;
	parseRelayStatusJson(
		"{\"request_id\":\"cccccccccccccccccccccccccccccccc\",\"phase\":\"sending\","
		"\"outcome\":\"deferred\",\"terminal\":false,\"updated_at_ms\":3000}",
		&sameRequestDefer);
	RelayStatusView sameRequestFailed;
	parseRelayStatusJson(
		"{\"request_id\":\"cccccccccccccccccccccccccccccccc\",\"phase\":\"failed\","
		"\"terminal\":true,\"updated_at_ms\":2500}",
		&sameRequestFailed);
	expect(shouldAcceptRelayStatusUpdate(sameRequestFailed, sameRequestDefer, true),
		"same request defer update allowed");
}

static void testRelayHitTestRequiresVisibleButtons(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-hit.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	CompactBarLayout layout;
	expect(computeBaseViewportBarLayout(0, BASESCAPE_VIEWPORT_TOP_Y, BASESCAPE_VIEWPORT_WIDTH, &layout), "base bar layout");
	expect(!relayControlRectHitTest(layout.sendX + 1, layout.sendY + 1, layout.sendX, layout.sendY,
		COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, false),
		"hidden relay rect does not hit-test");
	expect(relayControlRectHitTest(layout.sendX + 1, layout.sendY + 1, layout.sendX, layout.sendY,
		COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true),
		"visible relay rect hit-test");
}

static void testRelayHitTargetUsesCompactBounds(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-hit-target.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	CompactBarLayout layout;
	expect(computeBattlescapeTopBarLayout(320, 200, &layout), "battlescape layout for hit target");
	const int pastRetrieveRight = layout.retrieveX + COMPACT_BUTTON_WIDTH + 8;
	expect(relayControlHitTarget(pastRetrieveRight, layout.retrieveY + 1,
		layout.sendX, layout.sendY, COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true,
		layout.retrieveX, layout.retrieveY, COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true)
	    == RELAY_CONTROL_HIT_NONE,
		"click beyond compact retrieve width must not hit relay controls");
	expect(relayControlHitTarget(layout.retrieveX + COMPACT_BUTTON_WIDTH - 1, layout.retrieveY + 1,
		layout.sendX, layout.sendY, COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true,
		layout.retrieveX, layout.retrieveY, COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true)
	    == RELAY_CONTROL_HIT_RETRIEVE,
		"retrieve edge uses compact width");
	expect(relayControlHitTarget(layout.sendX + 1, layout.sendY + 1,
		layout.sendX, layout.sendY, COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true,
		layout.retrieveX, layout.retrieveY, COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, true)
	    == RELAY_CONTROL_HIT_SEND,
		"send hit takes precedence outside retrieve bounds");
}

static void testCompactBarLayoutDimensions()
{
	expect(compactBarFitsBaseViewport(BASESCAPE_VIEWPORT_WIDTH), "108px bar fits 192px base viewport");
	CompactBarLayout baseLayout;
	expect(computeBaseViewportBarLayout(0, BASESCAPE_VIEWPORT_TOP_Y, BASESCAPE_VIEWPORT_WIDTH, &baseLayout), "base layout computes");
	expect(baseLayout.sendY == 0, "base strip anchors at y=0 when viewport top is y=8");
	expect(baseLayout.sendX == 42, "base bar centered at x=42 on 192px viewport");
	expect(baseLayout.retrieveX == baseLayout.sendX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP, "retrieve follows send");
	expect(baseLayout.statusY == baseLayout.sendY + COMPACT_BUTTON_HEIGHT + 1, "status below buttons");
	expect(baseLayout.statusWidth == BASESCAPE_VIEWPORT_WIDTH, "status width fits 192px viewport");

	CompactBarLayout battleLayout;
	expect(computeBattlescapeTopBarLayout(320, 200, &battleLayout), "battlescape layout at 320px");
	expect(battleLayout.sendY == BATTLESCAPE_TOP_BAR_Y, "battlescape top bar y");
	expect(battleLayout.sendX == 46, "battlescape send anchors clustered bar at 320px width");
	expect(battleLayout.autoToggleX == battleLayout.retrieveX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP,
		"auto row follows retrieve");
	expect(battleLayout.statusWidth == 200, "battlescape status width at 320px");
	expect(battleLayout.statusX == 60, "battlescape status centered");
	expect(battleLayout.manualNoticeY == battleLayout.statusY + RELAY_STATUS_HEIGHT + 1,
		"battlescape manual notice below status");
}

static void testReconcileSupersededBriefing(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string relayDir = root;
	const std::string requestPath = relayDir + "/companion-on-demand-request.json";
	const std::string statusPath = relayDir + "/companion-on-demand-status.json";
	const std::string briefingPath = relayDir + "/briefing-request.json";
	writeFile(briefingPath, "{\n  \"request_id\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\n}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n"
		"  \"action\": \"send\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1,\n"
		"  \"briefing_request_id\": \"cccccccccccccccccccccccccccccccc\"\n"
		"}\n");
	const long long wallNowMs = 5000;
	expect(reconcilePendingOnDemandRelay(wallNowMs), "superseded briefing terminates pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "pending request file removed");
	std::string statusBody;
	std::ifstream statusIn(statusPath.c_str());
	std::ostringstream ss;
	ss << statusIn.rdbuf();
	statusBody = ss.str();
	std::string phase;
	bool terminal = false;
	readStatusPhase(statusBody, &phase, &terminal);
	expect(phase == "failed", "superseded status phase failed");
	expect(terminal, "superseded status terminal");
}

static void testReconcileStaleHealthOffline(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-stale-health.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string readyPath = root + "/.companion-on-demand-daemon-ready.json";
	const std::string healthPath = root + "/.companion-on-demand-daemon-health.json";
	writeFile(readyPath,
		"{\n  \"pid\": 99999,\n  \"ready_at_ms\": 1\n}\n");
	writeFile(healthPath,
		"{\n  \"schema_version\": 1,\n  \"pid\": 99999,\n  \"state\": \"ready\",\n  \"updated_at_ms\": 1000\n}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1000\n"
		"}\n");
	expect(reconcilePendingOnDemandRelay(6000), "stale daemon health terminates pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "stale health pending request removed");
}

static void testReconcileOfflineRelay(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live-offline.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string requestPath = root + "/companion-on-demand-request.json";
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"dddddddddddddddddddddddddddddddd\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1000\n"
		"}\n");
	expect(reconcilePendingOnDemandRelay(4000), "offline relay terminates pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "offline pending request removed");
}

static const long long kEpochMs = 1793814448770LL;

static void setupRelayEnv(const std::string &root, const std::string &snapshotName)
{
	const std::string snapshotPath = root + "/" + snapshotName;
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
}

static void testReconcileFreshHealthRealisticEpoch(const std::string &root)
{
	setupRelayEnv(root, "tactical-fresh-health.yaml");
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string healthPath = root + "/.companion-on-demand-daemon-health.json";
	writeFile(healthPath,
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"pid\": 11492,\n"
		"  \"state\": \"ready\",\n"
		"  \"updated_at_ms\": 1793814448770\n"
		"}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"11111111111111111111111111111111\",\n"
		"  \"action\": \"send\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1793814445000\n"
		"}\n");
	const long long wallNowMs = kEpochMs + 1000;
	expect(!reconcilePendingOnDemandRelay(wallNowMs), "fresh realistic health keeps pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(pendingCheck.good(), "pending request still present with fresh health");
}

static void testReconcileStaleHealthRealisticEpoch(const std::string &root)
{
	setupRelayEnv(root, "tactical-stale-realistic.yaml");
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string healthPath = root + "/.companion-on-demand-daemon-health.json";
	writeFile(healthPath,
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"pid\": 11492,\n"
		"  \"state\": \"ready\",\n"
		"  \"updated_at_ms\": 1793814448770\n"
		"}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"22222222222222222222222222222222\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1793814445000\n"
		"}\n");
	const long long wallNowMs = kEpochMs + 5000;
	expect(reconcilePendingOnDemandRelay(wallNowMs), "stale realistic health terminates pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "stale realistic pending request removed");
}

static void testReconcileBusyHealthRealisticEpoch(const std::string &root)
{
	setupRelayEnv(root, "tactical-busy-health.yaml");
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string healthPath = root + "/.companion-on-demand-daemon-health.json";
	writeFile(healthPath,
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"pid\": 11492,\n"
		"  \"state\": \"busy\",\n"
		"  \"updated_at_ms\": 1793814448770\n"
		"}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"33333333333333333333333333333333\",\n"
		"  \"action\": \"send\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1793814445000\n"
		"}\n");
	expect(!reconcilePendingOnDemandRelay(kEpochMs + 500), "busy realistic health keeps pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(pendingCheck.good(), "pending request still present with busy health");
}

static void testReconcileFutureHealthTimestampFailsClosed(const std::string &root)
{
	setupRelayEnv(root, "tactical-future-health.yaml");
	const std::string requestPath = root + "/companion-on-demand-request.json";
	const std::string healthPath = root + "/.companion-on-demand-daemon-health.json";
	writeFile(healthPath,
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"pid\": 11492,\n"
		"  \"state\": \"ready\",\n"
		"  \"updated_at_ms\": 1793814458770\n"
		"}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"44444444444444444444444444444444\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1793814445000\n"
		"}\n");
	expect(reconcilePendingOnDemandRelay(kEpochMs + 1000), "future health timestamp fails closed");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "future health pending request removed");
}

static void testReconcileStartupGraceRealisticEpoch(const std::string &root)
{
	setupRelayEnv(root, "tactical-grace.yaml");
	const std::string requestPath = root + "/companion-on-demand-request.json";
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"55555555555555555555555555555555\",\n"
		"  \"action\": \"send\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1793814448770\n"
		"}\n");
	expect(!reconcilePendingOnDemandRelay(kEpochMs + 2000), "realistic startup grace suppresses offline failure");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(pendingCheck.good(), "pending request preserved during startup grace");
}

static void testReconcileStartupGraceExpiredRealisticEpoch(const std::string &root)
{
	setupRelayEnv(root, "tactical-grace-expired.yaml");
	const std::string requestPath = root + "/companion-on-demand-request.json";
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"66666666666666666666666666666666\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1793814448770\n"
		"}\n");
	expect(reconcilePendingOnDemandRelay(kEpochMs + 3000), "expired startup grace with no health fails closed");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "pending request removed after grace without health");
}

static void testJsonInt64ExtractionUsedByRelayPath()
{
	long long parsed = 0;
	expect(CompanionJsonLite::extractJsonInt64Field(
		"{\"created_at_ms\":1793814448770}", "created_at_ms", &parsed),
		"relay path int64 extraction");
	expect(parsed == kEpochMs, "relay path int64 value");
}

static void testAutoRetrieveDefaultsAndBattlescapeScope()
{
	resetForNewGameSession();
	expect(!isEnabled(), "default auto retrieve off");
	expect(intervalSec() == AUTO_RETRIEVE_INTERVAL_DEFAULT_SEC, "default interval");
	expect(!isBattlescapeActive(), "inactive until battlescape enter");

	enterBattlescape();
	expect(!isEnabled(), "battlescape enter keeps auto off");
	expect(isBattlescapeActive(), "battlescape active after enter");

	setEnabled(true);
	expect(isEnabled(), "toggle on in battlescape");
	leaveBattlescape();
	expect(!isEnabled(), "leave battlescape disables auto");
	expect(!isBattlescapeActive(), "leave clears battlescape active");
	setEnabled(true);
	expect(!isEnabled(), "cannot enable auto outside battlescape");
}

static void testAutoRetrieveScheduling()
{
	resetForNewGameSession();
	enterBattlescape();
	const long long t0 = 100000;
	expect(!shouldAttemptAutoRetrieve(RELAY_UI_IDLE, t0), "off skips before enable");
	setEnabled(true);
	expect(shouldAttemptAutoRetrieve(RELAY_UI_IDLE, t0), "first eligible when on");
	noteAutoRetrieveAttempt(t0);
	expect(!shouldAttemptAutoRetrieve(RELAY_UI_IDLE, t0 + 1000), "interval not elapsed");
	expect(shouldAttemptAutoRetrieve(RELAY_UI_IDLE, t0 + 30000), "interval elapsed");
	expect(!shouldAttemptAutoRetrieve(RELAY_UI_RETRIEVING, t0 + 40000), "busy retrieving blocks");
	expect(!shouldAttemptAutoRetrieve(RELAY_UI_SENDING, t0 + 40000), "busy sending blocks");
	expect(!shouldAttemptAutoRetrieve(RELAY_UI_DEFERRED, t0 + 40000), "deferred blocks");
	setEnabled(false);
	expect(!shouldAttemptAutoRetrieve(RELAY_UI_IDLE, t0 + 60000), "disabled stops scheduling");
}

static void testAutoRetrieveIntervalAdjustment()
{
	resetForNewGameSession();
	enterBattlescape();
	expect(intervalSec() == 30, "baseline interval");
	adjustIntervalSec(1);
	expect(intervalSec() == 45, "interval step up");
	adjustIntervalSec(-1);
	expect(intervalSec() == 30, "interval step down");
	for (int i = 0; i < 20; ++i)
	{
		adjustIntervalSec(1);
	}
	expect(intervalSec() == AUTO_RETRIEVE_INTERVAL_MAX_SEC, "interval clamped max");
	for (int i = 0; i < 20; ++i)
	{
		adjustIntervalSec(-1);
	}
	expect(intervalSec() == AUTO_RETRIEVE_INTERVAL_MIN_SEC, "interval clamped min");
	std::string label;
	formatIntervalLabel(30, &label);
	expect(label == "30s", "interval label");
	formatToggleButtonText(true, &label);
	expect(label == "AUTO:ON", "toggle on label");
	formatToggleButtonText(false, &label);
	expect(label == "AUTO:OFF", "toggle off label");
}

static bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	if (ax + aw <= bx || bx + bw <= ax)
	{
		return false;
	}
	if (ay + ah <= by || by + bh <= ay)
	{
		return false;
	}
	return true;
}

static void testBattlescapeLayoutAutoRetrieveRow()
{
	CompactBarLayout battleLayout;
	expect(computeBattlescapeTopBarLayout(320, 200, &battleLayout), "battlescape layout with auto row");
	expect(battleLayout.autoRowY == BATTLESCAPE_TOP_BAR_Y, "auto row on top bar with send/retrieve");
	expect(battleLayout.autoToggleX == battleLayout.retrieveX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP,
		"auto toggle adjacent to retrieve");
	expect(battleLayout.autoIntervalPlusX > battleLayout.autoIntervalTextX, "plus after interval text");
	const int autoRight = battleLayout.autoIntervalPlusX + AUTO_RETRIEVE_STEP_BUTTON_WIDTH;
	const int topBarBottom = battleLayout.autoRowY + COMPACT_BUTTON_HEIGHT;
	const int iconBarTopY = 200 - 56;
	expect(topBarBottom < iconBarTopY, "top relay cluster clears soldier icon bar");
	expect(!rectsOverlap(battleLayout.autoToggleX, battleLayout.autoRowY, autoRight - battleLayout.autoToggleX,
		COMPACT_BUTTON_HEIGHT, 48, iconBarTopY, 224, 56),
		"auto row does not overlap primary soldier control band at 320x200");
	const struct { int w; int h; } resolutions[] = {
		{320, 200},
		{640, 400},
		{960, 600},
	};
	for (const auto &res : resolutions)
	{
		CompactBarLayout layout;
		expect(computeBattlescapeTopBarLayout(res.w, res.h, &layout),
			"battlescape layout at reference resolution");
		const int clusterRight = layout.autoIntervalPlusX + AUTO_RETRIEVE_STEP_BUTTON_WIDTH;
		expect(layout.autoRowY == BATTLESCAPE_TOP_BAR_Y, "auto row y stable across resolutions");
		expect(clusterRight <= res.w - 36, "cluster fits reserved right margin");
		expect(layout.sendY == layout.autoRowY, "send and auto share top row");
	}
}

static void testSendFreshnessManualMutationNeedsBlue()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.sessionId = "0123456789abcdef0123456789abcdef";
	input.live.turn = 3;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"manual tactical mutation needs publication");
}

static void testSendFreshnessExportWithoutConfirmStillBlue()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	const std::string sha = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	noteSnapshotExportBinding(14, sha, "0123456789abcdef0123456789abcdef", 3);
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.sessionId = "0123456789abcdef0123456789abcdef";
	input.live.turn = 3;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	input.lastExport = getLastExportBinding();
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"export alone does not clear publication-needed state");
}

static void testSendFreshnessConfirmedInSyncAfterManualSend(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	const std::string sha = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
	const std::string session = "0123456789abcdef0123456789abcdef";
	noteSnapshotExportBinding(14, sha, session, 3);
	TacticalBindingSnapshot live = {};
	live.sessionId = session;
	live.turn = 3;
	live.mutationGeneration = getTacticalMutationGeneration();
	removeFileIfPresent(root + "/companion-tactical-publication-confirm.json");
	writeFile(root + "/briefing-ack.json",
		"{\n"
		"  \"status\": \"published\",\n"
		"  \"session_id\": \"" + session + "\",\n"
		"  \"turn\": 3,\n"
		"  \"snapshot_sha256\": \"" + sha + "\"\n"
		"}\n");
	ConfirmedPublicationView confirmed;
	expect(loadConfirmedPublicationForLiveScope(root, live, getLastExportBinding(), &confirmed),
		"ack file loads for confirm");
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live = live;
	input.lastExport = getLastExportBinding();
	input.confirmed = confirmed;
	expect(getMutationAtLastConfirmed() == input.lastExport.mutationGeneration,
		"confirm binds export mutation generation");
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NORMAL,
		"confirmed publication matching export is normal");
}

static void testSendFreshnessManualStandAfterConfirmBlueAgain(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	const std::string sha = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
	const std::string session = "0123456789abcdef0123456789abcdef";
	noteSnapshotExportBinding(14, sha, session, 3);
	TacticalBindingSnapshot live = {};
	live.sessionId = session;
	live.turn = 3;
	live.mutationGeneration = getTacticalMutationGeneration();
	removeFileIfPresent(root + "/companion-tactical-publication-confirm.json");
	writeFile(root + "/briefing-ack.json",
		"{\n"
		"  \"status\": \"published\",\n"
		"  \"session_id\": \"" + session + "\",\n"
		"  \"turn\": 3,\n"
		"  \"snapshot_sha256\": \"" + sha + "\"\n"
		"}\n");
	ConfirmedPublicationView confirmed;
	loadConfirmedPublicationForLiveScope(root, live, getLastExportBinding(), &confirmed);
	noteTacticalBattleMutation();
	live.mutationGeneration = getTacticalMutationGeneration();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live = live;
	input.lastExport = getLastExportBinding();
	input.confirmed = confirmed;
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"manual stand after confirm needs blue again");
}

static void testSendFreshnessMutatedAfterExportNeedsBlue()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	const std::string sha = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	noteSnapshotExportBinding(14, sha, "0123456789abcdef0123456789abcdef", 3);
	noteTacticalBattleMutation();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.sessionId = "0123456789abcdef0123456789abcdef";
	input.live.turn = 3;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	input.lastExport = getLastExportBinding();
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"mutation after export needs blue");
}

static void testSendFreshnessStaleAckIgnored(const std::string & /*root*/)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	const std::string sha = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
	noteSnapshotExportBinding(16, sha, "0123456789abcdef0123456789abcdef", 3);
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.sessionId = "0123456789abcdef0123456789abcdef";
	input.live.turn = 3;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	input.lastExport = getLastExportBinding();
	input.confirmed.parsed = true;
	input.confirmed.terminalSuccess = true;
	input.confirmed.sessionId = "ffffffffffffffffffffffffffffffff";
	input.confirmed.turn = 2;
	input.confirmed.snapshotSha256 = sha;
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"wrong session ack never treated as synchronized");
}

static void testSendFreshnessRelayBusyNotBlue()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.relayBusy = suppressSendPublicationHighlight(RELAY_UI_SENDING);
	input.live.mutationGeneration = getTacticalMutationGeneration();
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NORMAL,
		"in-flight send suppresses needs-publication highlight");
}

static void testSendFreshnessInitialNoMutationGreyContract()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.relayBusy = suppressSendPublicationHighlight(RELAY_UI_IDLE);
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NORMAL,
		"initial battlescape with no tactical mutation is normal");
	expect(!suppressSendPublicationHighlight(RELAY_UI_IDLE), "idle does not suppress highlight");
}

static void testSendFreshnessTerminalSentDoesNotSuppressHighlight(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	const std::string sha = "fafafafafafafafafafafafafafafafafafafafafafafafafafafafafafafafa";
	const std::string session = "0123456789abcdef0123456789abcdef";
	noteTacticalBattleMutation();
	noteSnapshotExportBinding(20, sha, session, 4);
	writeFile(root + "/briefing-ack.json",
		"{\n"
		"  \"status\": \"published\",\n"
		"  \"session_id\": \"" + session + "\",\n"
		"  \"turn\": 4,\n"
		"  \"snapshot_sha256\": \"" + sha + "\"\n"
		"}\n");
	TacticalBindingSnapshot live = {};
	live.sessionId = session;
	live.turn = 4;
	live.mutationGeneration = getTacticalMutationGeneration();
	ConfirmedPublicationView confirmed;
	expect(loadConfirmedPublicationForLiveScope(root, live, getLastExportBinding(), &confirmed),
		"synced export before post-send mutation");
	noteTacticalBattleMutation();
	live.mutationGeneration = getTacticalMutationGeneration();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live = live;
	input.lastExport = getLastExportBinding();
	input.confirmed = confirmed;
	input.relayBusy = suppressSendPublicationHighlight(RELAY_UI_SENT);
	expect(!suppressSendPublicationHighlight(RELAY_UI_SENT),
		"terminal Published status is not highlight-suppressed");
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"new change after sent terminal status needs publication");
}

static void testSendFreshnessFailedWithStaleAckStillNeedsHighlight()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	const std::string oldSha = "1212121212121212121212121212121212121212121212121212121212121212";
	const std::string newSha = "2323232323232323232323232323232323232323232323232323232323232323232";
	const std::string session = "0123456789abcdef0123456789abcdef";
	noteTacticalBattleMutation();
	noteSnapshotExportBinding(22, newSha, session, 5);
	ConfirmedPublicationView staleAck = {};
	staleAck.parsed = true;
	staleAck.terminalSuccess = true;
	staleAck.sessionId = session;
	staleAck.turn = 5;
	staleAck.snapshotSha256 = oldSha;
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.sessionId = session;
	input.live.turn = 5;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	input.lastExport = getLastExportBinding();
	input.confirmed = staleAck;
	input.relayBusy = suppressSendPublicationHighlight(RELAY_UI_SEND_FAILED);
	expect(!suppressSendPublicationHighlight(RELAY_UI_SEND_FAILED), "send failed is not busy");
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"failed publish with export ahead of ack needs publication");
}

static void testSendFreshnessDeferredSendingSuppressesHighlight()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	input.relayBusy = suppressSendPublicationHighlight(RELAY_UI_DEFERRED);
	expect(suppressSendPublicationHighlight(RELAY_UI_DEFERRED), "deferred rate limit suppresses highlight");
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NORMAL,
		"deferred send shows normal colour while status explains delay");
}

static void testSendFreshnessNewSessionResetsBinding(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	const std::string sha = "3434343434343434343434343434343434343434343434343434343434343434";
	const std::string session = "0123456789abcdef0123456789abcdef";
	noteTacticalBattleMutation();
	noteSnapshotExportBinding(18, sha, session, 2);
	writeFile(root + "/briefing-ack.json",
		"{\n"
		"  \"status\": \"published\",\n"
		"  \"session_id\": \"" + session + "\",\n"
		"  \"turn\": 2,\n"
		"  \"snapshot_sha256\": \"" + sha + "\"\n"
		"}\n");
	TacticalBindingSnapshot live = {};
	live.sessionId = session;
	live.turn = 2;
	live.mutationGeneration = getTacticalMutationGeneration();
	ConfirmedPublicationView confirmed;
	expect(loadConfirmedPublicationForLiveScope(root, live, getLastExportBinding(), &confirmed),
		"session reset test: confirm loads");
	expect(getMutationAtLastConfirmed() > 0, "precondition: confirmed mutation generation bound");
	onBattleSessionContextChanged();
	expect(getTacticalMutationGeneration() == 0, "new session resets mutation generation");
	expect(getMutationAtLastConfirmed() == 0, "new session clears last-confirmed mutation");
}

static void testSendFreshnessMutationTrackedWithoutLiveSessionId()
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	noteTacticalBattleMutation();
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live.mutationGeneration = getTacticalMutationGeneration();
	input.relayBusy = suppressSendPublicationHighlight(RELAY_UI_IDLE);
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION,
		"mutation without export needs publication even when session id not wired");
}

/**
 * Recorded regression (manual verify later): popState sets actionFailed when action.result is shown to
 * the player, so we do not bump tactical mutation even if the battle state changed slightly (partial TU,
 * facing, etc.). Until a finer hook exists, SEND may not turn blue for that delta alone; confirm via
 * snapshot export vs GitHub after replaying the aborted-walk scenario (cf. movement stop diagnostics).
 */
static void testSendFreshnessRegressionFailedActionPartialMutation(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	const std::string sha = "abababababababababababababababababababababababababababababababab";
	const std::string session = "0123456789abcdef0123456789abcdef";
	noteTacticalBattleMutation();
	noteSnapshotExportBinding(10, sha, session, 2);
	TacticalBindingSnapshot live = {};
	live.sessionId = session;
	live.turn = 2;
	live.mutationGeneration = getTacticalMutationGeneration();
	removeFileIfPresent(pathJoin(root, "companion-tactical-publication-confirm.json"));
	writeFile(pathJoin(root, "briefing-ack.json"),
		"{\n"
		"  \"status\": \"published\",\n"
		"  \"session_id\": \"" + session + "\",\n"
		"  \"turn\": 2,\n"
		"  \"snapshot_sha256\": \"" + sha + "\"\n"
		"}\n");
	ConfirmedPublicationView confirmed;
	expect(loadConfirmedPublicationForLiveScope(root, live, getLastExportBinding(), &confirmed),
		"REGRESSION partial-mutation: synchronized before hypothetical failed popState");
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = true;
	input.live = live;
	input.lastExport = getLastExportBinding();
	input.confirmed = confirmed;
	expect(evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NORMAL,
		"REGRESSION partial-mutation: no generation bump if popState actionFailed; re-verify export manually");
}

static void testNotePlayerMutationOnlyWhenSnapshotConfigured(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	resetConfirmedPublicationBinding();
	const std::string snapshotPath = root + "/tactical-mutation.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	notePlayerTacticalMutationIfSnapshotConfigured();
	expect(getTacticalMutationGeneration() == 1, "mutation increments when companion configured");
}

static void testPublicationConfirmFileMerge(const std::string &root)
{
	using namespace CompanionRelayPublicationFreshnessCore;
	const std::string snapshotPath = root + "/tactical-fresh.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	writeFile(root + "/companion-tactical-publication-confirm.json",
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"session_id\": \"0123456789abcdef0123456789abcdef\",\n"
		"  \"turn\": 3,\n"
		"  \"state_version\": 15,\n"
		"  \"snapshot_sha256\": \"abababababababababababababababababababababababababababababababab\",\n"
		"  \"source\": \"post_command\",\n"
		"  \"status\": \"published\"\n"
		"}\n");
	ConfirmedPublicationView confirm;
	expect(readTacticalPublicationConfirmFile(root, &confirm), "publication confirm file parses");
	expect(confirm.stateVersion == 15, "publication confirm state_version");
}

int main()
{
	const std::string exeDir = executableDirectory();
	const std::string root = exeDir.empty()
		? std::string("companion_on_demand_relay_test_tmp")
		: pathJoin(exeDir, "companion_on_demand_relay_test_tmp");
	COMPANION_MKDIR(root.c_str());
	removeFileIfPresent(pathJoin(root, "companion-tactical-publication-confirm.json"));
	removeFileIfPresent(pathJoin(root, "briefing-ack.json"));
	testEnqueueAndDuplicateGuard(root);
	testCompletedRequestMarkerAllowsEnqueue(root);
	testCompletedMarkerAllowsRetrieveAfterReload(root);
	testStatusPhaseMapping();
	testRelayStatusLabels();
	testLegacyStatusJsonWithoutOptionalFields();
	testRelayStatusStaleAndPersistence();
	testCompactBarLayoutDimensions();
	testRelayHitTestRequiresVisibleButtons(root);
	testRelayHitTargetUsesCompactBounds(root);
	testReconcileSupersededBriefing(root);
	testReconcileOfflineRelay(root);
	testReconcileStaleHealthOffline(root);
	testJsonInt64ExtractionUsedByRelayPath();
	testReconcileFreshHealthRealisticEpoch(root);
	testReconcileStaleHealthRealisticEpoch(root);
	testReconcileBusyHealthRealisticEpoch(root);
	testReconcileFutureHealthTimestampFailsClosed(root);
	testReconcileStartupGraceRealisticEpoch(root);
	testReconcileStartupGraceExpiredRealisticEpoch(root);
	testAutoRetrieveDefaultsAndBattlescapeScope();
	testAutoRetrieveScheduling();
	testAutoRetrieveIntervalAdjustment();
	testBattlescapeLayoutAutoRetrieveRow();
	testSendFreshnessManualMutationNeedsBlue();
	testSendFreshnessExportWithoutConfirmStillBlue();
	testSendFreshnessConfirmedInSyncAfterManualSend(root);
	testSendFreshnessManualStandAfterConfirmBlueAgain(root);
	testSendFreshnessMutatedAfterExportNeedsBlue();
	testSendFreshnessStaleAckIgnored(root);
	testSendFreshnessRelayBusyNotBlue();
	testSendFreshnessInitialNoMutationGreyContract();
	testSendFreshnessTerminalSentDoesNotSuppressHighlight(root);
	testSendFreshnessFailedWithStaleAckStillNeedsHighlight();
	testSendFreshnessDeferredSendingSuppressesHighlight();
	testSendFreshnessNewSessionResetsBinding(root);
	testSendFreshnessMutationTrackedWithoutLiveSessionId();
	testSendFreshnessRegressionFailedActionPartialMutation(root);
	testPublicationConfirmFileMerge(root);
	testNotePlayerMutationOnlyWhenSnapshotConfigured(root);
	std::cout << (g_failures == 0 ? "OK" : "FAILED") << std::endl;
	return g_failures == 0 ? 0 : 1;
}
