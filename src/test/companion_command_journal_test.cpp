/*
 * Open X-Com Companion — tactical command journal serialization tests (M1-27D).
 */
#include "Engine/CompanionCommandJournalCore.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionCommandJournalCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static std::string recoveredJournalFixture()
{
	return std::string(
		"{\n"
		"  \"schema_version\": 1,\n"
		"  \"command_ids\": [\n"
		"    \"a5e3026c541a4ddc9891b31bc590a1f4\",\n"
		"    \"2c0297ea33bd438d87c27d895994a20e\",\n"
		"    \"45284bf3b77c1ae1635cb423bc11ff4d\",\n"
		"    \"2d7c2413ab704257b26dbeb0e9102bc8\",\n"
		"    \"186ff02de5034d3d9c886e1d5552f835\",\n"
		"    \"c4e2b53091f84a669d073ae286513fbc\",\n"
		"    \"383b87f990795a47e038976dd250a845\",\n"
		"    \"cc2f9bd224cd44105264d5aa19cb563f\",\n"
		"    \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n"
		"    \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\n"
		"    \"cccccccccccccccccccccccccccccccc\",\n"
		"    \"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\",\n"
		"    \"ffffffffffffffffffffffffffffffff\",\n"
		"    \"c69b213f1840427d81611e3e99df460a\"\n"
		"  ]\n"
		"}\n");
}

static const char *TEMP_JOURNAL = "companion_command_journal_test.json";

static std::string tempJournalPath()
{
	return TEMP_JOURNAL;
}

static void removeTemp()
{
	std::remove(TEMP_JOURNAL);
}

static void testParseEmptyJournal()
{
	CommandJournalDocument doc = {};
	expect(parseCommandJournalJson(std::string(), &doc), "empty input parses");
	expect(doc.schemaVersion == 1, "empty schema version");
	expect(doc.commandIds.empty(), "empty command_ids");
}

static void testAppendFirstCommand()
{
	removeTemp();
	expect(appendCommandJournalEntry(tempJournalPath(), "11111111111111111111111111111111") == JOURNAL_APPEND_OK,
		"first append ok");
	std::string json;
	{
		std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		json = ss.str();
	}
	CommandJournalDocument doc = {};
	expect(parseCommandJournalJson(json, &doc), "first append valid json");
	expect(doc.commandIds.size() == 1U, "one command id");
	expect(doc.commandIds[0] == "11111111111111111111111111111111", "first id preserved");
	removeTemp();
}

