#pragma once

#include "RenderingData.h"

namespace Engine::Graphics3D {
class Object3D;
}

namespace DirectXGame {

class GameLightSettings {
public:
	GameLightSettings();

	void SetLightingEnabled(bool enabled) { lightingEnabled_ = enabled; }
	bool IsLightingEnabled() const { return lightingEnabled_; }

	void SetDirectionalLight(const DirectionalLight& light) { directionalLight_ = light; }
	const DirectionalLight& GetDirectionalLight() const { return directionalLight_; }

	void SetPointLight(const PointLight& light) { pointLight_ = light; }
	const PointLight& GetPointLight() const { return pointLight_; }

	void SetSpotLight(const SpotLight& light) { spotLight_ = light; }
	const SpotLight& GetSpotLight() const { return spotLight_; }

	void ApplyTo(Engine::Graphics3D::Object3D& object) const;

private:
	bool lightingEnabled_ = true;
	DirectionalLight directionalLight_{};
	PointLight pointLight_{};
	SpotLight spotLight_{};
};

}
