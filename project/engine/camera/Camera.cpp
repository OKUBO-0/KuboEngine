#include "Camera.h"
#include "MyMath.h"

namespace {
const Vector3 kDefaultCameraTranslate = { 0.0f, 0.0f, -5.0f };
constexpr float kDefaultCameraFovY = 0.45f;
constexpr float kDefaultNearClip = 0.1f;
constexpr float kDefaultFarClip = 100.0f;
}

namespace Engine::CameraSystem {

Camera::Camera()
{
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f} ,kDefaultCameraTranslate };
	
	fovY = kDefaultCameraFovY;
	aspectRatio = float(Engine::Base::WinApp::kClientWidth) / float(Engine::Base::WinApp::kClientHeight);
	nearClip_ = kDefaultNearClip;
	farClip = kDefaultFarClip;
	projectionMatrix = MyMath::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip_, farClip);
	worldMatrix = MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	viewMatrix = worldMatrix.Inverse();
	viewProjectionMatrix = viewMatrix * projectionMatrix;

}

void Camera::Update()
{
	if (orthographic_) {
		// Centered unit bounds supply the DX12 depth terms. Compute the scale
		// directly in double to avoid overflowing height * aspect for large spans.
		projectionMatrix = MyMath::MakeOrthographicMatrix(-1, 1, 1, -1, nearClip_, farClip);
		projectionMatrix.m[0][0] = static_cast<float>(2.0 / orthographicHeight_ / aspectRatio);
		projectionMatrix.m[1][1] = static_cast<float>(2.0 / orthographicHeight_);
	} else {
		projectionMatrix = MyMath::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip_, farClip);
	}
	worldMatrix = hasAuthoredWorldMatrix_ ? authoredWorldMatrix_ : MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	viewMatrix = worldMatrix.Inverse();
	viewProjectionMatrix = viewMatrix * projectionMatrix;

}

}
