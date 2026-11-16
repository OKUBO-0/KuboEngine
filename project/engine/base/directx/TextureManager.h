#pragma once
#include "TextureLoadOptions.h"
#include "../ProjectPaths.h"
#include <string>
#include"externals/DirectXTex/DirectXTex.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <compare>
#include <unordered_map>
#include <vector>
#include <unordered_set>

/// @brief テクスチャ資産の読込と SRV 管理を行うクラス
/// @details テクスチャファイルを GPU リソースへ展開し、ファイルパス単位で
///          SRV ハンドルとメタデータを再利用できるよう保持する。
namespace Engine::Base {

class DirectXCommon;
class SrvManager;

// Keep this lease alive while using its descriptor. Owner-thread cache API only.
struct TextureLease {
	std::string key;
	uint32_t srvIndex = UINT32_MAX;
	DirectX::TexMetadata metadata{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
};

class TextureManager
{
private:
	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = default;
	TextureManager& operator=(TextureManager&) = delete;
	struct TextureKey {
		std::string path;
		TextureLoadOptions options{};
		bool memory = false;
		auto operator<=>(const TextureKey&) const = default;
	};

	//テクスチャ1枚分のデータ
	struct TexturData {

		DirectX::TexMetadata metadata;
		Microsoft::WRL::ComPtr<ID3D12Resource>resource;
		uint32_t srvIndex = UINT32_MAX;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;

	};
public:
	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return TextureManager のインスタンス
	static TextureManager* GetInstance();
	// Returns an opaque revision key. Old resources/descriptors remain valid until Finalize.
	std::string LoadTextureRevision(const std::string& filePath, const std::string& revision);
	std::shared_ptr<const TextureLease> AcquireTextureRevision(const std::string& filePath, const std::string& revision);
	std::shared_ptr<const TextureLease> AcquireTextureFromMemory(const std::string& key, const void* data, size_t size, bool generateMipMaps = true);
	std::shared_ptr<const TextureLease> AcquireTextureFromRGBA(const std::string& key, uint32_t width, uint32_t height, const void* rgbaData, size_t rowPitch, bool generateMipMaps = true);
	// Owner-thread view creation in a caller-owned heap. A live managed lease
	// retains the resource; shader-visible descriptors cannot be copy sources.
	void CreateLeasedTexture2DView(const TextureLease& lease, D3D12_CPU_DESCRIPTOR_HANDLE destination) const;
	// Before recording draws. Flushes pending commands before deleting resources.
	size_t CollectUnusedTextures();
	size_t GetCachedTextureCount() const { return textureDatas.size(); }

	/// @brief 保持しているテクスチャ資産を解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief テクスチャ管理に必要な共通リソースを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvManager SRV 管理クラス
	/// @return なし
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager,
		const std::filesystem::path& projectRoot = {});

	/// @brief 読み込み済みテクスチャのメタデータを取得する
	/// @param filepath テクスチャのファイルパス
	/// @return 指定テクスチャのメタデータ
	const DirectX::TexMetadata& GetMetaData(const std::string& filepath, TextureLoadOptions options = {});
	
	/// @brief テクスチャファイルを読み込んで GPU へ登録する
	/// @param filePath 読み込むテクスチャのファイルパス
	/// @return なし
	// Pass matching options to getters. Preserve keeps authored DDS mips.
	void LoadTexture(const std::string& filePath, TextureLoadOptions options = {});
	void LoadTextures(const std::vector<std::string>& filePaths, TextureLoadOptions options = {});
	void LoadTextureFromMemory(
		const std::string& key,
		const void* data,
		size_t size,
		bool generateMipMaps = true);
	void LoadTextureFromRGBA(
		const std::string& key,
		uint32_t width,
		uint32_t height,
		const void* rgbaData,
		size_t rowPitch,
		bool generateMipMaps = true);

	/// @brief ファイルパスに対応するテクスチャの SRV インデックスを取得する
	/// @param filePath 検索対象のファイルパス
	/// @return 該当テクスチャの SRV インデックス
	uint32_t GetTextureIndexByFilePath(const std::string& filePath, TextureLoadOptions options = {});


	/// @brief ファイルパスに対応するテクスチャの GPU ハンドルを取得する
	/// @param filepath 検索対象のファイルパス
	/// @return 該当テクスチャの GPU デスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filepath, TextureLoadOptions options = {});

	//Srvの最初
	static uint32_t kSRVIndexTop;

private:
	std::string ResolveTexturePath(const std::string& filePath) const;
	std::shared_ptr<const TextureLease> AcquireLoadedTexture(const std::string& key);
	void LoadTextureFromMemoryInternal(const std::string& key, const void* data, size_t size, bool generateMipMaps, bool leased);
	void LoadTextureFromRGBAInternal(const std::string& key, uint32_t width, uint32_t height, const void* rgbaData, size_t rowPitch, bool generateMipMaps, bool leased);
	std::string LoadTextureRevisionInternal(const std::string& filePath, const std::string& revision, bool leased);
	TextureKey MakeTextureKey(const std::string& filePath, TextureLoadOptions options) const;
	std::shared_ptr<Engine::Base::DirectXCommon> GetDirectXCommon() const;
	DirectX::ScratchImage LoadTextureImage(const std::string& filePath, TextureLoadOptions options);
	DirectX::ScratchImage PrepareFileTexture(const std::string& filePath, TextureLoadOptions options);
	DirectX::ScratchImage LoadTextureImageFromMemory(const void* data, size_t size);
	DirectX::ScratchImage CreateRgbaImage(
		uint32_t width,
		uint32_t height,
		const void* rgbaData,
		size_t rowPitch);
	DirectX::ScratchImage CreateMipImages(DirectX::ScratchImage&& image, bool strict = false,
		DirectX::TEX_FILTER_FLAGS filter = DirectX::TEX_FILTER_SRGB);
	void UploadTextureResource(TexturData& textureData, const DirectX::ScratchImage& mipImages);
	Microsoft::WRL::ComPtr<ID3D12Resource> RecordTextureUpload(
		TexturData& textureData, const DirectX::ScratchImage& mipImages);
	void CreateTextureSrv(TexturData& textureData);
	const TexturData& GetLoadedTexture(
		const std::string& filePath, TextureLoadOptions options) const;

	//テクスチャデータ
	
	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;
	std::filesystem::path projectRoot_;
	std::map<TextureKey, TexturData> textureDatas;
	mutable std::unordered_map<std::string, std::string> resolvedTextureKeys_;
	std::map<std::string, std::shared_ptr<const TextureLease>> textureLeases_;
	mutable std::unordered_set<std::string> collectibleTextures_;
	Engine::Base::SrvManager* srvManager_ = nullptr;

};

}

