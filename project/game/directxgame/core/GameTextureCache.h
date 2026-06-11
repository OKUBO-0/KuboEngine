#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>

namespace DirectXGame {

using TextureHandle = uint32_t;

class GameTextureCache {
public:
	static TextureHandle Load(const std::string& relativePath);
	static void LoadBatch(const std::vector<std::string>& relativePaths);
	static const std::string& GetPath(TextureHandle handle);
	static D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(TextureHandle handle);
};

}
