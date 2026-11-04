/*
 * Open X-Com Companion — tactical command journal JSON (M1-27D).
 */
#include "CompanionCommandJournalCore.h"
#include "CompanionCommandCore.h"
#include "CompanionJsonLite.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace OpenXcom
{

namespace CompanionCommandJournalCore
{

namespace
{

bool readTextFile(const std::string &path, std::string *out)
{
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

bool writeTextFileAtomic(const std::string &path, const std::string &content)
{
	const std::string tmp = path + ".companion.tmp";
	{
		std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
		if (!out)
		{
			return false;
		}
		out << content;
		if (!out)
		{
			return false;
		}
	}
#ifdef _WIN32
	if (MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
	{
		DeleteFileA(tmp.c_str());
		return false;
	}
	return true;
#else
	if (std::rename(tmp.c_str(), path.c_str()) != 0)
	{
		std::remove(tmp.c_str());
		return false;
	}
	return true;
#endif
}

bool parseJsonStringArray(const std::string &json, std::string::size_type arrayStart, std::vector<std::string> *out)
{
	if (out == 0 || arrayStart >= json.size() || json[arrayStart] != '[')
	{
		return false;
	}
	std::string::size_type pos = arrayStart + 1;
	if (!CompanionJsonLite::skipWhitespace(json, &pos))
	{
		return false;
	}
	if (pos < json.size() && json[pos] == ']')
	{
		return true;
	}
	while (pos < json.size())
	{
		if (json[pos] != '"')
		{
			return false;
		}
		std::string value;
		if (!CompanionJsonLite::parseJsonStringValue(json, pos, &value))
		{
			return false;
		}
		std::string::size_type scan = pos + 1;
		while (scan < json.size() && json[scan] != '"')
		{
			++scan;
		}
		if (scan >= json.size())
		{
			return false;
		}
		pos = scan + 1;
		if (!CompanionCommandCore::isValidCommandIdFormat(value))
		{
			return false;
		}
		out->push_back(value);
		if (!CompanionJsonLite::skipWhitespace(json, &pos))
		{
			return false;
		}
		if (pos >= json.size())
		{
			return false;
		}
		if (json[pos] == ']')
		{
			return true;
		}
		if (json[pos] != ',')
		{
			return false;
		}
		++pos;
		if (!CompanionJsonLite::skipWhitespace(json, &pos))
		{
			return false;
		}
	}
	return false;
}

} // namespace

bool parseCommandJournalJson(const std::string &json, CommandJournalDocument *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = CommandJournalDocument();
	if (json.empty())
	{
		out->schemaVersion = 1;
		return true;
	}
	int schemaVersion = 0;
	if (!CompanionJsonLite::extractJsonIntField(json, "schema_version", &schemaVersion) || schemaVersion != 1)
	{
		return false;
	}
	out->schemaVersion = schemaVersion;
	std::string::size_type pos = 0;
	if (!CompanionJsonLite::findJsonKeyValueStart(json, "command_ids", &pos))
	{
		return false;
	}
	if (!CompanionJsonLite::skipWhitespace(json, &pos) || pos >= json.size() || json[pos] != '[')
	{
		return false;
	}
	std::vector<std::string> ids;
	if (!parseJsonStringArray(json, pos, &ids))
	{
		return false;
	}
	for (std::vector<std::string>::size_type i = 0; i < ids.size(); ++i)
	{
		for (std::vector<std::string>::size_type j = i + 1; j < ids.size(); ++j)
		{
			if (ids[i] == ids[j])
			{
				return false;
			}
		}
	}
	out->commandIds.swap(ids);
	return true;
}

std::string serializeCommandJournalJson(const CommandJournalDocument &doc)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": " << doc.schemaVersion << ",\n"
	     << "  \"command_ids\": [\n";
	for (std::vector<std::string>::size_type i = 0; i < doc.commandIds.size(); ++i)
	{
		json << "    \"" << doc.commandIds[i] << "\"";
		if (i + 1 < doc.commandIds.size())
		{
			json << ",";
		}
		json << "\n";
	}
	json << "  ]\n"
	     << "}\n";
	return json.str();
}

bool journalDocumentContainsCommandId(const CommandJournalDocument &doc, const std::string &commandId)
{
	for (std::vector<std::string>::size_type i = 0; i < doc.commandIds.size(); ++i)
	{
		if (doc.commandIds[i] == commandId)
		{
			return true;
		}
	}
	return false;
}

AppendJournalResult appendCommandId(CommandJournalDocument *doc, const std::string &commandId)
{
	if (doc == 0)
	{
		return JOURNAL_APPEND_WRITE_FAILED;
	}
	if (!CompanionCommandCore::isValidCommandIdFormat(commandId))
	{
		return JOURNAL_APPEND_INVALID_COMMAND_ID;
	}
	if (journalDocumentContainsCommandId(*doc, commandId))
	{
		return JOURNAL_APPEND_DUPLICATE;
	}
	doc->commandIds.push_back(commandId);
	return JOURNAL_APPEND_OK;
}

AppendJournalResult appendCommandJournalEntry(const std::string &journalPath, const std::string &commandId)
{
	if (!CompanionCommandCore::isValidCommandIdFormat(commandId))
	{
		return JOURNAL_APPEND_INVALID_COMMAND_ID;
	}
	std::string existing;
	const bool fileExists = readTextFile(journalPath, &existing);
	CommandJournalDocument doc = {};
	if (!fileExists || existing.empty())
	{
		doc.schemaVersion = 1;
	}
	else if (!parseCommandJournalJson(existing, &doc))
	{
		return JOURNAL_APPEND_MALFORMED_EXISTING;
	}
	const AppendJournalResult appendResult = appendCommandId(&doc, commandId);
	if (appendResult != JOURNAL_APPEND_OK)
	{
		return appendResult;
	}
	const std::string serialized = serializeCommandJournalJson(doc);
	if (!writeTextFileAtomic(journalPath, serialized))
	{
		return JOURNAL_APPEND_WRITE_FAILED;
	}
	return JOURNAL_APPEND_OK;
}

JournalReadiness classifyExistingJournalJson(const std::string &journalJson)
{
	if (journalJson.empty())
	{
		return JOURNAL_READABLE_EMPTY;
	}
	CommandJournalDocument doc = {};
	if (parseCommandJournalJson(journalJson, &doc))
	{
		return JOURNAL_READABLE;
	}
	return JOURNAL_UNREADABLE;
}

bool journalJsonContainsCommandId(const std::string &journalJson, const std::string &commandId)
{
	CommandJournalDocument doc = {};
	if (!parseCommandJournalJson(journalJson, &doc))
	{
		return false;
	}
	return journalDocumentContainsCommandId(doc, commandId);
}

} // namespace CompanionCommandJournalCore

} // namespace OpenXcom
