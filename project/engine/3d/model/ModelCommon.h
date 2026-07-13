#pragma once
#include <memory>

namespace Engine::Base {
class DirectXCommon;
class SrvManager;
}

/// @brief 3D モデル描画で共有する共通依存をまとめるクラス
/// @details DirectX 共通クラスと SRV 管理クラスへの参照を保持し、
///          モデル個別インスタンスが共通描画資源へアクセスできるようにする。
namespace Engine::Graphics3D {

class ModelCommon
{
public:

	/// @brief モデル描画共通クラスを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvManager SRV 管理クラス
	/// @return なし
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager);

	/// @brief DirectX 共通管理を shared_ptr で返し、モデル側の一時利用中に破棄されないようにする
	std::shared_ptr<Engine::Base::DirectXCommon> GetDxCommon() const { return dxCommon_.lock(); }
	Engine::Base::SrvManager* GetSRVManager() { return srvManager_; }

private:
	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;
	Engine::Base::SrvManager* srvManager_ = nullptr;


};

}

