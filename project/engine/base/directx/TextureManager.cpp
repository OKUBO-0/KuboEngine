#include "TextureManager.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "SrvManager.h"
#include "StringUtility.h"
#include <Windows.h>
#include <array>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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

bool IsUiTexture(const std::string& filePath)
{
	const std::string normalized = std::filesystem::path(filePath).generic_string();
	return normalized.find("Resources/DirectXGame/ui/") != std::string::npos;
}

}

TextureManager* TextureManager::GetInstance()
{
	static TextureManager instance;
	return &instance;
}

void TextureManager::Finalize()
{
	if (srvManager_) {
		for (const auto& [path, textureData] : textureDatas) {
			static_cast<void>(path);
			if (const auto dxCommon = dxCommon_.lock(); dxCommon && textureData.resource) {
				dxCommon->UntrackResourceState(textureData.resource.Get());
			}
			if (textureData.srvIndex != UINT32_MAX) {
				srvManager_->Free(textureData.srvIndex);
			}
		}
	}
	textureDatas.clear();
	dxCommon_.reset();
	srvManager_ = nullptr;

}

void TextureManager::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager)
{
	assert(dxCommon);
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	textureDatas.reserve(srvManager_->GetMaxCount());

}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filepath)
{
	return GetLoadedTexture(filepath).metadata;
}

//Imgui で０番を使用するため１番から使用
uint32_t TextureManager::kSRVIndexTop = 1;
void TextureManager::LoadTexture(const std::string& filePath)
{
	if (textureDatas.contains(filePath)) {
		return;//読み込み済みなら早期return
	}

	DirectX::ScratchImage image = LoadTextureImage(filePath);
	DirectX::ScratchImage mipImages =
		IsUiTexture(filePath) ? std::move(image) : CreateMipImages(std::move(image));
	TexturData textureData{};
	UploadTextureResource(textureData, mipImages);
	textureDatas.emplace(filePath, std::move(textureData));
}

void TextureManager::LoadTextures(const std::vector<std::string>& filePaths)
{
	std::vector<std::string> pendingPaths;
	pendingPaths.reserve(filePaths.size());
	std::unordered_set<std::string> seenPaths;
	for (const std::string& filePath : filePaths) {
		if (!textureDatas.contains(filePath) && seenPaths.insert(filePath).second) {
			pendingPaths.push_back(filePath);
		}
	}
	if (pendingPaths.empty()) {
		return;
	}

	if (pendingPaths.size() > srvManager_->GetRemainingCount()) {
		throw std::runtime_error(
			"TextureManager batch exceeds remaining SRV descriptor capacity");
	}

	std::vector<DirectX::ScratchImage> mipImageBatch;
	mipImageBatch.reserve(pendingPaths.size());
	for (const std::string& filePath : pendingPaths) {
		DirectX::ScratchImage image = LoadTextureImage(filePath);
		mipImageBatch.push_back(
			IsUiTexture(filePath)
			? std::move(image)
			: CreateMipImages(std::move(image)));
	}

	std::vector<TexturData> textureDataBatch(pendingPaths.size());
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources;
	intermediateResources.reserve(pendingPaths.size());
	for (size_t index = 0; index < pendingPaths.size(); ++index) {
		intermediateResources.push_back(RecordTextureUpload(
			textureDataBatch[index],
			mipImageBatch[index]));
	}

	GetDirectXCommon()->CommandKick();
	try {
		for (TexturData& textureData : textureDataBatch) {
			CreateTextureSrv(textureData);
		}
	} catch (...) {
		for (const TexturData& textureData : textureDataBatch) {
			if (textureData.resource) {
				GetDirectXCommon()->UntrackResourceState(textureData.resource.Get());
			}
			if (textureData.srvIndex != UINT32_MAX) {
				srvManager_->Free(textureData.srvIndex);
			}
		}
		throw;
	}

	for (size_t index = 0; index < pendingPaths.size(); ++index) {
		textureDatas.emplace(
			pendingPaths[index],
			std::move(textureDataBatch[index]));
	}
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
	std::ostringstream operation;
	operation << "TextureManager::LoadTextureImage"
		<< " requestedPath=\"" << filePath << "\""
		<< " resolvedPath=\"" << resolvedPath << '"';
	ThrowIfFailed(hr, operation.str().c_str());
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
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		RecordTextureUpload(textureData, mipImages);
	GetDirectXCommon()->DeferResourceRelease(std::move(intermediateResource));
	try {
		CreateTextureSrv(textureData);
	} catch (...) {
		if (textureData.srvIndex != UINT32_MAX) {
			srvManager_->Free(textureData.srvIndex);
			textureData.srvIndex = UINT32_MAX;
		}
		// The recorded copy still references the destination until this frame completes.
		GetDirectXCommon()->UntrackResourceState(textureData.resource.Get());
		GetDirectXCommon()->DeferResourceRelease(std::move(textureData.resource));
		throw;
	}
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::RecordTextureUpload(
	TexturData& textureData, const DirectX::ScratchImage& mipImages)
{
	textureData.metadata = mipImages.GetMetadata();
	textureData.resource = GetDirectXCommon()->CreateTextureResource(textureData.metadata);
	return GetDirectXCommon()->UploadTextureData(textureData.resource, mipImages);
}

void TextureManager::CreateTextureSrv(TexturData& textureData)
{
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
	return GetLoadedTexture(filepath).srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filepath)
{
	return GetLoadedTexture(filepath).srvHandleGPU;
}

const TextureManager::TexturData& TextureManager::GetLoadedTexture(
	const std::string& filePath) const
{
	const auto it = textureDatas.find(filePath);
	if (it == textureDatas.end() ||
		it->second.srvIndex == UINT32_MAX) {
		throw std::out_of_range(
			"TextureManager texture is not loaded: " + filePath);
	}
	return it->second;
}

std::shared_ptr<Engine::Base::DirectXCommon> TextureManager::GetDirectXCommon() const
{
	auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	return dxCommon;
}

}


