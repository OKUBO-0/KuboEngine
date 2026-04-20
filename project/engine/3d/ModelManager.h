#pragma once
#include <map>
#include <string>
#include "Model.h"

/// @brief モデル資産の共有読込と検索を担当するクラス
/// @details モデルファイルを一度だけ読み込み、キーとなるファイルパスで再利用できるよう管理する。
namespace Engine::Graphics3D {

class ModelManager
{
	ModelManager() = default;
	~ModelManager() = default;
	ModelManager(ModelManager&) = default;
	ModelManager& operator=(ModelManager&) = delete;

public:
	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return ModelManager のインスタンス
	static ModelManager* GetInstance();

	/// @brief 保持しているモデル資産を解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief モデル管理に必要な共通リソースを初期化する
	/// @param dxcommon DirectX 共通管理クラス
	/// @param srvmnager SRV 管理クラス
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxcommon, Engine::Base::SrvManager* srvmnager);

	/// @brief モデルファイルを読み込んで管理対象へ登録する
	/// @param filePath 読み込むモデルのファイルパス
	/// @return なし
	void LoadModel(const std::string& filePath);

	/// @brief 登録済みモデルをファイルパスから検索する
	/// @param filePath 検索対象のファイルパス
	/// @return 見つかったモデル。未登録なら nullptr
	Model* FindModel(const std::string& filePath);

private:
	//モデルデータ
	std::map<std::string, std::unique_ptr < Model>> models;

	std::unique_ptr< ModelCommon> modelCommon = nullptr;
	Engine::Base::SrvManager* srvmnager_ = nullptr;
};

}

