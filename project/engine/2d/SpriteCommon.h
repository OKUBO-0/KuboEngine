#pragma once
#include <cstdint>
#include <memory>
#include <wrl/client.h>
#include <d3d12.h>

namespace Engine::Base {
class DirectXCommon;
class GraphicsPipeline;
}

namespace Engine::Graphics2D {

/// @brief Sprite 描画の共通設定を管理するクラス
/// @details DirectX 共通クラスと 2D 用グラフィックスパイプラインを束ね、
///          スプライト描画前の共通ステートを適用する。
class SpriteCommon
{
public:
    struct DrawStats {
        uint32_t drawCallCount = 0;
        uint32_t uploadedBytes = 0;
    };

    static SpriteCommon* GetInstance();

    /// @brief Sprite 描画の共通リソースを初期化する
    /// @param dxCommon DirectX 共通管理クラス
    /// @return なし
    void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon);

    /// @brief 共通管理インスタンスを解放する
    /// @param なし
    /// @return なし
    void Finalize();

    /// @brief Sprite 描画の共通ステートを設定する
    /// @param なし
    /// @return なし
    void CommonDraw();
    void RecordSpriteDraw(uint32_t uploadedBytes);
    const DrawStats& GetLastDrawStats() const { return lastDrawStats_; }
    const DrawStats& GetCurrentDrawStats() const { return currentDrawStats_; }
    const D3D12_INDEX_BUFFER_VIEW& GetSharedIndexBufferView() const
    {
        return sharedIndexBufferView_;
    }

    /// @brief DirectX 共通管理を shared_ptr で返し、呼び出し側の一時保持中に破棄されないようにする
    std::shared_ptr<Engine::Base::DirectXCommon> GetDxCommon() const { return dxCommon_.lock(); }

private:
    SpriteCommon() = default;
    ~SpriteCommon();
    SpriteCommon(const SpriteCommon&) = delete;
    SpriteCommon& operator=(const SpriteCommon&) = delete;
    void InitializeSharedBuffers();

    std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_; // Framework所有。使用時だけ shared_ptr 化する
    std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_; // グラフィックスパイプライン
    Microsoft::WRL::ComPtr<ID3D12Resource> sharedIndexBuffer_;
    D3D12_INDEX_BUFFER_VIEW sharedIndexBufferView_{};
    DrawStats currentDrawStats_{};
    DrawStats lastDrawStats_{};
};

}
