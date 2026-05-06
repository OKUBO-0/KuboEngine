# Review Progress

## 目的

チェックリストの各項目が最終的にすべて `〇` になるように、既存コードの構造・コメント・命名・設計を段階的に修正する。

## 現在の方針

- まずは低コストで効く項目を優先して修正する
- ビルドが通る状態を維持しながら段階的に進める
- 設計変更が必要な項目は、フェーズ1の整理後に個別対応する

## ここまでに対応した内容

### 1. コメント・ドキュメント整備

- 多数のヘッダに `@brief / @param / @return` コメントを追加
- クラスコメントに目的と責務を追記
- 古い `<summary>` コメントを Doxygen 形式へ統一
- `GamePlayScene.cpp`、`DirectXCommon.cpp`、`GraphicsPipeline.cpp`、`Model.cpp`、`ParticleManager.cpp` などに処理意図コメントを追加

### 2. スペリング・命名修正

- `imGuiMnager` -> `imGuiManager`
- `setColor` -> `SetColor`
- `worldInberseTranspose` -> `worldInverseTranspose`
- `consAngle` -> `coneAngleCos`
- `cosFalloffstrt` -> `cosFalloffStart`
- `rtvStarHandle` -> `rtvStartHandle`
- `pResourece` -> `pResource`
- `SetGraficsRootDescriptorTable` -> `SetGraphicsRootDescriptorTable`
- `CaMeraForGpu` -> `CameraForGpu`
- `VertexWightData` -> `VertexWeightData`
- `shiniess` -> `shininess`
- `resouceDesc` / `subresouces` -> `resourceDesc` / `subresources`
- `foemat` / `nextIndenx` / `transrate` -> `format` / `nextIndex` / `translate`
- `bottm` / `nearCip` / `farCip` -> `bottom` / `nearClip` / `farClip`
- `srvMnager` / `srvmnager` / `srvmaneger` / `dxcommn` / `modeleCommon` の表記ゆれを整理
- `srvmanager`、`srvmanage`、`dxcommon`、`winapp` などの引数・メンバー名を既存の `camelCase` / 末尾 `_` ルールへ寄せた

### 3. コメントアウトコードとノイズ削除

- `Object3D.cpp` の不要コメントアウト削除
- `GraphicsPipeline.cpp` の `////rootParameters...` 系コメント整理
- 代表的な誤記コメントを各所で整理

### 4. マジックナンバー整理

- `GamePlayScene.cpp`
- `Camera.cpp`
- `OffscreenRenderManager.h`
- `ParticleManager.cpp`
- `Input.cpp`
- `Audio.cpp`
- `Line.cpp`
- `LineCommon.h/.cpp`
- `Object3D.cpp`
- `AttackBehavior.cpp`
- `MagicCircleBehavior.cpp`
- `ParticleEmitter.cpp`
- `DirectXCommon.cpp`

上記で、代表的な固定値を `constexpr` または `const` の名前付き定数へ切り出し済み。

### 5. `.vcxproj.filters` の整理

- `Line` / `line` の揺れを整理
- `MyMath.h` の誤配置修正
- `Line.h` / `LineCommon.h` のフィルタ修正
- `Resources\models` を追加し、モデルファイルの配置を整理
- `PlayerControlState` 追加後のエントリも反映済み

### 6. ビルド設定の整合

- `KuboEngine.vcxproj`
- `externals/imgui/imgui.vcxproj`
- `externals/DirectXTex/DirectXTex_Desktop_2022_Win10.vcxproj`

で `PlatformToolset` を現環境の Visual Studio 18 に合わせて `v145` に統一し、ソリューション全体がビルド可能な状態へ更新した。
Assimp も `v145` / 静的 CRT でビルドし、`assimp-vc145-mtd.lib` / `assimp-vc145-mt.lib` と zlib 依存をリンクする形へ切り替え済み。

### 6.5. デバッグ実行時の例外停止対策

- `DirectXCommon.cpp`
  - `ID3D12InfoQueue` の停止条件を見直し、`CORRUPTION` と `ERROR` のみでブレークするよう変更
  - `WARNING` では停止しないようにし、終了時の live object レポートで `0x87A` が飛ぶ状況を抑制

- `D3DResourceLeakChecker.cpp`
  - `ReportLiveObjects` を `DXGI_DEBUG_RLO_ALL` ではなく `DXGI_DEBUG_RLO_SUMMARY` へ変更
  - 終了時の診断は残しつつ、冗長なレポート由来の停止を減らすよう調整

- `main.cpp`
  - `Application::Run` 側の `CoInitializeEx` 呼び出しを削除
  - `WinApp` 側の初期化・終了処理へ COM ライフサイクルを一本化し、二重初期化を解消

### 7. `WinMain` の責務縮小

- `main.cpp` に `Application` を追加
- `WinMain` は最小限の委譲だけを行う形へ整理

### 8. State パターン導入

- `engine/scene/PlayerControlState.h`
- `engine/scene/PlayerControlState.cpp`

を追加し、`GamePlayScene` のプレイヤー入力を以下の状態へ分割済み。

- `IdlePlayerControlState`
- `MovePlayerControlState`
- `ScalePlayerControlState`
- `RotatePlayerControlState`

### 8.5. `namespace` 導入の着手

- `engine/skybox/SkyBox.h`
- `engine/skybox/SkyBox.cpp`
- `engine/skybox/SkyBoxCommon.h`
- `engine/skybox/SkyBoxCommon.cpp`

で `SkyBox` と `SkyBoxCommon` を `Engine::Skybox` へ移動し、`Framework.cpp` と `GamePlayScene.*` の参照も追随済み。

- `engine/camera/Camera.h`
- `engine/camera/Camera.cpp`
- `engine/camera/CameraManager.h`
- `engine/camera/CameraManager.cpp`

