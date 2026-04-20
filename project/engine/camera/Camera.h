#pragma once
#include "RenderingData.h"
#include <WinApp.h>

/// @brief ビュー行列と射影行列を生成するカメラクラス
/// @details 位置、回転、投影パラメータからワールド・ビュー・射影行列を更新し、
///          描画時に利用するビュー射影情報を提供する。
namespace Engine::CameraSystem {

class Camera
{
public:
	/// @brief 既定値でカメラを生成する
	/// @param なし
	/// @return なし
	Camera();

	/// @brief カメラ行列を更新する
	/// @param なし
	/// @return なし
	void Update();

	/// @brief カメラの回転を設定する
	/// @param rotate 設定する回転
	/// @return なし
	void SetRotate(const Vector3& rotate) { this->transform.rotate = rotate; }
	/// @brief カメラの位置を設定する
	/// @param translate 設定する位置
	/// @return なし
	void SetTranslate(const Vector3& translate) { this->transform.translate = translate; }
	/// @brief 垂直方向視野角を設定する
	/// @param fovy 設定する視野角
	/// @return なし
	void SetFovY(float fovy) { this->fovY = fovy; }
	/// @brief アスペクト比を設定する
	/// @param aspectRation 設定するアスペクト比
	/// @return なし
	void SetAspectRatio(float aspectRation) { this->aspectRatio = aspectRation; }
	/// @brief ニアクリップ距離を設定する
	/// @param nearClip 設定するニアクリップ距離
	/// @return なし
	void SetNearClip(float nearClip) { this->nearClip_ = nearClip; }
	/// @brief ファークリップ距離を設定する
	/// @param farClip 設定するファークリップ距離
	/// @return なし
	void SetFarClip(float farClip) { this->farClip = farClip; }

	const Matrix4x4& GetWorldMatrix()const { return worldMatrix; }
	const Matrix4x4& GetViewMatrix()const { return viewMatrix; }
	const Matrix4x4& GetProjectionMatrix()const { return projectionMatrix; }
	const Matrix4x4& GetViewProjectionMatrix()const { return viewProjectionMatrix; }
	const EulerTransform& GetTransform()const { return transform; }

private:
	
	//カメラ用のTransformを作る
	EulerTransform transform;
	Matrix4x4 worldMatrix;
	Matrix4x4 viewMatrix;
	Matrix4x4 projectionMatrix;
	Matrix4x4 viewProjectionMatrix;


	float fovY=0.45f;
	float aspectRatio= float(Engine::Base::WinApp::kClientWidth) / float(Engine::Base::WinApp::kClientHeight);
	float nearClip_ = 0.1f;
	float farClip=100.0f;

	
};

}

