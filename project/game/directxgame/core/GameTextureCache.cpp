#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include "TextureManager.h"
#include <cassert>
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

}

TextureHandle GameTextureCache::Load(const std::string& relativePath)
{
	const std::string fullPath = ResourcePaths::MakePath(relativePath);
	auto& pathToHandle = GetPathToHandle();
	if (pathToHandle.contains(fullPath)) {
		return pathToHandle.at(fullPath);
	}

	Engine::Base::TextureManager::GetInstance()->LoadTexture(fullPath);
	const TextureHandle handle = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(fullPath);
	pathToHandle.emplace(fullPath, handle);
	GetHandleToPath().emplace(handle, fullPath);
	return handle;
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