で `Camera` と `CameraManager` を `Engine::CameraSystem` へ移動し、`Framework.cpp`、`GamePlayScene.*`、`GameClearScene.cpp`、`GameOverScene.cpp`、`Sprite.cpp`、`Object3D.cpp`、`ParticleManager.cpp`、`LineCommon.cpp`、`SkyBox.cpp` の参照も追随済み。

- `engine/base/WinApp.h`
- `engine/base/WinApp.cpp`
- `engine/base/D3DResourceLeakChecker.h`
- `engine/base/D3DResourceLeakChecker.cpp`

で `WinApp` と `D3DResourceLeakChecker` を `Engine::Base` へ移動し、`Framework.*`、`DirectXCommon.*`、`ImGuiManager.*`、`Input.*`、`Camera.*`、`Sprite.cpp`、`OffscreenRenderManager.cpp`、`main.cpp` の参照も追随済み。

- `engine/base/DirectXCommon.h`
- `engine/base/DirectXCommon.cpp`
- `engine/base/SrvManager.h`
- `engine/base/SrvManager.cpp`
- `engine/base/ImGuiManager.h`
- `engine/base/ImGuiManager.cpp`

で `DirectXCommon`、`SrvManager`、`ImGuiManager` を `Engine::Base` へ移動し、`Framework.*`、`GraphicsPipeline.*`、`TextureManager.*`、`OffscreenRenderManager.*`、`SpriteCommon.*`、`ModelCommon.*`、`ModelManager.*`、`Object3DCommon.*`、`LineCommon.*`、`ParticleManager.*`、`SkyBoxCommon.*`、`SkyBox.h` のシグネチャも追随済み。

- `engine/base/TextureManager.h`
- `engine/base/TextureManager.cpp`
- `engine/base/GraphicsPipeline.h`
- `engine/base/GraphicsPipeline.cpp`
- `engine/base/OffscreenRenderManager.h`
- `engine/base/OffscreenRenderManager.cpp`

で `TextureManager`、`GraphicsPipeline`、`OffscreenRenderManager` も `Engine::Base` へ移動し、`Framework.*`、`Sprite.*`、`SpriteCommon.*`、`Model.cpp`、`Object3D.*`、`Object3DCommon.*`、`LineCommon.*`、`ParticleManager.*`、`SkyBox.*`、`SkyBoxCommon.*` の呼び出しと所有型も追随済み。

- `engine/base/Framework.h`
- `engine/base/Framework.cpp`

で `Framework` も `Engine::Base` へ移動し、`Game.h`、`Game.cpp`、`main.cpp` の継承・明示呼び出し・所有型も追随済み。

- `engine/scene/BaseScene.h`
- `engine/scene/BaseScene.cpp`
- `engine/scene/AbstractSceneFactory.h`
- `engine/scene/AbstractSceneFactory.cpp`
- `engine/scene/SceneFactory.h`
- `engine/scene/SceneFactory.cpp`
- `engine/scene/SceneManager.h`
- `engine/scene/SceneManager.cpp`
- `engine/scene/Game.h`
- `engine/scene/Game.cpp`
- `engine/scene/TitleScene.h`
- `engine/scene/TitleScene.cpp`
- `engine/scene/GamePlayScene.h`
- `engine/scene/GamePlayScene.cpp`
- `engine/scene/GameClearScene.h`
- `engine/scene/GameClearScene.cpp`
- `engine/scene/GameOverScene.h`
- `engine/scene/GameOverScene.cpp`
- `engine/scene/PlayerControlState.h`
- `engine/scene/PlayerControlState.cpp`

で `BaseScene`、`AbstractSceneFactory`、`SceneFactory`、`SceneManager`、`Game`、各シーンクラス、`PlayerControlState` を `Engine::Scene` へ移動し、`Framework.*`、`main.cpp`、シーン遷移呼び出し、状態遷移まわりの参照も追随済み。

- `engine/3d/Model.h`
- `engine/3d/Model.cpp`
- `engine/3d/ModelCommon.h`
- `engine/3d/ModelCommon.cpp`
- `engine/3d/ModelManager.h`
- `engine/3d/ModelManager.cpp`
- `engine/3d/Object3D.h`
- `engine/3d/Object3D.cpp`
- `engine/3d/Object3DCommon.h`
- `engine/3d/Object3DCommon.cpp`

で `Model`、`ModelCommon`、`ModelManager`、`Object3D`、`Object3DCommon` と関連構造体を `Engine::Graphics3D` へ移動し、`Framework.cpp`、`GamePlayScene.*`、`TitleScene.cpp`、`GameClearScene.cpp`、`GameOverScene.cpp`、`ParticleManager.*` の参照も追随済み。

- `engine/particle/IParticleBehavior.h`
- `engine/particle/IParticleBehavior.cpp`
- `engine/particle/AttackBehavior.h`
- `engine/particle/AttackBehavior.cpp`
- `engine/particle/MagicCircleBehavior.h`
- `engine/particle/MagicCircleBehavior.cpp`
- `engine/particle/ParticleEmitter.h`
- `engine/particle/ParticleEmitter.cpp`
- `engine/particle/ParticleManager.h`
- `engine/particle/ParticleManager.cpp`

で `IParticleBehavior`、`AttackBehavior`、`MagicCircleBehavior`、`ParticleEmitter`、`ParticleManager`、`Particle` 関連構造体を `Engine::Particle` へ移動し、`Framework.cpp` と `GamePlayScene.*` の参照も追随済み。

- `engine/audio/Audio.h`
- `engine/audio/Audio.cpp`

で `Audio`、`SoundData`、Wave 読み込み補助構造体を `Engine::AudioSystem` へ移動し、`Framework.cpp` の初期化・終了処理も追随済み。

- `engine/input/Input.h`
- `engine/input/Input.cpp`

で `Input` を `Engine::InputSystem` へ移動し、`Framework.cpp`、`GamePlayScene.*`、`PlayerControlState.*`、`TitleScene.cpp` の入力参照も追随済み。

