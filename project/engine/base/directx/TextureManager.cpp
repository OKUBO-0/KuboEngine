#include "TextureManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "StringUtility.h"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <sstream>

namespace Engine::Base {

namespace {

std::filesystem::path GetExecutableDirectory()
{
	std::array<char, MAX_PATH> path{};
	const DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size()) {
		return {};
	}
	return std::filesystem::path(path.data()).parent_path();
}

std::string ResolveTexturePath(const std::string& filePath)
{
	if (filePath.empty()) {
		return filePath;
	}

	const std::filesystem::path requestedPath(filePath);
	if (std::filesystem::exists(requestedPath)) {
		return requestedPath.generic_string();
	}

	const std::filesystem::path exeDirectory = GetExecutableDirectory();
	const std::filesystem::path currentDirectory = std::filesystem::current_path();
	const std::array<std::filesystem::path, 4> baseDirectories{
		exeDirectory,
		exeDirectory.parent_path(),
		currentDirectory,
		currentDirectory / "project",
	};

	for (const std::filesystem::path& baseDirectory : baseDirectories) {
		if (baseDirectory.empty()) {
			continue;
		}

		const std::filesystem::path candidate = baseDirectory / requestedPath;
		if (std::filesystem::exists(candidate)) {
			return candidate.generic_string();
		}
	}

	return requestedPath.generic_string();
}

}

TextureManager* TextureManager::GetInstance()
{
	static TextureManager instance;
	return &instance;
}

void TextureManager::Finalize()
{
	textureDatas.clear();
	dxCommon_ = nullptr;
	srvManager_ = nullptr;

}

void TextureManager::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	textureDatas.reserve(Engine::Base::DirectXCommon::kMaxSRVCount);

}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filepath)
{
	assert(textureDatas.size() + kSRVIndexTop < Engine::Base::DirectXCommon::kMaxSRVCount);
	TexturData& textureData = textureDatas[filepath];
	return textureData.metadata;
}

//Imgui で０番を使用するため１番から使用
uint32_t TextureManager::kSRVIndexTop = 1;
void TextureManager::LoadTexture(const std::string& filePath)
{
	if (textureDatas.contains(filePath)) {
		return;//読み込み済みなら早期return
	}

	assert(srvManager_->CheckTexturesNumber());
	DirectX::ScratchImage image = LoadTextureImage(filePath);
	DirectX::ScratchImage mipImages = CreateMipImages(std::move(image));
	TexturData& textureData = textureDatas[filePath];
	UploadTextureResource(textureData, mipImages);
}

DirectX::ScratchImage TextureManager::LoadTextureImage(const std::string& filePath)
{
	DirectX::ScratchImage image{};
	const std::string resolvedPath = ResolveTexturePath(filePath);
	std::wstring filePathW = StringUtility::ConvertString(resolvedPath);
	HRESULT hr;
	if (filePathW.ends_with(L".dds")) {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	} else {
		hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	}
	if (FAILED(hr)) {
		std::ostringstream message;
		message << "[TextureManager::LoadTextureImage] failed to load texture"
			<< " requestedPath=\"" << filePath << "\""
			<< " resolvedPath=\"" << resolvedPath << "\""
			<< " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
		OutputDebugStringA(message.str().c_str());
	}
	assert(SUCCEEDED(hr));
	return image;
}

DirectX::ScratchImage TextureManager::CreateMipImages(DirectX::ScratchImage&& image)
{
	if (DirectX::IsCompressed(image.GetMetadata().format)) {
		return std::move(image);
	}

	DirectX::ScratchImage mipImages{};
	HRESULT hr = DirectX::GenerateMipMaps(
		image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 4, mipImages);
	if (FAILED(hr)) {
		// Some UI textures load correctly via WIC but do not support mip generation.
		// Fall back to the original single-mip image instead of asserting on startup.
		return std::move(image);
	}
	return mipImages;
}

void TextureManager::UploadTextureResource(TexturData& textureData, const DirectX::ScratchImage& mipImages)
{
	textureData.metadata = mipImages.GetMetadata();
	textureData.resource = dxCommon_->CreateTextureResource(textureData.metadata);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		dxCommon_->UploadTextureData(textureData.resource, mipImages);
	dxCommon_->CommandKick();

	textureData.srvIndex = srvManager_->Allocate();
	textureData.srvHandleCPU = srvManager_->GetCPUDescriptorHandle(textureData.srvIndex);
	textureData.srvHandleGPU = srvManager_->GetGPUDescriptorHandle(textureData.srvIndex);

	srvManager_->CreateSRVforTexture2D(
		textureData.srvIndex,
		textureData.resource.Get(),
		textureData.metadata.format,
		static_cast<UINT>(textureData.metadata.mipLevels),
		textureData.metadata);
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filepath)
{

	if (textureDatas.contains(filepath)) {

		return textureDatas[filepath].srvIndex;



	}

	assert(0);
	return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filepath)
{
	assert(textureDatas.size() + kSRVIndexTop < Engine::Base::DirectXCommon::kMaxSRVCount);

	return textureDatas.at(filepath).srvHandleGPU;
}

}


