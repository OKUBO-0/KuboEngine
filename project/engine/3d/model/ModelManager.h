#pragma once
#include <map>
#include <memory>
#include <string>

namespace Engine::Base {
class DirectXCommon;
class SrvManager;
}

/// @brief モデル資産の共有読込と検索を担当するクラス
/// @details モデルファイルを一度だけ読み込み、キーとなるファイルパスで再利用できるよう管理する。
namespace Engine::Graphics3D {

class Model;
class ModelCommon;

class ModelManager
{
	ModelManager() = default;
	~ModelManager();
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
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvManager SRV 管理クラス
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager);

	/// @brief モデルファイルを読み込んで管理対象へ登録する
	/// @param filePath 読み込むモデルのファイルパス
	/// @return なし
	void LoadModel(const std::string& filePath);

	/// @brief リソースルートを指定してモデルを読み込む
	/// @param resourceRoot モデル探索の起点となるリソースルート
	/// @param filePath 読み込むモデルのファイルパス
	/// @return なし
	void LoadModelFromResourceRoot(const std::string& resourceRoot, const std::string& filePath);

	/// @brief 登録済みモデルをファイルパスから検索する
	/// @param filePath 検索対象のファイルパス
	/// @return 見つかったモデル。未登録なら nullptr
	Model* FindModel(const std::string& filePath);

	/// @brief リソースルート付きキーで登録済みモデルを検索する
	/// @param resourceRoot モデル探索の起点となるリソースルート
	/// @param filePath 検索対象のモデルパス
	/// @return 見つかったモデル。未登録なら nullptr
	Model* FindModelFromResourceRoot(const std::string& resourceRoot, const std::string& filePath);

private:
	static std::string MakeModelKey(const std::string& resourceRoot, const std::string& filePath);
	//モデルデータ
	std::map<std::string, std::unique_ptr < Model>> models;

	std::unique_ptr< ModelCommon> modelCommon = nullptr;
	Engine::Base::SrvManager* srvManager_ = nullptr;
};

}

