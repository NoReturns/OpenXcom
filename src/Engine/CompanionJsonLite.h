#pragma once
/*
 * Open X-Com Companion — minimal JSON field extraction (whitespace tolerant).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionJsonLite
{

bool skipWhitespace(const std::string &json, std::string::size_type *pos);

bool findJsonKeyValueStart(const std::string &json, const std::string &key, std::string::size_type *valueStart);

bool parseJsonStringValue(const std::string &json, std::string::size_type valueStart, std::string *out);

bool parseJsonIntValue(const std::string &json, std::string::size_type valueStart, int *out);

bool extractJsonStringField(const std::string &json, const std::string &key, std::string *out);

bool extractJsonIntField(const std::string &json, const std::string &key, int *out);

/// Sets *out when the key exists and parses as int; returns false when the key is absent (out unchanged).
bool tryExtractJsonIntField(const std::string &json, const std::string &key, int *out);

bool parseJsonInt64Value(const std::string &json, std::string::size_type valueStart, long long *out);

bool extractJsonInt64Field(const std::string &json, const std::string &key, long long *out);

} // namespace CompanionJsonLite

} // namespace OpenXcom
