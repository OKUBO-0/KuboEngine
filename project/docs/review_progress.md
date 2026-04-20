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

で `PlatformToolset` を現環境に合わせて `v143` に統一し、ソリューション全体が再びビルド可能な状態へ戻した。

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

## 追加された主なファイル

- `docs/review_progress.md`
- `engine/scene/PlayerControlState.h`
- `engine/scene/PlayerControlState.cpp`

## 現時点でビルド確認済み

- 構成: `Debug|x64`
- 結果: 成功
- 出力: `generated/outputs/Debug/KuboEngine.exe`
- 備考: `KuboEngine.vcxproj`、`externals/imgui/imgui.vcxproj`、`externals/DirectXTex/DirectXTex_Desktop_2022_Win10.vcxproj` の `PlatformToolset` が `v145` に戻っていたため、`v143` に再修正して再確認済み

ビルドコマンド:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' KuboEngine.sln /p:Configuration=Debug /p:Platform=x64
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

- `フォルダ構成の分割`: `×`
  - 8件超のフォルダが複数残っており、 strict な基準では未達

- `.vcxproj.filters`: `〇`
  - `project` / `engine\\*` / `Resources\\*` に整理し、実フォルダ構成ベースのフィルタへ統一済み

- `namespace で分類`: `×`
  - `Logger`、`StringUtility`、`MyMath` に加えて `Engine::Skybox`、`Engine::CameraSystem`、`Engine::Base`、`Engine::Scene`、`Engine::Graphics3D`、`Engine::Particle`、`Engine::AudioSystem`、`Engine::InputSystem`、`Engine::Graphics2D` の主要クラスまでは導入済み
  - ただし主要クラス群の大半はグローバル名前空間のまま

- `グローバル関数の排除`: `×`
  - `WinMain` は最小委譲化したが、 strict に見るとグローバル関数自体は残る

- `コメント密度`: `×`
  - 主要箇所は改善したが、「4行につき1コメント」基準は未達の可能性が高い

### 再判定メモ

- `フォルダ構成の分割`
  - `engine/base` 22件
  - `engine/scene` 20件
  - `engine/particle` 10件
  - `engine/3d` 10件
  - `engine/math` 9件
  - `Resources/Models` 54件
  - `Resources/Shaders` 36件
  - 8件超のフォルダが依然として複数残っているため未達
  - 物理移動まで着手すると `vcxproj` / include パス / 相対参照修正が大きく波及するため、現時点では見送り判断

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
  - `Logger`、`StringUtility`、`MyMath`、`Engine::Skybox`、`Engine::CameraSystem`、`Engine::Base`、`Engine::Scene`、`Engine::Graphics3D`、`Engine::Particle`、`Engine::AudioSystem`、`Engine::InputSystem`、`Engine::Graphics2D` の主要クラスまでは導入済み
  - エンジン層クラスの大半は引き続きグローバル名前空間のまま

- `各関数が 40 行以内`
  - 長関数候補として追っていた `DirectXCommon.cpp`、`Line.cpp`、`LineCommon.cpp`、`SkyBox.cpp`、`GamePlayScene.cpp` は追加分割まで完了
  - `engine/*.cpp` の簡易自動走査では `40` 行超のメンバ関数は検出されなかった

- `グローバル関数の排除`
  - `main.cpp` の `WinMain` は最小委譲まで縮小済み
  - ただし strict に見るとグローバル関数自体は残る

- `コメント密度`
  - クラスコメント、関数コメント、主要 cpp の意図コメントはかなり改善済み
  - ただし「4行につき1コメント」基準は依然として未達の可能性が高い

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
  - ただしヘッダ、cpp、前方宣言、`using`、参照側の修正が大量に波及するため、低コスト項目ではない

### 優先度 高

- `namespace で分類`
  - エンジン層のクラスは大半がグローバル名前空間のまま

### 優先度 中

- `フォルダ構成の再編`
  - `engine/base`
  - `engine/scene`
  - `Resources/Shaders`
  - `Resources/models`

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
  - `DirectXCommon.cpp` の `using namespace Microsoft::WRL`、`Logger`、`StringUtility` は削除済み
  - `engine` 配下の `using namespace` は簡易走査上 `0` 件

- `const 参照 / 基本型 accessor 整理`
  - `Sprite.h` の `float` / `bool` getter・setter を値返し / 値受けへ変更済み
  - `Camera.h` の `float` setter を値受けへ変更済み
  - `ParticleEmitter.h` の `float` / `uint32_t` getter・setter を値返し / 値受けへ変更済み

- `ビルド確認`
  - 上記反映後も `Debug|x64` の MSBuild は成功

## 次に進めるべき順番

1. `ParticleManager.h`、`Object3D.h`、`Model.h` など重いヘッダの前方宣言化を再点検する
2. `namespace` 導入後の include/using 整理の残りを詰める
3. `const 参照` の追加適用余地がないか最終確認する
4. 残っている `×` 項目のうち対応方針を決める
5. 物理フォルダ再編を本当に行うか再判断する

## 次回の再開ポイント

- 最優先の再開地点は `ParticleManager.h`、`Object3D.h`、`Model.h` のヘッダ依存見直し
- 特に `ParticleManager.h` は `Model.h`、`GraphicsPipeline.h`、`DirectXCommon.h`、`SrvManager.h` を直接抱えているため、前方宣言化で効果が出る可能性が高い
- ここは波及が出やすいので、1ファイルずつ `修正 -> ビルド` の刻みで進める
- 判断が必要になるのは、前方宣言では吸収できず API 形状変更が必要になった時点

## 再開時の依頼文の例

以下のように依頼すれば続きから再開しやすい。

```text
docs/review_progress.md を見て続きから進めてください
```

または

```text
review_progress.md の「次に進めるべき順番」の 1 から再開してください
```