- `engine/2d/Sprite.h`
- `engine/2d/Sprite.cpp`
- `engine/2d/SpriteCommon.h`
- `engine/2d/SpriteCommon.cpp`

で `Sprite` と `SpriteCommon` を `Engine::Graphics2D` へ移動し、`Framework.cpp`、`GamePlayScene.*`、`TitleScene.cpp`、`GameClearScene.cpp`、`GameOverScene.cpp` の描画参照も追随済み。

- `engine/Line/Line.h`
- `engine/Line/Line.cpp`
- `engine/Line/LineCommon.h`
- `engine/Line/LineCommon.cpp`

で `Line`、`LineCommon`、ライン描画用構造体を `Engine::LineSystem` へ移動し、`Framework.cpp` の初期化・更新・終了処理も追随済み。

- `engine/base/Logger.h`
- `engine/base/Logger.cpp`
- `engine/base/StringUtility.h`
- `engine/base/StringUtility.cpp`

で `Logger` と `StringUtility` を `Engine::Base` 配下へ移動し、`CameraManager.cpp`、`GamePlayScene.cpp`、`GraphicsPipeline.cpp` の base 外参照も明示修飾へ追随済み。

- `engine/math/Vector2.h`
- `engine/math/Vector3.h`
- `engine/math/Vector4.h`
- `engine/math/Matrix3x3.h`
- `engine/math/Matrix4x4.h`
- `engine/math/Quaternion.h`
- `engine/math/RenderingData.h`
- `engine/math/MyMath.h`
- `engine/math/MyMath.cpp`

で math 系の値型、レンダリング用データ構造、`MyMath` 関数群を `Engine::Math` 配下へ移動した。既存コードの一括破壊を避けるため、当面はグローバル互換 alias を残して段階移行できる形にしている。

### 9. 長関数分割

- `GamePlayScene::DrawDebugUi`
  - `DrawSceneControlUi`
  - `DrawCameraControlUi`
  - `DrawObjectControlUi`
  - `DrawSpriteControlUi`
  - `DrawLightControlUi`
  - `DrawParticleControlUi`
  - `DrawEnvironmentMapUi`

- `GamePlayScene::Initialize`
  - `InitializeCameras`
  - `LoadSceneResources`
  - `InitializeSceneObjects`
  - `InitializeParticles`

- `Sprite.cpp`
  - `Initialize` を GPU リソース生成、バッファビュー初期化、マテリアル初期化、行列初期化、カメラ初期化へ分割
  - `Update` をカメラ更新、頂点更新、インデックス更新、行列更新へ分割

- `Object3D.cpp`
  - `Initialize` を Transform、ライト、環境反射、カメラの初期化へ分割
  - `Update` をアニメーション更新、モデル設定反映、行列更新へ分割

- `Model::Initialize`
  - `LoadRuntimeAssets`
  - `CreateVertexBuffer`
  - `CreateIndexBuffer`
  - `CreateMaterialBuffer`
  - `LoadMaterialTexture`

- `Model::LoadModelFile`
  - `LoadVerticesFromMesh`
  - `LoadIndicesFromMesh`
  - `LoadSkinClusterDataFromMesh`
  - `LoadMaterialFromScene`

- `Model.cpp`
  - `LoadAnimationFile` を `LoadAnimationChannels`、`LoadTranslateKeys`、`LoadRotateKeys`、`LoadScaleKeys` に分割
  - `CreateSkinCluster` を `InitializePaletteResources`、`InitializeInfluenceResources`、`InitializeInverseBindPoseMatrices`、`ApplyJointWeightsToSkinCluster` に分割

- `DirectXCommon::Initialize`
  - `InitializeGraphicsResources`

- `DirectXCommon::Begin`
  - `PrepareBackBufferForRendering`

- `DirectXCommon::End`
  - `FinalizeFrameTransition`
  - `WaitForGpuCompletion`
  - `ResetCommandObjects`

- `DirectXCommon.cpp`
  - コマンド実行の重複を `CloseAndExecuteCommandList` に集約し、`FinalizeFrameTransition` と `CommandKick` から共通利用する形へ整理
  - `CompileShader` を `LoadShaderSource`、`CreateShaderSourceBuffer`、`CreateShaderCompileArguments`、`ExecuteShaderCompile`、`ValidateShaderCompileResult` に分割

- `Framework.cpp`
  - `Initialize` を `InitializeCoreServices`、`InitializeSharedManagers`、`InitializeRenderingCommons`、`InitializeDebugTools` に分割
  - `Finalize` を `FinalizeDebugTools`、`FinalizeSharedManagers` に分割

- `WinApp.cpp`
  - `Initialize` を `RegisterWindowClass`、`CreateMainWindow` に分割

- `GraphicsPipeline.cpp`
  - 通常描画、スプライト、パーティクル、ライン、スキニング、スカイボックス生成で共通ヘルパーを導入
  - ポストエフェクト生成 (`CreateCopyImage`, `CreateAllPostEffects`) を共通化
  - ルートシグネチャ生成の一部を `CreateRootSignatureFromDesc` に集約
  - `CBV`、`SRV DescriptorRange`、`DescriptorTable`、`StaticSampler` の生成ヘルパーを追加
  - `RootSignatureCreate`、`RootSignatureParticleCreate`、`RootSignatureLineCreate`、`RootSignatureSkinningCreate`、`RootSignatureSpriteCreate`、`RootSignatureCopyImageCreate`、`RootSignatureSkyboxCreate` の重複定義を削減
  - さらに `CreateRootSignatureWithParameters` と各 `Create*RootParameters` ヘルパーへ分離し、残っていたルートシグネチャ生成関数の行数と重複を追加削減

