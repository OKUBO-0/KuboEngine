#include "ModelManager.h"
#include "Model.h"
#include "ModelCommon.h"
#include "ModelAssetResolver.h"
#include "../../base/AssetPath.h"
#include "../../assets/AssetRevision.h"
#include "../../assets/LeasedCache.h"
#include "Object3D.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include <utility>
#include <vector>
#include <unordered_set>
#include <stdexcept>

namespace Engine::Graphics3D {

ModelManager::~ModelManager() = default;

ModelManager* ModelManager::GetInstance()
{
	static ModelManager instance;
	return &instance;
}

std::string ModelManager::MakeModelKey(const std::string& resourceRoot, const std::string& filePath)
{
	// The root also controls fallback material lookup, so keep it in the identity.
	return Engine::Base::NormalizeAssetPath(resourceRoot) + "::" +
		Engine::Base::NormalizeAssetPath(ResolveModelAssetPath(resourceRoot, filePath));
}

void ModelManager::Finalize()
{
	for (auto& [key, model] : models) {
		static_cast<void>(key);
		if (model) {
			Object3D::InvalidateModelCaches(model.get());
			model->Finalize();
		}
	}
	models.clear();
	collectibleModels_.clear();
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

Model* ModelManager::GetOrLoadModel(const std::string& filePath, const std::string& resourceRoot, const std::string& assetIdentity, const std::string& revision)
{
	return GetOrLoadModelInternal(filePath, resourceRoot, assetIdentity, revision, false).get();
}
std::shared_ptr<Model> ModelManager::AcquireModel(const std::string& filePath, const std::string& resourceRoot, const std::string& assetIdentity, const std::string& revision)
{
	return GetOrLoadModelInternal(filePath, resourceRoot, assetIdentity, revision, true);
}
void ModelManager::PinModel(Model* model)
{
	for (const auto& entry : models) if (entry.second.get() == model) { collectibleModels_.erase(entry.first); return; }
}
size_t ModelManager::CollectUnusedModels()
{
	if (!modelCommon) return 0;
	const auto removed = Assets::CollectUnusedLeasedEntries(models, collectibleModels_,
		[&] {
			// Upload/state-transition commands may still be recorded, not submitted.
			// A fence-only wait cannot protect resources referenced by that list.
			auto graphics = modelCommon->GetDxCommon();
			graphics->CommandKick();
			graphics->WaitForAllFrames();
		}, [](Model* model) {
			Object3D::InvalidateModelCaches(model); model->Finalize();
		});
	Base::TextureManager::GetInstance()->CollectUnusedTextures();
	return removed;
}
std::shared_ptr<Model> ModelManager::GetOrLoadModelInternal(const std::string& filePath, const std::string& resourceRoot, const std::string& assetIdentity, const std::string& revision, bool leased)
{
	if (!modelCommon) {
		throw std::logic_error("ModelManager must be initialized before loading models");
	}
	if (filePath.empty() || resourceRoot.empty()) {
		throw std::invalid_argument("Model path and resource root must not be empty");
	}
	// Retain old versions while live Object3Ds hold leases (or raw API pins). Distinct
	// persistent identities must not share a GPU model just because a path is reused.
	const auto identity = Assets::RevisionIdentity(assetIdentity, revision);
	if (!identity.empty()) {
		const auto key = MakeModelKey(resourceRoot, filePath) + "::identity:" + identity;
		if (const auto found = models.find(key); found != models.end()) {
			if (!leased) collectibleModels_.erase(key); // Raw-pointer exposure pins permanently.
			return found->second;
		}
		const auto resolved = ResolveModelAssetPath(resourceRoot, filePath);
		if (!std::filesystem::is_regular_file(Base::PathFromUtf8(resolved))) throw std::runtime_error("Model file not found: " + resolved);
		auto model = std::make_shared<Model>();
		try { model->Initialize(modelCommon.get(), resourceRoot, filePath, true, revision); }
		catch (...) { model->Finalize(); throw; }
		models.emplace(key, model);
		if (leased) collectibleModels_.insert(key);
		return model;
	}
	if (Model* model = FindModelFromResourceRoot(resourceRoot, filePath)) {
		(void)model;
		return models.at(MakeModelKey(resourceRoot, filePath));
	}
	const auto resolved = ResolveModelAssetPath(resourceRoot, filePath);
	if (!std::filesystem::is_regular_file(Base::PathFromUtf8(resolved))) {
		throw std::runtime_error("Model file not found: " + filePath + " (resolved: " + resolved + ")");
	}
	LoadModelFromResourceRoot(resourceRoot, filePath);
	return models.at(MakeModelKey(resourceRoot, filePath));
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
	std::unordered_set<std::string> pendingKeys;

	for (const std::string& filePath : filePaths) {
		const std::string modelKey = MakeModelKey(resourceRoot, filePath);
		if (models.contains(modelKey) || !pendingKeys.insert(modelKey).second) {
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
