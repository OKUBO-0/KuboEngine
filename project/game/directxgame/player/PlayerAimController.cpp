#include "game/directxgame/player/PlayerAimController.h"

#include "Camera.h"
#include "Input.h"
#include "MyMath.h"
#include "Object3D.h"
#include "game/directxgame/core/GameInputBindings.h"
#include "game/directxgame/core/ScreenUtil.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

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

}

namespace DirectXGame {

void PlayerAimController::Update(
	float deltaTime,
	const Vector3& playerPosition,
	float& playerRotationY,
	Engine::CameraSystem::Camera* camera)
{
	Engine::InputSystem::Input* input = Engine::InputSystem::Input::GetInstance();
	if (!input || GameInputBindings::IsGameInputSuppressedByImGui()) {
		return;
	}

	const Engine::InputSystem::Input::MouseMove mouseMove = input->GetMouseMove();
	const bool mouseActive =
		mouseMove.lX != 0 || mouseMove.lY != 0 || input->PushMouse(0) || input->PushMouse(1);
	const bool keyboardMouseActive =
		GameInputBindings::HasKeyboardNavigationInput(input) || mouseActive;
	const bool gamepadActive = GameInputBindings::HasGamepadNavigationInput(input);

	Vector2 padAim{};
	if (GameInputBindings::GetAimVector(input, padAim)) {
		inputDevice_ = InputDevice::Gamepad;
		indicatorTracksMouse_ = false;
		const float targetAngle = std::atan2(padAim.x, padAim.y);
		const float difference = NormalizeAngle(targetAngle - playerRotationY);
		const float rotateLerp = std::clamp(deltaTime * 30.0f, 0.0f, 1.0f);
		playerRotationY = NormalizeAngle(playerRotationY + difference * rotateLerp);
		return;
	}

	if (gamepadActive) {
		inputDevice_ = InputDevice::Gamepad;
		indicatorTracksMouse_ = false;
	}
	if (keyboardMouseActive) {
		inputDevice_ = InputDevice::KeyboardMouse;
	}

	if (inputDevice_ != InputDevice::KeyboardMouse || !mouseAimEnabled_ || !camera) {
		return;
	}

	const Vector2 clientSize = ScreenUtil::GetClientSize();
	if (clientSize.x <= 0.0f || clientSize.y <= 0.0f ||
		!ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return;
	}

	const Vector2 mousePosition = ScreenUtil::ToGamePosition(input->GetMousePos());
	const Matrix4x4& viewProjection = camera->GetViewProjectionMatrix();
	Vector3 mouseGroundPosition{};
	if (UnprojectMouseToGround(
			mousePosition,
			clientSize,
			viewProjection,
			2.3f,
			mouseGroundPosition)) {
		indicatorPosition_ = mouseGroundPosition;
		indicatorTracksMouse_ = true;
	}

	Vector2 playerScreen{};
	Vector2 xAxisScreen{};
	Vector2 zAxisScreen{};
	if (!ProjectWorldToScreen(playerPosition, viewProjection, clientSize, playerScreen) ||
		!ProjectWorldToScreen(
			playerPosition + Vector3{ 1.0f, 0.0f, 0.0f },
			viewProjection,
			clientSize,
			xAxisScreen) ||
		!ProjectWorldToScreen(
			playerPosition + Vector3{ 0.0f, 0.0f, 1.0f },
			viewProjection,
			clientSize,
			zAxisScreen)) {
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
	const float determinant =
		xAxisDelta.x * zAxisDelta.y - zAxisDelta.x * xAxisDelta.y;
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
	const float difference = NormalizeAngle(targetAngle - playerRotationY);
	const float rotateLerp = std::clamp(deltaTime * 30.0f, 0.0f, 1.0f);
	playerRotationY = NormalizeAngle(playerRotationY + difference * rotateLerp);
	indicatorTracksMouse_ = true;
}

void PlayerAimController::ApplyIndicatorTransform(
	Engine::Graphics3D::Object3D* indicatorObject,
	const Vector3& playerPosition,
	float playerRotationY) const
{
	if (!indicatorObject) {
		return;
	}

	const Vector3 forward{
		std::sin(playerRotationY),
		0.0f,
		std::cos(playerRotationY),
	};
	const Vector3 indicatorPosition = indicatorTracksMouse_
		? indicatorPosition_
		: Vector3{
			playerPosition.x + forward.x * 8.0f,
			2.3f,
			playerPosition.z + forward.z * 8.0f,
		};
	indicatorObject->SetRotate({ 0.0f, 0.0f, 0.0f });
	indicatorObject->SetScale({ 0.48f, 0.48f, 0.48f });
	indicatorObject->SetTranslate(indicatorPosition);
}

} // namespace DirectXGame
