#include "game/directxgame/core/CsvReader.h"
#include "game/directxgame/core/GameplayRules.h"
#include "game/directxgame/player/PlayerStats.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {

using DirectXGame::GameplayRules::CalculateGaugeRate;
using DirectXGame::GameplayRules::CalculateCandidateReductionPercent;
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

void TestCollisionCandidateReduction()
{
	Require(
		CalculateCandidateReductionPercent(25, 100) == 75.0,
		"collision candidate reduction");
	Require(
		CalculateCandidateReductionPercent(0, 0) == 0.0,
		"collision candidate reduction empty baseline");
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

void TestPlayerStatResolution()
{
	DirectXGame::PlayerStats stats;
	const DirectXGame::WeaponBaseStats base{
		10.0f, 1.0f, 2.0f, 30.0f, 1.0f, 1 };
	auto runtime = stats.Resolve(base);
	Require(runtime.damage == 10, "weapon base damage");
	Require(runtime.interval == 1.0f, "weapon base interval");
	Require(runtime.projectileCount == 1, "weapon base projectile count");

	stats.AddPermanentModifier(DirectXGame::PlayerStatType::Damage, 0.2f);
	stats.AddRunModifier(DirectXGame::PlayerStatType::Damage, 0.5f);
	stats.AddRunModifier(DirectXGame::PlayerStatType::AttackSpeed, 1.0f);
	stats.AddRunModifier(DirectXGame::PlayerStatType::Duration, 0.5f);
	stats.AddRunModifier(DirectXGame::PlayerStatType::ProjectileCount, 2.0f);
	stats.AddRunModifier(DirectXGame::PlayerStatType::Knockback, 0.5f);
	runtime = stats.Resolve(base);
	Require(runtime.damage == 17, "damage layers are additive before resolution");
	Require(runtime.interval == 0.5f, "attack speed reduces interval");
	Require(runtime.duration == 45.0f, "duration scales weapon lifetime");
	Require(runtime.projectileCount == 3, "projectile count bonus");
	Require(stats.GetKnockbackMultiplier() == 1.5f, "knockback multiplier");

	runtime = stats.Resolve(
		base,
		DirectXGame::WeaponStatApplicability{
			true, false, true, false, true, false });
	Require(runtime.damage == 17, "masked stats keep damage");
	Require(runtime.interval == 1.0f, "masked stats ignore attack speed");
	Require(runtime.projectileSpeed == 2.0f, "masked stats ignore projectile speed");
	Require(runtime.projectileCount == 1, "masked stats ignore projectile count");

	stats.CharacterModifiers().critChance = 0.25f;
	auto hit = stats.ResolveHit(10, 0.10f);
	Require(hit.damage == 15 && hit.criticalTier == 1, "fractional critical hit");
	hit = stats.ResolveHit(10, 0.90f);
	Require(hit.damage == 10 && !hit.IsCritical(), "fractional critical miss");
	stats.CharacterModifiers().critChance = 1.25f;
	hit = stats.ResolveHit(10, 0.10f);
	Require(hit.damage == 23 && hit.criticalTier == 2, "overcrit stacks multiplier");
	stats.CharacterModifiers().armor = 100.0f;
	stats.CharacterModifiers().evasion = 100.0f;
	stats.CharacterModifiers().hpRegen = 100.0f;
	stats.CharacterModifiers().lifeSteal = 10.0f;
	Require(stats.GetArmorReduction() == 0.8f, "armor hard cap");
	Require(stats.GetEvasionChance() == 0.75f, "evasion hard cap");
	Require(stats.GetHpRegenPerSecond() == 20.0f, "hp regen hard cap");
	Require(stats.GetLifeStealChance() == 3.0f, "lifesteal hard cap");
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

} // namespace

int main()
{
	try {
		TestCellKey();
		TestCollisionCandidateReduction();
		TestGaugeRate();
		TestCsvParsing();
		TestWeaponIntervals();
		TestPlayerStatResolution();
		TestRunSeedDerivation();
		std::cout << "cpu_regression_checks: PASS\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "cpu_regression_checks: FAIL: " << error.what() << '\n';
		return 1;
	}
}
