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
    void Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon);

    /// @brief 共通管理インスタンスを解放する
    /// @param なし
    /// @return なし
    void Finalize();

    /// @brief Sprite 描画の共通ステートを設定する
    /// @param なし
    /// @return なし
    void CommonDraw();

    /// @brief DirectX 共通管理を shared_ptr で返し、呼び出し側の一時保持中に破棄されないようにする
    std::shared_ptr<Engine::Base::DirectXCommon> GetDxCommon() const { return dxCommon_.lock(); }

private:
    SpriteCommon() = default;
    ~SpriteCommon();
    SpriteCommon(const SpriteCommon&) = delete;
    SpriteCommon& operator=(const SpriteCommon&) = delete;

    std::weak_ptr<Engine::Base::DirectXCommon> dxCommon_; // Framework所有。公開時だけ shared_ptr 化する
    Engine::Base::DirectXCommon* dxCommonRaw_ = nullptr; // フレーム内描画用の非所有キャッシュ
    std::unique_ptr<Engine::Base::GraphicsPipeline> graphicsPipeline_; // グラフィックスパイプライン
};

}
