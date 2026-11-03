#pragma once
/*
 * Open X-Com Companion — SHA-256 over UTF-8 byte strings (snapshot identity binding).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionSha256
{

/// Writes 64 lowercase hex chars to @p outHex64 on success.
bool digestHexLowerUtf8(const std::string &utf8Bytes, std::string *outHex64);

bool isValidHexDigest(const std::string &hex);

} // namespace CompanionSha256

} // namespace OpenXcom
