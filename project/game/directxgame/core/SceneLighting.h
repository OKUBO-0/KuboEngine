#pragma once

#include "RenderingData.h"
#include "UILayoutIO.h"
#include <string_view>
#include <vector>

namespace DirectXGame::SceneLighting {

struct Defaults {
	SceneLightData light{};
	bool shadowEnabled = true;
	float shadowStrength = 0.72f;
	float shadowSoftness = 1.8f;
	float shadowBias = 0.0012f;
	float shadowArea = 90.0f;
};

void ApplyDefaults(const Defaults& defaults);
void Load(const UILayoutIO::LayoutMap& tuning, std::string_view keyPrefix = {});
void AppendTuningEntries(
	std::vector<UILayoutIO::Entry>& entries,
	std::string_view keyPrefix = {});

#ifdef _DEBUG
void DrawDebugUI();
#endif

}
