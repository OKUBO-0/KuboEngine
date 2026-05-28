#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "Camera.h"
#include "GraphicsPipeline.h"
#include <memory>

/// @brief スカイボックス描画の共通設定を管理するクラス
/// @details スカイボックス用パイプラインと DirectX 依存を保持し、
///          個別 SkyBox インスタンスへ共通描画状態を提供する。
namespace Engine::Skybox {

class SkyBoxCommon
{
public:	
	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return SkyBoxCommon のインスタンス
	static SkyBoxCommon* GetInstance();
	
	/// @brief スカイボックス共通描画資源を初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvmanager SRV 管理クラス
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager*srvmanager);

	/// @brief 共通管理インスタンスを解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief スカイボックス描画の共通ステートを設定する
	/// @param なし
	/// @return なし
	void commonDraw();
	

	//DXCommon
	Engine::Base::DirectXCommon* GetDxCommon()const { return dxCommon_; }
	//srvManager
	Engine::Base::SrvManager* GetSrvManager()const { return srvManager_; }

private:
	// DirectX共通
	Engine::Base::DirectXCommon* dxCommon_ = nullptr;
	// シェーダーリソースマネージャー
	Engine::Base::SrvManager* srvManager_ = nullptr;
	// パイプライン
	std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_ = nullptr;
	

};

}

