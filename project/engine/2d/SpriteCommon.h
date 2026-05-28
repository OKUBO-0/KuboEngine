#pragma once
#include <memory>

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
    static SpriteCommon* GetInstance();

    /// @brief Sprite 描画の共通リソースを初期化する
    /// @param dxCommon DirectX 共通管理クラス
    /// @return なし
    void Initialize(Engine::Base::DirectXCommon* dxCommon);

    /// @brief 共通管理インスタンスを解放する
    /// @param なし
    /// @return なし
    void Finalize();

    /// @brief Sprite 描画の共通ステートを設定する
    /// @param なし
    /// @return なし
    void CommonDraw();

    Engine::Base::DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    SpriteCommon() = default;
    ~SpriteCommon();
    SpriteCommon(const SpriteCommon&) = delete;
    SpriteCommon& operator=(const SpriteCommon&) = delete;

    Engine::Base::DirectXCommon* dxCommon_ = nullptr; // DX共通クラス参照
    std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_; // グラフィックスパイプライン
};

}