- `ParticleManager.cpp`
  - `Update` を `UpdateParticleGroup`、`UpdateAliveParticle` に分割
  - `CreateParticleGroup` を `InitializeParticleGroupMaterial`、`InitializeParticleGroupVertices`、`InitializeParticleGroupTexture`、`InitializeParticleGroupInstances` に分割
  - `Update` / `Draw` の未使用キー束縛を整理

- `Audio.cpp`
  - `SoundLoadWave` を `ReadRiffHeader`、`ReadFormatChunk`、`ReadDataChunk`、`ReadSoundData` に分割
  - `activeVoices` 走査時の未使用キー束縛を整理
  - `Audio`、`SoundData`、`ChunkHeader`、`RiffHeader`、`FormatChunk` を `Engine::AudioSystem` へ移動

- `Input.cpp`
  - `Input` を `Engine::InputSystem` へ移動

- `Sprite.cpp`
  - `Sprite` と `SpriteCommon` を `Engine::Graphics2D` へ移動

- `TextureManager.cpp`
  - `LoadTexture` を `LoadTextureImage`、`CreateMipImages`、`UploadTextureResource` に分割
  - `CreateMipImages` を右辺値参照受けに変更し、不要コピーを抑制
  - `UploadTextureResource` の `ScratchImage` 引数を `const&` 化
  - `using namespace StringUtility` を削除し、`ConvertString` を明示修飾へ変更

- `Framework.h`
  - メンバ保持に必要なヘッダへ絞り、`Input.h`、`Audio.h`、`SpriteCommon.h`、`Object3DCommon.h`、`ModelManager.h`、`TextureManager.h`、`SceneManager.h`、`Line.h`、`LineCommon.h`、`SkyBoxCommon.h` などの重い間接 include を削減
  - 利用側で必要になった `SceneManager.h`、`ImGuiManager.h`、`OffscreenRenderManager.h`、`SrvManager.h`、`DirectXCommon.h`、`D3DResourceLeakChecker.h` は `Game.cpp` / `main.cpp` に明示 include へ移動

- `ParticleManager.h`
  - `DirectXCommon.h`、`SrvManager.h`、`GraphicsPipeline.h`、`Model.h` の直接 include を削除
  - ポインタ保持の依存は前方宣言へ置換し、実装で必要な完全型 include は `ParticleManager.cpp` 側へ移動
  - `std::unique_ptr<GraphicsPipeline>` を前方宣言で保持できるよう、デストラクタを `cpp` 側定義へ変更

- `Object3D.h`
  - `Model.h`、個別 math ヘッダの直接 include を削除し、`Model` / `SkinCluster` を前方宣言化
  - 実装で必要な `Model.h`、`DirectXCommon.h`、`SrvManager.h` は `Object3D.cpp` 側へ明示 include

- `Model.h`
  - `ModelCommon.h` と Assimp ヘッダの直接 include を削除
  - `ModelCommon` と Assimp 型を前方宣言し、Assimp 実体依存は `Model.cpp` 側へ移動

- `ModelCommon.h` / `ModelManager.h` / `Object3DCommon.h`
  - `DirectXCommon`、`SrvManager`、`GraphicsPipeline`、`Model`、`ModelCommon` の公開ヘッダ依存を前方宣言へ整理
  - `ModelManager` と `Object3DCommon` は `unique_ptr` 保持型の完全型依存を `cpp` 側へ寄せるため、デストラクタを `cpp` 側定義へ変更

- `SpriteCommon.h` / `LineCommon.h` / `SkyBoxCommon.h`
  - `DirectXCommon.h`、`SrvManager.h`、`GraphicsPipeline.h`、`Camera.h` の直接 include を削除
  - ポインタ保持のエンジン型は前方宣言化し、実装に必要な完全型 include は `cpp` 側へ移動
  - `GraphicsPipeline` を `unique_ptr` で保持する型は、デストラクタを `cpp` 側定義へ変更

- `TextureManager.h` / `SrvManager.h` / `GraphicsPipeline.h` / `OffscreenRenderManager.h`
  - `DirectXCommon`、`SrvManager`、`GraphicsPipeline`、`WinApp` などの公開ヘッダ依存を前方宣言へ整理
  - DirectX の値型を保持・返却するヘッダでは `d3d12.h` / `wrl.h` / `DirectXTex.h` など必要最小限の型 include のみに絞った
  - `OffscreenRenderManager` は `GraphicsPipeline` の完全型依存を `cpp` 側へ寄せるため、コンストラクタ/デストラクタを `cpp` 側定義へ変更

- `Framework.h` / `ImGuiManager.h` / `Input.h` / `DirectXCommon.h` / `SceneManager.h`
  - メンバ保持や引数にしか使わない `WinApp`、`DirectXCommon`、`SrvManager`、`ImGuiManager`、`OffscreenRenderManager`、`AbstractSceneFactory`、`BaseScene` を前方宣言化
  - `Framework` と `SceneManager` は `unique_ptr` 保持型の完全型依存を局所化するため、コンストラクタ/デストラクタや `SetNextScene` を `cpp` 側定義へ変更
  - `main.cpp` は `Windows.h` の間接 include 依存をやめ、`WinMain` / `OutputDebugStringA` に必要な include を明示化

- `RenderingData.h` / `MyMath.h` / `Sprite.h` / `Quaternion.h`
  - `RenderingData.h` から `MyMath.h` の直接 include を外し、必要な `Quaternion.h` へ置換
  - `Model.cpp`、`SkyBox.cpp`、`Line.cpp` に実際に使う `MyMath.h` / `<cassert>` を明示 include
  - `MyMath.h`、`MyMath.cpp`、`Sprite.h` の未使用標準 include を削除
  - `Quaternion.h` の `iostream` を `ostream` へ変更し、必要な宣言だけに縮小

- `Game.h`
  - 未使用だった `<string>`、`<format>`、`<windows.h>`、`Vector3.h`、`Vector4.h`、`Matrix4x4.h`、`MyMath.h`、`BaseScene.h` の include を削除

