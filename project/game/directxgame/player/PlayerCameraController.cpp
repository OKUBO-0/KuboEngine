#include "PlayerCameraController.h"

#include "Camera.h"
#include "CameraManager.h"
#include "GameInputBindings.h"
#include "Input.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr char kGameCameraName[] = "directxgame_player";
constexpr float kMegabonkMouseYawSensitivity = 0.0026f;
constexpr float kMegabonkMaxMouseDelta = 80.0f;
constexpr float kMegabonkMinPitch = 0.52f;
constexpr float kMegabonkMaxPitch = 1.02f;
constexpr float kMegabonkLookAheadDistance = 7.0f;
constexpr float kMegabonkFocusHeight = 2.2f;

float NormalizeAngle(float angle)
{
	constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
	while (angle > std::numbers::pi_v<float>) {
		angle -= kTwoPi;
	}
	while (angle < -std::numbers::pi_v<float>) {
		angle += kTwoPi;
	}
	return angle;
}

Vector3 LookAtRotation(const Vector3& cameraPosition, const Vector3& focusPosition)
{
	const Vector3 direction = focusPosition - cameraPosition;
	const float horizontalLength =
		std::sqrt(direction.x * direction.x + direction.z * direction.z);
	return {
		std::atan2(-direction.y, horizontalLength),
		std::atan2(direction.x, direction.z),
		0.0f,
	};
}

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
	} else if (mode_ == Mode::Megabonk) {
		focusPosition_ = playerPosition;
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
		shakeCooldownTimer_ = (std::max)(0.0f, shakeCooldownTimer_ - kFixedDeltaTime);
		shakeTimer_ = (std::max)(0.0f, shakeTimer_ - kFixedDeltaTime);
		shakePhase_ += 2.1f;
	}
	if (mode_ == Mode::Megabonk && advanceFollow) {
		Engine::InputSystem::Input* input =
			Engine::InputSystem::Input::GetInstance();
		if (input && !GameInputBindings::IsGameInputSuppressedByImGui()) {
			const Engine::InputSystem::Input::MouseMove mouseMove =
				input->GetMouseMove();
			const float horizontalDelta = std::clamp(
				static_cast<float>(mouseMove.lX),
				-kMegabonkMaxMouseDelta,
				kMegabonkMaxMouseDelta);
			targetYaw_ = NormalizeAngle(
				targetYaw_ +
				horizontalDelta * kMegabonkMouseYawSensitivity);
			targetPitch_ = std::clamp(
				targetPitch_,
				kMegabonkMinPitch,
				kMegabonkMaxPitch);
		}
		const float yawDifference = NormalizeAngle(targetYaw_ - yaw_);
		const float pitchDifference = targetPitch_ - pitch_;
		const float smoothing = std::clamp(lookSmoothing_, 0.0f, 0.95f);
		const float lookRate = smoothing <= 0.0f
			? 1.0f
			: 1.0f - std::pow(smoothing, 60.0f * kFixedDeltaTime);
		yaw_ = NormalizeAngle(yaw_ + yawDifference * lookRate);
		pitch_ = std::clamp(
			pitch_ + pitchDifference * lookRate,
			kMegabonkMinPitch,
			kMegabonkMaxPitch);
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
	case Mode::Megabonk: {
		const Vector3 forward{
			std::sin(yaw_),
			0.0f,
			std::cos(yaw_),
		};
		const Vector3 focus{
			focusPosition_.x + forward.x * kMegabonkLookAheadDistance,
			focusPosition_.y + kMegabonkFocusHeight,
			focusPosition_.z + forward.z * kMegabonkLookAheadDistance,
		};
		const float horizontalDistance =
			std::cos(pitch_) * combatDistance_;
		const float verticalDistance =
			std::sin(pitch_) * combatDistance_;
		const Vector3 cameraPosition{
			focusPosition_.x - forward.x * horizontalDistance,
			focusPosition_.y + verticalDistance,
			focusPosition_.z - forward.z * horizontalDistance,
		};
		camera->SetTranslate(cameraPosition);
		camera->SetRotate(LookAtRotation(cameraPosition, focus));
		break;
	}
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

void PlayerCameraController::SetHeight(float height)
{
	height_ = height;
	combatHeight_ = height;
}

void PlayerCameraController::SetDistance(float distance)
{
	distance_ = distance;
	combatDistance_ = distance;
}

} // namespace DirectXGame
