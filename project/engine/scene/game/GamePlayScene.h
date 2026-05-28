#pragma once

#include "BaseScene.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>

namespace Engine::InputSystem {
class Input;
}

namespace Engine::CameraSystem {
class Camera;
}

namespace Engine::Graphics2D {
class Sprite;
}

namespace Engine::Graphics3D {
class Object3D;
}

namespace Engine::Particle {
class ParticleEmitter;
}

namespace Engine::Skybox {
class SkyBox;
}

/// @brief ゲーム中のメイン処理を担当するシーンクラス
/// @details カメラ、3D オブジェクト、スプライト、パーティクル、スカイボックスを管理し、
///          ゲームプレイ中の更新と描画を統括する。
namespace Engine::Scene {

class PlayerControlState;
class IdlePlayerControlState;
class MovePlayerControlState;
class ScalePlayerControlState;
class RotatePlayerControlState;

class GamePlayScene : public BaseScene
{
    friend class PlayerControlState;
    friend class IdlePlayerControlState;
    friend class MovePlayerControlState;
    friend class ScalePlayerControlState;
    friend class RotatePlayerControlState;

public:
    GamePlayScene();
    ~GamePlayScene() override;

    /// @brief ゲームプレイシーンを初期化する
    /// @param なし
    /// @return なし
    void Initialize() override;

    /// @brief ゲームプレイシーンの終了処理を行う
    /// @param なし
    /// @return なし
    void Finalize() override;

    /// @brief ゲームプレイシーンを更新する
    /// @param なし
    /// @return なし
    void Update() override;

    /// @brief ゲームプレイシーンを描画する
    /// @param なし
    /// @return なし
    void Draw() override;

    /// @brief シーンで使用するモデルを読み込む
    /// @param なし
    /// @return なし
    void LoadModel();

private:
    /// @brief シーン用カメラを初期化する
    /// @param なし
    /// @return なし
    void InitializeCameras();

    /// @brief モデルロード時間を計測しつつ事前ロードを行う
    /// @param なし
    /// @return なし
    void LoadSceneResources();

    /// @brief 3D/2D オブジェクトを初期化する
    /// @param なし
    /// @return なし
    void InitializeSceneObjects();

    /// @brief パーティクルとエミッタを初期化する
    /// @param なし
    /// @return なし
    void InitializeParticles();

    /// @brief 入力に応じたシーン内操作を更新する
    /// @param なし
    /// @return なし
    void UpdateInput();

    /// @brief シーン内オブジェクトの状態を更新する
    /// @param なし
    /// @return なし
    void UpdateSceneObjects();

    /// @brief デバッグ用 UI を描画する
    /// @param なし
    /// @return なし
    void DrawDebugUi();

    /// @brief シーン切り替え用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawSceneControlUi();

    /// @brief カメラ調整用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawCameraControlUi();

    /// @brief 3D オブジェクト調整用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawObjectControlUi();

    /// @brief スプライト調整用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawSpriteControlUi();

    /// @brief ライト調整用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawLightControlUi();
    void DrawDirectionalLightUi();
    void DrawPointLightUi();
    void DrawSpotLightUi();
    void DrawPointLightToggleUi();
    void DrawPointLightSettingsUi(Vector4& color, Vector3& position, float& intensity, float& decay, float& radius);
    void DrawSpotLightToggleUi();
    void DrawSpotLightBasicSettingsUi(Vector4& color, Vector3& position, Vector3& direction, float& intensity, float& distance);
    void DrawSpotLightFalloffSettingsUi(float& decay, float& coneAngleCos, float& cosFalloffStart);

    /// @brief パーティクル調整用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawParticleControlUi();

    /// @brief 環境マップ調整用デバッグ UI を描画する
    /// @param なし
    /// @return なし
    void DrawEnvironmentMapUi();

    /// @brief 移動入力に応じてプレイヤー位置を更新する
    /// @param deltaX X 軸移動量
    /// @param deltaZ Z 軸移動量
    /// @return なし
    void ApplyMoveInput(float deltaX, float deltaZ);

    /// @brief 拡縮入力に応じてプレイヤースケールを更新する
    /// @param scaleDelta 各軸へ加える拡縮量
    /// @return なし
    void ApplyScaleInput(float scaleDelta);

    /// @brief 回転入力に応じてプレイヤー回転を更新する
    /// @param rotateDeltaY Y 軸へ加える回転量
    /// @return なし
    void ApplyRotateInput(float rotateDeltaY);

    /// @brief トグル入力に応じてデバッグフラグを更新する
    /// @param input 入力管理クラス
    /// @return なし
    void UpdateToggleFlag(Engine::InputSystem::Input& input);

    // カメラ
    std::unique_ptr<Engine::CameraSystem::Camera> camera1;   // メインカメラ
    std::unique_ptr<Engine::CameraSystem::Camera> camera2;   // サブカメラ

    // 3Dオブジェクト
    std::unique_ptr<Engine::Graphics3D::Object3D> object3D; // メインオブジェクト
    std::unique_ptr<Engine::Graphics3D::Object3D> terrain;  // 地形オブジェクト

    // パーティクル
    std::unique_ptr<Engine::Particle::ParticleEmitter> particleEmitter;   // パーティクルエミッタ1
    std::unique_ptr<Engine::Particle::ParticleEmitter> particleEmitter2;  // パーティクルエミッタ2

    // ライト設定フラグ
    bool light = true;           // ライト全体のON/OFF
    bool directionLight = true;  // 平行光源ON/OFF
    bool pointLight = false;     // ポイントライトON/OFF
    bool spotLight = false;      // スポットライトON/OFF

    // 2Dスプライト
    std::unique_ptr<Engine::Graphics2D::Sprite> sprite;

    // デバッグ用フラグ
    bool number = 0;

    // プレイヤー操作状態
    std::unique_ptr<PlayerControlState> controlState_;

    // スカイボックス
    std::unique_ptr<Engine::Skybox::SkyBox> skyBox;
};

}
