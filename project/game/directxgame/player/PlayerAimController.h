#pragma once

#include "Vector3.h"

namespace Engine::CameraSystem {
class Camera;
}

namespace Engine::Graphics3D {
class Object3D;
}

namespace DirectXGame {

class PlayerAimController final {
public:
	enum class InputDevice {
		KeyboardMouse,
		Gamepad,
	};

	void Update(
		float deltaTime,
		const Vector3& playerPosition,
		float& playerRotationY,
		Engine::CameraSystem::Camera* camera);
	void ApplyIndicatorTransform(
		Engine::Graphics3D::Object3D* indicatorObject,
		const Vector3& playerPosition,
		float playerRotationY) const;

	bool IsMouseAimEnabled() const { return mouseAimEnabled_; }
	void SetMouseAimEnabled(bool enabled) { mouseAimEnabled_ = enabled; }
	InputDevice GetInputDevice() const { return inputDevice_; }

private:
	bool mouseAimEnabled_ = true;
	InputDevice inputDevice_ = InputDevice::KeyboardMouse;
	Vector3 indicatorPosition_{ 0.0f, -1.0f, 6.0f };
	bool indicatorTracksMouse_ = false;
};

} // namespace DirectXGame
