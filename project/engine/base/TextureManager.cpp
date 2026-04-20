#include "TextureManager.h"
#include "StringUtility.h"

namespace Engine::Base {

TextureManager* TextureManager::GetInstance()
{
	static TextureManager instance;
	return &instance;
}

void TextureManager::Finalize()
{
	textureDatas.clear();
	dxCommon_ = nullptr;
	srvmanager = nullptr;

}

void TextureManager::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvmanager)
{
	dxCommon_ = dxCommon;
	this->srvmanager = srvmanager;
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
		return;//酔いこみ済みなら早期return
	}

	assert(srvmanager->CheckTexturesNumber());
	DirectX::ScratchImage image = LoadTextureImage(filePath);
	DirectX::ScratchImage mipImages = CreateMipImages(std::move(image));
	TexturData& textureData = textureDatas[filePath];
	UploadTextureResource(textureData, mipImages);
}

DirectX::ScratchImage TextureManager::LoadTextureImage(const std::string& filePath)
{
	DirectX::ScratchImage image{};
	std::wstring filePathW = StringUtility::ConvertString(filePath);
	HRESULT hr;
	if (filePathW.ends_with(L".dds")) {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	} else {
		hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
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
	assert(SUCCEEDED(hr));
	return mipImages;
}

void TextureManager::UploadTextureResource(TexturData& textureData, const DirectX::ScratchImage& mipImages)
{
	textureData.metadata = mipImages.GetMetadata();
	textureData.resource = dxCommon_->CreateTextureResource(textureData.metadata);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
		dxCommon_->UploadTextureData(textureData.resource, mipImages);
	dxCommon_->CommandKick();

	textureData.srvIndex = srvmanager->Allocate();
	textureData.srvHandleCPU = srvmanager->GetCPUDescriptorHandle(textureData.srvIndex);
	textureData.srvHandleGPU = srvmanager->GetGPUDescriptorHandle(textureData.srvIndex);

	srvmanager->CreateSRVforTexture2D(
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


