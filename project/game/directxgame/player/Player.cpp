#include "game/directxgame/player/Player.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/GameModelCache.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "CameraManager.h"
#include "Input.h"
#include "MyMath.h"
#include "Object3DCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float kFixedDeltaTime = 1.0f / 60.0f;
constexpr char kGameCameraName[] = "directxgame_player";
constexpr char kEnvironmentTexturePath[] = "Resources/textures/skybox/test.dds";
constexpr float kPlayerModelScale = 1.0f;

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

Vector3 NormalizeOrZero(const Vector3& vector)
{
	const float length = MyMath::Length(vector);
	if (length <= 0.0001f) {
		return { 0.0f, 0.0f, 0.0f };
	}
	return { vector.x / length, vector.y / length, vector.z / length };
}

bool ProjectWorldToScreen(
	const Vector3& worldPosition,
	const Matrix4x4& viewProjection,
	const Vector2& clientSize,
	Vector2& outScreen)
{
	const Vector3 ndc = MyMath::Transform(worldPosition, viewProjection);
	outScreen = {
		(ndc.x + 1.0f) * 0.5f * clientSize.x,
		(1.0f - ndc.y) * 0.5f * clientSize.y,
	};
	return std::isfinite(outScreen.x) && std::isfinite(outScreen.y);
}

}

namespace DirectXGame {

void Player::Initialize()
{
	InitializeCamera();
	InitializeObjects();
	ApplyTransforms();
}

void Player::InitializeCamera()
{
	camera_ = std::make_unique<Engine::CameraSystem::Camera>();
	UpdateCamera();
	camera_->Update();

	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(kGameCameraName, camera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kGameCameraName);
}

void Player::InitializeObjects()
{
	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);

	const ModelHandle playerHandle = GameModelCache::Load("octopus.obj");
	playerObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	playerObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*playerObject_, playerHandle);
	playerObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	playerObject_->SetEnvironmentReflectionStrength(0.0f);
	playerObject_->SetEnvironmentRoughness(1.0f);
	playerObject_->SetScale({ kPlayerModelScale, kPlayerModelScale, kPlayerModelScale });
	lightSettings_.ApplyTo(*playerObject_);

	const ModelHandle indicatorHandle = GameModelCache::Load("cube.obj");
	aimIndicatorObject_ = std::make_unique<Engine::Graphics3D::Object3D>();
	aimIndicatorObject_->Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	GameModelCache::ApplyToObject(*aimIndicatorObject_, indicatorHandle);
	aimIndicatorObject_->SetSkyboxFilePath(kEnvironmentTexturePath);
	aimIndicatorObject_->SetEnvironmentReflectionStrength(0.0f);
	aimIndicatorObject_->SetEnvironmentRoughness(1.0f);
	aimIndicatorObject_->SetColor({ 1.0f, 1.0f, 1.0f, 0.9f });
	lightSettings_.ApplyTo(*aimIndicatorObject_);
}

void Player::Update(float deltaTime)
{
	UpdateMovement(deltaTime);
	UpdateCamera();
	UpdateAim(deltaTime);
	UpdateCamera();
	ApplyTransforms();

	if (camera_) {
		camera_->Update();
	}
	if (playerObject_) {
		playerObject_->Update();
	}
	if (aimIndicatorObject_) {
		aimIndicatorObject_->Update();
	}
}

void Player::Draw()
{
	if (aimIndicatorObject_) {
		aimIndicatorObject_->Draw();
	}
	if (visible_ && playerObject_) {
		playerObject_->Draw();
	}
}

void Player::SetLightSettings(const GameLightSettings& lightSettings)
{
	lightSettings_ = lightSettings;
	if (playerObject_) {
		lightSettings_.ApplyTo(*playerObject_);
	}
	if (aimIndicatorObject_) {
		lightSettings_.ApplyTo(*aimIndicatorObject_);
	}
}

void Player::UpdateMovement(float deltaTime)
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const Vector2 moveInput = GameInputBindings::GetMoveVector(input);
	const float movePerFrame = moveSpeedPerSecond_ * deltaTime;

	position_.x += moveInput.x * movePerFrame;
	position_.z += moveInput.y * movePerFrame;
}

