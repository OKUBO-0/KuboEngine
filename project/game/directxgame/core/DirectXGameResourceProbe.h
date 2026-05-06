#pragma once

namespace DirectXGame {

struct DirectXGameResourceProbeStatus {
	bool verified = false;
	bool textureLoaded = false;
	bool modelLoaded = false;
	bool csvOpened = false;
	bool gameplayDataLoaded = false;
	bool uiLayoutsLoaded = false;
	bool hudLayoutReady = false;
};

class DirectXGameResourceProbe {
public:
	static const DirectXGameResourceProbeStatus& Verify();
};

}
