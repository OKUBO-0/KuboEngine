# DirectXGame Migration Plan

## 目的

`C:\Users\k023g\source\repos\就職作品\DirectXGame\DirectXGame` にある KamataEngine 製ゲームを、`KuboEngine` の自作 DirectX 12 エンジン上へ移行する。

基本方針:

- ゲームの挙動・仕様・見た目の意図は維持する
- KamataEngine API 依存を段階的に自作エンジン API へ置き換える
- 自作エンジン側に不足している機能は、まず小さく追加する
- 既存の CSV 調整データ、画像、音声、モデル資産は可能な限り維持する
- 自作エンジン側で自然に強化できる表現・デバッグ・調整機能は積極的に追加する
- 各フェーズごとにビルド可能な状態を保つ

## アレンジ方針

移行の第一目標は「KamataEngine 版と同じゲームとして成立すること」だが、自作エンジン側で実装済みまたは実装しやすい機能は積極的に使う。

ただし、ゲームバランスや基本ルールを変えるアレンジは、移行完了後または明示的に分離したフェーズで行う。

### 積極的にアレンジしてよい領域

- 描画表現
  - Offscreen render を使ったポストエフェクト
  - vignette
  - grayscale
  - radial blur
  - outline
  - hit flash
  - damage overlay
  - level up 時の画面演出

- 3D 表現
  - Skybox / environment map
  - 床や敵のライティング強化
  - 環境反射
  - 敵タイプ別の色、スケール、発光風表現
  - プレイヤー照準インジケータの見た目強化

- パーティクル/ライン
  - 死亡パーティクル
  - ヒットパーティクル
  - EXP orb 吸収線
  - 雷撃エフェクト
  - レベルアップ時の波紋
  - デバッグ用範囲表示

- デバッグ/調整機能
  - ImGui によるゲームバランス調整
  - CSV 保存
  - 敵スポーン設定のリアルタイム確認
  - 武器レベルの強制変更
  - 当たり判定可視化
  - FPS / enemy count / active bullet count 表示

- 入力体験
  - キーボード UI 操作の完全対応
  - マウス/ゲームパッド/キーボードの入力元切り替え表示
  - ボタン表記 UI の自動切り替え

- エンジン機能としての追加
  - `GameAudioCache`
  - `GameModelCache`
  - `GameSpriteFactory`
  - `GameLightSettings`
  - `CsvReader`
  - `DebugDraw`

### アレンジ時に注意する領域

- 敵 HP、速度、スポーン数
- 武器強化倍率
- EXP 量
- レベルアップ候補の重み
- プレイヤー移動速度
- 被ダメージ仕様
- クリア/死亡/時間切れ条件

これらはプレイ感に直結するため、まず移行元 CSV の値を維持する。

## アレンジ候補リスト

### 優先度 高

- キーボード UI 操作の完全対応
  - 移行元では `IsKeyboard*` 系があるが、実際の UI 遷移では十分使われていない
  - KuboEngine 版では `W/S`、`ArrowUp/Down`、`Enter/Space`、`Esc` を標準対応する

- DebugDraw 追加
  - 敵スポーン半径
  - 敵再配置距離
  - 弾当たり判定
  - プレイヤー接触判定
  - 雷撃範囲
  - ミニマップ範囲

- AudioCache 追加
  - 音声をハンドルで扱えるようにし、移行元の `LoadWave/PlayWave` に近い使い心地にする

- SpriteFactory 追加
  - `Create(texturePath, position)` 形式で UI 移植を簡単にする

### 優先度 中

- ポストエフェクト演出
  - pause 中は軽い vignette
  - death 中は grayscale + vignette
  - level up 中は radial blur または明度変化
  - hit 時に短い flash

- Skybox / 環境光の強化
  - Title / Game で背景の印象を高める
  - プレイヤーや敵の見栄えを上げる

- 雷撃演出の強化
  - LineSystem で落雷ライン
  - Particle で着弾火花
  - 範囲円の短時間表示

- EXP orb 吸収演出
  - orb からプレイヤーへ線/粒子を出す
  - 取得時に小さな光る演出を足す

- ImGui 調整パネル
  - `playerStatus.csv`
  - `enemySpawnSettings.csv`
  - `weaponUpgradeSettings.csv`
  - `levelupWeights.csv`
  - 各 UI layout

### 優先度 低

- UI トランジション追加
  - Title menu の hover animation
  - LevelUp 選択肢の pulse
  - Result count-up の easing

- エンジン側汎用コンポーネント化
  - `Transform2D`
  - `Transform3D`
  - `SpriteRenderer`
  - `ModelRenderer`
  - `AudioSource`

- アセット形式拡張
  - ogg/mp3 対応
  - JSON 化
  - sprite atlas 定義

## 現在の実装段階

状態: `Stage 16 完了 / Stage 1-15 完了`

作業ブランチ:

- `feature/directxgame-migration-stage6`

最終確認:

- `v145 Debug|x64` ビルド成功
- `v145 Release|x64` ビルド成功
- `generated/outputs/Debug/KuboEngine.exe` の 5 秒起動確認で `RUNNING_OK`
- DirectXGame の Title -> Game -> Result 系フロー確認で `DEBUG_FLOW_RUNNING_OK` / `DEBUG_DEATH_RUNNING_OK`
- LevelUp 確認で `DEBUG_LEVELUP_RUNNING_OK`
- 30 秒 Gameplay 確認で `DEBUG_30S_GAMEPLAY_RUNNING_OK`
- Release Gameplay 確認で `RELEASE_GAME_RUNNING_OK`
- Debug 音量調整追加後の Title -> Game 確認で `DEBUG_AUDIO_MASTER_GAME_RUNNING_OK`
- Release 音量調整追加後の起動確認で `RELEASE_AUDIO_MASTER_RUNNING_OK`
- Debug 入力デバイス表示追加後の Title -> Game 確認で `DEBUG_INPUT_DEVICE_GAME_RUNNING_OK`
- Release 入力デバイス表示追加後の起動確認で `RELEASE_INPUT_DEVICE_RUNNING_OK`
- Debug Result レイアウト調整追加後の Title -> Game -> Result 確認で `DEBUG_RESULT_LAYOUT_RUNNING_OK`
- Release Result レイアウト調整追加後の起動確認で `RELEASE_RESULT_LAYOUT_RUNNING_OK`
- `TextureManager` の mipmap 生成失敗時は単一 mip のまま続行する fallback を追加済み
- TitleScene の SE は、実体が `ID3` 形式だった `se_select.wav` / `se_decide.wav` を避け、移行元と同じ `se_pause.wav` / `se_exp.wav` を使用

実施済み:

- KamataEngine 版 DirectXGame の主要構造を確認
- KuboEngine 側の主要 API を確認
- 移行に必要な機能差分を整理
- 本移行台帳を作成
- `game/directxgame/` の追加
- DirectXGame 用の空 `Title` / `Game` / `Result` シーン追加
- `SceneFactory` への DirectXGame シーン登録
- 起動シーンを DirectXGame title へ切り替え
- `Resources/DirectXGame` へ移行元リソースを配置
- DirectXGame 用リソースパス定数と起動時プローブを追加
- `GameTextureCache` / `GameAudioCache` / `GameModelCache` / `GameInputBindings` / `ScreenUtil` の最小版を追加
- `GameLightSettings` / `GameSpriteFactory` を追加
- `ResultData` / `GameSessionContext` 互換名を `DirectXGameSessionContext` に追加
- `CsvReader` / `UILayoutIO` を追加
- `ui_layout_hud.csv` を追加し、欠落時デフォルト復帰と Debug 限定保存を追加
- `UIElement` / `UIPanel` / `UILabel` / `UIBar` / `DigitSpriteUtil` を追加
- `Timer` / `HpGauge` / `ExpGauge` を追加
- Title シーンに UI プレビューを追加し、画像 / 数字スプライト / HP/EXP ゲージ表示を確認可能にした
- `CurtainTransition` を KuboEngine 側へ追加
- TitleScene を Play / Guide / Quit、ガイド fade、BGM/SE、octopus/skydome 表示つきで移植
- GameScene に Player / GridPlane / SkyDome を追加し、移動、照準、追従カメラ、床/空表示を実装
- PlayerManager / NormalBullet / OrbitBullet / Drone を追加し、player status / weapon upgrade CSV、通常弾発射、周囲弾、ドローン追従、武器レベル内部状態を実装
- Enemy / EnemyManager / EnemyBehavior / ExpOrb を追加し、enemy types / spawn settings CSV、敵無限湧き、4 種類の敵行動、空間分割 separation、遠距離再配置、死亡時 EXP orb ドロップを実装
- GameScene に EnemyManager を接続し、敵数 / EXP orb / kill count の Debug 表示と全敵ダメージ確認ボタンを追加
- EnemyManager に通常弾 / 周囲弾 / ドローン弾 / ライトニング / プレイヤー接触の衝突判定を追加
- Drone は EnemyManager の最寄り敵を狙って射撃するように接続
- EXP orb 回収を PlayerManager の EXP / LevelUp request に接続
- GameScene の Result summary kill count を EnemyManager の kill count に接続
- GameScene に Start / Playing / Pause / LevelUp / Dead の状態管理を追加
- Start overlay / Pause overlay / HUD Timer / HP gauge / EXP gauge を GameScene に接続
- KeyUI を GameScene に接続し、プレイ中の WASD / Esc 押下状態を移植元と同じキー画像で表示するようにした
- MiniMap を Pause 表示に接続し、敵 / EXP orb / player icon をプレイヤー基準の円形マップに表示するようにした
- Pause 中の Build icon 表示を追加し、通常弾 / Orbit / Drone / Lightning / Attack の状態を移植元と同じアイコンで表示するようにした
- LevelUp UI に右から入って左へ抜ける slide animation と、開始時の Confetti 演出を追加した
- LevelUp request 発生時に 3 択候補を表示し、選択した強化を PlayerManager に反映
- LevelUp / Pause の W/S / Up/Down / Enter/Space / Esc / GamePad 操作を接続
- ResultScene に ResultData 表示、EXP / Level / Kill / TotalScore count-up、入力スキップ、Title 復帰を実装
- GameScene から ResultScene へ total EXP / final level / kill count / elapsed frames を渡すように接続
- `Audio::SoundPlayWave` / `GameAudioCache` にループ再生 API を追加し、Title BGM をループ再生へ変更
- GameScene に start / pause / levelup / gameover SE を接続し、Finalize で停止するように整理
- EnemyManager に player damage SE、ResultScene に count-up 完了 SE を接続
- 移植元を再確認し、Title BGM / 主要 SE の音源と初期音量を合わせた上で、Debug ImGui から個別音量を調整できるようにした
- GameScene の追従カメラは移植元の固定追従値を初期値にしつつ、Debug ImGui から Height / Distance / Pitch を調整できるようにした
- カメラは初期状態では移植元準拠の World Back 固定追従とし、Debug ImGui から World Back / Player Back / World Front / Top Down を切り替えられるようにした
- `CameraManager::AddCamera` が登録時コピーを持つため、Player 側カメラ更新だけでは描画用カメラへ反映されない問題を修正し、毎フレーム登録済み `directxgame_player` カメラへ同期するようにした
- GameScene にプレイヤーダメージ時の画面 flash、死亡 overlay、Result 遷移前の短い待ち時間を追加
- GameScene に LineSystem の DebugDraw を接続し、プレイヤー/敵の当たり判定目安とスポーン範囲を可視化
- Game / Result Scene に CurtainTransition を接続し、Scene 遷移時はカーテン close 完了後に切り替える
- PlayerManager に雷撃直後の target 情報を保持し、GameScene で LineSystem による雷撃/EXP 吸収ラインを表示
- DirectXGame 用 Particle behavior を追加し、ダメージ / 雷撃 / EXP 回収時に spark / ripple を発火
- GridPlane / SkyDome に色味とゆるいアニメーションを追加し、床と空の見た目を調整
- GameScene の状態に応じて Offscreen post effect を自動切り替えするように接続
- `KuboEngine.vcxproj` / `.filters` に DirectXGame リソースの主要カテゴリを登録
- `Debug|x64` / `Release|x64` のビルドと 5 秒起動確認を実施

未実施:

- BGM / SE の音量バランス、演出の視認性、UI 重なりの最終的な目視・聴覚確認

## 移行元の概要

移行元パス:

```text
C:\Users\k023g\source\repos\就職作品\DirectXGame\DirectXGame
```

主な構成:

- `core/main.cpp`
  - KamataEngine 初期化
  - `SceneManager` に `Title` / `Game` / `Result` を登録
  - メインループ、FPS 表示

- `scene/core`
  - `IScene`
  - `SceneManager`
  - `GameSessionContext`

- `scene/title`
  - タイトル画面
  - BGM、ガイド表示、メニュー選択、カーテン遷移

- `scene/game`
  - `GameScene`
  - `GameStartController`
  - `GamePauseController`
  - `GameLevelUpController`

- `scene/result`
  - リザルト画面
  - EXP / Level / Kill count 表示

- `player`
  - `Player`
  - `PlayerManager`
  - `NormalBullet`
  - `OrbitBullet`
  - `Drone`

- `enemy`
  - `Enemy`
  - `EnemyManager`
  - `IEnemyBehavior`
  - 敵タイプ別 Strategy

- `effects`
  - カーテン遷移
  - ヒット/死亡/波紋/雷撃エフェクト

- `ui`
  - HUD
  - ゲージ
  - スコア
  - レイアウト CSV I/O

- `Resources/data`
  - プレイヤー初期値
  - 敵定義
  - 敵スポーン設定
  - 武器強化設定
  - レベルアップ重み
  - UI レイアウト

## 移行先の概要

移行先パス:

```text
C:\Users\k023g\source\repos\KuboEngine\KuboEngine\project
```

既存の主な機能:

- シーン管理
  - `Engine::Scene::BaseScene`
  - `Engine::Scene::SceneManager`
  - `Engine::Scene::SceneFactory`

- 3D 描画
  - `Engine::Graphics3D::ModelManager`
  - `Engine::Graphics3D::Object3D`
  - glTF / obj 読み込み
  - スキニング対応

- 2D 描画
  - `Engine::Graphics2D::Sprite`
  - `Engine::Graphics2D::SpriteCommon`

- 入力
  - `Engine::InputSystem::Input`
  - キーボード
  - マウス
  - XInput ゲームパッド

- 音声
  - `Engine::AudioSystem::Audio`
  - Wave 読み込み/再生/停止

- リソース
  - `Engine::Base::TextureManager`
  - `Engine::Graphics3D::ModelManager`

- その他
  - Skybox
  - Particle
  - Line
  - ImGui
  - Offscreen render

## API 対応表