void Player::UpdateAim(float deltaTime)
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const Engine::InputSystem::Input::MouseMove mouseMove = input ? input->GetMouseMove() : Engine::InputSystem::Input::MouseMove{};
	const bool mouseMoved = mouseMove.lX != 0 || mouseMove.lY != 0;

	Vector2 padAim{};
	if (!mouseMoved && GameInputBindings::GetAimVector(input, padAim)) {
		const float targetAngle = std::atan2(padAim.x, padAim.y);
		const float diff = NormalizeAngle(targetAngle - rotationY_);
		const float rotateLerp = std::clamp(deltaTime * 30.0f, 0.0f, 1.0f);
		rotationY_ = NormalizeAngle(rotationY_ + diff * rotateLerp);
		return;
	}

	if (!mouseAimEnabled_ || !camera_ || !input) {
		return;
	}

	const Vector2 clientSize = ScreenUtil::GetClientSize();
	if (clientSize.x <= 0.0f || clientSize.y <= 0.0f) {
		return;
	}

	const Vector2 mousePosition = input->GetMousePos();
	const Matrix4x4& viewProjection = camera_->GetViewProjectionMatrix();

	Vector2 playerScreen{};
	Vector2 xAxisScreen{};
	Vector2 zAxisScreen{};
	if (!ProjectWorldToScreen(position_, viewProjection, clientSize, playerScreen) ||
		!ProjectWorldToScreen(position_ + Vector3{ 1.0f, 0.0f, 0.0f }, viewProjection, clientSize, xAxisScreen) ||
		!ProjectWorldToScreen(position_ + Vector3{ 0.0f, 0.0f, 1.0f }, viewProjection, clientSize, zAxisScreen)) {
		return;
	}

	const Vector2 mouseDelta{
		mousePosition.x - playerScreen.x,
		mousePosition.y - playerScreen.y,
	};
	if (std::abs(mouseDelta.x) <= 1.0f && std::abs(mouseDelta.y) <= 1.0f) {
		return;
	}

	const Vector2 xAxisDelta{
		xAxisScreen.x - playerScreen.x,
		xAxisScreen.y - playerScreen.y,
	};
	const Vector2 zAxisDelta{
		zAxisScreen.x - playerScreen.x,
		zAxisScreen.y - playerScreen.y,
	};
	const float determinant = xAxisDelta.x * zAxisDelta.y - zAxisDelta.x * xAxisDelta.y;
	if (std::abs(determinant) <= 0.0001f) {
		return;
	}

	Vector3 aimDirection{
		(mouseDelta.x * zAxisDelta.y - zAxisDelta.x * mouseDelta.y) / determinant,
		0.0f,
		(xAxisDelta.x * mouseDelta.y - mouseDelta.x * xAxisDelta.y) / determinant,
	};
	aimDirection = NormalizeOrZero(aimDirection);
	if (MyMath::Length(aimDirection) <= 0.0001f) {
		return;
	}
	const float targetAngle = std::atan2(aimDirection.x, aimDirection.z);
	const float diff = NormalizeAngle(targetAngle - rotationY_);
	const float rotateLerp = std::clamp(deltaTime * 30.0f, 0.0f, 1.0f);
	rotationY_ = NormalizeAngle(rotationY_ + diff * rotateLerp);
}

void Player::UpdateAimIndicator()
{
	if (!aimIndicatorObject_) {
		return;
	}

	const Vector3 forward{ std::sin(rotationY_), 0.0f, std::cos(rotationY_) };
	constexpr float kIndicatorLength = 2.0f;
	constexpr float kIndicatorStartOffset = 5.75f;
	constexpr float kIndicatorHalfLength = kIndicatorLength * 0.5f;

	aimIndicatorObject_->SetRotate({ 0.0f, rotationY_, 0.0f });
	aimIndicatorObject_->SetScale({ 0.28f, 0.08f, kIndicatorLength });
	aimIndicatorObject_->SetTranslate({
		position_.x + forward.x * (kIndicatorStartOffset + kIndicatorHalfLength),
		-1.0f,
		position_.z + forward.z * (kIndicatorStartOffset + kIndicatorHalfLength),
		});
}

void Player::UpdateCamera()
{
	if (!camera_) {
		return;
	}

	auto applyCamera = [this](Engine::CameraSystem::Camera& camera) {
		switch (cameraMode_) {
		case CameraMode::PlayerBack: {
			const Vector3 forward{ std::sin(rotationY_), 0.0f, std::cos(rotationY_) };
			camera.SetTranslate({
				position_.x - forward.x * cameraDistance_,
				cameraHeight_,
				position_.z - forward.z * cameraDistance_,
				});
			camera.SetRotate({ cameraPitch_, rotationY_, 0.0f });
			break;
		}
		case CameraMode::WorldFront:
			camera.SetTranslate({ position_.x, cameraHeight_, position_.z + cameraDistance_ });
			camera.SetRotate({ cameraPitch_, 3.14159265f, 0.0f });
			break;
		case CameraMode::TopDown:
			camera.SetTranslate({ position_.x, cameraHeight_, position_.z });
			camera.SetRotate({ 1.57079633f, 0.0f, 0.0f });
			break;
		case CameraMode::WorldBack:
		default:
			camera.SetTranslate({ position_.x, cameraHeight_, position_.z - cameraDistance_ });
			camera.SetRotate({ cameraPitch_, 0.0f, 0.0f });
			break;
		}
		camera.SetFarClip(500.0f);
		camera.Update();
		};

	applyCamera(*camera_);
	Engine::CameraSystem::CameraManager* cameraManager =
		Engine::CameraSystem::CameraManager::GetInstance();
	if (cameraManager->SyncCamera(kGameCameraName, camera_.get())) {
		cameraManager->SetActiveCamera(kGameCameraName);
	}
}

void Player::ApplyTransforms()
{
	if (playerObject_) {
		playerObject_->SetRotate({ 0.0f, rotationY_, 0.0f });
		playerObject_->SetTranslate(position_);
	}
	UpdateAimIndicator();
}

} // namespace DirectXGame