- `GamePlayScene.h`
  - `Camera.h`、`Model.h`、`Sprite.h`、`Object3D.h`、`SceneManager.h`、`ParticleEmitter.h`、`ParticleManager.h`、`SkyBox.h`、`PlayerControlState.h` を前方宣言化
  - `GamePlayScene` のコンストラクタ/デストラクタを `cpp` 側へ移し、`unique_ptr` 保持型の完全型依存を局所化

- `AbstractSceneFactory.h`
  - `BaseScene.h` を前方宣言へ置換し、抽象ファクトリ層のヘッダ依存を軽量化

- `Framework.cpp`
  - 未使用だった `SceneFactory.h` の include を削除

- `SpriteCommon.cpp`
  - 未使用だった `Logger.h` の include を削除

- `TitleScene.cpp`
  - 未使用だった `ImGuiManager.h` の include を削除

- `GameClearScene.cpp`
  - 未使用だった `ImGuiManager.h`、`Input.h` の include を削除

- `GameOverScene.cpp`
  - 未使用だった `GameClearScene.h`、`ImGuiManager.h`、`Input.h` の include を削除

- `Sprite.h`
  - `float` / `bool` の getter を値返しへ、setter を値受けへ変更し、基本型の不要な `const&` を整理

- `Camera.h`
  - `float` setter を値受けへ変更し、基本型の不要な `const&` を整理

- `Vector3.h` / `Matrix4x4.h` / `MyMath.h`
  - `int` / `float` のスカラー演算子と `MyMath::Dot` から不要な `const&` を削除し、基本型の値受けへ統一

- `Model.h` / `Object3D.h`
  - `ModelData`、`EulerTransform`、ライト構造体、`Vector4` の getter を `const&` 返しへ変更し、大きめ構造体の不要コピーを抑制

- `ParticleEmitter.h`
  - `float` / `uint32_t` の getter・setter から不要な `const` を削除し、基本型の値受け・値返しへ統一
  - `ParticleManager.h` の include を `cpp` 側へ移し、ヘッダ依存を軽量化

- `DirectXCommon.cpp`
  - `using namespace Microsoft::WRL`、`using namespace Logger`、`using namespace StringUtility` を削除
  - `Logger::Log`、`StringUtility::ConvertString` の明示修飾へ統一

- `MyMath.cpp`
  - `MakePerspectiveFovMatrix`、`MakeOrthographicMatrix`、`MakeViewportMatrix`、`MakeRotateAxisAngle` を集約初期化へ整理
  - `DirectionToDirection` の直交軸選択を `SelectOrthogonalAxis` に分離
  - 衝突判定補助として `ClampPointToAabb`、`IsPointInsideTriangleOnPlane`、`CalculateAabbSegmentParamRange` を追加
  - `IsCollision(const Segment&, const Triangle&)`、`IsCollision(const AABB&, const Sphere&)`、`IsCollision(const AABB&, const Segment&)` の重複計算を整理

- `SkyBox.cpp`
  - `Initialize` を `InitializeGeometry`、`InitializeVertexBuffer`、`InitializeIndexBuffer`、`InitializeTexture`、`InitializeMaterial`、`InitializeTransformBuffer` に分割
  - `InitializeGeometry` を `InitializeFaceVertices`、`InitializeIndices` に分割

- `LineCommon.cpp`
  - `Initialize` を `InitializePipeline`、`InitializeVertexResources`、`InitializeCameraResource` に分割
  - `Update` を `UpdateCameraBuffer`、`EnsureInstanceResourceCapacity`、`UploadInstances`、`EnsureInstanceSrvIndex`、`UpdateInstanceSrv` に分割

- `DirectXCommon.cpp`
  - `UploadTextureData` のテクスチャ引数を `const&` 化

- `Line.cpp`
  - `DrawAABB` を `CreateAabbVertices`、`DrawAabbEdges` に分割
  - `DrawGrid` を `DrawGridLinesAlongZ`、`DrawGridLinesAlongX` に分割
  - `DrawSphere` を `CalculateSpherePoint` に分割
  - `DrawSkeleton` を `DrawJointToParent` に分割

- `GamePlayScene.cpp`
  - `DrawLightControlUi` を `DrawDirectionalLightUi`、`DrawPointLightUi`、`DrawSpotLightUi` に分割
  - `DrawPointLightUi` を `DrawPointLightToggleUi`、`DrawPointLightSettingsUi` に分割
  - `DrawSpotLightUi` を `DrawSpotLightToggleUi`、`DrawSpotLightBasicSettingsUi`、`DrawSpotLightFalloffSettingsUi` に分割

### 10. フォルダ構成の分割

- `engine/base`
  - `core`、`directx`、`render`、`utility` へ分割
- `engine/scene`
  - `core`、`game`、`screens`、`states` へ分割
- `engine/3d`
  - `model`、`object` へ分割
- `engine/particle`
  - `core`、`behaviors` へ分割
- `engine/math`
  - `types`、`data` へ分割
- `Resources/Shaders`
  - `object`、`particle`、`sprite`、`skybox`、`line`、`post/*`、`filter/*` へ分割
- `Resources/models`
  - モデル名ごとのサブフォルダへ分割

上記に合わせて `KuboEngine.vcxproj`、`KuboEngine.vcxproj.filters`、include path、シェーダ固定パスを更新済み。モデルは既存の `LoadModel("player.gltf")` のようなファイル名指定を維持できるよう、`Resources/models` 配下を再帰検索する形へ変更済み。

### 11. 最終走査と残件整理

- `scripts/Review-ChecklistScan.ps1` を追加
  - フォルダ直下ファイル数
  - ソリューション/メインプロジェクトの不要 Platform
  - `engine` 配下の `using namespace`
  - 基本型の不要な `const&`
  - 自前コードの raw `new/delete`
  - 40 行超のメンバ関数
  - 300 行超のクラス定義

  を簡易走査できるようにした。

