#pragma once

#include "Vector3.h"

namespace Engine::CameraSystem {
class Camera;
}

namespace Engine::Graphics3D {
class Object3D;
}

namespace DirectXGame {

class PlayerCameraController;

class PlayerPresentationController final {
public:
	enum class State {
		Normal,
		Intro,
		Death,
	};

	void BeginNormalFrame();
	void StartIntro(
		const Vector3& playerPosition,
		PlayerCameraController& cameraController);
	void UpdateIntro(
		float elapsedTime,
		float duration,
		const Vector3& playerPosition,
		float playerRotationY,
		Engine::Graphics3D::Object3D* playerObject,
		Engine::CameraSystem::Camera* camera,
		const PlayerCameraController& cameraController);
	void StartDeath(
		const Vector3& playerPosition,
		float playerRotationY,
		Engine::Graphics3D::Object3D* playerObject,
		Engine::CameraSystem::Camera* camera,
		PlayerCameraController& cameraController);
	void UpdateDeath(
		float elapsedTime,
		float duration,
		const Vector3& playerPosition,
		float playerRotationY,
		Engine::Graphics3D::Object3D* playerObject,
		Engine::CameraSystem::Camera* camera);
	void ApplyPlayerTransform(
		Engine::Graphics3D::Object3D* playerObject,
		const Vector3& playerPosition,
		float playerRotationY,
		bool isDodging) const;

	bool IsActive() const { return state_ != State::Normal; }
	State GetState() const { return state_; }

private:
	void ApplyDeathPose(
		Engine::Graphics3D::Object3D* playerObject,
		const Vector3& playerPosition,
		float playerRotationY,
		float progress) const;
	void SyncCamera(Engine::CameraSystem::Camera* camera) const;

	State state_ = State::Normal;
	float deathStartCameraHeight_ = 80.0f;
	float deathStartCameraDistance_ = 45.0f;
	float deathStartCameraPitch_ = 1.0f;
};

} // namespace DirectXGame
