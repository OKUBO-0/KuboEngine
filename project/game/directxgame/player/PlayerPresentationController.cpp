#include "PlayerPresentationController.h"

#include "Camera.h"
#include "CameraManager.h"
#include "Object3D.h"
#include "PlayerCameraController.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr char kGameCameraName[] = "directxgame_player";
constexpr float kPlayerModelScale = 1.0f;
constexpr float kPresentationCameraDistance = 24.0f;
constexpr float kPresentationCameraHeight = 24.0f;
constexpr float kPresentationCameraPitch = 0.72f;

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
		playerObject->SetRotate({ 0.0f, playerRotationY, 0.0f });
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
	const float distance = LerpFloat(18.0f, cameraController.GetDistance(), returnProgress);
	const float height = LerpFloat(10.0f, cameraController.GetHeight(), returnProgress);
	const float orbitAngle = LerpFloat(-1.55f, 0.0f, orbitProgress);
	const float pitch = std::atan2(height - playerPosition.y, distance);
	camera->SetTranslate({
		playerPosition.x + std::sin(orbitAngle) * distance,
		height,
		playerPosition.z - std::cos(orbitAngle) * distance,
		});
	camera->SetRotate({ pitch, -orbitAngle, 0.0f });
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
	const float pitch = LerpFloat(
		deathStartCameraPitch_,
		kPresentationCameraPitch,
		progress);
	camera->SetTranslate({
		playerPosition.x,
		height,
		playerPosition.z - distance,
		});
	camera->SetRotate({ pitch, 0.0f, 0.0f });
	SyncCamera(camera);
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

	playerObject->SetRotate({ 0.0f, playerRotationY, 0.0f });
	playerObject->SetTranslate(playerPosition);
	playerObject->SetScale(isDodging
		? Vector3{
			kPlayerModelScale * 0.8f,
			kPlayerModelScale * 0.8f,
			kPlayerModelScale * 1.35f,
			}
		: Vector3{
			kPlayerModelScale,
			kPlayerModelScale,
			kPlayerModelScale,
			});
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
	playerObject->SetRotate({
		fall * 1.42f,
		playerRotationY,
		fall * -0.28f,
		});
	playerObject->SetTranslate({
		playerPosition.x,
		playerPosition.y - fall * 0.55f,
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