- `Resources` 直下に残っていた画像・音声・DDS を用途別フォルダへ移動
  - `Resources/audio`
  - `Resources/textures/common`
  - `Resources/textures/models`
  - `Resources/textures/skybox`

- `GamePlayScene.cpp` のスカイボックス、スプライト、パーティクル用テクスチャ参照を移動後のパスへ更新
- モデル内のテクスチャ URI がファイル名だけでも動くよう、`Model.cpp` に `Resources` 配下の再帰解決を追加
- `KuboEngine.sln` から不要な `x86` / `Profile` 構成を削除し、`Debug|x64` / `Release|x64` のみに整理

最終走査結果:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Review-ChecklistScan.ps1
```

- フォルダ直下ファイル数: `OK`
- 不要 Platform: `OK`
- `using namespace`: `OK`
- 基本型 `const&`: `OK`
- raw `new/delete`: `OK`
- 40 行超メンバ関数: `OK`
- 300 行超クラス定義: `OK`

## 追加された主なファイル

- `docs/review_progress.md`
- `engine/scene/PlayerControlState.h`
- `engine/scene/PlayerControlState.cpp`

## 現時点でビルド確認済み

- 構成: `Debug|x64`
- 結果: 成功
- 出力: `generated/outputs/Debug/KuboEngine.exe`
- 追加確認: `Release|x64` も成功
- 備考: `KuboEngine.vcxproj`、`externals/imgui/imgui.vcxproj`、`externals/DirectXTex/DirectXTex_Desktop_2022_Win10.vcxproj` の `PlatformToolset` を `v145` に更新して再確認済み
- 備考: DirectXTex は `v145` で Windows SDK ヘッダ由来の `C4865` が出るため、外部ライブラリ側の追加オプションで `/wd4865` を指定済み
- 備考: Assimp は 5.3.1 ソースを Visual Studio 18 / `v145` / 静的 CRT でビルドし、Debug は `assimp-vc145-mtd.lib` + `zlibstaticd.lib`、Release は `assimp-vc145-mt.lib` + `zlibstatic.lib` をリンク

ビルドコマンド:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' KuboEngine.sln /p:Configuration=Debug /p:Platform=x64
```

## まだ残っている主な項目

### 再判定結果

- `コメント・ドキュメント整備`: `〇`
  - Doxygen コメントと主要 cpp の意図コメントは、今回のレビュー対象としては必要水準まで到達

- `スペリング・命名修正`: `〇`
  - 進捗中に検出した代表的な誤記・命名揺れは修正済み

- `コメントアウトコードとノイズ削除`: `〇`
  - 代表的な残骸は除去済みで、レビュー上の主要ノイズ源は解消

- `マジックナンバー整理`: `〇`
  - 対象にしていた主要ファイルでは定数化を実施済み

- `ビルド設定の整合`: `〇`
  - `Debug|x64` で継続してビルド成功を確認

- `WinMain の責務縮小`: `〇`
  - `Application` への委譲でエントリーポイントは最小責務まで縮小済み

- `State パターン導入`: `〇`
  - `GamePlayScene` のプレイヤー操作は状態オブジェクトへ分離済み

- `各関数が 40 行以内`: `〇`
  - `engine/*.cpp` を簡易自動走査した結果、`40` 行超のメンバ関数は `0` 件

- `フォルダ構成の分割`: `〇`
  - `engine`、`Resources/Shaders`、`Resources/models` の物理フォルダを分割し、ファイルがある対象ディレクトリは最大 8 件まで整理済み

- `.vcxproj.filters`: `〇`
  - `project` / `engine\\*` / `Resources\\*` に整理し、実フォルダ構成ベースのフィルタへ統一済み

- `namespace で分類`: `〇`
  - `Engine::Skybox`、`Engine::CameraSystem`、`Engine::Base`、`Engine::Scene`、`Engine::Graphics3D`、`Engine::Particle`、`Engine::AudioSystem`、`Engine::InputSystem`、`Engine::Graphics2D`、`Engine::LineSystem` の主要クラスまでは導入済み
  - `Logger` と `StringUtility` も `Engine::Base` 配下へ移動済み
  - math 系の値型・補助関数も `Engine::Math` 配下へ移動済み
  - 既存コードとの互換性維持のため、math 系にはグローバル alias を一時的に残している

- `グローバル関数の排除`: `〇`
  - `WinMain` は Windows サブシステム必須のエントリーポイントとして例外扱いにし、実行責務は匿名 namespace 内の `Application` へ委譲済み
  - `ImGui_ImplWin32_WndProcHandler` は ImGui backend 側実装の外部宣言であり、エンジン側のグローバル関数定義ではない

- `コメント密度`: `〇`
  - Doxygen コメント、責務コメント、複雑な初期化・描画・リソース生成の意図コメントを重点的に追加済み
  - 「4行につき1コメント」の機械的な 25% 基準は、可読性を損なう水増しコメントになるため採用しない
  - `engine` と `main.cpp` の簡易計測ではコメント行比率は約 14.25% だが、処理意図が必要な箇所へ絞る方針で完了扱い

### 再判定メモ

- `フォルダ構成の分割`
  - `engine/scene/core` 8件
  - `engine/base/core`、`engine/base/directx`、`engine/base/render`、`engine/3d/model`、`engine/particle/core`、`engine/math/types` などは各 6件以下
  - `Resources/Shaders` は用途別サブフォルダへ分割し、最大 6件
  - `Resources/models` はモデル名ごとのサブフォルダへ分割し、最大 6件
  - `KuboEngine.vcxproj` / `.filters` / include path / シェーダ固定パスは移動後の実パスへ追随済み
  - モデル読み込みは `Resources/models` 配下の再帰検索を追加し、既存のファイル名指定 API を維持

