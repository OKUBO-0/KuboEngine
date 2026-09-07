#include "ModelManager.h"
#include "Model.h"
#include "ModelCommon.h"
#include "TextureManager.h"
#include <utility>
#include <vector>

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
	for (auto& [key, model] : models) {
		static_cast<void>(key);
		if (model) {
			model->Finalize();
		}
	}
	models.clear();
	modelCommon.reset();
	srvManager_ = nullptr;
}

void ModelManager::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager)
{
	srvManager_ = srvManager;
	modelCommon = std::make_unique<ModelCommon>();
	modelCommon->Initialize(dxCommon, srvManager_);


}

void ModelManager::LoadModel(const std::string& filePath)
{
	LoadModelFromResourceRoot("Resources", filePath);
}

void ModelManager::LoadModels(const std::vector<std::string>& filePaths)
{
	LoadModelsFromResourceRoot("Resources", filePaths);
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

void ModelManager::LoadModelsFromResourceRoot(
	const std::string& resourceRoot,
	const std::vector<std::string>& filePaths)
{
	std::vector<std::pair<std::string, std::unique_ptr<Model>>> pendingModels;
	std::vector<std::string> texturePaths;
	pendingModels.reserve(filePaths.size());
	texturePaths.reserve(filePaths.size());

	for (const std::string& filePath : filePaths) {
		const std::string modelKey = MakeModelKey(resourceRoot, filePath);
		if (models.contains(modelKey)) {
			continue;
		}

		std::unique_ptr<Model> model = std::make_unique<Model>();
		model->Initialize(modelCommon.get(), resourceRoot, filePath, false);
		const ModelData& modelData = model->GetModelData();
		if (!modelData.materials.empty()) {
			for (const MaterialData& material : modelData.materials) {
				texturePaths.push_back(material.textureFilePath);
			}
		} else {
			texturePaths.push_back(modelData.material.textureFilePath);
		}
		pendingModels.emplace_back(modelKey, std::move(model));
	}

	if (pendingModels.empty()) {
		return;
	}

	Engine::Base::TextureManager::GetInstance()->LoadTextures(texturePaths);
	for (auto& [modelKey, model] : pendingModels) {
		model->LoadMaterialTexture();
		models.insert(std::make_pair(modelKey, std::move(model)));
	}
}

bool ModelManager::LoadAnimationClipFromResourceRoot(
	const std::string& resourceRoot,
	const std::string& modelFilePath,
	const std::string& clipName,
	const std::string& animationFilePath)
{
	LoadModelFromResourceRoot(resourceRoot, modelFilePath);
	Model* model = FindModelFromResourceRoot(resourceRoot, modelFilePath);
	if (!model) {
		return false;
	}
	return model->LoadAnimationClip(clipName, resourceRoot, animationFilePath);
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
