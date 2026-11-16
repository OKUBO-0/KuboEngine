#include "TextureManager.h"
#include "../../assets/AssetRevision.h"
#include "../../assets/LeasedCache.h"
#include "../AssetPath.h"
#include "TextureAssetResolver.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "SrvManager.h"
#include "StringUtility.h"
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <set>
#include <cwctype>

namespace Engine::Base {

namespace {

std::filesystem::path GetExecutableDirectory()
{
	std::array<wchar_t, 32768> path{};
	const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (length == 0 || length >= path.size()) {
		return {};
	}
	return std::filesystem::path(path.data()).parent_path();
}

bool IsUiTexture(const std::string& filePath)
{
	const std::string normalized = PathToUtf8(PathFromUtf8(filePath));
	return normalized.find("/ui/") != std::string::npos;
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
	textureLeases_.clear();
	collectibleTextures_.clear();
	resolvedTextureKeys_.clear();
	dxCommon_.reset();
	srvManager_ = nullptr;
	projectRoot_.clear();

}

void TextureManager::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager,
    const std::filesystem::path& projectRoot)
{
	assert(dxCommon);
	projectRoot_ = ProjectPaths(projectRoot).Root();
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;

}

std::string TextureManager::ResolveTexturePath(const std::string& filePath) const
{
    return ResolveTextureAssetPath(filePath, GetExecutableDirectory(), projectRoot_);
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filepath, TextureLoadOptions options)
{
	return GetLoadedTexture(filepath, options).metadata;
}

//Imgui で０番を使用するため１番から使用
uint32_t TextureManager::kSRVIndexTop = 1;
void TextureManager::LoadTexture(const std::string& filePath, TextureLoadOptions options)
{
	const TextureKey key = MakeTextureKey(filePath, options);
	collectibleTextures_.erase(key.path); // Legacy load exposes a permanently valid key.
	if (textureDatas.contains(key)) {
		return;//読み込み済みなら早期return
	}

	DirectX::ScratchImage mipImages = PrepareFileTexture(key.path, options);
	TexturData textureData{};
	UploadTextureResource(textureData, mipImages);
	textureDatas.emplace(key, std::move(textureData));
	resolvedTextureKeys_[filePath] = key.path;
}

std::string TextureManager::LoadTextureRevision(const std::string& filePath, const std::string& revision)
{
	return LoadTextureRevisionInternal(filePath, revision, false);
}

std::shared_ptr<const TextureLease> TextureManager::AcquireTextureRevision(const std::string& filePath, const std::string& revision)
{
	const auto opaque = LoadTextureRevisionInternal(filePath, revision, true);
	return AcquireLoadedTexture(opaque);
}

std::shared_ptr<const TextureLease> TextureManager::AcquireLoadedTexture(const std::string& opaque)
{
	const auto key = MakeTextureKey(opaque, {});
	if (const auto found = textureLeases_.find(key.path); found != textureLeases_.end()) return found->second;
	const auto& data = textureDatas.at(key);
	auto lease = std::make_shared<const TextureLease>(TextureLease{key.path, data.srvIndex, data.metadata});
	textureLeases_.emplace(key.path, lease);
	return lease;
}

size_t TextureManager::CollectUnusedTextures()
{
	if (!srvManager_ || dxCommon_.expired()) return 0;
	return Assets::CollectUnusedLeasedEntries(textureLeases_, collectibleTextures_, [&] {
		auto graphics = GetDirectXCommon();
		graphics->CommandKick(); graphics->WaitForAllFrames();
	}, [&](const TextureLease* lease) {
		const auto found = textureDatas.find(TextureKey{lease->key, {}, true});
		if (found == textureDatas.end()) throw std::logic_error("Missing leased texture entry");
		if (!srvManager_->Free(found->second.srvIndex)) throw std::logic_error("Leased texture SRV was already freed");
		GetDirectXCommon()->UntrackResourceState(found->second.resource.Get());
		textureDatas.erase(found);
	});
}

void TextureManager::CreateLeasedTexture2DView(const TextureLease& lease,D3D12_CPU_DESCRIPTOR_HANDLE destination) const
{
	const auto key=MakeTextureKey(lease.key,{});
	const auto owner=textureLeases_.find(key.path);
	const auto texture=textureDatas.find(key);
	if(!destination.ptr||owner==textureLeases_.end()||owner->second.get()!=&lease||texture==textureDatas.end()||texture->second.srvIndex!=lease.srvIndex)
		throw std::invalid_argument("Texture view requires a live managed lease and destination");
	const auto& data=texture->second;const auto& metadata=data.metadata;
	if(metadata.dimension!=DirectX::TEX_DIMENSION_TEXTURE2D||metadata.arraySize!=1||metadata.depth!=1||metadata.IsCubemap()||!metadata.mipLevels)
		throw std::invalid_argument("Texture view requires a single 2D texture");
	D3D12_SHADER_RESOURCE_VIEW_DESC description{};description.Format=metadata.format;
	description.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	description.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;description.Texture2D.MipLevels=static_cast<UINT>(metadata.mipLevels);
	GetDirectXCommon()->GetDevice()->CreateShaderResourceView(data.resource.Get(),&description,destination);
}

std::string TextureManager::LoadTextureRevisionInternal(const std::string& filePath, const std::string& revision, bool leased)
{
	if (revision.empty()) { LoadTexture(filePath); return filePath; }
	Assets::RevisionIdentity({}, revision);
	const auto source = MakeTextureKey(filePath, {});
	// Embedded textures are already revision-keyed by Model before decoding.
	if (source.memory) { if (!leased) collectibleTextures_.erase(source.path); return filePath; }
	const auto opaque = Assets::RevisionIdentity("texture:" + source.path, revision);
	const TextureKey key{opaque, {}, true};
	if (!textureDatas.contains(key)) {
		auto image = PrepareFileTexture(source.path, {});
		TexturData textureData{};
		UploadTextureResource(textureData, image);
		textureDatas.emplace(key, std::move(textureData));
		if (leased) collectibleTextures_.insert(opaque);
	}
	if (!leased) collectibleTextures_.erase(opaque);
	return opaque;
}

void TextureManager::LoadTextureFromMemory(
	const std::string& key,
	const void* data,
	size_t size,
	bool generateMipMaps)
{
	LoadTextureFromMemoryInternal(key, data, size, generateMipMaps, false);
}

std::shared_ptr<const TextureLease> TextureManager::AcquireTextureFromMemory(const std::string& key, const void* data, size_t size, bool generateMipMaps)
{
	if (key.empty()) throw std::invalid_argument("Texture lease key must not be empty");
	LoadTextureFromMemoryInternal(key, data, size, generateMipMaps, true);
	return AcquireLoadedTexture(key);
}

void TextureManager::LoadTextureFromMemoryInternal(const std::string& key, const void* data, size_t size, bool generateMipMaps, bool leased)
{
	if (textureDatas.contains(TextureKey{key, {}, true})) {
		if (!leased) collectibleTextures_.erase(key);
		return;
	}
	if (!data || size == 0) {
		throw std::invalid_argument("TextureManager embedded texture memory is empty: " + key);
	}

	DirectX::ScratchImage image = LoadTextureImageFromMemory(data, size);
	DirectX::ScratchImage mipImages =
		generateMipMaps ? CreateMipImages(std::move(image)) : std::move(image);
	TexturData textureData{};
	UploadTextureResource(textureData, mipImages);
	textureDatas.emplace(TextureKey{key, {}, true}, std::move(textureData));
	if (leased) collectibleTextures_.insert(key);
}

void TextureManager::LoadTextureFromRGBA(
	const std::string& key,
	uint32_t width,
	uint32_t height,
	const void* rgbaData,
	size_t rowPitch,
	bool generateMipMaps)
{
	LoadTextureFromRGBAInternal(key, width, height, rgbaData, rowPitch, generateMipMaps, false);
}

std::shared_ptr<const TextureLease> TextureManager::AcquireTextureFromRGBA(const std::string& key, uint32_t width, uint32_t height, const void* rgbaData, size_t rowPitch, bool generateMipMaps)
{
	if (key.empty()) throw std::invalid_argument("Texture lease key must not be empty");
	LoadTextureFromRGBAInternal(key, width, height, rgbaData, rowPitch, generateMipMaps, true);
	return AcquireLoadedTexture(key);
}

void TextureManager::LoadTextureFromRGBAInternal(const std::string& key, uint32_t width, uint32_t height, const void* rgbaData, size_t rowPitch, bool generateMipMaps, bool leased)
{
	if (textureDatas.contains(TextureKey{key, {}, true})) {
		if (!leased) collectibleTextures_.erase(key);
		return;
	}
	if (!rgbaData || width == 0 || height == 0 || rowPitch < width * 4ull) {
		throw std::invalid_argument("TextureManager RGBA texture data is invalid: " + key);
	}

	DirectX::ScratchImage image = CreateRgbaImage(width, height, rgbaData, rowPitch);
	DirectX::ScratchImage mipImages =
		generateMipMaps ? CreateMipImages(std::move(image)) : std::move(image);
	TexturData textureData{};
	UploadTextureResource(textureData, mipImages);
	textureDatas.emplace(TextureKey{key, {}, true}, std::move(textureData));
	if (leased) collectibleTextures_.insert(key);
}

void TextureManager::LoadTextures(const std::vector<std::string>& filePaths, TextureLoadOptions options)
{
	std::vector<TextureKey> pendingPaths;
	pendingPaths.reserve(filePaths.size());
	std::set<TextureKey> seenPaths;
	for (const std::string& filePath : filePaths) {
		const TextureKey key = MakeTextureKey(filePath, options);
		collectibleTextures_.erase(key.path);
		if (!textureDatas.contains(key) && seenPaths.insert(key).second) {
			pendingPaths.push_back(key);
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
	for (const TextureKey& key : pendingPaths) {
		mipImageBatch.push_back(PrepareFileTexture(key.path, options));
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

DirectX::ScratchImage TextureManager::LoadTextureImage(const std::string& filePath, TextureLoadOptions options)
{
	DirectX::ScratchImage image{};
	const std::string resolvedPath = ResolveTexturePath(filePath);
	std::wstring filePathW = StringUtility::ConvertString(resolvedPath);
	HRESULT hr;
	auto extension = std::filesystem::path(filePathW).extension().wstring();
	for (auto& c : extension) { c = static_cast<wchar_t>(std::towlower(c)); }
	if (extension == L".dds") {
		hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	} else {
		hr = DirectX::LoadFromWICFile(filePathW.c_str(),
			options.colorSpace == TextureColorSpace::Linear ? DirectX::WIC_FLAGS_IGNORE_SRGB : DirectX::WIC_FLAGS_FORCE_SRGB,
			nullptr, image);
	}
	std::ostringstream operation;
	operation << "TextureManager::LoadTextureImage"
		<< " requestedPath=\"" << filePath << "\""
		<< " resolvedPath=\"" << resolvedPath << '"';
	ThrowIfFailed(hr, operation.str().c_str());
	if (options.colorSpace != TextureColorSpace::Automatic) {
		const DXGI_FORMAT format = options.colorSpace == TextureColorSpace::Srgb
			? DirectX::MakeSRGB(image.GetMetadata().format) : DirectX::MakeLinear(image.GetMetadata().format);
		if ((options.colorSpace == TextureColorSpace::Srgb && !DirectX::IsSRGB(format)) || !image.OverrideFormat(format)) {
			throw std::runtime_error("Texture format does not support the requested color space: " + resolvedPath);
		}
	}
	return image;
}

DirectX::ScratchImage TextureManager::PrepareFileTexture(const std::string& filePath, TextureLoadOptions options)
{
	auto image = LoadTextureImage(filePath, options);
	if (options.mipMode == TextureMipMode::Preserve ||
		(options.mipMode == TextureMipMode::Automatic && IsUiTexture(filePath))) {
		return image;
	}
	return CreateMipImages(std::move(image), options.mipMode == TextureMipMode::Generate,
		options.colorSpace == TextureColorSpace::Linear ? DirectX::TEX_FILTER_DEFAULT : DirectX::TEX_FILTER_SRGB);
}

DirectX::ScratchImage TextureManager::LoadTextureImageFromMemory(const void* data, size_t size)
{
	DirectX::ScratchImage image{};
	const HRESULT hr = DirectX::LoadFromWICMemory(
		static_cast<const uint8_t*>(data),
		size,
		DirectX::WIC_FLAGS_FORCE_SRGB,
		nullptr,
		image);
	ThrowIfFailed(hr, "TextureManager::LoadTextureImageFromMemory");
	return image;
}

DirectX::ScratchImage TextureManager::CreateRgbaImage(
	uint32_t width,
	uint32_t height,
	const void* rgbaData,
	size_t rowPitch)
{
	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		width,
		height,
		1,
		1);
	ThrowIfFailed(hr, "TextureManager::CreateRgbaImage Initialize2D");

	const DirectX::Image* destination = image.GetImage(0, 0, 0);
	if (!destination || !destination->pixels) {
		throw std::runtime_error("TextureManager::CreateRgbaImage destination image is invalid");
	}

	const uint8_t* sourcePixels = static_cast<const uint8_t*>(rgbaData);
	for (uint32_t y = 0; y < height; ++y) {
		std::memcpy(
			destination->pixels + destination->rowPitch * y,
			sourcePixels + rowPitch * y,
			width * 4ull);
	}
	return image;
}

DirectX::ScratchImage TextureManager::CreateMipImages(DirectX::ScratchImage&& image, bool strict, DirectX::TEX_FILTER_FLAGS filter)
{
	if (DirectX::IsCompressed(image.GetMetadata().format)) {
		if (strict) { throw std::invalid_argument("Mip generation requires an uncompressed texture; use Preserve for authored DDS mips"); }
		return std::move(image);
	}

	DirectX::ScratchImage mipImages{};
	HRESULT hr = DirectX::GenerateMipMaps(
		image.GetImages(), image.GetImageCount(), image.GetMetadata(), filter, strict ? 0 : 4, mipImages);
	if (FAILED(hr)) {
		if (strict) { ThrowIfFailed(hr, "TextureManager explicit mip generation"); }
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

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filepath, TextureLoadOptions options)
{
	return GetLoadedTexture(filepath, options).srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filepath, TextureLoadOptions options)
{
	return GetLoadedTexture(filepath, options).srvHandleGPU;
}

const TextureManager::TexturData& TextureManager::GetLoadedTexture(
	const std::string& filePath, TextureLoadOptions options) const
{
	const auto it = textureDatas.find(MakeTextureKey(filePath, options));
	if (it == textureDatas.end() ||
		it->second.srvIndex == UINT32_MAX) {
		throw std::out_of_range(
			"TextureManager texture is not loaded: " + filePath);
	}
	collectibleTextures_.erase(it->first.path); // Raw metadata/descriptor exposure pins the entry.
	return it->second;
}

TextureManager::TextureKey TextureManager::MakeTextureKey(const std::string& filePath, TextureLoadOptions options) const
{
	if (options.colorSpace < TextureColorSpace::Automatic || options.colorSpace > TextureColorSpace::Linear ||
		options.mipMode < TextureMipMode::Automatic || options.mipMode > TextureMipMode::Generate) {
		throw std::invalid_argument("Invalid texture load options");
	}
	// Memory/RGBA entries retain their caller-provided opaque names.
	if (textureDatas.contains(TextureKey{filePath, {}, true})) {
		if (options != TextureLoadOptions{}) { throw std::invalid_argument("File load options cannot be applied to a memory texture"); }
		return {filePath, {}, true};
	}
	if (const auto found = resolvedTextureKeys_.find(filePath); found != resolvedTextureKeys_.end()) {
		return {found->second, options};
	}
	const TextureKey key{NormalizeAssetPath(ResolveTexturePath(filePath)), options};
	// Cache successful aliases only; failed loads must remain retryable.
	if (textureDatas.contains(key)) {
		resolvedTextureKeys_[filePath] = key.path;
	}
	return key;
}

std::shared_ptr<Engine::Base::DirectXCommon> TextureManager::GetDirectXCommon() const
{
	auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	return dxCommon;
}

}


