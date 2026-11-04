#pragma once
/*
 * Open X-Com Companion — tactical command journal JSON (M1-27D).
 */
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionCommandJournalCore
{

enum AppendJournalResult
{
	JOURNAL_APPEND_OK,
	JOURNAL_APPEND_DUPLICATE,
	JOURNAL_APPEND_INVALID_COMMAND_ID,
	JOURNAL_APPEND_MALFORMED_EXISTING,
	JOURNAL_APPEND_WRITE_FAILED
};

struct CommandJournalDocument
{
	int schemaVersion;
	std::vector<std::string> commandIds;
};

bool parseCommandJournalJson(const std::string &json, CommandJournalDocument *out);

std::string serializeCommandJournalJson(const CommandJournalDocument &doc);

bool journalDocumentContainsCommandId(const CommandJournalDocument &doc, const std::string &commandId);

AppendJournalResult appendCommandId(CommandJournalDocument *doc, const std::string &commandId);

AppendJournalResult appendCommandJournalEntry(const std::string &journalPath, const std::string &commandId);

} // namespace CompanionCommandJournalCore

} // namespace OpenXcom