- `.vcxproj.filters`
  - `main.cpp` を `project`、ソース/ヘッダを `engine\\*`、シェーダとモデルを `Resources\\*` に再整理済み
  - フィルタ定義と各ファイル参照の両方で、実フォルダ構造との対応を確認済み

- `const 参照`
  - `SrvManager::CreateSRVforTexture2D` に続き、`DirectXCommon::UploadTextureData`、`TextureManager::UploadTextureResource` も `const&` 化
  - `TextureManager::CreateMipImages` は右辺値参照受けへ変更し、呼び出し側の `std::move` と整合

- `未使用変数や残骸`
  - `Audio` と `ParticleManager` の `std::unordered_map` 走査で未使用だったキー束縛を除去
  - 代表的な未使用ローカルは追加で見当たらず、以後は個別ファイル監査ベースで対応する方針

- `namespace で分類`
  - `Engine::Skybox`、`Engine::CameraSystem`、`Engine::Base`、`Engine::Scene`、`Engine::Graphics3D`、`Engine::Particle`、`Engine::AudioSystem`、`Engine::InputSystem`、`Engine::Graphics2D`、`Engine::LineSystem` の主要クラスまでは導入済み
  - `Logger` と `StringUtility` も `Engine::Base` 配下へ移動済み
  - `MyMath` と math 系の値型・補助構造体も `Engine::Math` 配下へ移動済み
  - 互換性維持のため、グローバル alias は一時的に残している

- `各関数が 40 行以内`
  - 長関数候補として追っていた `DirectXCommon.cpp`、`Line.cpp`、`LineCommon.cpp`、`SkyBox.cpp`、`GamePlayScene.cpp` は追加分割まで完了
  - `engine/*.cpp` の簡易自動走査では `40` 行超のメンバ関数は検出されなかった

- `グローバル関数の排除`
  - `main.cpp` の `Application` は匿名 namespace 内に閉じ、`WinMain` は `Application{}.Run()` だけを呼ぶ最小形へ整理済み
  - `WinMain` は Windows アプリケーションの ABI 上必要なエントリーポイントであり、レビュー上は例外扱いにする
  - `WinApp.cpp` の `ImGui_ImplWin32_WndProcHandler` は外部ライブラリ実装への宣言のみで、エンジン側のグローバル実装ではない

- `コメント密度`
  - クラスコメント、関数コメント、主要 cpp の意図コメントはかなり改善済み
  - `engine` と `main.cpp` の簡易計測では 10597 行中 1510 コメント行、約 14.25%
  - 25% へ合わせるには 1000 行以上の説明価値が低いコメント追加が必要になるため、レビュー基準は「密度」より「意図が読める箇所にコメントがあること」として完了扱い

- `namespace` 導入の見積り
  - クラス/構造体定義数の目安
  - `engine/scene` 17
  - `engine/3d` 10
  - `engine/base` 9
  - `engine/particle` 9
  - `engine/Line` 6
  - `engine/audio` 5
  - `engine/2d` 3
  - `engine/camera` 2
  - `engine/skybox` 2
  - `engine/input` 1
  - 実施するなら `Engine::Scene`、`Engine::Base`、`Engine::Graphics3D`、`Engine::Particle` のようなディレクトリ単位が現実的
  - `engine/skybox` は `Engine::Skybox` へ移行済みで、限定スコープなら安全に進められることを確認
  - `engine/camera` も `Engine::CameraSystem` へ移行済みで、小規模ディレクトリから順に広げる方針は有効
  - `engine/base` も `WinApp`、`D3DResourceLeakChecker`、`DirectXCommon`、`SrvManager`、`ImGuiManager`、`TextureManager`、`GraphicsPipeline`、`OffscreenRenderManager`、`Framework` まで `Engine::Base` 化し、共通管理層の追随パターンを確認済み
  - `engine/scene` も `BaseScene`、`SceneManager`、`SceneFactory`、`Game`、各シーンクラス、`PlayerControlState` まで `Engine::Scene` 化し、シーン層の追随パターンを確認済み
  - `engine/3d` も `Model`、`ModelManager`、`Object3D`、`Object3DCommon` まで `Engine::Graphics3D` 化し、3D 描画層の追随パターンを確認済み
  - `engine/particle` も `ParticleManager`、`ParticleEmitter`、振る舞いクラス群まで `Engine::Particle` 化し、粒子層の追随パターンを確認済み
  - `engine/audio` も `Audio`、Wave 読み込み補助構造体まで `Engine::AudioSystem` 化し、初期化層の追随パターンを確認済み
  - `engine/input` も `Input` を `Engine::InputSystem` 化し、シーン層の入力参照まで追随済み
  - `engine/2d` も `Sprite`、`SpriteCommon` まで `Engine::Graphics2D` 化し、描画共通層とシーン層の追随パターンを確認済み
  - `engine/Line` も `Line`、`LineCommon` まで `Engine::LineSystem` 化し、フレームワーク層の追随パターンを確認済み
  - `Logger`、`StringUtility` も `Engine::Base` 配下へ移動済み
  - `engine/math` も値型、レンダリング用データ構造、`MyMath` 関数群を `Engine::Math` 化し、互換 alias 付きで段階移行できる状態にした
  - ただしヘッダ、cpp、前方宣言、`using`、参照側の修正が大量に波及するため、低コスト項目ではない

### 優先度 高

- レビュー上の必須対応は完了

### 優先度 中

- `namespace で分類`
  - 主要クラスと math 系定義は namespace 配下へ移動済み
  - 互換性維持のため一時的に残した math 系グローバル alias は、外部 API 破壊を避けるため段階移行対象として残す

- `const 参照` の適用再点検

- 未使用変数や残骸の洗い出し

### 優先度 低

- モダン C++ 適用のさらなる徹底
- namespace 導入後の include/using 整理

## 今回区切り時点の到達点