| KamataEngine 側 | KuboEngine 側 | 移行方針 |
| --- | --- | --- |
| `KamataEngine::Initialize/Update/Finalize` | `Engine::Base::Framework` / `Engine::Scene::Game` | KuboEngine のアプリ基盤を利用 |
| `KamataEngine::SceneManager` 相当 | `Engine::Scene::SceneManager` | 文字列シーン名ベースへ合わせる |
| `KamataEngine::Model::CreateFromOBJ` | `Engine::Graphics3D::ModelManager::LoadModel` + `Object3D` | `ModelCache` 相当を追加 |
| `KamataEngine::WorldTransform` | `EulerTransform` / `Object3D` の transform | `WorldTransformEx` 相当の薄い型を追加するか、直接 `EulerTransform` を使う |
| `KamataEngine::Camera` | `Engine::CameraSystem::Camera` | カメラ参照/アクティブカメラ更新を整理 |
| `KamataEngine::Sprite` | `Engine::Graphics2D::Sprite` | `SpriteHandle`/薄いラッパー追加を検討 |
| `KamataEngine::TextureManager::Load` | `Engine::Base::TextureManager::LoadTexture` | パス指定と戻り値の違いを吸収するラッパーを追加 |
| `KamataEngine::Audio::LoadWave/PlayWave/StopWave/IsPlaying` | `Engine::AudioSystem::Audio` | ハンドル型 AudioCache を追加 |
| `KamataEngine::Input` | `Engine::InputSystem::Input` | `InputBindings` を KuboEngine API へ移植 |
| `KamataEngine::LightGroup` | `Object3D` 内蔵ライト | 共有ライト管理が不足。追加するか、各 `Object3D` へ同期 |
| `ObjectColor` | `Object3D::SetColor` | 色変更は移植可能。白テクスチャ差し替え表現は要確認 |

## 不足している可能性が高い機能

### 必須

- DirectXGame 用の `Scene` 群
  - `TitleScene`
  - `GameScene`
  - `ResultScene`

- DirectXGame 用の共有コンテキスト
  - `GameSessionContext`
  - `ResultData`

- KamataEngine 互換の薄い補助レイヤ
  - `GameModelCache`
  - `GameTextureCache`
  - `GameAudioCache`
  - `GameInputBindings`
  - `ScreenUtil`

- CSV 読み込みユーティリティ
  - 現状は `std::ifstream` + `stringstream`
  - まずは既存実装を移植
  - 後で堅牢化するなら小さな CSV parser か自作 `CsvReader`

- UI 用部品
  - `UIElement`
  - `UIPanel`
  - `UILabel`
  - `UIBar`
  - `DigitSpriteUtil`
  - `Score`
  - `Timer`
  - `MiniMap`
  - `KeyUI`
  - `HpGauge`
  - `ExpGauge`

- 3D ゲームオブジェクト薄ラッパー
  - `GameObject3D`
  - `TransformComponent`
  - `ModelRenderer`
  - 既存 `Object3D` を直接使うか、DirectXGame 移植用に薄く包む

### 追加検討

- ループ BGM / SE ハンドル管理
  - KamataEngine 版は `LoadWave` がハンドルを返す前提
  - KuboEngine 版 `Audio` は `SoundData` ベース
  - `SoundHandle` を追加して移行元の呼び出しに近づける

- スプライト生成ヘルパー
  - KamataEngine 版 `Sprite::Create(texture, pos)` に相当する API がない
  - `GameSprite` または `SpriteFactory` を追加すると移植コストが下がる

- 共有ライト
  - KamataEngine 版 `LightGroup` のような一括ライト管理がない
  - まずは `GameLightSettings` を作り、各 `Object3D` に反映

- モデル単位のマテリアル/UV 操作
  - `GridPlane` が KamataEngine の mesh/material に直接アクセスしている
  - KuboEngine の `Model` で同等操作ができない場合、床専用描画またはシェーダ側対応が必要

- レイキャスト/マウス照準補助
  - `Player::UpdateAim` がマウス座標から地面交点を計算
  - KuboEngine math で再実装可能

### OSS 利用候補

導入する場合は、ライセンスと最新状況を確認してから採用する。

- CSV
  - まずは既存の簡易 CSV 読み込みを維持
  - 複雑化したら header-only CSV parser を検討

- JSON
  - UI レイアウトやゲームバランスを JSON 化するなら `nlohmann/json` などを検討
  - ただし現状の CSV 資産を維持する方が移行コストは低い

- Audio
  - 現状の Wave 再生だけなら自作 `Audio` 拡張で十分
  - ogg/mp3 が必要になった段階で miniaudio 等を検討

## 移行しないもの / 後回しにするもの

- ゲーム仕様の変更
- バランス調整
- アセット差し替え
- UI デザイン刷新
- シェーダ演出の大規模変更
- ECS 化などの大規模設計変更

まずは KamataEngine 版と同じ遊びが成立することを優先する。

## フェーズ別作業計画

### Stage 0: 分析と台帳作成

状態: `完了`

完了条件:

- 移行元の主要構造を把握
- 移行先の主要 API を把握
- 本 md を作成

成果物:

- `docs/directxgame_migration_plan.md`

### Stage 1: 移行用ディレクトリと最小シーン作成

状態: `完了`

目的:

KuboEngine 側に DirectXGame 用の受け皿を作り、空の Title/Game/Result シーンを切り替えられるようにする。

作業:

- `game/` または `apps/directxgame/` ディレクトリを追加
- `DirectXGame::SceneId` または文字列シーン名を定義
- `DirectXGameTitleScene`
- `DirectXGameScene`
- `DirectXGameResultScene`
- `DirectXGameSessionContext`
- `SceneFactory` に移行用シーンを登録
- 起動時の初期シーンを DirectXGame title に切り替える方法を用意

完了条件:

- KuboEngine 上で DirectXGame 用 Title -> Game -> Result の空遷移ができる
- `Debug|x64` ビルド成功

注意:

- 既存の KuboEngine サンプルシーンを壊さない
- 既存 `GamePlayScene` をすぐ削除しない

### Stage 2: リソース配置とパス方針の確定

状態: `完了`

目的:

DirectXGame の `Resources` を KuboEngine 側で読み込める形に移す。

作業:

- 移行元 `Resources` のコピー先を決める
  - 候補: `Resources/DirectXGame/...`
  - 候補: 既存 `Resources` に統合
- 画像、音声、モデル、CSV の配置を整理
- KuboEngine の `KuboEngine.vcxproj` / `.filters` に必要な項目を追加
- 既存 `TextureManager` / `ModelManager` のパス探索で読めるか確認
- モデル読み込み方式を確認
  - OBJ の mtl/texture 解決
  - 既存 recursive model search との整合

完了条件:

- DirectXGame の主要モデルが `ModelManager` で読み込める
- DirectXGame の主要テクスチャが `TextureManager` で読み込める
- CSV が相対パスで読める

注意:

- 既存 KuboEngine の `Resources/models` と衝突しない命名にする

### Stage 3: 移行補助レイヤ作成

状態: `完了`

目的:

KamataEngine API との差を小さいクラスで吸収し、ゲームロジック移植の負担を下げる。

追加候補:

- `GameSessionContext`
- `ResultData`
- `ScreenUtil`
- `GameInputBindings`
- `GameModelCache`
- `GameTextureCache`
- `GameAudioCache`
- `GameLightSettings`
- `GameSpriteFactory`

完了条件:

- `TextureManager::LoadTexture` をハンドル風に使える
- `Audio::SoundLoadWave` / `SoundPlayWave` をハンドル風に扱える
- 画面サイズ取得が UI 側から使える
- 入力判定が DirectXGame 版の呼び出しに近い形で使える

### Stage 4: CSV / UI レイアウト I/O 移植

状態: `完了`

目的:

移行元のデータ駆動設計を維持する。

作業:

- `UILayoutIO` を移植
- `playerStatus.csv`
- `enemyTypes.csv`
- `enemySpawnSettings.csv`
- `weaponUpgradeSettings.csv`
- `levelupWeights.csv`
- `ui_layout_title.csv`
- `ui_layout_pause.csv`
- `ui_layout_levelup.csv`
- `ui_layout_result.csv`

完了条件:

- CSV 読み込みユーティリティが KuboEngine 上でビルドできる
- 存在しないレイアウトファイルでもデフォルト値で動く
- 保存系は Debug 時のみ有効にする

注意:

- `ui_layout_result.csv` は移行元にも存在していないため、KuboEngine 側で新規作成する

### Stage 5: 2D UI 基盤移植

状態: `完了`

目的:

Title / Pause / LevelUp / Result / HUD を描けるようにする。

移植対象:

- `UIElement`
- `UIPanel`
- `UILabel`
- `UIBar`
- `DigitSpriteUtil`
- `Score`
- `Timer`
- `KeyUI`
- `MiniMap`
- `HpGauge`
- `ExpGauge`

作業:

- KamataEngine `Sprite` 依存を KuboEngine `Engine::Graphics2D::Sprite` へ置換
- `Sprite::Create` 相当を `GameSpriteFactory` で提供
- `SetSize`
- `SetPosition`
- `SetColor`
- `SetRotation`
- `SetAnchorPoint`
- `SetTextureLeftTop`
- `SetTextureSize`

完了条件:

- 画像 1 枚を画面に表示できる
- 数字スプライトを表示できる
- HP/EXP ゲージを表示できる

### Stage 6: TitleScene 移植

状態: `完了`

目的:

タイトル画面を KuboEngine 上で再現する。

作業:

- タイトル背景/ロゴ/カーソル/ガイド UI
- BGM 再生
- SE 再生
- マウス/ゲームパッド/キーボード操作
- カーテン遷移
- SkyDome 表示
- タイトル用モデル表示
- ImGui レイアウト調整

完了条件:

- Title が表示される
- Play / Guide / Quit が動く
- Guide の fade in/out が動く
- Play で GameScene へ遷移できる

移行時に直す候補:

- 移行元ではキーボード UI 操作が実質未完成
- KuboEngine 版では `W/S`、`Enter/Space`、`Esc` を明示対応する
- 未使用の `titleUISprite_` 相当は持ち込まない

### Stage 7: World / Player 移植

状態: `完了`

目的:

ゲーム画面でプレイヤーを動かし、カメラと照準を動かせるようにする。

移植対象:

- `Player`
- `GridPlane`
- `SkyDome`
- `InputBindings`
- `ScreenUtil`

作業:

- プレイヤーモデル表示
- WASD / gamepad 移動
- マウス照準
- 右スティック照準
- プレイヤー追従カメラ
- 照準インジケータ
- 床タイル追従
- 天球表示

完了条件:

- GameScene 上でプレイヤーが移動できる
- カメラが追従する
- マウス/右スティックで向きを変えられる
- 床と空が描画される

注意:

- `GridPlane` の UV 操作は KuboEngine の `Model` API に合わせて再実装が必要な可能性あり

### Stage 8: PlayerManager / 武器移植

状態: `完了`

目的:

プレイヤー成長、通常弾、周囲弾、ドローン、ライトニングを動かす。

移植対象:

- `PlayerManager`
- `NormalBullet`
- `OrbitBullet`
- `Drone`
- `Bullet`
- `RippleEffect`
- `LightningStrikeEffect`

作業:

- player status CSV 読み込み
- weapon upgrade CSV 読み込み
- 通常弾発射
- 弾寿命/射程/貫通
- 周囲弾の回転
- ドローン追従と自動射撃
- ライトニング対象選択
- レベルアップ時の波紋

完了条件:

- 通常弾で敵なしでも発射/消滅が確認できる
- 武器レベルアップの内部状態が変わる
- 各武器の描画が出る

補足:

- 敵未移植のため、Drone は現段階ではプレイヤー向きへ射撃する
- Lightning は設定値とレベル状態まで実装し、対象選択とダメージ適用は Stage 9 / Stage 10 で EnemyManager と接続する
- RippleEffect は Stage 14 の演出移植で KuboEngine の Particle / Line と合わせて再実装する

### Stage 9: Enemy / EnemyManager 移植

状態: `完了`

目的:

敵生成、敵行動、敵同士の押し合い、EXP ドロップを再現する。

移植対象:

- `Enemy`
- `EnemyManager`
- `EnemyBehavior`
- `ExpOrb`
- `DeathParticle`
- `HitParticle`

作業:

- enemy types CSV 読み込み
- spawn settings CSV 読み込み
- 敵の無限湧き
- 4 種類の敵行動
  - Chase
  - BurstChase
  - CircleApproach
  - KeepDistanceRush
- 空間分割
- 敵同士の separation
- 遠距離敵の再配置
- 死亡時 EXP orb
- ヒット/死亡パーティクル

完了条件:

- 敵が湧く
- 敵がプレイヤーへ向かう
- 敵タイプごとの行動差が出る
- 倒すと EXP orb が出る

補足:

- Stage 9 では EnemyManager の Debug ボタンから全敵ダメージを与えて EXP orb ドロップを確認できる
- 弾 / 周囲弾 / ドローン / ライトニング / プレイヤー接触との戦闘統合は Stage 10 に残す
- HitParticle / DeathParticle は Stage 14 の演出移植で KuboEngine の Particle / Line と合わせて再実装する

### Stage 10: 衝突判定と戦闘統合

状態: `完了`

目的:

プレイヤー、敵、弾、EXP orb の相互作用を成立させる。

作業:

- 通常弾 vs 敵
- 周囲弾 vs 敵
- ドローン弾 vs 敵
- ライトニング vs 敵
- 敵 vs プレイヤー
- EXP orb 吸収
- ダメージ無敵
- HP 0 死亡

完了条件:

- 敵を倒せる
- EXP が増える
- レベルアップ要求が立つ
- 敵接触で HP が減る
- 死亡状態へ入れる

補足:

- 通常弾 / 周囲弾 / ドローン弾は EnemyManager の空間分割結果を使って敵へダメージを与える
- Lightning は PlayerManager の interval / strike count / radius / damage 設定を使い、EnemyManager のランダム対象へ範囲ダメージを与える
- Drone は EnemyManager の最寄り敵座標を使って射撃方向を決める
- HP 0 後の死亡演出と Result 遷移の本接続は Stage 11 / Stage 12 で UI 制御と合わせて整理する

### Stage 11: HUD / Pause / LevelUp 移植

状態: `完了`

目的:

ゲーム中 UI とメニュー系を再現する。

移植対象:

- `GameStartController`
- `GamePauseController`
- `GameLevelUpController`
- `MiniMap`
- `Timer`
- `KeyUI`
- `HpGauge`
- `ExpGauge`

作業:

- Start overlay
- Pause overlay
- Guide display
- Build icon display
- MiniMap
- LevelUp candidate lottery
- LevelUp slide animation
- Confetti
- Timer
- HP / EXP gauge

完了条件:

- 開始待ち UI が出る
- Pause できる
- LevelUp 選択肢が出る
- 選択した強化が反映される
- HUD がプレイ状態に追従する

移行時に直す候補:

- キーボード UI 操作を統一対応する
- Title / Pause / LevelUp のナビゲーション処理を共通化する

補足:

- Stage 11 では Timer / HP / EXP gauge、Start overlay、Pause overlay、LevelUp 3 択選択を GameScene に直接接続した
- KeyUI / MiniMap / Build icon / LevelUp slide animation / Confetti は追加実装済み
- UI 操作は W/S、Up/Down、Enter/Space、Esc、GamePad A/B/Start を標準対応した

### Stage 12: ResultScene 移植

状態: `完了`

目的:

プレイ結果表示と Title への復帰を再現する。

作業:

- `ResultData` 表示
- EXP count-up
- Level count-up
- Kill count-up
- スキップ入力
- カーテン遷移
- Title へ戻る
- `ui_layout_result.csv` 新規作成

完了条件:

- GameScene 終了時に Result へ遷移
- EXP / Level / Kill が表示される
- 入力で Title へ戻れる

