#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace DirectXGame::GameplayRules {

inline constexpr float kMinimumWeaponInterval = 0.01f;

inline uint64_t MakeCellKey(int32_t cellX, int32_t cellZ)
{
	return (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32) |
		static_cast<uint32_t>(cellZ);
}

inline float CalculateGaugeRate(int32_t displayedValue, int32_t maxValue)
{
	if (maxValue <= 0) {
		return 0.0f;
	}
	return std::clamp(
		static_cast<float>(displayedValue) / static_cast<float>(maxValue),
		0.0f,
		1.0f);
}

inline int32_t ClampGaugeValue(int32_t current, int32_t maxValue)
{
	return std::clamp(current, 0, std::max<int32_t>(1, maxValue));
}

inline float NormalizeWeaponInterval(float interval, float minimumInterval)
{
	if (!std::isfinite(interval) || interval <= 0.0f ||
		!std::isfinite(minimumInterval) || minimumInterval <= 0.0f) {
		throw std::runtime_error("invalid weapon interval");
	}
	return (std::max)(
		(std::max)(kMinimumWeaponInterval, minimumInterval),
		interval);
}

inline uint32_t DeriveRunSeed(uint32_t baseSeed, uint32_t runCount)
{
	return baseSeed + runCount * 0x9E3779B9u;
}

}
