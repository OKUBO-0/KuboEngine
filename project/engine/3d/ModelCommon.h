#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"

/// @brief 3D モデル描画で共有する共通依存をまとめるクラス
/// @details DirectX 共通クラスと SRV 管理クラスへの参照を保持し、
///          モデル個別インスタンスが共通描画資源へアクセスできるようにする。
namespace Engine::Graphics3D {

class ModelCommon
{
public:

	/// @brief モデル描画共通クラスを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvMnager SRV 管理クラス
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvMnager);

	//DXCommon
	Engine::Base::DirectXCommon* GetDxCommon()const { return dxCommon_; }
	Engine::Base::SrvManager* GetSRVManager() { return srvMnager_; }

private:
	Engine::Base::DirectXCommon* dxCommon_;
	Engine::Base::SrvManager* srvMnager_ = nullptr;


};

}