補足:

- ResultScene は `ui_layout_result.csv` の座標を読み込み、EXP / Level / Kill / TotalScore を数字スプライトで count-up 表示する
- TotalScore は移植元と同じく `EXP + Kill * 100 + Level * 1000` で計算する
- Confirm / Cancel 入力は count-up 中なら即時完了、完了後なら Title へ戻る
- カーテン遷移の本接続は Stage 14 の見た目調整で Title / Game / Result の横断演出として整理する

### Stage 13: 音声統合

状態: `完了`

目的:

移行元と同等の BGM / SE 再生を実現する。

作業:

- `GameAudioCache` 追加
- SoundData のハンドル化
- BGM ループ再生
- 音量指定
- Scene finalize 時の Stop 整理
- Title / Game / Result への主要 SE 接続

完了条件:

- Title BGM がループする
- shot / hit / death / pause / levelup SE が鳴る
- Result/Scene 遷移時に不要な音が止まる

実装済み:

- `Audio::SoundPlayWave` に loop 指定を追加し、`GameAudioCache::PlayLoop` から利用できるようにした
- Title BGM は `GameAudioCache::PlayLoop` でループ再生する
- GameScene に start / pause / levelup / gameover SE を接続し、Finalize で停止する
- EnemyManager に player damage SE を接続した
- ResultScene に count-up 完了 SE を接続し、Finalize で停止する
- Title BGM / select / decide、Game start / pause / levelup / death、shot / enemy hit / enemy death / player damage / EXP pickup、Result finish の個別音量キーを追加
- 移植元に合わせて Title BGM は 0.1、Pause toggle は 0.5、Enemy hit は 0.5、Player damage は 0.8、その他主要 SE は 1.0 を初期値にした
- Game start / levelup は `se_exp.wav`、death は `se_death.wav`、player damage は `se_hit.wav`、Result finish は `se_pause.wav` を使用する

補足:

- 同一 SoundData の完全な多重 SE 再生は現行 Audio の仕様上まだ拡張していない
- 個別音量はランタイム調整用であり、現時点では CSV 等への永続保存はしていない

### Stage 14: 見た目調整と演出移植

状態: `完了`

目的:

KamataEngine 版の見た目に近づけつつ、自作エンジン側で自然に強化できる演出を追加する。

作業:

- カーテン遷移の再現
- 死亡 overlay
- ヒット flash
- 敵タイプ別色/scale
- 雷撃演出
- 波紋演出
- 床 UV 見た目
- SkyDome ライティング
- Offscreen render を使ったポストエフェクト
- LineSystem / Particle を使った雷撃・吸収・範囲演出
- DebugDraw による当たり判定/スポーン範囲可視化

完了条件:

- 移行元と同じゲーム状態が視覚的に判別できる
- 主要 UI が重ならない
- デバッグ表示で調整できる
- 自作エンジン機能を使った追加演出が、基本仕様を壊さずに有効化できる

実装済み:

- プレイヤーダメージ時に赤い画面 flash を表示
- Player death 時に `ui/game/death.png` overlay を表示し、約 1.15 秒後または Confirm 入力で Result へ遷移
- Debug ImGui に collision / spawn range の LineSystem 表示 toggle を追加
- DebugDraw 有効時、プレイヤー/敵の当たり判定目安とプレイヤー中心のスポーン範囲をライン表示
- GameScene / ResultScene の入退場に CurtainTransition を接続
- 雷撃発生時の target を PlayerManager に短時間保持し、GameScene で落雷ラインと範囲ラインを表示
- 近距離 EXP orb と Player の間に吸収ラインを表示
- `RippleParticleBehavior` / `SparkParticleBehavior` を追加し、雷撃 / ダメージ / EXP 回収時に Particle を発火
- GridPlane の tile color を軽く pulse させ、SkyDome をゆっくり回転/色変化させる
- Offscreen render の post effect を Scene 状態から自動適用できるようにし、Pause は Vignette、LevelUp は RadialBlur、Dead は Grayscale を使う
- Debug ImGui から `Auto Scene Effect` を切って手動で post effect を確認できる

残り:

- なし

### Stage 14.5: 自作エンジン向けアレンジ強化（履歴）

状態: `完了 / 今後の追加改善は Stage 17 へ移管`

目的:

移行完了後のゲームを、KuboEngine 製として見せやすくする。

補足:

この Stage は、移植元再現の後に追加したカメラ、音量調整、HUD、LevelUp 演出、Mouse Aim などの履歴を残すための章。
今後の新規改善は `Stage 17: 追加演出 / エンジン拡張候補` に集約する。

作業候補:

- death 中の grayscale + vignette
- pause 中の vignette
- level up 中の radial blur / screen flash
- hit 時の短時間 flash
- 雷撃ラインと着弾パーティクル
- EXP orb 吸収ライン
- 敵スポーン範囲/当たり判定の DebugDraw
- 入力デバイスに応じた UI 表示切り替え
- ImGui から CSV を編集/保存

完了条件:

- 移行元の基本挙動は維持されている
- 追加演出は ON/OFF 可能
- `Debug|x64` / `Release|x64` でビルド成功

実装済み:

- `GameAudioCache` に master volume を追加し、各 SE/BGM の個別音量に全体倍率を掛けて再生するようにした
- master volume 変更時、再生中の SoundData にも即時反映するようにした
- Debug ImGui の Title / Game / Result に `Master Volume` スライダーを追加し、手動確認時に音量バランスを調整しやすくした
- `GameAudioCache` に個別音量チューニングを追加し、Debug ImGui の `Audio Balance` から BGM / SE の用途別音量を調整できるようにした
- Player の追従カメラに Height / Distance / Pitch の調整値を追加し、初期値は移植元と同じ Height 80 / Distance 45 / Pitch 1.0 にした
- Player camera mode を追加し、Debug ImGui の `Camera Mode` から固定バック、プレイヤー背後、固定フロント、真上カメラを切り替え可能にした
- Player camera 更新時に `CameraManager` 内の登録済みカメラへ translate / rotate / far clip を同期し、描画に使われる active camera がプレイヤー移動へ追従するようにした
- Player のマウス照準を明示的に有効化し、画面上のプレイヤー位置からマウス位置への差分をワールド方向へ変換して、プレイヤー本体 / AimIndicator / 通常弾の向きを同期するようにした
- マウスが動いたフレームは右スティックよりマウス照準を優先し、接続中 gamepad の微小入力でマウス操作が無視されないようにした
- Player 更新中に照準更新後も camera を再同期し、Player Back camera 使用時も向き変更とカメラの追従が同フレームで反映されるようにした
- Debug ImGui の Game 画面に `Mouse Aim` checkbox を追加し、マウス照準の ON/OFF を確認できるようにした
- `GameInputBindings` に入力デバイス推定と表示ラベル生成を追加
- Debug ImGui の Title / Game / Result に現在の入力デバイスと Confirm / Cancel / Pause の操作表記を表示するようにした
- ResultScene に Debug 用レイアウト調整を追加し、背景 / Result UI / finish UI / 数字位置 / digit size / score scale を ImGui から調整できるようにした
- ResultScene の `Save Result Layout` から `ui_layout_result.csv` へ調整値を保存できるようにした
- ResultScene の 4 行目表示を Time から移植元準拠の TotalScore に変更した
- KeyUI / MiniMap を GameScene に追加し、Debug ImGui から表示 ON/OFF と位置 / サイズ / ミニマップ半径 / 縮尺 / アイコンサイズを調整できるようにした
- Pause Build icon を GameScene に追加し、Debug ImGui から表示 ON/OFF と位置 / 間隔 / アイコンサイズを調整、`ui_layout_pause.csv` へ保存できるようにした
- LevelUp UI に移植元準拠の slide enter / exit と Confetti particle を追加し、選択確定後は exit 完了時に強化を反映するようにした
- LevelUp Normal 選択肢の参照先を存在しない `ui/game/normal/upgrade.png` から実在する `ui/game/normal/choice.png` へ修正し、LevelUp 開始時の texture load assert を回避した

