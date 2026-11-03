/*
 * Open X-Com Companion — SHA-256 over UTF-8 byte strings (snapshot identity binding).
 */
#include "CompanionSha256.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#ifndef BCRYPT_HASH_REUSABLE_FLAG
#define BCRYPT_HASH_REUSABLE_FLAG 0x00000020
#endif
#endif

namespace OpenXcom
{

namespace CompanionSha256
{

namespace
{

bool formatDigestLowerHex(const unsigned char digest[32], std::string *outHex64)
{
	if (outHex64 == 0)
	{
		return false;
	}
	char buf[65];
	for (int i = 0; i < 32; ++i)
	{
		std::sprintf(buf + (i * 2), "%02x", digest[i]);
	}
	buf[64] = '\0';
	*outHex64 = std::string(buf, 64);
	return isValidHexDigest(*outHex64);
}

#ifndef _WIN32

// Portable FIPS 180-4 SHA-256 (no external crypto dependency on non-Windows builds).
struct Sha256Ctx
{
	uint32_t state[8];
	uint64_t bitCount;
	unsigned char buffer[64];
	size_t bufferUsed;
};

static uint32_t rotr32(uint32_t x, uint32_t n)
{
	return (x >> n) | (x << (32 - n));
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static uint32_t sig0(uint32_t x) { return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22); }
static uint32_t sig1(uint32_t x) { return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25); }
static uint32_t gam0(uint32_t x) { return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3); }
static uint32_t gam1(uint32_t x) { return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256Transform(Sha256Ctx *ctx, const unsigned char block[64])
{
	uint32_t w[64];
	for (int i = 0; i < 16; ++i)
	{
		w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
			| (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
			| (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
			| static_cast<uint32_t>(block[i * 4 + 3]);
	}
	for (int i = 16; i < 64; ++i)
	{
		w[i] = gam1(w[i - 2]) + w[i - 7] + gam0(w[i - 15]) + w[i - 16];
	}

	uint32_t a = ctx->state[0];
	uint32_t b = ctx->state[1];
	uint32_t c = ctx->state[2];
	uint32_t d = ctx->state[3];
	uint32_t e = ctx->state[4];
	uint32_t f = ctx->state[5];
	uint32_t g = ctx->state[6];
	uint32_t h = ctx->state[7];

	for (int i = 0; i < 64; ++i)
	{
		const uint32_t t1 = h + sig1(e) + ch(e, f, g) + K256[i] + w[i];
		const uint32_t t2 = sig0(a) + maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

static void sha256Init(Sha256Ctx *ctx)
{
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
	ctx->bitCount = 0;
	ctx->bufferUsed = 0;
}

static void sha256Update(Sha256Ctx *ctx, const unsigned char *data, size_t len)
{
	while (len > 0)
	{
		const size_t space = 64 - ctx->bufferUsed;
		const size_t take = len < space ? len : space;
		std::memcpy(ctx->buffer + ctx->bufferUsed, data, take);
		ctx->bufferUsed += take;
		data += take;
		len -= take;
		ctx->bitCount += static_cast<uint64_t>(take) * 8ULL;
		if (ctx->bufferUsed == 64)
		{
			sha256Transform(ctx, ctx->buffer);
			ctx->bufferUsed = 0;
		}
	}
}

static void sha256Final(Sha256Ctx *ctx, unsigned char out[32])
{
	const unsigned char padBit = 0x80;
	sha256Update(ctx, &padBit, 1);
	unsigned char zero = 0;
	while (ctx->bufferUsed != 56)
	{
		sha256Update(ctx, &zero, 1);
	}
	unsigned char lenBe[8];
	const uint64_t bitCountBe = ctx->bitCount;
	for (int i = 0; i < 8; ++i)
	{
		lenBe[7 - i] = static_cast<unsigned char>((bitCountBe >> (i * 8)) & 0xff);
	}
	sha256Update(ctx, lenBe, 8);
	for (int i = 0; i < 8; ++i)
	{
		out[i * 4] = static_cast<unsigned char>((ctx->state[i] >> 24) & 0xff);
		out[i * 4 + 1] = static_cast<unsigned char>((ctx->state[i] >> 16) & 0xff);
		out[i * 4 + 2] = static_cast<unsigned char>((ctx->state[i] >> 8) & 0xff);
		out[i * 4 + 3] = static_cast<unsigned char>(ctx->state[i] & 0xff);
	}
}

static bool digestPortable(const std::string &utf8Bytes, std::string *outHex64)
{
	Sha256Ctx ctx;
	sha256Init(&ctx);
	if (!utf8Bytes.empty())
	{
		sha256Update(&ctx, reinterpret_cast<const unsigned char *>(utf8Bytes.data()), utf8Bytes.size());
	}
	unsigned char digest[32];
	sha256Final(&ctx, digest);
	return formatDigestLowerHex(digest, outHex64);
}

#endif // !_WIN32

#ifdef _WIN32

static bool digestWin32(const std::string &utf8Bytes, std::string *outHex64)
{
	BCRYPT_ALG_HANDLE algorithm = 0;
	NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, 0, 0);
	if (status != 0)
	{
		return false;
	}

	DWORD objectLength = 0;
	DWORD hashLength = 0;
	DWORD cbData = 0;
	status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
		sizeof(objectLength), &cbData, 0);
	if (status != 0)
	{
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return false;
	}
	status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
		sizeof(hashLength), &cbData, 0);
	if (status != 0 || hashLength != 32)
	{
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return false;
	}

	std::string hashObject(objectLength, '\0');
	BCRYPT_HASH_HANDLE hash = 0;
	status = BCryptCreateHash(algorithm, &hash, reinterpret_cast<PUCHAR>(&hashObject[0]), objectLength,
		0, 0, 0);
	if (status != 0)
	{
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return false;
	}

	if (!utf8Bytes.empty())
	{
		status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(utf8Bytes.data())),
			static_cast<ULONG>(utf8Bytes.size()), 0);
	}
	else
	{
		status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>("")), 0, 0);
	}
	if (status != 0)
	{
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return false;
	}

	unsigned char digest[32];
	status = BCryptFinishHash(hash, digest, hashLength, 0);
	BCryptDestroyHash(hash);
	BCryptCloseAlgorithmProvider(algorithm, 0);
	if (status != 0)
	{
		return false;
	}
	return formatDigestLowerHex(digest, outHex64);
}

#endif

} // namespace

bool isValidHexDigest(const std::string &hex)
{
	if (hex.size() != 64)
	{
		return false;
	}
	for (std::string::size_type i = 0; i < hex.size(); ++i)
	{
		const char c = hex[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

bool digestHexLowerUtf8(const std::string &utf8Bytes, std::string *outHex64)
{
#ifdef _WIN32
	return digestWin32(utf8Bytes, outHex64);
#else
	return digestPortable(utf8Bytes, outHex64);
#endif
}

} // namespace CompanionSha256

} // namespace OpenXcom
