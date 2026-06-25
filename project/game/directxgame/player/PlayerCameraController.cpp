#include "game/directxgame/player/PlayerCameraController.h"

#include "Camera.h"
#include "CameraManager.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr char kGameCameraName[] = "directxgame_player";

}

namespace DirectXGame {

void PlayerCameraController::Update(
	Engine::CameraSystem::Camera* camera,
	const Vector3& playerPosition,
	float playerRotationY,
	bool advanceFollow)
{
	if (!camera) {
		return;
	}

	if (!followInitialized_) {
		ResetFocus(playerPosition);
	} else if (advanceFollow) {
		const float clampedSmoothness = (std::max)(0.0f, followSmoothness_);
		const float followRate = clampedSmoothness <= 0.0f
			? 0.0f
			: 1.0f - std::exp(-clampedSmoothness * kFixedDeltaTime);
		focusPosition_.x += (playerPosition.x - focusPosition_.x) * followRate;
		focusPosition_.y += (playerPosition.y - focusPosition_.y) * followRate;
		focusPosition_.z += (playerPosition.z - focusPosition_.z) * followRate;
	}

	if (advanceFollow) {
		const float zoomRate = 1.0f - std::exp(-3.6f * kFixedDeltaTime);
		combatDistance_ += (combatTargetDistance_ - combatDistance_) * zoomRate;
		combatHeight_ += (combatTargetHeight_ - combatHeight_) * zoomRate;
		shakeCooldownTimer_ = (std::max)(0.0f, shakeCooldownTimer_ - kFixedDeltaTime);
		shakeTimer_ = (std::max)(0.0f, shakeTimer_ - kFixedDeltaTime);
		shakePhase_ += 2.1f;
	}

	switch (mode_) {
	case Mode::PlayerBack: {
		const Vector3 forward{
			std::sin(playerRotationY),
			0.0f,
			std::cos(playerRotationY),
		};
		camera->SetTranslate({
			focusPosition_.x - forward.x * combatDistance_,
			combatHeight_,
			focusPosition_.z - forward.z * combatDistance_,
			});
		camera->SetRotate({ pitch_, playerRotationY, 0.0f });
		break;
	}
	case Mode::WorldFront:
		camera->SetTranslate({
			focusPosition_.x,
			combatHeight_,
			focusPosition_.z + combatDistance_,
			});
		camera->SetRotate({ pitch_, 3.14159265f, 0.0f });
		break;
	case Mode::TopDown:
		camera->SetTranslate({
			focusPosition_.x,
			combatHeight_,
			focusPosition_.z,
			});
		camera->SetRotate({ 1.57079633f, 0.0f, 0.0f });
		break;
	case Mode::WorldBack:
	default:
		camera->SetTranslate({
			focusPosition_.x,
			combatHeight_,
			focusPosition_.z - combatDistance_,
			});
		camera->SetRotate({ pitch_, 0.0f, 0.0f });
		break;
	}

	if (shakeTimer_ > 0.0f && shakeDuration_ > 0.0f) {
		const float fade = shakeTimer_ / shakeDuration_;
		Vector3 shakenPosition = camera->GetTransform().translate;
		shakenPosition.x +=
			std::sin(shakePhase_ * 2.3f) * shakeStrength_ * fade;
		shakenPosition.y +=
			std::cos(shakePhase_ * 1.7f) * shakeStrength_ * 0.38f * fade;
		camera->SetTranslate(shakenPosition);
	}

	camera->SetFarClip(500.0f);
	camera->Update();

	Engine::CameraSystem::CameraManager* cameraManager =
		Engine::CameraSystem::CameraManager::GetInstance();
	if (cameraManager->GetCamera(kGameCameraName) == camera) {
		cameraManager->SetActiveCamera(kGameCameraName);
	}
}

void PlayerCameraController::ResetFocus(const Vector3& playerPosition)
{
	focusPosition_ = playerPosition;
	followInitialized_ = true;
}

void PlayerCameraController::RequestShake(float duration, float strength)
{
	if (shakeCooldownTimer_ > 0.0f) {
		return;
	}
	shakeDuration_ = std::clamp(duration, 0.0f, 0.16f);
	shakeTimer_ = shakeDuration_;
	shakeStrength_ = std::clamp(strength, 0.0f, 0.85f);
	shakeCooldownTimer_ = 0.12f;
	shakePhase_ = 0.0f;
}

void PlayerCameraController::SetCombatTarget(float distance, float height)
{
	combatTargetDistance_ = std::clamp(distance, 30.0f, 62.0f);
	combatTargetHeight_ = std::clamp(height, 54.0f, 96.0f);
}

void PlayerCameraController::SetHeight(float height)
{
	height_ = height;
	combatHeight_ = height;
	combatTargetHeight_ = height;
}

void PlayerCameraController::SetDistance(float distance)
{
	distance_ = distance;
	combatDistance_ = distance;
	combatTargetDistance_ = distance;
}

} // namespace DirectXGame