確認済み:

- `v145 Debug|x64` ビルド成功、警告 0 / エラー 0
- `v145 Release|x64` ビルド成功、警告 0 / エラー 0
- Debug Title -> Game 確認: `DEBUG_AUDIO_MASTER_GAME_RUNNING_OK`
- Release 起動確認: `RELEASE_AUDIO_MASTER_RUNNING_OK`
- Debug 入力デバイス表示確認: `DEBUG_INPUT_DEVICE_GAME_RUNNING_OK`
- Release 入力デバイス表示後の起動確認: `RELEASE_INPUT_DEVICE_RUNNING_OK`
- Debug Result レイアウト調整後の Result 遷移確認: `DEBUG_RESULT_LAYOUT_RUNNING_OK`
- Release Result レイアウト調整後の起動確認: `RELEASE_RESULT_LAYOUT_RUNNING_OK`
- Debug 音量チューニング / カメラ調整 / TotalScore Result 確認: `DEBUG_AUDIO_CAMERA_RESULT_RUNNING_OK`
- Release 音量チューニング / カメラ調整 / TotalScore Result 起動確認: `RELEASE_AUDIO_CAMERA_RESULT_RUNNING_OK`
- Debug KeyUI / MiniMap Title -> Game -> Pause 確認: `DEBUG_KEYUI_MINIMAP_RUNNING_OK`
- Release KeyUI / MiniMap 起動確認: `RELEASE_KEYUI_MINIMAP_RUNNING_OK`
- Debug Pause HUD / Build icon 確認: `DEBUG_PAUSE_HUD_RUNNING_OK`
- Release Pause HUD 起動確認: `RELEASE_PAUSE_HUD_RUNNING_OK`
- Debug LevelUp slide / Confetti 確認: `DEBUG_LEVELUP_ANIMATION_RUNNING_OK`
- Release LevelUp slide / Confetti 起動確認: `RELEASE_LEVELUP_ANIMATION_RUNNING_OK`
- Debug Camera Mode 追加後の Title -> Game 確認: `DEBUG_CAMERA_MODE_RUNNING_OK`
- Release Camera Mode 追加後の起動確認: `RELEASE_CAMERA_MODE_RUNNING_OK`
- Debug Camera follow 修正後の Title -> Game -> movement 確認: `DEBUG_CAMERA_FOLLOW_RUNNING_OK`
- Release Camera follow 修正後の起動確認: `RELEASE_CAMERA_FOLLOW_RUNNING_OK`
- Debug Mouse Aim 更新後の起動確認: `DEBUG_MOUSE_AIM_RUNNING_OK`
- Release Mouse Aim 更新後の起動確認: `RELEASE_MOUSE_AIM_RUNNING_OK`
- Debug screen-space Mouse Aim 更新後の起動確認: `DEBUG_MOUSE_AIM_SCREEN_RUNNING_OK`
- Release screen-space Mouse Aim 更新後の起動確認: `RELEASE_MOUSE_AIM_SCREEN_RUNNING_OK`
- Debug LevelUp texture path 修正後の起動確認: `DEBUG_LEVELUP_TEXTURE_PATH_RUNNING_OK`
- Release LevelUp texture path 修正後の起動確認: `RELEASE_LEVELUP_TEXTURE_PATH_RUNNING_OK`
- Debug Title -> Game -> F8 LevelUp 遷移確認: `DEBUG_LEVELUP_F8_RUNNING_OK`

### Stage 15: ビルド設定 / フィルタ / ドキュメント整理

状態: `完了`

目的:

Visual Studio で扱いやすい構成にする。

作業:

- `KuboEngine.vcxproj` へ移植ファイル追加
- `.filters` 整理
- `Resources/DirectXGame` のフィルタ整理
- include path 確認
- 不要ファイル除外
- docs 更新

完了条件:

- Visual Studio 上でファイル構造が追いやすい
- `Debug|x64` / `Release|x64` でビルド成功

実装済み:

- C++ の移行ファイルが `KuboEngine.vcxproj` / `.filters` に登録済みであることを確認
- `Resources/DirectXGame` の audio / data / legacy models / textures / ui を `None` 項目と filter に登録
- `Debug|x64` ビルド成功、5 秒起動確認 `DEBUG_RUNNING_OK`
- `Release|x64` ビルド成功、5 秒起動確認 `RELEASE_RUNNING_OK`

### Stage 16: 最終確認

状態: `完了`

目的:

ゲームとして通しで遊べる状態にする。

確認項目:

- Title -> Game -> Result -> Title
- Game start
- Pause / Guide
- Player movement
- Aim
- Enemy spawn
- Combat
- EXP pickup
- LevelUp
- Weapon upgrades
- Death
- Time-up
- Result score
- BGM / SE
- Debug UI

完了条件:

- `Debug|x64` ビルド成功
- `Release|x64` ビルド成功
- 通しプレイでクラッシュしない
- 移行元の基本仕様が維持されている

実装済み:

- GameScene に 300 秒の Time-up 条件を接続し、ResultScene へ Result summary を渡すようにした
- Death / Time-up / Debug Result 遷移の Result summary 記録を `RecordResultSummary` / `RequestResultScene` に集約
- Debug build 限定の確認ショートカットを追加
  - `F6`: Death
  - `F8`: LevelUp 用 EXP 加算
  - `F9`: 全武器最大化
  - `F10`: Result 遷移
  - `F11`: Time-up Result 遷移
- `PlayerManager::ForceDebugDeath` を Debug build 限定で追加し、死亡フロー確認で無敵時間に引っかからないようにした

確認済み:

- `v145 Debug|x64` ビルド成功、警告 0 / エラー 0
- `v145 Release|x64` ビルド成功、警告 0 / エラー 0
- Title -> Game -> Time-up Result -> Title の自動操作確認: `DEBUG_FLOW_RUNNING_OK`
- Title -> Game -> Death Result の自動操作確認: `DEBUG_DEATH_RUNNING_OK`
- LevelUp 選択フローの自動操作確認: `DEBUG_LEVELUP_RUNNING_OK`
- Debug 30 秒 Gameplay 確認: `DEBUG_30S_GAMEPLAY_RUNNING_OK`
- Release Gameplay 確認: `RELEASE_GAME_RUNNING_OK`
- Debug 音量チューニング / カメラ調整 / TotalScore Result 確認: `DEBUG_AUDIO_CAMERA_RESULT_RUNNING_OK`
- Release 音量チューニング / カメラ調整 / TotalScore Result 起動確認: `RELEASE_AUDIO_CAMERA_RESULT_RUNNING_OK`
- Debug KeyUI / MiniMap Title -> Game -> Pause 確認: `DEBUG_KEYUI_MINIMAP_RUNNING_OK`
- Release KeyUI / MiniMap 起動確認: `RELEASE_KEYUI_MINIMAP_RUNNING_OK`
- Debug Pause HUD / Build icon 確認: `DEBUG_PAUSE_HUD_RUNNING_OK`
- Release Pause HUD 起動確認: `RELEASE_PAUSE_HUD_RUNNING_OK`
- Debug LevelUp slide / Confetti 確認: `DEBUG_LEVELUP_ANIMATION_RUNNING_OK`
- Release LevelUp slide / Confetti 起動確認: `RELEASE_LEVELUP_ANIMATION_RUNNING_OK`
- Debug Camera Mode 追加後の Title -> Game 確認: `DEBUG_CAMERA_MODE_RUNNING_OK`
- Release Camera Mode 追加後の起動確認: `RELEASE_CAMERA_MODE_RUNNING_OK`
- Debug Camera follow 修正後の Title -> Game -> movement 確認: `DEBUG_CAMERA_FOLLOW_RUNNING_OK`
- Release Camera follow 修正後の起動確認: `RELEASE_CAMERA_FOLLOW_RUNNING_OK`

