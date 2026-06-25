#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/GameplayRules.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {

using DirectXGame::GameplayRules::CalculateGaugeRate;
using DirectXGame::GameplayRules::ClampGaugeValue;
using DirectXGame::GameplayRules::DeriveRunSeed;
using DirectXGame::GameplayRules::MakeCellKey;
using DirectXGame::GameplayRules::NormalizeWeaponInterval;

void Require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Fn>
void RequireThrows(Fn&& fn, const char* message)
{
	try {
		fn();
	} catch (...) {
		return;
	}
	throw std::runtime_error(message);
}

void TestCellKey()
{
	Require(MakeCellKey(0, 0) == 0ull, "cell key origin");
	Require(MakeCellKey(-1, 0) == 0xFFFFFFFF00000000ull, "cell key negative x");
	Require(MakeCellKey(0, -1) == 0x00000000FFFFFFFFull, "cell key negative z");
	Require(MakeCellKey(-12, 34) != MakeCellKey(34, -12), "cell key axis uniqueness");
}

void TestGaugeRate()
{
	Require(CalculateGaugeRate(50, 100) == 0.5f, "gauge normal ratio");
	Require(CalculateGaugeRate(150, 100) == 1.0f, "gauge upper clamp");
	Require(CalculateGaugeRate(-10, 100) == 0.0f, "gauge lower clamp");
	Require(CalculateGaugeRate(10, 0) == 0.0f, "gauge zero max");
	Require(CalculateGaugeRate(10, -5) == 0.0f, "gauge negative max");
	Require(ClampGaugeValue(120, 100) == 100, "hp upper clamp");
	Require(ClampGaugeValue(-4, 100) == 0, "hp lower clamp");
	Require(ClampGaugeValue(5, 0) == 1, "hp invalid max fallback");
}

void TestCsvParsing()
{
	Require(DirectXGame::CsvReader::ParseFloat("1.25", "test") == 1.25f, "float parse valid");
	Require(DirectXGame::CsvReader::ParseInt32("-42", "test") == -42, "int parse valid");
	RequireThrows([] { static_cast<void>(DirectXGame::CsvReader::ParseFloat("1.0x", "test")); }, "float trailing garbage");
	RequireThrows([] { static_cast<void>(DirectXGame::CsvReader::ParseFloat("nan", "test")); }, "float nan rejection");
	RequireThrows([] { static_cast<void>(DirectXGame::CsvReader::ParseInt32("12.0", "test")); }, "int decimal rejection");
	RequireThrows([] { static_cast<void>(DirectXGame::CsvReader::ParseInt32("999999999999999999999", "test")); }, "int overflow rejection");
}

void TestWeaponIntervals()
{
	Require(NormalizeWeaponInterval(0.25f, 0.1f) == 0.25f, "weapon interval unchanged");
	Require(NormalizeWeaponInterval(0.005f, 0.005f) == 0.01f, "weapon min hard clamp");
	Require(NormalizeWeaponInterval(0.05f, 0.1f) == 0.1f, "weapon min dominates");
	RequireThrows([] { static_cast<void>(NormalizeWeaponInterval(0.0f, 0.1f)); }, "zero interval rejected");
	RequireThrows([] { static_cast<void>(NormalizeWeaponInterval(0.1f, -1.0f)); }, "negative min rejected");
}

void TestRunSeedDerivation()
{
	Require(DeriveRunSeed(1234u, 1u) == DeriveRunSeed(1234u, 1u),
		"run seed reproducibility");
	Require(DeriveRunSeed(1234u, 1u) != DeriveRunSeed(1234u, 2u),
		"run seed changes per run");
	Require(DeriveRunSeed(1234u, 1u) != DeriveRunSeed(5678u, 1u),
		"base seed changes sequence");
}

void TestSoundHandleContract()
{
	static_assert(!std::is_convertible_v<DirectXGame::SoundHandle, uint32_t>);
	Require(!DirectXGame::SoundHandle{}, "default sound handle is invalid");
	Require(static_cast<bool>(DirectXGame::SoundHandle{ 7 }),
		"non-zero sound handle is valid");
	Require(DirectXGame::SoundHandle{ 7 } == DirectXGame::SoundHandle{ 7 },
		"sound handle identity");
}

} // namespace

int main()
{
	try {
		TestCellKey();
		TestGaugeRate();
		TestCsvParsing();
		TestWeaponIntervals();
		TestRunSeedDerivation();
		TestSoundHandleContract();
		std::cout << "cpu_regression_checks: PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "cpu_regression_checks: FAIL: " << error.what() << '\n';
		return 1;
	}
}
