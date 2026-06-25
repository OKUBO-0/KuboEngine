#include "game/directxgame/core/GameModelCache.h"
#include "game/directxgame/core/ResourcePaths.h"
#include "ModelManager.h"
#include "Object3D.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace DirectXGame {

namespace {

struct CachedModelEntry {
	std::string requestedName;
	std::string resolvedFileName;
};

std::unordered_map<std::string, ModelHandle>& GetNameToHandle()
{
	static std::unordered_map<std::string, ModelHandle> cache;
	return cache;
}

std::unordered_map<ModelHandle, CachedModelEntry>& GetHandleToModel()
{
	static std::unordered_map<ModelHandle, CachedModelEntry> cache;
	return cache;
}

ModelHandle& GetNextHandle()
{
	static ModelHandle nextHandle = 1;
	return nextHandle;
}

struct ModelPathIndex {
	std::vector<std::filesystem::path> activePaths;
	std::vector<std::filesystem::path> legacyPaths;
};

bool IsModelAsset(const std::filesystem::path& path)
{
	std::string extension = path.extension().string();
	std::ranges::transform(extension, extension.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return extension == ".obj" || extension == ".fbx" ||
		extension == ".gltf" || extension == ".glb";
}

ModelPathIndex BuildModelPathIndex()
{
	ModelPathIndex result;
	const std::filesystem::path resourceRoot =
		ResourcePaths::GetModelResourceRoot();
	for (const std::filesystem::directory_entry& entry :
		std::filesystem::recursive_directory_iterator(resourceRoot)) {
		if (!entry.is_regular_file() || !IsModelAsset(entry.path())) {
			continue;
		}
		const std::filesystem::path relativePath =
			std::filesystem::relative(entry.path(), resourceRoot);
		const bool isLegacy =
			!relativePath.empty() && *relativePath.begin() == "models";
		(isLegacy ? result.legacyPaths : result.activePaths)
			.push_back(relativePath);
	}
	return result;
}

ModelPathIndex& GetMutableModelPathIndex()
{
	static ModelPathIndex index = BuildModelPathIndex();
	return index;
}

const ModelPathIndex& GetModelPathIndex()
{
	return GetMutableModelPathIndex();
}

std::string FindUniqueModelPath(
	const std::vector<std::filesystem::path>& paths,
	const std::filesystem::path& requestedPath,
	const char* scope)
{
	std::vector<std::filesystem::path> matches;
	for (const std::filesystem::path& path : paths) {
		const bool isMatch = requestedPath.has_extension()
			? path.filename() == requestedPath.filename()
			: path.stem() == requestedPath;
		if (isMatch) {
			matches.push_back(path);
		}
	}
	if (matches.size() == 1) {
		return matches.front().generic_string();
	}
	if (matches.size() > 1) {
		std::ostringstream message;
		message << "GameModelCache model name is ambiguous in " << scope
			<< ": " << requestedPath.generic_string();
		for (const std::filesystem::path& match : matches) {
			message << " [" << match.generic_string() << ']';
		}
		throw std::runtime_error(message.str());
	}
	return {};
}

std::string ResolveModelFileName(const std::string& modelName)
{
	const std::filesystem::path resourceRoot = ResourcePaths::GetModelResourceRoot();
	const std::filesystem::path requestedPath(modelName);

	const std::filesystem::path directPath = resourceRoot / requestedPath;
	if (std::filesystem::exists(directPath)) {
		return requestedPath.generic_string();
	}

	const ModelPathIndex& index = GetModelPathIndex();
	if (const std::string activePath =
		FindUniqueModelPath(index.activePaths, requestedPath, "active assets");
		!activePath.empty()) {
		return activePath;
	}
	if (const std::string legacyPath =
		FindUniqueModelPath(index.legacyPaths, requestedPath, "legacy assets");
		!legacyPath.empty()) {
		return legacyPath;
	}

	GameModelCache::RefreshModelPathIndex();
	const ModelPathIndex& refreshedIndex = GetModelPathIndex();
	if (const std::string activePath =
		FindUniqueModelPath(refreshedIndex.activePaths, requestedPath, "active assets after refresh");
		!activePath.empty()) {
		return activePath;
	}
	if (const std::string legacyPath =
		FindUniqueModelPath(refreshedIndex.legacyPaths, requestedPath, "legacy assets after refresh");
		!legacyPath.empty()) {
		return legacyPath;
	}

	return requestedPath.generic_string();
}

}

ModelHandle GameModelCache::Load(const std::string& modelName)
{
	auto& nameToHandle = GetNameToHandle();
	if (nameToHandle.contains(modelName)) {
		return nameToHandle.at(modelName);
	}

	const std::string resolvedFileName = ResolveModelFileName(modelName);
	Engine::Graphics3D::ModelManager::GetInstance()->LoadModelFromResourceRoot(
		ResourcePaths::GetModelResourceRoot(), resolvedFileName);

	ModelHandle handle = GetNextHandle()++;
	nameToHandle.emplace(modelName, handle);
	GetHandleToModel().emplace(handle, CachedModelEntry{ modelName, resolvedFileName });
	return handle;
}

void GameModelCache::LoadBatch(const std::vector<std::string>& modelNames)
{
	auto& nameToHandle = GetNameToHandle();
	std::vector<std::pair<std::string, std::string>> pendingModels;
	std::vector<std::string> resolvedFileNames;
	std::unordered_set<std::string> seenResolvedFileNames;
	pendingModels.reserve(modelNames.size());
	resolvedFileNames.reserve(modelNames.size());

	for (const std::string& modelName : modelNames) {
		if (nameToHandle.contains(modelName)) {
			continue;
		}

		const std::string resolvedFileName =
			ResolveModelFileName(modelName);
		pendingModels.emplace_back(modelName, resolvedFileName);
		if (seenResolvedFileNames.insert(resolvedFileName).second) {
			resolvedFileNames.push_back(resolvedFileName);
		}
	}

	if (pendingModels.empty()) {
		return;
	}

	Engine::Graphics3D::ModelManager::GetInstance()->LoadModelsFromResourceRoot(
		ResourcePaths::GetModelResourceRoot(),
		resolvedFileNames);

	auto& handleToModel = GetHandleToModel();
	for (const auto& [modelName, resolvedFileName] : pendingModels) {
		if (nameToHandle.contains(modelName)) {
			continue;
		}

		const ModelHandle handle = GetNextHandle()++;
		nameToHandle.emplace(modelName, handle);
		handleToModel.emplace(
			handle,
			CachedModelEntry{ modelName, resolvedFileName });
	}
}

void GameModelCache::RefreshModelPathIndex()
{
	GetMutableModelPathIndex() = BuildModelPathIndex();
}

Engine::Graphics3D::Model* GameModelCache::Get(ModelHandle handle)
{
	return Engine::Graphics3D::ModelManager::GetInstance()->FindModelFromResourceRoot(
		ResourcePaths::GetModelResourceRoot(), GetResolvedFileName(handle));
}

const std::string& GameModelCache::GetResolvedFileName(ModelHandle handle)
{
	const auto& handleToModel = GetHandleToModel();
	assert(handleToModel.contains(handle));
	return handleToModel.at(handle).resolvedFileName;
}

void GameModelCache::ApplyToObject(Engine::Graphics3D::Object3D& object, ModelHandle handle)
{
	object.SetModelFromResourceRoot(ResourcePaths::GetModelResourceRoot(), GetResolvedFileName(handle));
}

}