## 既知の注意点

### キーボード UI 入力

移行元には `IsKeyboardMenuUpTriggered` などがあるが、Title / Pause / LevelUp のナビゲーションでは十分使われていない。

KuboEngine 移行時は以下を標準対応にする:

- `W/S` または `Up/Down`
- `Space/Enter`
- `Esc`
- Mouse
- Gamepad

### `ui_layout_result.csv`

移行元 `ResultScene` は `Resources/data/ui_layout_result.csv` を読むが、実ファイルが存在しない。

KuboEngine 側では新規作成する。

### KamataEngine の `LightGroup`

KuboEngine は `Object3D` ごとにライト情報を持つ構造。

対応案:

1. `GameLightSettings` を作る
2. 共有ライト設定を保持する
3. 各 `Object3D` 作成時/更新時に同期する

### KamataEngine の `Model` / `WorldTransform`

移行元は `Model::Draw(worldTransform, camera)` 形式。

KuboEngine は `Object3D` が transform と model を保持するため、以下のどちらかで移植する:

- A: `Object3D` を直接メンバに持つ
- B: `GameObject3D` ラッパーを作って `WorldTransform` 風 API を提供する

推奨: 初期移植は B。ゲームロジック側の修正量を減らせる。

## 推奨ディレクトリ案

```text
game/directxgame/
  core/
    GameSessionContext.h
    GameInputBindings.h
    GameModelCache.h
    GameTextureCache.h
    GameAudioCache.h
    ScreenUtil.h
  scene/
    DirectXGameTitleScene.h/.cpp
    DirectXGameScene.h/.cpp
    DirectXGameResultScene.h/.cpp
    GameStartController.h/.cpp
    GamePauseController.h/.cpp
    GameLevelUpController.h/.cpp
  player/
    Player.h/.cpp
    PlayerManager.h/.cpp
    weapons/
  enemy/
    Enemy.h/.cpp
    EnemyManager.h/.cpp
    EnemyBehavior.h/.cpp
  effects/
  ui/
  world/
Resources/DirectXGame/
  audio/
  data/
  models/
  textures/
  ui/
```

## 作業時のルール

- 1 フェーズごとにビルド確認する
- 大きな API 置換はラッパーを先に作る
- ゲーム仕様変更は別タスクに分離する
- CSV の値は基本的に変更しない
- アセット名は可能な限り維持する
- KuboEngine 側の既存機能を優先して使う
- OSS 導入は必要性、ライセンス、保守コストを確認してから行う

## Stage 17: 追加演出 / エンジン拡張候補

状態: `未着手`

目的:

DirectXGame の見た目と手触りをさらに上げるため、既存エンジンで実装できる範囲と、エンジン側へ追加した方がよい機能を分けて整理する。

### 17.1 既存機能だけで進める項目

現在の `Engine::Particle::ParticleManager` は CPU で粒子を生成・更新し、GPU には `StructuredBuffer<ParticleForGPU>` と `DrawInstanced` で描画する構成。

以下は既存機能だけで対応できる:

- 敵 hit / death の火花、衝撃波、短い煙
- EXP 取得時の ripple / sparkle 強化
- LevelUp の confetti を `Sprite` 個別描画から `ParticleManager` へ寄せる
- Player damage / death の画面演出と粒子連動
- Lightning の着弾 sparkle / ring / line 補強
- Pause / LevelUp / Result の UI pulse、alpha、scale animation
- Debug ImGui から particle count / speed / lifetime / scale / color を調整する簡易 editor

実装時の方針:

- 最初は CPU particle + GPU instancing のまま進める
- 1 演出ごとに `IParticleBehavior` を追加する
- Emit 数はまず 10-100 程度に抑える
- 既存 `DirectXGame.Ripple` / `DirectXGame.Spark` を拡張し、足りない場合だけ group を追加する
- Debug ImGui から値を調整し、必要なら CSV 保存する

### 17.2 エンジン側へ追加した方がよい項目

GPU particle や大量演出へ進む場合は、以下の DirectX12 基盤を先に足す。

- Compute shader 用 PSO / root signature 生成
- `GraphicsPipeline` への `CreateComputePipeline` 相当の追加
- `SrvManager` への UAV descriptor 作成 API 追加
- `DirectXCommon` への resource state transition helper 追加
- `RWStructuredBuffer` 用 default heap buffer と upload/readback 補助
- `Dispatch` 実行 API の整理
- GPU particle 用 alive/dead list、emit counter、free list
- `ExecuteIndirect` / indirect draw は第 2 段階で検討
- GPU particle 用 shader 配置: `Resources/Shaders/particle/gpu/`

実装順:

1. Compute shader が 1 本動く最小サンプルを作る
2. UAV buffer に粒子位置を書き込む
3. 既存 particle VS が読む `StructuredBuffer` へ接続する
4. CPU emitter から GPU emitter へ発生数だけ渡す
5. lifetime / velocity / color / scale を GPU 更新へ移す
6. 必要なら indirect draw と GPU sort を追加する

### 17.3 OSS / 外部機能の扱い

現時点では OSS particle engine を丸ごと導入する優先度は低い。

理由:

- 既存の `DirectXCommon` / `SrvManager` / `GraphicsPipeline` と密結合するため統合コストが高い
- DirectX12 の descriptor / resource state / command list 方針がエンジンごとに違う
- ライセンス確認と保守負荷が増える

使うなら以下の用途に限定する:

- Compute particle の参考実装
- ノイズ関数、カーブ補間、ランダム分布の実装参考
- エディタ UI の設計参考

導入前に確認すること:

- ライセンス
- DirectX12 対応状況
- 依存ライブラリ数
- 既存ビルド構成 `v145 Debug|x64` / `Release|x64` への影響
- 学校/提出物として外部コード利用が許可されるか

### 17.4 ゲーム側で追加・修正した方がよい項目

演出追加と並行して、以下のゲーム側の手当ても優先度が高い。

- LevelUp / Pause / Title / Result の入力処理を共通 controller へ分離する
- LevelUp 候補画像パスのようなアセット欠落を起動時 resource probe で検出する
- `GameTextureCache::Load` / `GameAudioCache::LoadWave` の失敗時メッセージを具体化する
- Debug ImGui の調整値を `ui_layout_*.csv` / effect tuning CSV へ保存する
- CameraManager が camera をコピー保持する仕様を見直すか、明示的な sync helper を用意する
- Mouse aim / gamepad aim / keyboard move の入力優先度を Debug 表示する
- LevelUp 中、Pause 中、Dead 中に gameplay update が走らないことを自動確認する
- Normal / Orbit / Drone / Lightning の upgrade 表示と実際の強化値を Debug 表示する
- EXP orb / enemy / bullet 数の上限と間引き方針を決める
- `Resources/DirectXGame` の必須アセット一覧を md または JSON/CSV で管理する

### 17.5 エンジン側で追加・修正した方がよい項目

DirectXGame 以外の今後の制作にも効くエンジン改善。