static void testAppendSecondDistinctCommand()
{
	removeTemp();
	expect(appendCommandJournalEntry(tempJournalPath(), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == JOURNAL_APPEND_OK,
		"first id");
	expect(appendCommandJournalEntry(tempJournalPath(), "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") == JOURNAL_APPEND_OK,
		"second id");
	std::string json;
	{
		std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		json = ss.str();
	}
	CommandJournalDocument doc = {};
	expect(parseCommandJournalJson(json, &doc), "two append valid json");
	expect(doc.commandIds.size() == 2U, "two ids");
	expect(doc.commandIds[0] == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "order first");
	expect(doc.commandIds[1] == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "order second");
	removeTemp();
}

static void writeTemp(const std::string &content)
{
	std::ofstream out(TEMP_JOURNAL, std::ios::out | std::ios::trunc | std::ios::binary);
	out << content;
}

static void testAppendManyDistinctCommands()
{
	removeTemp();
	for (int i = 0; i < 100; ++i)
	{
		char id[33] = {};
		std::snprintf(id, sizeof(id), "%032x", i);
		expect(appendCommandJournalEntry(tempJournalPath(), id) == JOURNAL_APPEND_OK, "bulk append");
	}
	CommandJournalDocument doc = {};
	std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	expect(parseCommandJournalJson(ss.str(), &doc), "bulk journal parses");
	expect(doc.commandIds.size() == 100U, "100 unique ids");
	removeTemp();
}

static void testDuplicateReplayDoesNotGrowJournal()
{
	removeTemp();
	const std::string id = "cccccccccccccccccccccccccccccccc";
	expect(appendCommandJournalEntry(tempJournalPath(), id) == JOURNAL_APPEND_OK, "initial");
	expect(appendCommandJournalEntry(tempJournalPath(), id) == JOURNAL_APPEND_DUPLICATE, "duplicate");
	std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	CommandJournalDocument doc = {};
	expect(parseCommandJournalJson(ss.str(), &doc), "duplicate journal parses");
	expect(doc.commandIds.size() == 1U, "duplicate replay size");
	removeTemp();
}

static void testRecoveredJournalStructure()
{
	CommandJournalDocument doc = {};
	const std::string fixture = recoveredJournalFixture();
	expect(parseCommandJournalJson(fixture, &doc), "recovered fixture parses");
	expect(doc.commandIds.size() == 14U, "recovered fixture count");
	expect(doc.commandIds.back() == "c69b213f1840427d81611e3e99df460a", "recovered last id");
}

static void testMalformedJournalFailClosed()
{
	removeTemp();
	writeTemp("{\n  \"schema_version\": 1,\n  \"command_ids\": [\n    \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n    \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\n  ]\n}\n");
	const std::string before = recoveredJournalFixture();
	{
		std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		expect(ss.str().find("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != std::string::npos, "malformed seed present");
	}
	expect(appendCommandJournalEntry(tempJournalPath(), "dddddddddddddddddddddddddddddddd") == JOURNAL_APPEND_MALFORMED_EXISTING,
		"malformed append rejected");
	std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	expect(ss.str().find("dddddddddddddddddddddddddddddddd") == std::string::npos, "malformed file not mutated");
	(void)before;
	removeTemp();
}

static void testPreserveExistingHistoryOnAppend()
{
	removeTemp();
	writeTemp(recoveredJournalFixture());
	expect(appendCommandJournalEntry(tempJournalPath(), "c4e2b53091f84a669d073ae286513fbc") == JOURNAL_APPEND_DUPLICATE,
		"existing terminal id duplicate");
	CommandJournalDocument before = {};
	parseCommandJournalJson(recoveredJournalFixture(), &before);
	expect(appendCommandJournalEntry(tempJournalPath(), "99999999999999999999999999999999") == JOURNAL_APPEND_OK,
		"append fresh id");
	std::ifstream in(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	CommandJournalDocument after = {};
	expect(parseCommandJournalJson(ss.str(), &after), "history append parses");
	expect(after.commandIds.size() == before.commandIds.size() + 1U, "history grew by one");
	for (std::vector<std::string>::size_type i = 0; i < before.commandIds.size(); ++i)
	{
		expect(after.commandIds[i] == before.commandIds[i], "prior history preserved");
	}
	expect(after.commandIds.back() == "99999999999999999999999999999999", "new id appended");
	removeTemp();
}

static void testUnreadableJournalClassification()
{
	const std::string malformed =
		"{\n  \"schema_version\": 1,\n  \"command_ids\": [\n    \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n    \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\n  ]\n}\n";
	expect(classifyExistingJournalJson(std::string()) == JOURNAL_READABLE_EMPTY, "empty journal readable");
	expect(classifyExistingJournalJson(recoveredJournalFixture()) == JOURNAL_READABLE, "recovered journal readable");
	expect(classifyExistingJournalJson(malformed) == JOURNAL_UNREADABLE, "corrupt journal unreadable");
}

static void testMalformedJournalDoesNotSubstringMatch()
{
	const std::string malformed =
		"{\n  \"schema_version\": 1,\n  \"command_ids\": [\n    \"cccccccccccccccccccccccccccccccc\"\n    \"dddddddddddddddddddddddddddddddd\",\n  ]\n}\n";
	expect(!journalJsonContainsCommandId(malformed, "cccccccccccccccccccccccccccccccc"),
		"unreadable journal must not treat ids as present");
	expect(!journalJsonContainsCommandId(malformed, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
		"unknown id absent when journal unreadable");
}

#ifdef _WIN32
static void testFailedAppendPreservesExistingJournal()
{
	removeTemp();
	writeTemp(recoveredJournalFixture());
	std::ifstream beforeIn(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
	std::ostringstream beforeSs;
	beforeSs << beforeIn.rdbuf();
	const std::string before = beforeSs.str();
	SetFileAttributesA(TEMP_JOURNAL, FILE_ATTRIBUTE_READONLY);
	const AppendJournalResult result =
		appendCommandJournalEntry(tempJournalPath(), "99999999999999999999999999999999");
	SetFileAttributesA(TEMP_JOURNAL, FILE_ATTRIBUTE_NORMAL);
	expect(result == JOURNAL_APPEND_WRITE_FAILED, "readonly journal blocks atomic replace");
	std::ifstream afterIn(tempJournalPath().c_str(), std::ios::in | std::ios::binary);
	std::ostringstream afterSs;
	afterSs << afterIn.rdbuf();
	expect(afterSs.str() == before, "failed append preserves journal bytes");
	removeTemp();
}
#endif

int main()
{
	testParseEmptyJournal();
	testAppendFirstCommand();
	testAppendSecondDistinctCommand();
	testAppendManyDistinctCommands();
	testDuplicateReplayDoesNotGrowJournal();
	testRecoveredJournalStructure();
	testMalformedJournalFailClosed();
	testPreserveExistingHistoryOnAppend();
	testUnreadableJournalClassification();
	testMalformedJournalDoesNotSubstringMatch();
#ifdef _WIN32
	testFailedAppendPreservesExistingJournal();
#endif
	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_command_journal_test: all tests passed." << std::endl;
	return 0;
}
