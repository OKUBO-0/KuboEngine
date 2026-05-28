#include "ModelManager.h"
#include "Model.h"
#include "ModelCommon.h"

namespace Engine::Graphics3D {

ModelManager::~ModelManager() = default;

ModelManager* ModelManager::GetInstance()
{
	static ModelManager instance;
	return &instance;
}

std::string ModelManager::MakeModelKey(const std::string& resourceRoot, const std::string& filePath)
{
	return resourceRoot + "::" + filePath;
}

void ModelManager::Finalize()
{
	models.clear();
	modelCommon.reset();
	srvManager_ = nullptr;
}

void ModelManager::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{
	srvManager_ = srvManager;
	modelCommon = std::make_unique<ModelCommon>();
	modelCommon->Initialize(dxCommon, srvManager_);


}

void ModelManager::LoadModel(const std::string& filePath)
{
	LoadModelFromResourceRoot("Resources", filePath);
}

void ModelManager::LoadModelFromResourceRoot(const std::string& resourceRoot, const std::string& filePath)
{
	const std::string modelKey = MakeModelKey(resourceRoot, filePath);

	//読み込み済みモデルを検索
	if (models.contains(modelKey)) {
		//読み込み済みなら早期return
		return;
	}
	//モデルの生成とファイル読み込み、初期化
	std::unique_ptr<Model>model = std::make_unique<Model>();
	model->Initialize(modelCommon.get(), resourceRoot, filePath);

	//モデルをmapコンテナに格納する
	models.insert(std::make_pair(modelKey, std::move(model)));

}

Model* ModelManager::FindModel(const std::string& filePath)
{
	return FindModelFromResourceRoot("Resources", filePath);
}

Model* ModelManager::FindModelFromResourceRoot(const std::string& resourceRoot, const std::string& filePath)
{
	const std::string modelKey = MakeModelKey(resourceRoot, filePath);

	//読み込みモデルを戻り値としてreturn
	if (models.contains(modelKey)) {
		return models.at(modelKey).get();
	}

	//ファイル名一致なし
	return nullptr;
}

}
