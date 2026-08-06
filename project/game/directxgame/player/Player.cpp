#include "Player.h"
#include "CameraManager.h"
#include <algorithm>

namespace {

constexpr char kGameCameraName[] = "directxgame_player";
constexpr float kVisualMoveStartDistanceSq = 0.0004f;
constexpr float kVisualMoveStopGraceSeconds = 0.12f;
}

namespace DirectXGame {

void Player::Initialize()
{
	InitializeCamera();
	view_.Initialize();
	ApplyTransforms();
	cameraController_.ResetFocus(position_);
	if (camera_) {
		cameraController_.Update(camera_.get(), position_, rotationY_, false);
	}
	view_.Update();
}

void Player::SetCharacterId(CharacterId characterId)
{
	view_.SetCharacterId(characterId);
	ApplyTransforms();
	view_.Update();
}

void Player::InitializeCamera()
{
	camera_ = std::make_unique<Engine::CameraSystem::Camera>();
	cameraController_.Update(camera_.get(), position_, rotationY_, false);
	camera_->Update();

	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera(kGameCameraName, camera_.get());
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(kGameCameraName);
}

float Player::GetCollisionRadius() const
{
	return view_.GetCollisionRadius();
}

Engine::Math::AABB Player::GetCollisionAabb() const
{
	return view_.GetCollisionAabb(position_);
}

Engine::Math::OBB Player::GetCollisionObb() const
{
	return view_.GetCollisionObb(position_);
}

void Player::Update(float deltaTime)
{
	presentationController_.BeginNormalFrame();
	const Vector3 previousPosition = position_;
	movementController_.Update(
		deltaTime,
		position_,
		rotationY_,
		moveSpeedPerSecond_,
		cameraController_.GetYaw(),
		cameraController_.UsesCameraRelativeMovement());
	const float movedDistanceSq =
		(position_.x - previousPosition.x) * (position_.x - previousPosition.x) +
		(position_.z - previousPosition.z) * (position_.z - previousPosition.z);
	if (movedDistanceSq > kVisualMoveStartDistanceSq) {
		visualMoving_ = true;
		visualMovingHoldTimer_ = kVisualMoveStopGraceSeconds;
	} else {
		visualMovingHoldTimer_ =
			(std::max)(0.0f, visualMovingHoldTimer_ - deltaTime);
		visualMoving_ = visualMovingHoldTimer_ > 0.0f;
	}
	view_.SetMotionState(visualMoving_, IsDashing(), IsJumping());
	presentationController_.UpdateMotion(deltaTime, visualMoving_ || IsDashing(), false);
	cameraController_.Update(camera_.get(), position_, rotationY_, true);
	if (!cameraController_.UsesMouseLook()) {
		aimController_.Update(deltaTime, position_, rotationY_, camera_.get());
	}
	cameraController_.Update(camera_.get(), position_, rotationY_, false);
	ApplyTransforms();

	if (camera_) {
		camera_->Update();
	}
	view_.Update();
}

void Player::Draw()
{
	view_.Draw(visible_, !presentationController_.IsActive());
}

void Player::DrawShadow()
{
	view_.DrawShadow(visible_);
}

void Player::StartIntroPresentation()
{
	visible_ = true;
	presentationController_.StartIntro(position_, cameraController_);
	UpdateIntroPresentation(0.0f, 1.0f);
}

void Player::UpdateIntroPresentation(float elapsedTime, float duration)
{
	visible_ = true;
	presentationController_.UpdateIntro(
		elapsedTime,
		duration,
		position_,
		rotationY_,
		view_.GetPlayerObject(),
		camera_.get(),
		cameraController_);
}

void Player::BeginCinematicPresentation()
{
	visible_ = true;
	presentationController_.BeginNormalFrame();
	position_.y = 0.0f;
	visualMoving_ = false;
	visualMovingHoldTimer_ = 0.0f;
	movementController_.ResetActionState();
	view_.SetMotionState(false, false, false);
	ApplyTransforms();
	view_.Update();
}

void Player::UpdateCinematicPresentation()
{
	visible_ = true;
	presentationController_.BeginNormalFrame();
	position_.y = 0.0f;
	visualMoving_ = false;
	visualMovingHoldTimer_ = 0.0f;
	view_.SetMotionState(false, false, false);
	ApplyTransforms();
	view_.Update();
}

void Player::StartDeathPresentation()
{
	visible_ = true;
	position_.y = 0.0f;
	movementController_.ResetActionState();
	view_.SetMotionState(false, false, false);
	view_.SetDeathAnimationActive(true);
	presentationController_.StartDeath(
		position_,
		rotationY_,
		view_.GetPlayerObject(),
		camera_.get(),
		cameraController_);
}

void Player::NotifyHitReact()
{
	view_.NotifyHitReact();
}

void Player::UpdateDeathPresentation(float elapsedTime, float duration)
{
	visible_ = true;
	presentationController_.UpdateDeath(
		elapsedTime,
		duration,
		position_,
		rotationY_,
		view_.GetPlayerObject(),
		camera_.get());
}

float Player::GetDodgeCooldownRatio() const
{
	return movementController_.GetDodgeCooldownRatio();
}

void Player::RequestCameraShake(float duration, float strength)
{
	cameraController_.RequestShake(duration, strength);
}

void Player::ApplyTransforms()
{
	presentationController_.ApplyPlayerTransform(
		view_.GetPlayerObject(),
		position_,
		rotationY_,
		false);
	aimController_.ApplyIndicatorTransform(
		view_.GetAimIndicatorObject(),
		position_,
		rotationY_);
}

} // namespace DirectXGame
