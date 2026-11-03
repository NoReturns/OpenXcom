/*
 * Open X-Com Companion — minimal JSON field extraction (whitespace tolerant).
 */
#include "CompanionJsonLite.h"
#include <cctype>
#include <cstdlib>

namespace OpenXcom
{

namespace CompanionJsonLite
{

bool skipWhitespace(const std::string &json, std::string::size_type *pos)
{
	if (pos == 0)
	{
		return false;
	}
	while (*pos < json.size() && std::isspace(static_cast<unsigned char>(json[*pos])))
	{
		++(*pos);
	}
	return *pos < json.size();
}

bool findJsonKeyValueStart(const std::string &json, const std::string &key, std::string::size_type *valueStart)
{
	if (valueStart == 0)
	{
		return false;
	}
	const std::string quotedKey = "\"" + key + "\"";
	std::string::size_type pos = 0;
	while (pos < json.size())
	{
		pos = json.find(quotedKey, pos);
		if (pos == std::string::npos)
		{
			return false;
		}
		pos += quotedKey.size();
		if (!skipWhitespace(json, &pos))
		{
			return false;
		}
		if (json[pos] != ':')
		{
			++pos;
			continue;
		}
		++pos;
		if (!skipWhitespace(json, &pos))
		{
			return false;
		}
		*valueStart = pos;
		return true;
	}
	return false;
}

bool parseJsonStringValue(const std::string &json, std::string::size_type valueStart, std::string *out)
{
	if (out == 0 || valueStart >= json.size() || json[valueStart] != '"')
	{
		return false;
	}
	std::string::size_type pos = valueStart + 1;
	std::string value;
	while (pos < json.size())
	{
		const char c = json[pos];
		if (c == '"')
		{
			*out = value;
			return true;
		}
		if (c == '\\')
		{
			return false;
		}
		value.push_back(c);
		++pos;
	}
	return false;
}

bool parseJsonIntValue(const std::string &json, std::string::size_type valueStart, int *out)
{
	if (out == 0 || valueStart >= json.size())
	{
		return false;
	}
	std::string::size_type pos = valueStart;
	if (!skipWhitespace(json, &pos))
	{
		return false;
	}
	char *endPtr = 0;
	const long value = std::strtol(json.c_str() + pos, &endPtr, 10);
	if (endPtr == json.c_str() + pos)
	{
		return false;
	}
	*out = static_cast<int>(value);
	return true;
}

bool extractJsonStringField(const std::string &json, const std::string &key, std::string *out)
{
	std::string::size_type valueStart = 0;
	if (!findJsonKeyValueStart(json, key, &valueStart))
	{
		return false;
	}
	return parseJsonStringValue(json, valueStart, out);
}

bool extractJsonIntField(const std::string &json, const std::string &key, int *out)
{
	std::string::size_type valueStart = 0;
	if (!findJsonKeyValueStart(json, key, &valueStart))
	{
		return false;
	}
	return parseJsonIntValue(json, valueStart, out);
}

} // namespace CompanionJsonLite

} // namespace OpenXcom
