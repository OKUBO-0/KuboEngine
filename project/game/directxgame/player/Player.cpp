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
constexpr float kPlayerFallbackCollisionRadius = 1.0f;
constexpr float kPresentationCameraDistance = 24.0f;
constexpr float kPresentationCameraHeight = 24.0f;
constexpr float kPresentationCameraPitch = 0.72f;

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

bool UnprojectMouseToGround(
	const Vector2& mousePosition,
	const Vector2& clientSize,
	const Matrix4x4& viewProjection,
	float groundY,
	Vector3& outPosition)
{
	const float ndcX = mousePosition.x / clientSize.x * 2.0f - 1.0f;
	const float ndcY = 1.0f - mousePosition.y / clientSize.y * 2.0f;
	const Matrix4x4 inverseViewProjection = viewProjection.Inverse();
	const Vector3 nearPoint = MyMath::Transform({ ndcX, ndcY, 0.0f }, inverseViewProjection);
	const Vector3 farPoint = MyMath::Transform({ ndcX, ndcY, 1.0f }, inverseViewProjection);
	const Vector3 ray = farPoint - nearPoint;
	if (std::abs(ray.y) <= 0.0001f) {
		return false;
	}
	const float distance = (groundY - nearPoint.y) / ray.y;
	if (distance < 0.0f || !std::isfinite(distance)) {
		return false;
	}
	outPosition = nearPoint + ray * distance;
	return std::isfinite(outPosition.x) && std::isfinite(outPosition.z);
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

void Player::Initialize()
{
	InitializeCamera();
	InitializeObjects();
	ApplyTransforms();
	cameraFocusPosition_ = position_;
	cameraFollowInitialized_ = true;
	if (camera_) {
		UpdateCamera(false);
	}
	if (playerObject_) {
		playerObject_->Update();
	}
	if (aimIndicatorObject_) {
		aimIndicatorObject_->Update();
	}
}

void Player::InitializeCamera()
{
	camera_ = std::make_unique<Engine::CameraSystem::Camera>();
	UpdateCamera(false);
	camera_->Update();

	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(kGameCameraName, camera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kGameCameraName);
}

float Player::GetCollisionRadius() const
{
	return playerObject_ ? playerObject_->GetScaledModelBoundingRadius(kPlayerFallbackCollisionRadius) : kPlayerFallbackCollisionRadius;
}

Engine::Math::AABB Player::GetCollisionAabb() const
{
	return playerObject_ ? playerObject_->GetScaledModelAabb(kPlayerFallbackCollisionRadius) : Engine::Math::AABB{
		{ position_.x - kPlayerFallbackCollisionRadius, position_.y - kPlayerFallbackCollisionRadius, position_.z - kPlayerFallbackCollisionRadius },
		{ position_.x + kPlayerFallbackCollisionRadius, position_.y + kPlayerFallbackCollisionRadius, position_.z + kPlayerFallbackCollisionRadius },
	};
}

Engine::Math::OBB Player::GetCollisionObb() const
{
	return playerObject_ ? playerObject_->GetScaledModelObb(kPlayerFallbackCollisionRadius) : Engine::Math::OBB{
		position_,
		{ Vector3{ 1.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f }, Vector3{ 0.0f, 0.0f, 1.0f } },
		Vector3{ kPlayerFallbackCollisionRadius, kPlayerFallbackCollisionRadius, kPlayerFallbackCollisionRadius },
	};
}

void Player::InitializeObjects()
{
	Engine::Base::TextureManager::GetInstance()->LoadTexture(kEnvironmentTexturePath);

	const ModelHandle playerHandle = GameModelCache::Load("cube.obj");
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
	aimIndicatorObject_->SetLighting(false);
	aimIndicatorObject_->SetColor({ 0.15f, 1.0f, 1.0f, 1.0f });
	lightSettings_.ApplyTo(*aimIndicatorObject_);
}

void Player::Update(float deltaTime)
{
	deathPresentationActive_ = false;
	introPresentationActive_ = false;
	UpdateMovement(deltaTime);
	UpdateCamera(true);
	UpdateAim(deltaTime);
	UpdateCamera(false);
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
	if (!deathPresentationActive_ && !introPresentationActive_ && aimIndicatorObject_) {
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

void Player::StartIntroPresentation()
{
	visible_ = true;
	introPresentationActive_ = true;
	cameraFocusPosition_ = position_;
	cameraFollowInitialized_ = true;
	UpdateIntroPresentation(0.0f, 1.0f);
}

void Player::UpdateIntroPresentation(float elapsedTime, float duration)
{
	introPresentationActive_ = true;
	visible_ = true;
	const float progress = SmoothStep(duration > 0.0f ? elapsedTime / duration : 1.0f);
	if (playerObject_) {
		playerObject_->SetRotate({ 0.0f, rotationY_, 0.0f });
		playerObject_->SetTranslate(position_);
		playerObject_->Update();
	}

	if (camera_) {
		const float distance = LerpFloat(kPresentationCameraDistance, cameraDistance_, progress);
		const float height = LerpFloat(kPresentationCameraHeight, cameraHeight_, progress);
		const float pitch = LerpFloat(kPresentationCameraPitch, cameraPitch_, progress);
		camera_->SetTranslate({ position_.x, height, position_.z - distance });
		camera_->SetRotate({ pitch, 0.0f, 0.0f });
		camera_->SetFarClip(500.0f);
		camera_->Update();

		Engine::CameraSystem::CameraManager* cameraManager =
			Engine::CameraSystem::CameraManager::GetInstance();
		if (cameraManager->SyncCamera(kGameCameraName, camera_.get())) {
			cameraManager->SetActiveCamera(kGameCameraName);
		}
	}
}

void Player::StartDeathPresentation()
{
	visible_ = true;
	deathPresentationActive_ = true;
	deathStartCameraHeight_ = cameraHeight_;
	deathStartCameraDistance_ = cameraDistance_;
	deathStartCameraPitch_ = cameraPitch_;
	cameraFocusPosition_ = position_;
	cameraFollowInitialized_ = true;
	ApplyDeathPose(0.0f);
	UpdateDeathPresentation(0.0f, 1.0f);
}

void Player::UpdateDeathPresentation(float elapsedTime, float duration)
{
	deathPresentationActive_ = true;
	visible_ = true;
	const float progress = SmoothStep(duration > 0.0f ? elapsedTime / duration : 1.0f);
	ApplyDeathPose(progress);

	if (camera_) {
		const float distance = LerpFloat(deathStartCameraDistance_, kPresentationCameraDistance, progress);
		const float height = LerpFloat(deathStartCameraHeight_, kPresentationCameraHeight, progress);
		const float pitch = LerpFloat(deathStartCameraPitch_, kPresentationCameraPitch, progress);
		camera_->SetTranslate({ position_.x, height, position_.z - distance });
		camera_->SetRotate({ pitch, 0.0f, 0.0f });
		camera_->SetFarClip(500.0f);
		camera_->Update();

		Engine::CameraSystem::CameraManager* cameraManager =
			Engine::CameraSystem::CameraManager::GetInstance();
		if (cameraManager->SyncCamera(kGameCameraName, camera_.get())) {
			cameraManager->SetActiveCamera(kGameCameraName);
		}
	}
}

void Player::UpdateMovement(float deltaTime)
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const Vector2 moveInput = GameInputBindings::GetMoveVector(input);
	UpdateDodge(deltaTime, moveInput);
	if (IsDodging()) {
		position_.x += dodgeDirection_.x * kDodgeSpeed * deltaTime;
		position_.z += dodgeDirection_.z * kDodgeSpeed * deltaTime;
		return;
	}
	const float movePerFrame = moveSpeedPerSecond_ * deltaTime;

	position_.x += moveInput.x * movePerFrame;
	position_.z += moveInput.y * movePerFrame;
}

void Player::UpdateDodge(float deltaTime, const Vector2& moveInput)
{
	dodgeCooldownTimer_ = (std::max)(0.0f, dodgeCooldownTimer_ - deltaTime);
	dodgeTimer_ = (std::max)(0.0f, dodgeTimer_ - deltaTime);

	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	const bool dodgeTriggered =
		input &&
		!GameInputBindings::IsGameInputSuppressedByImGui() &&
		(input->TriggerKey(DIK_SPACE) || input->TriggerGamePadButton(XINPUT_GAMEPAD_B));
	if (suppressNextDodgeTrigger_) {
		suppressNextDodgeTrigger_ = false;
		return;
	}
	if (dodgeTriggered && dodgeCooldownTimer_ <= 0.0f) {
		Vector3 direction{ moveInput.x, 0.0f, moveInput.y };
		const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
		if (length > 0.001f) {
			direction.x /= length;
			direction.z /= length;
		} else {
			direction = { std::sin(rotationY_), 0.0f, std::cos(rotationY_) };
		}
		dodgeDirection_ = direction;
		dodgeTimer_ = kDodgeDuration;
		dodgeCooldownTimer_ = kDodgeCooldown;
		rotationY_ = std::atan2(direction.x, direction.z);
	}
}

float Player::GetDodgeCooldownRatio() const
{
	return std::clamp(dodgeCooldownTimer_ / kDodgeCooldown, 0.0f, 1.0f);
}

void Player::UpdateAim(float deltaTime)
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (!input || GameInputBindings::IsGameInputSuppressedByImGui()) {
		return;
	}

	const Engine::InputSystem::Input::MouseMove mouseMove = input ? input->GetMouseMove() : Engine::InputSystem::Input::MouseMove{};
	const bool mouseActive = mouseMove.lX != 0 || mouseMove.lY != 0 || input->PushMouse(0) || input->PushMouse(1);
	const bool keyboardActive = GameInputBindings::HasKeyboardNavigationInput(input);
	const bool keyboardMouseActive = keyboardActive || mouseActive;
	const bool gamepadActive = GameInputBindings::HasGamepadNavigationInput(input);

	Vector2 padAim{};
	if (GameInputBindings::GetAimVector(input, padAim)) {
		aimInputDevice_ = AimInputDevice::Gamepad;
		aimIndicatorTracksMouse_ = false;
		const float targetAngle = std::atan2(padAim.x, padAim.y);
		const float diff = NormalizeAngle(targetAngle - rotationY_);
		const float rotateLerp = std::clamp(deltaTime * 30.0f, 0.0f, 1.0f);
		rotationY_ = NormalizeAngle(rotationY_ + diff * rotateLerp);
		return;
	}

	if (gamepadActive) {
		aimInputDevice_ = AimInputDevice::Gamepad;
		aimIndicatorTracksMouse_ = false;
	}
	if (keyboardMouseActive) {
		aimInputDevice_ = AimInputDevice::KeyboardMouse;
	}

	if (aimInputDevice_ != AimInputDevice::KeyboardMouse) {
		return;
	}

	if (!mouseAimEnabled_ || !camera_ || !input) {
		return;
	}

	const Vector2 clientSize = ScreenUtil::GetClientSize();
	if (clientSize.x <= 0.0f || clientSize.y <= 0.0f) {
		return;
	}

	if (!ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return;
	}
	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
	const Matrix4x4& viewProjection = camera_->GetViewProjectionMatrix();
	Vector3 mouseGroundPosition{};
	if (UnprojectMouseToGround(mousePosition, clientSize, viewProjection, 2.3f, mouseGroundPosition)) {
		aimIndicatorPosition_ = mouseGroundPosition;
		aimIndicatorTracksMouse_ = true;
	}
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
	aimIndicatorTracksMouse_ = true;
}

void Player::UpdateAimIndicator()
{
	if (!aimIndicatorObject_) {
		return;
	}

	const Vector3 forward{ std::sin(rotationY_), 0.0f, std::cos(rotationY_) };
	const Vector3 indicatorPosition = aimIndicatorTracksMouse_
		? aimIndicatorPosition_
		: Vector3{ position_.x + forward.x * 8.0f, 2.3f, position_.z + forward.z * 8.0f };
	aimIndicatorObject_->SetRotate({ 0.0f, 0.0f, 0.0f });
	aimIndicatorObject_->SetScale({ 0.48f, 0.48f, 0.48f });
	aimIndicatorObject_->SetTranslate(indicatorPosition);
}

void Player::UpdateCamera(bool advanceFollow)
{
	if (!camera_) {
		return;
	}

	if (!cameraFollowInitialized_) {
		cameraFocusPosition_ = position_;
		cameraFollowInitialized_ = true;
	} else if (advanceFollow) {
		const float clampedSmoothness = (std::max)(0.0f, cameraFollowSmoothness_);
		const float followRate = clampedSmoothness <= 0.0f
			? 0.0f
			: 1.0f - std::exp(-clampedSmoothness * kFixedDeltaTime);
		cameraFocusPosition_.x += (position_.x - cameraFocusPosition_.x) * followRate;
		cameraFocusPosition_.y += (position_.y - cameraFocusPosition_.y) * followRate;
		cameraFocusPosition_.z += (position_.z - cameraFocusPosition_.z) * followRate;
	}
	if (advanceFollow) {
		const float zoomRate = 1.0f - std::exp(-3.6f * kFixedDeltaTime);
		combatCameraDistance_ += (combatCameraTargetDistance_ - combatCameraDistance_) * zoomRate;
		combatCameraHeight_ += (combatCameraTargetHeight_ - combatCameraHeight_) * zoomRate;
		cameraShakeCooldownTimer_ = (std::max)(0.0f, cameraShakeCooldownTimer_ - kFixedDeltaTime);
		cameraShakeTimer_ = (std::max)(0.0f, cameraShakeTimer_ - kFixedDeltaTime);
		cameraShakePhase_ += 2.1f;
	}

	const Vector3 focus = cameraFocusPosition_;

	auto applyCamera = [this, focus](Engine::CameraSystem::Camera& camera) {
		const float effectiveDistance = combatCameraDistance_;
		const float effectiveHeight = combatCameraHeight_;
		switch (cameraMode_) {
		case CameraMode::PlayerBack: {
			const Vector3 forward{ std::sin(rotationY_), 0.0f, std::cos(rotationY_) };
			camera.SetTranslate({
				focus.x - forward.x * effectiveDistance,
				effectiveHeight,
				focus.z - forward.z * effectiveDistance,
				});
			camera.SetRotate({ cameraPitch_, rotationY_, 0.0f });
			break;
		}
		case CameraMode::WorldFront:
			camera.SetTranslate({ focus.x, effectiveHeight, focus.z + effectiveDistance });
			camera.SetRotate({ cameraPitch_, 3.14159265f, 0.0f });
			break;
		case CameraMode::TopDown:
			camera.SetTranslate({ focus.x, effectiveHeight, focus.z });
			camera.SetRotate({ 1.57079633f, 0.0f, 0.0f });
			break;
		case CameraMode::WorldBack:
		default:
			camera.SetTranslate({ focus.x, effectiveHeight, focus.z - effectiveDistance });
			camera.SetRotate({ cameraPitch_, 0.0f, 0.0f });
			break;
		}
		if (cameraShakeTimer_ > 0.0f && cameraShakeDuration_ > 0.0f) {
			const float fade = cameraShakeTimer_ / cameraShakeDuration_;
			Vector3 shakenPosition = camera.GetTransform().translate;
			shakenPosition.x += std::sin(cameraShakePhase_ * 2.3f) * cameraShakeStrength_ * fade;
			shakenPosition.y += std::cos(cameraShakePhase_ * 1.7f) * cameraShakeStrength_ * 0.38f * fade;
			camera.SetTranslate(shakenPosition);
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

void Player::RequestCameraShake(float duration, float strength)
{
	if (cameraShakeCooldownTimer_ > 0.0f) {
		return;
	}
	cameraShakeDuration_ = std::clamp(duration, 0.0f, 0.16f);
	cameraShakeTimer_ = cameraShakeDuration_;
	cameraShakeStrength_ = std::clamp(strength, 0.0f, 0.85f);
	cameraShakeCooldownTimer_ = 0.12f;
	cameraShakePhase_ = 0.0f;
}

void Player::SetCombatCameraTarget(float distance, float height)
{
	combatCameraTargetDistance_ = std::clamp(distance, 30.0f, 62.0f);
	combatCameraTargetHeight_ = std::clamp(height, 54.0f, 96.0f);
}

void Player::ApplyTransforms()
{
	if (playerObject_) {
		if (deathPresentationActive_) {
			ApplyDeathPose(1.0f);
		} else {
			playerObject_->SetRotate({ 0.0f, rotationY_, 0.0f });
			playerObject_->SetTranslate(position_);
			playerObject_->SetScale(IsDodging()
				? Vector3{ kPlayerModelScale * 0.8f, kPlayerModelScale * 0.8f, kPlayerModelScale * 1.35f }
				: Vector3{ kPlayerModelScale, kPlayerModelScale, kPlayerModelScale });
			playerObject_->SetColor(IsDodging()
				? Vector4{ 0.55f, 0.9f, 1.0f, 0.72f }
				: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
		}
	}
	UpdateAimIndicator();
}

void Player::ApplyDeathPose(float progress)
{
	if (!playerObject_) {
		return;
	}

	const float fall = SmoothStep(progress);
	playerObject_->SetRotate({
		fall * 1.42f,
		rotationY_,
		fall * -0.28f,
		});
	playerObject_->SetTranslate({
		position_.x,
		position_.y - fall * 0.55f,
		position_.z,
		});
	playerObject_->Update();
}

} // namespace DirectXGame