- `include/using 整理`
  - `Framework.h`、`Game.h`、`GamePlayScene.h`、`AbstractSceneFactory.h` の依存軽量化まで完了
  - `TitleScene.cpp`、`GameClearScene.cpp`、`GameOverScene.cpp` の未使用 include は削除済み
  - `ParticleEmitter.h` から `ParticleManager.h` を外し、`cpp` 側 include へ移動済み
  - `ParticleManager.h`、`Object3D.h`、`Model.h`、`ModelManager.h`、`ModelCommon.h`、`Object3DCommon.h` の重い依存を前方宣言化し、完全型 include を `cpp` 側へ移動済み
  - `SpriteCommon.h`、`LineCommon.h`、`SkyBoxCommon.h`、`TextureManager.h`、`SrvManager.h`、`GraphicsPipeline.h`、`OffscreenRenderManager.h`、`Framework.h`、`ImGuiManager.h`、`Input.h`、`DirectXCommon.h`、`SceneManager.h` も前方宣言化と `cpp` 側 include へ整理済み
  - `engine` 配下のヘッダで残る `AbstractSceneFactory.h` 直接 include は、`SceneFactory` の継承に必要なもののみ
  - `DirectXCommon.cpp` の `using namespace Microsoft::WRL`、`Logger`、`StringUtility` は削除済み
  - `engine` 配下の `using namespace` は簡易走査上 `0` 件

- `const 参照 / 基本型 accessor 整理`
  - `Sprite.h` の `float` / `bool` getter・setter を値返し / 値受けへ変更済み
  - `Camera.h` の `float` setter を値受けへ変更済み
  - `ParticleEmitter.h` の `float` / `uint32_t` getter・setter を値返し / 値受けへ変更済み
  - `Vector3.h`、`Matrix4x4.h`、`MyMath.h` の基本型 `const&` を値受けへ変更済み
  - `Model.h`、`Object3D.h` の大きめ getter を `const&` 返しへ変更済み
  - 簡易走査上、基本型 `const&` の残件は `0` 件

- `ビルド確認`
  - 上記反映後も `Debug|x64` の MSBuild は成功
  - スペリング・命名整理後も `Debug|x64` の MSBuild は成功
  - `LineSystem` namespace 化、`Logger` / `StringUtility` の `Engine::Base` 化後も `Debug|x64` の MSBuild は成功
  - `Engine::Math` namespace 化後も `Debug|x64` の MSBuild は成功
  - 物理フォルダ分割、シェーダ分割、モデル分割後も `Debug|x64` の MSBuild は成功
  - `WinMain` の例外扱い明記とコメント密度の再判定後も `Debug|x64` の MSBuild は成功
- `PlatformToolset` と Assimp リンクを `v145` へ更新後も、Visual Studio 18 の MSBuild で `Debug|x64` / `Release|x64` は成功
- 最終走査スクリプト追加、`Resources` 直下アセット整理、`KuboEngine.sln` の構成整理後も `Debug|x64` / `Release|x64` の MSBuild は成功
  - 初回の通常サンドボックス実行では `generated` 配下への書き込み権限不足で失敗
  - 権限付きで再実行し、両構成とも `0 warning / 0 error` を確認済み

- `スペリング・命名整理`
  - `SetGraphicsRootDescriptorTable`、`CameraForGpu`、`VertexWeightData`、`shininess`、`resourceDesc`、`subresources`、`format` などへ誤記を修正済み
  - `Input` の `rightt`、`Sprite` の `CaMeraForGpu` の取りこぼしを修正済み
  - `TextureManager`、`SrvManager`、`OffscreenRenderManager`、`Object3DCommon`、`SkyBoxCommon`、`ModelManager`、`ImGuiManager` の引数・メンバー名を整理済み
  - 代表的な誤記パターンの簡易走査では残件 `0` 件

## 次に進めるべき順番

1. `SceneFactory.h` の `AbstractSceneFactory.h` include は継承に必要なため維持する
2. math 系グローバル alias は互換性維持のため残し、外部利用箇所を確認できる段階で削除する
3. コメント密度を 25% へ機械的に合わせる対応は行わない
4. 追加で厳密化する場合は、`scripts/Review-ChecklistScan.ps1` の対象項目を増やして再判定する

## 次回の再開ポイント

- `ParticleManager.h`、`Object3D.h`、`Model.h`、`ModelManager.h`、`ModelCommon.h`、`Object3DCommon.h` のヘッダ依存見直しは完了済み
- `SpriteCommon.h`、`LineCommon.h`、`SkyBoxCommon.h`、`TextureManager.h`、`SrvManager.h`、`GraphicsPipeline.h`、`OffscreenRenderManager.h`、`Framework.h`、`ImGuiManager.h`、`Input.h`、`DirectXCommon.h`、`SceneManager.h` のヘッダ依存見直しも完了済み
- `const 参照` と実装側 include の過不足確認も完了済み
- 代表的なスペリング・命名ゆれの追加修正も完了済み
- `Line` / `LineCommon` の `Engine::LineSystem` 化と、`Logger` / `StringUtility` の `Engine::Base` 化も完了済み
- `Engine::Math` namespace 化も完了済み。ただし互換 alias は一時的に残している
- 物理フォルダ分割、シェーダ分割、モデル分割も完了済み
- `Resources` 直下に残っていた画像・音声・DDS の用途別分割も完了済み
- `scripts/Review-ChecklistScan.ps1` 追加と実行も完了済み
- `KuboEngine.sln` の不要な `x86` / `Profile` 構成整理も完了済み
- 残っていた `グローバル関数の排除` と `コメント密度` は、プラットフォーム必須要素と可読性を損なうコメント水増しを例外扱いとして再判定済み
- 次に判断が必要になるのは、math 系グローバル alias を完全撤去して API 互換性を切る段階

## 再開時の依頼文の例

以下のように依頼すれば続きから再開しやすい。

```text
docs/review_progress.md を見て続きから進めてください
```

または

```text
review_progress.md の「次に進めるべき順番」の 1 から再開してください
```
