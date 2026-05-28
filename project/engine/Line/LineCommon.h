#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine::Base {
class DirectXCommon;
class GraphicsPipeline;
class SrvManager;
}

namespace Engine::LineSystem {

struct VertexDataLine
{
	Vector3 position;

};

struct LineInstanceData {
	Vector3 start;
	Vector3 end;
	Vector4 color;
};

struct CameraBufferforGpu {
	Matrix4x4 view;
	Matrix4x4 projection;

};

/// @brief ライン描画の共通 GPU 資源を管理するクラス
/// @details ライン用パイプライン、インスタンスバッファ、カメラバッファを保持し、
///          フレームごとに蓄積されたライン群を一括描画する。
class LineCommon
{
public:
	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return LineCommon のインスタンス
	static LineCommon* GetInstance();

	/// @brief ライン描画共通リソースを初期化する
	/// @param dxCommon DirectX 共通管理クラス
	/// @param srvManager SRV 管理クラス
	/// @return なし
	void Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager);

	/// @brief 共通管理インスタンスを解放する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief ライン描画の共通ステートを設定する
	/// @param なし
	/// @return なし
	void CommonDraw();

	/// @brief CPU 側に蓄積したラインデータを GPU 向けに更新する
	/// @param なし
	/// @return なし
	void Update();

	/// @brief 蓄積済みラインを一括描画する
	/// @param なし
	/// @return なし
	void Draw();

	/// @brief 描画キューへ 1 本のラインを追加する
	/// @param start 開始点
	/// @param end 終了点
	/// @param color ライン色
	/// @return なし
	void DrawLine(const Vector3& start, const Vector3& end, const Vector4& color);
	//DXCommon
	Engine::Base::DirectXCommon* GetDxCommon()const { return dxCommon_; }
	//SrvManager
	Engine::Base::SrvManager* GetSrvManager()const { return srvManager_; }


private:
	LineCommon() = default;
	~LineCommon();
	LineCommon(const LineCommon&) = delete;
	LineCommon& operator=(const LineCommon&) = delete;
	void InitializePipeline();
	void InitializeVertexResources();
	void InitializeCameraResource();
	void UpdateCameraBuffer();
	void EnsureInstanceResourceCapacity(size_t instanceSize);
	void UploadInstances(size_t instanceSize);
	void EnsureInstanceSrvIndex();
	void UpdateInstanceSrv();
private:
	static const Vector3 kDefaultLineStart_;
	static const Vector3 kDefaultLineEnd_;
	static const Vector4 kDefaultLineColor_;
	static const std::array<VertexDataLine, 2> kDefaultLineVertices_;

	Engine::Base::DirectXCommon* dxCommon_;
	Engine::Base::SrvManager* srvManager_;
	std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_;

	uint32_t instanceSrvIndex_ = UINT32_MAX;
	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;

	LineInstanceData instance = {
		.start = kDefaultLineStart_,
		.end = kDefaultLineEnd_,
		.color = kDefaultLineColor_
	};

	std::vector<VertexDataLine>linevertices = { kDefaultLineVertices_.begin(), kDefaultLineVertices_.end() };
	std::vector<LineInstanceData> instances_; // ← 複数ライン用

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource;//カメラのデータを送るためのリソース
	CameraBufferforGpu* camerabuffer = nullptr;//カメラのデータをGPUに送るための構造体


};

}
