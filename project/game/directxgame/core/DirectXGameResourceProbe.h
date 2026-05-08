#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace DirectXGame {

struct DirectXGameResourceProbeStatus {
	bool verified = false;
	bool textureLoaded = false;
	bool modelLoaded = false;
	bool csvOpened = false;
	bool gameplayDataLoaded = false;
	bool uiLayoutsLoaded = false;
	bool hudLayoutReady = false;
	bool requiredAssetsReady = false;
	size_t requiredAssetCount = 0;
	std::vector<std::string> missingRequiredAssets;
};

class DirectXGameResourceProbe {
public:
	static const DirectXGameResourceProbeStatus& Verify();
};

}