- `TextureManager::LoadTexture` の assert 前に file path をログへ出す
- `Audio` の wav format assert に file path と chunk id を含める
- `CameraManager::AddCamera` のコピー保持仕様をコメントか API 名で明確化する
- `Sprite` / `Object3D` / `Particle` の debug name を持てるようにする
- `SrvManager` の SRV 使用数を Debug ImGui で見られるようにする
- Particle instance 上限 `1000` を group ごとに指定できるようにする
- Particle update deltaTime を固定 `1/60` から実 deltaTime へ切り替え可能にする
- `std::list<Particle>` は粒子数が増えると重いため、CPU particle は `std::vector` + alive compact へ変更を検討する
- math 系グローバル alias は互換性を見ながら段階削除する

### 17.6 完了条件

- `v145 Debug|x64` / `Release|x64` でビルド成功
- 既存 CPU particle 拡張は gameplay 中に落ちず、Debug ImGui で主要値を調整できる
- GPU particle 基盤を追加する場合は、最小 compute sample と既存描画への接続が確認できる
- LevelUp / Pause / Result / Dead の既存フローが壊れていない
- 追加したアセットは resource probe または一覧で検出できる

## ゲーム制作向け改善バックログ

状態: `整理済み / 未着手`

目的:

DirectXGame の移植だけでなく、今後このエンジンでゲームを作るときに詰まりやすい箇所を整理する。

### 優先度 A: 先に直すと安定する項目

- Resource probe 強化
  - `Resources/DirectXGame` の必須 texture / model / audio / csv を一覧化し、起動時にまとめて検査する
  - LevelUp の `normal/upgrade.png` 欠落のような問題を gameplay 中ではなく起動時に検出する
- エラー情報の具体化
  - `TextureManager::LoadTexture` の失敗時に file path をログ出力する
  - `Audio::SoundLoadWave` の assert に file path / chunk id / expected id を含める
  - `Model::Load` の失敗時に model path と Assimp error を出す
- Camera 管理の明確化
  - `CameraManager::AddCamera` は camera をコピー保持するため、API 名かコメントで明確化する
  - DirectXGame のように毎フレーム同期が必要な camera には sync helper を用意する
- 入力優先度の可視化
  - Mouse aim / right stick aim / keyboard move / gamepad move の現在値を Debug ImGui に表示する
  - ゲームパッド微小入力で mouse aim が潰れないか確認できるようにする
- シーン状態の安全確認
  - Start / Playing / Pause / LevelUp / Dead で、どの update が走るかを明示する
  - LevelUp / Pause / Dead 中に gameplay update が走らないことを Debug 表示またはテストで確認する

### 優先度 B: 作りやすさが上がる項目

- UI controller 分離
  - Title / Pause / LevelUp / Result の入力処理を共通 controller へ寄せる
  - `MoveMenuSelection` などを状態ごとの分岐から切り出す
- Debug tuning 保存
  - Audio balance、camera、particle、UI layout の調整値を CSV 保存できるようにする
  - 現在は一部 layout のみ保存済みなので、effect tuning も対象にする
- Gameplay debug dashboard
  - enemy count / bullet count / orb count / particle count / SRV 使用数をまとめて表示する
  - Normal / Orbit / Drone / Lightning の level、damage、interval、active 数を一覧化する
- アセット参照の型安全化
  - 文字列直書きを減らし、`DirectXGameResourcePaths` または generated manifest へ寄せる
  - texture/audio/model の handle を用途名で管理する
- 簡易自動確認
  - Title -> Game
  - F8 LevelUp
  - F6 Death
  - F10 Result
  - Pause -> Resume
  - 上記を短時間で流せる確認スクリプトを整える

### 優先度 C: 表現力を上げる項目

- Particle editor
  - count / lifetime / velocity / scale / color / gravity を ImGui で編集
  - CPU particle の behavior ごとに調整値を持つ
- 画面演出
  - hit flash、death grayscale、pause vignette、levelup radial blur の強度を調整可能にする
  - Result count-up や LevelUp choice の pulse / scale animation を追加する
- カメラ演出
  - damage / levelup / death の camera shake
  - GameCamera mode の保存
  - camera follow smoothing の ON/OFF と係数調整
- オーディオ演出
  - 同一 SE の多重再生
  - pitch variation
  - BGM fade in/out
  - scene transition 時の BGM crossfade

### 優先度 D: エンジン基盤として後で効く項目

- ParticleManager 改善
  - instance 上限 `1000` を group ごとに設定可能にする
  - `std::list<Particle>` から `std::vector` + alive compact へ変更を検討する
  - update deltaTime を固定 `1/60` だけでなく実 deltaTime へ切り替え可能にする
- Descriptor / SRV 可視化
  - `SrvManager` の使用数、残数、用途を Debug ImGui に表示する
  - texture / structured buffer / ImGui 用 descriptor の衝突を見つけやすくする
- Compute shader 基盤
  - compute PSO / root signature
  - UAV descriptor
  - resource barrier helper
  - `Dispatch` helper
  - GPU particle や post effect 拡張の前提になる
- Render debug
  - object / sprite / particle に debug name を持たせる
  - draw call / instance count / active camera 名を表示する
- 互換 alias 整理
  - math 系グローバル alias は互換性を見ながら段階削除する
  - namespace 化済みコードへ寄せる

### 今すぐの推奨着手順

1. Resource probe 強化
2. LevelUp confetti を `ParticleManager` へ移管
3. 敵 hit / death particle の見た目調整
4. Particle tuning ImGui 追加
5. Gameplay debug dashboard 追加
6. Compute / UAV 基盤の最小 sample

## 次に進めるべき順番

1. Stage 17.1 として既存 CPU particle + GPU instancing で DirectXGame 用演出を増やす
2. LevelUp confetti を `Sprite` 個別描画から particle group へ寄せる
3. 敵 hit / death / EXP / Lightning の演出を Debug ImGui で調整できるようにする
4. アセット欠落を resource probe で検出できるようにする
5. 必要になった段階で Stage 17.2 の Compute / UAV 基盤を追加する
6. 提出前に Debug / Release の最終ビルドと主要フローを再確認する

## 次回の再開ポイント

次に作業を再開する場合は、以下から始める。

- ブランチ `feature/directxgame-migration-stage6` から再開する
- Stage 16 は完了済み。次は Stage 17.1 として既存 CPU particle + GPU instancing の範囲で演出強化を始める
- Stage 16 最終確認は `v145 Debug|x64` / `v145 Release|x64` ビルド成功、`DEBUG_FLOW_RUNNING_OK` / `DEBUG_DEATH_RUNNING_OK` / `DEBUG_LEVELUP_RUNNING_OK` / `DEBUG_30S_GAMEPLAY_RUNNING_OK` / `RELEASE_GAME_RUNNING_OK` / `DEBUG_AUDIO_CAMERA_RESULT_RUNNING_OK` / `RELEASE_AUDIO_CAMERA_RESULT_RUNNING_OK` / `DEBUG_KEYUI_MINIMAP_RUNNING_OK` / `RELEASE_KEYUI_MINIMAP_RUNNING_OK` / `DEBUG_PAUSE_HUD_RUNNING_OK` / `RELEASE_PAUSE_HUD_RUNNING_OK` / `DEBUG_LEVELUP_ANIMATION_RUNNING_OK` / `RELEASE_LEVELUP_ANIMATION_RUNNING_OK` / `DEBUG_CAMERA_MODE_RUNNING_OK` / `RELEASE_CAMERA_MODE_RUNNING_OK` / `DEBUG_CAMERA_FOLLOW_RUNNING_OK` / `RELEASE_CAMERA_FOLLOW_RUNNING_OK`
- 追加実装後は `v145 Debug|x64` / `v145 Release|x64` ビルドと該当フロー確認を行う

## 再開時の依頼文例

```text
docs/directxgame_migration_plan.md を見て DirectXGame の手動確認で見つかった問題を修正してください
```

または

```text
docs/directxgame_migration_plan.md を見て Stage 17.1 の既存 particle 演出強化から進めてください
```
