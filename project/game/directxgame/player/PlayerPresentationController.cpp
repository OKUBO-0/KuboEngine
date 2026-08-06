#include "PlayerPresentationController.h"

#include "Camera.h"
#include "CameraManager.h"
#include "Object3D.h"
#include "PlayerCameraController.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kGameCameraName[] = "directxgame_player";
constexpr float kPlayerModelScale = 2.0f;
constexpr float kPlayerModelBasePitch = 0.0f;
constexpr float kPresentationCameraDistance = 20.0f;
constexpr float kPresentationCameraHeight = 10.0f;
constexpr float kPresentationCameraPitch = 0.40f;
constexpr float kPlayerWalkCycleSpeed = 6.5f;
constexpr float kPlayerIdleCycleSpeed = 2.0f;

float Clamp01(float value)
{
	return std::clamp(value, 0.0f, 1.0f);
}

float LerpFloat(float start, float end, float progress)
{
	return start + (end - start) * progress;
}

float SmoothStep(float progress)
{
	progress = std::clamp(progress, 0.0f, 1.0f);
	return progress * progress * (3.0f - 2.0f * progress);
}

Vector3 LookAtRotation(const Vector3& cameraPosition, const Vector3& focusPosition)
{
	const Vector3 direction{
		focusPosition.x - cameraPosition.x,
		focusPosition.y - cameraPosition.y,
		focusPosition.z - cameraPosition.z,
	};
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

void PlayerPresentationController::BeginNormalFrame()
{
	state_ = State::Normal;
}

void PlayerPresentationController::StartIntro(
	const Vector3& playerPosition,
	PlayerCameraController& cameraController)
{
	state_ = State::Intro;
	cameraController.ResetFocus(playerPosition);
}

void PlayerPresentationController::UpdateIntro(
	float elapsedTime,
	float duration,
	const Vector3& playerPosition,
	float playerRotationY,
	Engine::Graphics3D::Object3D* playerObject,
	Engine::CameraSystem::Camera* camera,
	const PlayerCameraController& cameraController)
{
	state_ = State::Intro;
	const float rawProgress = Clamp01(
		duration > 0.0f ? elapsedTime / duration : 1.0f);
	const float orbitProgress = SmoothStep(Clamp01(rawProgress / 0.9f));
	const float growProgress = SmoothStep(Clamp01(rawProgress / 0.86f));
	if (playerObject) {
		playerObject->SetRotate({ kPlayerModelBasePitch, playerRotationY, 0.0f });
		playerObject->SetTranslate(playerPosition);
		const float playerScale = LerpFloat(0.02f, kPlayerModelScale, growProgress);
		playerObject->SetScale({ playerScale, playerScale, playerScale });
		playerObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		playerObject->Update();
	}

	if (!camera) {
		return;
	}

	const float returnProgress = SmoothStep(Clamp01((rawProgress - 0.84f) / 0.16f));
	const PlayerCameraController::CameraPose normalCameraPose =
		cameraController.CalculatePose(playerPosition, playerRotationY);
	const float distance = LerpFloat(
		18.0f,
		cameraController.GetDistance(),
		returnProgress);
	const float height = LerpFloat(
		12.8f,
		normalCameraPose.position.y,
		returnProgress);
	const float orbitAngle = LerpFloat(-1.55f, 0.0f, orbitProgress);
	const float focusHeight = playerPosition.y + 2.7f;
	const float pitch = std::atan2(height - focusHeight, distance);
	const Vector3 orbitPosition{
		playerPosition.x + std::sin(orbitAngle) * distance,
		height,
		playerPosition.z - std::cos(orbitAngle) * distance,
		};
	const Vector3 orbitRotation{ pitch, -orbitAngle, 0.0f };
	camera->SetTranslate({
		LerpFloat(orbitPosition.x, normalCameraPose.position.x, returnProgress),
		LerpFloat(orbitPosition.y, normalCameraPose.position.y, returnProgress),
		LerpFloat(orbitPosition.z, normalCameraPose.position.z, returnProgress),
		});
	camera->SetRotate({
		LerpFloat(orbitRotation.x, normalCameraPose.rotation.x, returnProgress),
		LerpFloat(orbitRotation.y, normalCameraPose.rotation.y, returnProgress),
		LerpFloat(orbitRotation.z, normalCameraPose.rotation.z, returnProgress),
		});
	SyncCamera(camera);
}

void PlayerPresentationController::StartDeath(
	const Vector3& playerPosition,
	float playerRotationY,
	Engine::Graphics3D::Object3D* playerObject,
	Engine::CameraSystem::Camera* camera,
	PlayerCameraController& cameraController)
{
	state_ = State::Death;
	deathStartCameraHeight_ = cameraController.GetHeight();
	deathStartCameraDistance_ = cameraController.GetDistance();
	deathStartCameraPitch_ = cameraController.GetPitch();
	cameraController.ResetFocus(playerPosition);
	ApplyDeathPose(
		playerObject,
		playerPosition,
		playerRotationY,
		0.0f);
	UpdateDeath(
		0.0f,
		1.0f,
		playerPosition,
		playerRotationY,
		playerObject,
		camera);
}

void PlayerPresentationController::UpdateDeath(
	float elapsedTime,
	float duration,
	const Vector3& playerPosition,
	float playerRotationY,
	Engine::Graphics3D::Object3D* playerObject,
	Engine::CameraSystem::Camera* camera)
{
	state_ = State::Death;
	const float progress =
		SmoothStep(duration > 0.0f ? elapsedTime / duration : 1.0f);
	ApplyDeathPose(
		playerObject,
		playerPosition,
		playerRotationY,
		progress);

	if (!camera) {
		return;
	}

	const float distance = LerpFloat(
		deathStartCameraDistance_,
		kPresentationCameraDistance,
		progress);
	const float height = LerpFloat(
		deathStartCameraHeight_,
		kPresentationCameraHeight,
		progress);
	const float orbitYaw = playerRotationY + 0.35f;
	const Vector3 cameraPosition{
		playerPosition.x + std::sin(orbitYaw) * 5.2f,
		height,
		playerPosition.z - std::cos(orbitYaw) * distance,
	};
	const Vector3 focusPosition{
		playerPosition.x,
		playerPosition.y + 1.55f,
		playerPosition.z,
	};
	const Vector3 lookRotation = LookAtRotation(cameraPosition, focusPosition);
	const float pitch = LerpFloat(
		deathStartCameraPitch_,
		lookRotation.x,
		progress);
	camera->SetTranslate(cameraPosition);
	camera->SetRotate({ pitch, lookRotation.y, 0.0f });
	SyncCamera(camera);
}

void PlayerPresentationController::UpdateMotion(
	float deltaTime,
	bool isMoving,
	bool isDodging)
{
	motionTime_ += deltaTime;
	const float targetBlend = (isMoving || isDodging) ? 1.0f : 0.0f;
	const float blendSpeed = isMoving ? 9.5f : 6.0f;
	const float blendStep = std::clamp(deltaTime * blendSpeed, 0.0f, 1.0f);
	moveBlend_ = LerpFloat(moveBlend_, targetBlend, blendStep);
}

void PlayerPresentationController::ApplyPlayerTransform(
	Engine::Graphics3D::Object3D* playerObject,
	const Vector3& playerPosition,
	float playerRotationY,
	bool isDodging) const
{
	if (!playerObject) {
		return;
	}
	if (state_ == State::Death) {
		ApplyDeathPose(
			playerObject,
			playerPosition,
			playerRotationY,
			1.0f);
		return;
	}

	const float idleCycle = std::sin(motionTime_ * kPlayerIdleCycleSpeed);
	const float walkCycle = std::sin(motionTime_ * kPlayerWalkCycleSpeed);
	const float walkStep = std::abs(walkCycle);
	const float idleBob = idleCycle * 0.006f;
	const float walkBob = walkStep * 0.010f;
	const float bob = LerpFloat(idleBob, walkBob, moveBlend_);
	const float walkRoll = walkCycle * 0.006f * moveBlend_;
	const float walkLean = 0.006f * moveBlend_;
	const float idleScaleY = 1.0f + idleCycle * 0.002f * (1.0f - moveBlend_);
	const float walkScaleY = 1.0f - walkStep * 0.0015f * moveBlend_;
	const float squashScale = idleScaleY * walkScaleY;

	playerObject->SetRotate({
		kPlayerModelBasePitch + walkLean,
		playerRotationY,
		walkRoll,
		});
	playerObject->SetTranslate({
		playerPosition.x,
		playerPosition.y + bob,
		playerPosition.z,
		});
	if (isDodging) {
		playerObject->SetScale({
			kPlayerModelScale * 0.8f,
			kPlayerModelScale * 0.8f,
			kPlayerModelScale * 1.35f,
			});
	} else {
		playerObject->SetScale({
			kPlayerModelScale,
			kPlayerModelScale * squashScale,
			kPlayerModelScale,
			});
	}
	playerObject->SetColor(isDodging
		? Vector4{ 0.55f, 0.9f, 1.0f, 0.72f }
		: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
}

void PlayerPresentationController::ApplyDeathPose(
	Engine::Graphics3D::Object3D* playerObject,
	const Vector3& playerPosition,
	float playerRotationY,
	float progress) const
{
	if (!playerObject) {
		return;
	}

	const float fall = SmoothStep(progress);
	const bool hasDeathClip = playerObject->HasAnimationClip("death");
	playerObject->SetRotate({
		kPlayerModelBasePitch + (hasDeathClip ? 0.0f : fall * 1.42f),
		playerRotationY,
		hasDeathClip ? 0.0f : fall * -0.28f,
		});
	playerObject->SetScale({
		kPlayerModelScale,
		kPlayerModelScale,
		kPlayerModelScale,
		});
	playerObject->SetTranslate({
		playerPosition.x,
		playerPosition.y - (hasDeathClip ? 0.0f : fall * 0.55f),
		playerPosition.z,
		});
	playerObject->Update();
}

void PlayerPresentationController::SyncCamera(
	Engine::CameraSystem::Camera* camera) const
{
	camera->SetFarClip(500.0f);
	camera->Update();

	Engine::CameraSystem::CameraManager* cameraManager =
		Engine::CameraSystem::CameraManager::GetInstance();
	if (cameraManager->GetCamera(kGameCameraName) == camera) {
		cameraManager->SetActiveCamera(kGameCameraName);
	}
}

} // namespace DirectXGame
