#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include "TextureManager.h"
#include <Windows.h>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace DirectXGame {

namespace {

std::unordered_map<std::string, TextureHandle>& GetPathToHandle()
{
	static std::unordered_map<std::string, TextureHandle> cache;
	return cache;
}

std::unordered_map<TextureHandle, std::string>& GetHandleToPath()
{
	static std::unordered_map<TextureHandle, std::string> cache;
	return cache;
}

void LogTextureLoadMessage(const std::string& relativePath, const std::string& fullPath, const char* reason)
{
	std::ostringstream message;
	message << "[DirectXGame][GameTextureCache::Load] " << reason
		<< " relativePath=\"" << relativePath << "\""
		<< " fullPath=\"" << fullPath << "\""
		<< " currentPath=\"" << std::filesystem::current_path().generic_string() << "\"\n";
	OutputDebugStringA(message.str().c_str());
}

}

TextureHandle GameTextureCache::Load(const std::string& relativePath)
{
	const std::string fullPath = ResourcePaths::MakePath(relativePath);
	auto& pathToHandle = GetPathToHandle();
	if (pathToHandle.contains(fullPath)) {
		return pathToHandle.at(fullPath);
	}

	if (!std::filesystem::exists(fullPath)) {
		LogTextureLoadMessage(relativePath, fullPath, "texture file is not visible from current directory before TextureManager fallback");
	}
	Engine::Base::TextureManager::GetInstance()->LoadTexture(fullPath);
	const TextureHandle handle = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(fullPath);
	if (handle == 0) {
		LogTextureLoadMessage(relativePath, fullPath, "TextureManager returned invalid SRV handle");
		assert(false && "GameTextureCache::Load failed; see OutputDebugString for path details");
	}
	pathToHandle.emplace(fullPath, handle);
	GetHandleToPath().emplace(handle, fullPath);
	return handle;
}

void GameTextureCache::LoadBatch(const std::vector<std::string>& relativePaths)
{
	std::vector<std::string> fullPaths;
	fullPaths.reserve(relativePaths.size());
	for (const std::string& relativePath : relativePaths) {
		fullPaths.push_back(ResourcePaths::MakePath(relativePath));
	}

	Engine::Base::TextureManager::GetInstance()->LoadTextures(fullPaths);
	auto& pathToHandle = GetPathToHandle();
	auto& handleToPath = GetHandleToPath();
	for (const std::string& fullPath : fullPaths) {
		if (pathToHandle.contains(fullPath)) {
			continue;
		}

		const TextureHandle handle =
			Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(fullPath);
		if (handle == 0) {
			LogTextureLoadMessage(fullPath, fullPath, "TextureManager returned invalid SRV handle after batch load");
			assert(false && "GameTextureCache::LoadBatch failed; see OutputDebugString for path details");
		}
		pathToHandle.emplace(fullPath, handle);
		handleToPath.emplace(handle, fullPath);
	}
}

const std::string& GameTextureCache::GetPath(TextureHandle handle)
{
	const auto& handleToPath = GetHandleToPath();
	assert(handleToPath.contains(handle));
	return handleToPath.at(handle);
}

D3D12_GPU_DESCRIPTOR_HANDLE GameTextureCache::GetSrvHandleGPU(TextureHandle handle)
{
	return Engine::Base::TextureManager::GetInstance()->GetSrvHandleGPU(GetPath(handle));
}

}
