#pragma once
#include <string>
#include"externals/DirectXTex/DirectXTex.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

/// @brief テクスチャ資産の読込と SRV 管理を行うクラス
/// @details テクスチャファイルを GPU リソースへ展開し、ファイルパス単位で
///          SRV ハンドルとメタデータを再利用できるよう保持する。
namespace Engine::Base {

class DirectXCommon;
class SrvManager;

class TextureManager
{
private:
	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = default;
	TextureManager& operator=(TextureManager&) = delete;

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

	/// @brief 保持しているテクスチャ資産を解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief テクスチャ管理に必要な共通リソースを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvManager SRV 管理クラス
	/// @return なし
	void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon, Engine::Base::SrvManager* srvManager);

	/// @brief 読み込み済みテクスチャのメタデータを取得する
	/// @param filepath テクスチャのファイルパス
	/// @return 指定テクスチャのメタデータ
	const DirectX::TexMetadata& GetMetaData(const std::string&filepath);
	
	/// @brief テクスチャファイルを読み込んで GPU へ登録する
	/// @param filePath 読み込むテクスチャのファイルパス
	/// @return なし
	void LoadTexture(const std::string& filePath);
	void LoadTextures(const std::vector<std::string>& filePaths);
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
	uint32_t GetTextureIndexByFilePath(const std::string& filePath);


	/// @brief ファイルパスに対応するテクスチャの GPU ハンドルを取得する
	/// @param filepath 検索対象のファイルパス
	/// @return 該当テクスチャの GPU デスクリプタハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filepath);

	//Srvの最初
	static uint32_t kSRVIndexTop;

private:
	std::shared_ptr<Engine::Base::DirectXCommon> GetDirectXCommon() const;
	DirectX::ScratchImage LoadTextureImage(const std::string& filePath);
	DirectX::ScratchImage LoadTextureImageFromMemory(const void* data, size_t size);
	DirectX::ScratchImage CreateRgbaImage(
		uint32_t width,
		uint32_t height,
		const void* rgbaData,
		size_t rowPitch);
	DirectX::ScratchImage CreateMipImages(DirectX::ScratchImage&& image);
	void UploadTextureResource(TexturData& textureData, const DirectX::ScratchImage& mipImages);
	Microsoft::WRL::ComPtr<ID3D12Resource> RecordTextureUpload(
		TexturData& textureData, const DirectX::ScratchImage& mipImages);
	void CreateTextureSrv(TexturData& textureData);
	const TexturData& GetLoadedTexture(
		const std::string& filePath) const;

	//テクスチャデータ
	
	std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_;
	std::unordered_map<std::string, TexturData> textureDatas;
	Engine::Base::SrvManager* srvManager_ = nullptr;

};

}

