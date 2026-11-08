/*
 * Open X-Com Companion — CompanionJsonLite regression tests (M1-31G).
 */
#include "Engine/CompanionJsonLite.h"
#include <climits>
#include <iostream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionJsonLite;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testIntFieldPreservesSmallIntegerSemantics()
{
	const std::string json = "{\n  \"actor_unit_id\": 42,\n  \"turn\": 3\n}\n";
	int actor = 0;
	int turn = 0;
	expect(extractJsonIntField(json, "actor_unit_id", &actor), "small int field parses");
	expect(actor == 42, "actor_unit_id value");
	expect(extractJsonIntField(json, "turn", &turn), "turn parses");
	expect(turn == 3, "turn value");
}

static void testInt64FieldRealisticEpoch()
{
	const std::string json = "{\n  \"updated_at_ms\": 1793814448770\n}\n";
	long long updated = 0;
	expect(extractJsonInt64Field(json, "updated_at_ms", &updated), "epoch ms parses");
	expect(updated == 1793814448770LL, "epoch ms exact");
}

static void testInt64BeyondIntMax()
{
	const std::string json = "{\n  \"updated_at_ms\": 3000000000\n}\n";
	long long updated = 0;
	expect(extractJsonInt64Field(json, "updated_at_ms", &updated), "beyond int max parses as int64");
	expect(updated == 3000000000LL, "int64 value preserved");
}

static void testInt64MalformedAndOverflow()
{
	long long value = 0;
	expect(!extractJsonInt64Field("{\"updated_at_ms\": \"1793814448770\"}", "updated_at_ms", &value),
		"quoted numeric rejected");
	expect(!extractJsonInt64Field("{\"updated_at_ms\": not_a_number}", "updated_at_ms", &value),
		"malformed numeric rejected");
	expect(!extractJsonInt64Field("{\"updated_at_ms\": 9223372036854775808}", "updated_at_ms", &value),
		"above LLONG_MAX rejected");
	expect(!extractJsonInt64Field("{\"updated_at_ms\": -9223372036854775809}", "updated_at_ms", &value),
		"below LLONG_MIN rejected");
}

int main()
{
	testIntFieldPreservesSmallIntegerSemantics();
	testInt64FieldRealisticEpoch();
	testInt64BeyondIntMax();
	testInt64MalformedAndOverflow();
	std::cout << (g_failures == 0 ? "OK" : "FAILED") << std::endl;
	return g_failures == 0 ? 0 : 1;
}
