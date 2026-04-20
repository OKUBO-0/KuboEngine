#pragma once
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "SrvManager.h"

/// @brief 3D オブジェクト描画の共通設定を管理するクラス
/// @details 通常描画とスキニング描画のパイプラインを初期化し、
///          各 Object3D が使う共通レンダリング状態を適用する。
namespace Engine::Graphics3D {

class Object3DCommon
{
public:

	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return Object3DCommon のインスタンス
	static Object3DCommon* GetInstance();



	/// @brief 3D 描画共通リソースを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvmanager SRV 管理クラス
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon,Engine::Base::SrvManager*srvmanager);

	/// @brief 共通管理インスタンスを解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief 通常 3D 描画の共通ステートを設定する
	/// @param なし
	/// @return なし
	void CommonDraw();

	/// @brief スキニング 3D 描画の共通ステートを設定する
	/// @param なし
	/// @return なし
	void SkinningCommonDraw();

	//DXCommon
	Engine::Base::DirectXCommon* GetDxCommon()const { return dxCommon_; }
	//SrvManager
	Engine::Base::SrvManager* GetSrvManager()const { return srvManager_; }

	

private:

	Object3DCommon() = default;
	~Object3DCommon() = default;
	Object3DCommon(const Object3DCommon&) = delete;
	Object3DCommon& operator=(const Object3DCommon&) = delete;

private:
	Engine::Base::DirectXCommon* dxCommon_;
	Engine::Base::SrvManager* srvManager_ = nullptr;

	std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_;
	std::unique_ptr<Engine::Base::GraphicsPipeline> skinningGraphicsPipeline_;
};

}

