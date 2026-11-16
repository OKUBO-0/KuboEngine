#include "GamePlayScene.h"
#include "Camera.h"
#include <ModelManager.h>
#include "Sprite.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "SpriteCommon.h"
#include "SceneManager.h"
#include <imgui.h>
#ifndef KUBO_RUNTIME_ONLY
#include <imgui_internal.h>
#endif
#include "Input.h"
#include "CameraManager.h"
#include "ParticleEmitter.h"
#include "ParticleManager.h"
#include "SkyBox.h"
#include <Logger.h>
#include "AttackBehavior.h"
#include "MagicCircleBehavior.h"
#include "PlayerControlState.h"

namespace Engine::Scene {

namespace {
const Vector3 kMainCameraTranslate = { 0.0f, 0.0f, -10.0f };
const Vector3 kSubCameraTranslate = { 0.0f, 6.0f, -20.0f };
const Vector3 kSubCameraRotate = { 0.35f, 0.0f, 0.0f };
const Vector3 kPlayerInitialRotate = { 0.0f, -3.0f, 0.0f };
const EulerTransform kTerrainTransform = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f} };
const Vector3 kEmitterPosition = { 0.0f, 0.0f, 0.0f };
constexpr float kEmitterFrequency = 1.0f;
constexpr float kEmitterStartTime = 0.0f;
constexpr uint32_t kAttackParticleEmitCount = 10;
constexpr uint32_t kMagicCircleEmitCount = 1;
#ifndef KUBO_RUNTIME_ONLY
bool HasEditorFrame()
{
    const auto* context = ImGui::GetCurrentContext();
    return context && context->WithinFrameScope;
}
#endif
}

GamePlayScene::GamePlayScene() = default;
GamePlayScene::~GamePlayScene() = default;

void GamePlayScene::Initialize()
{
#ifndef KUBO_RUNTIME_ONLY
    if (HasEditorFrame()) playback_ = Playback::Editing;
#endif
    InitializeCameras();
    LoadSceneResources();
    InitializeSceneObjects();
    InitializeParticles();
    controlState_ = std::make_unique<IdlePlayerControlState>();
    sceneSchema_ = CaptureScene();
    std::error_code sceneError;
    if (std::filesystem::exists(scenePath_, sceneError)) { LoadScene(scenePath_); }
    recoveryAvailable_ = std::filesystem::exists(SceneDocument::AutosavePath(scenePath_), sceneError);
}

void GamePlayScene::InitializeCameras()
{
	// メインカメラ生成と登録
	camera1 = std::make_unique<Engine::CameraSystem::Camera>();
	camera1->SetTranslate(kMainCameraTranslate);
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera("maincam", camera1.get());

	// サブカメラ生成と登録
	camera2 = std::make_unique<Engine::CameraSystem::Camera>();
	camera2->SetTranslate(kSubCameraTranslate);
	camera2->SetRotate(kSubCameraRotate);
	Engine::CameraSystem::CameraManager::GetInstance()->AddCamera("subcam", camera2.get());

	// デフォルトカメラをメインに設定
	Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera("maincam");
}

void GamePlayScene::LoadSceneResources()
{
	// ロード処理全体の時間を計測する
	auto start = std::chrono::high_resolution_clock::now();
	LoadModel();
	auto end = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double, std::milli> duration = end - start;
	Engine::Base::Logger::Log("Total loading time: " + std::to_string(duration.count()) + " milliseconds");
}

void GamePlayScene::InitializeSceneObjects()
{
	// スカイボックス初期化
	skyBox = std::make_unique<Engine::Skybox::SkyBox>();
	skyBox->Initialize("Resources/textures/skybox/test.dds");

	// プレイヤーモデル初期化
    object3D = Engine::Graphics3D::Object3D::Create(
        Engine::Graphics3D::Object3DCommon::GetInstance(), "walk.gltf");
    object3D->SetLighting(true);
    object3D->SetPointLightEnable(false);
    object3D->SetDirectionalLightIntensity(1.0f);
    object3D->SetRotate(kPlayerInitialRotate);
    object3D->SetSkyboxFilePath(skyBox->GetTextureFilePath());

	// 地形モデル初期化
    terrain = Engine::Graphics3D::Object3D::Create(
        Engine::Graphics3D::Object3DCommon::GetInstance(), "terrain.obj");
    terrain->SetTransform(kTerrainTransform);
    terrain->SetLighting(true);
    terrain->SetDirectionalLightIntensity(1.0f);
    terrain->SetSkyboxFilePath(skyBox->GetTextureFilePath());

	// スプライト初期化
    sprite = std::make_unique<Engine::Graphics2D::Sprite>();
    sprite->Initialize(Engine::Graphics2D::SpriteCommon::GetInstance(), "Resources/textures/common/uvChecker.png");

	light = true;
}

void GamePlayScene::InitializeParticles()
{
	// パーティクル初期化
	Engine::Particle::ParticleManager::GetInstance()->CreateParticleGroup(
		"Particle1",
		"Resources/textures/common/gradationLine.png",
		Engine::Particle::VerticesType::Cylinder,
		std::make_unique<Engine::Particle::MagicCircleBehavior>());
	Engine::Particle::ParticleManager::GetInstance()->CreateParticleGroup(
		"Particle2",
		"Resources/textures/common/gradationLine.png",
		Engine::Particle::VerticesType::Ring,
		std::make_unique<Engine::Particle::AttackBehavior>());

	particleEmitter = std::make_unique<Engine::Particle::ParticleEmitter>(
		kEmitterPosition, kEmitterFrequency, kEmitterStartTime, kAttackParticleEmitCount, "Particle2");
	particleEmitter2 = std::make_unique<Engine::Particle::ParticleEmitter>(
		kEmitterPosition, kEmitterFrequency, kEmitterStartTime, kMagicCircleEmitCount, "Particle1");
}

void GamePlayScene::Finalize()
{
	// カメラ破棄
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera("maincam");
	Engine::CameraSystem::CameraManager::GetInstance()->RemoveCamera("subcam");
}

void GamePlayScene::Update()
{
#ifndef KUBO_RUNTIME_ONLY
    const bool editorFrame = HasEditorFrame();
#endif
    simulationDeltaTime_ = SimulationDeltaTime(SceneManager::GetInstance()->GetFrameDeltaTime());
    if (simulationDeltaTime_ > 0.0f) {
#ifndef KUBO_RUNTIME_ONLY
        if (!editorFrame || (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())) UpdateInput();
#else
        UpdateInput();
#endif
    }
	UpdateSceneObjects();
    stepPending_ = false;

#ifndef KUBO_RUNTIME_ONLY
    if (editorFrame) {
        const auto beforeUi = CaptureScene();
        historyAction_ = false;
        DrawDebugUi();
        if (!historyAction_ && playback_ == Playback::Editing) { RecordEdit(beforeUi, CaptureScene(), !ImGui::IsAnyItemActive()); }
        if (playback_ == Playback::Editing) { UpdateAutosave(SceneManager::GetInstance()->GetFrameDeltaTime()); }
    }
#endif
}

void GamePlayScene::UpdateInput()
{
    if (!controlState_) {
        controlState_ = std::make_unique<IdlePlayerControlState>();
    }

    // 入力状態に応じて操作状態オブジェクトへ委譲し、必要なら状態遷移する
    std::unique_ptr<PlayerControlState> nextState = controlState_->Update(*this, *Engine::InputSystem::Input::GetInstance());
    if (nextState) {
        controlState_ = std::move(nextState);
    }
}

void GamePlayScene::ApplyMoveInput(float deltaX, float deltaZ)
{
    Vector3 translate = object3D->GetTransform().translate;
    translate.x += deltaX;
    translate.z += deltaZ;
    object3D->SetTranslate(translate);
}

void GamePlayScene::ApplyScaleInput(float scaleDelta)
{
    object3D->SetScale(object3D->GetTransform().scale + Vector3(scaleDelta, scaleDelta, scaleDelta));
}

void GamePlayScene::ApplyRotateInput(float rotateDeltaY)
{
    object3D->SetRotate(object3D->GetTransform().rotate + Vector3(0.0f, rotateDeltaY, 0.0f));
}

void GamePlayScene::UpdateToggleFlag(Engine::InputSystem::Input& input)
{
    if (input.TriggerGamePadButton(XINPUT_GAMEPAD_A) || input.TriggerAction("toggle")) {
        number = !number;
    }
}

void GamePlayScene::UpdateSceneObjects()
{
    skyBox->Update();
    Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->Update();

    const float deltaTime = simulationDeltaTime_;
    object3D->Update(deltaTime);
    terrain->Update(deltaTime);

    particleEmitter->Update(deltaTime);
    particleEmitter2->Update(deltaTime);

    sprite->Update();
}

#ifndef KUBO_RUNTIME_ONLY
void GamePlayScene::DrawDebugUi()
{
    if (!ImGui::Begin("シーン固有デバッグ")) { ImGui::End(); return; }
    ImGui::Text("number %d", number);
    ImGui::Text("Control State: %s", controlState_ ? controlState_->GetName() : "None");

    DrawSceneControlUi();
    if (ImGui::CollapsingHeader("Sample objects")) {
        DrawHierarchyUi();
        DrawInspectorUi();
    }
    DrawCameraControlUi();
    DrawObjectControlUi();
    DrawSpriteControlUi();
    DrawLightControlUi();
    DrawParticleControlUi();
    DrawEnvironmentMapUi();
    ImGui::End();
}

void GamePlayScene::DrawSceneControlUi()
{
    DrawSceneDocumentUi();
    if (!ImGui::CollapsingHeader("Scene Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::BeginDisabled(HasUnsavedScene());
    if (ImGui::Button("GameClearScene")) {
        SceneManager::GetInstance()->ChangeScene("GAMECLEAR");
    }
    if (ImGui::Button("GameOverScene")) {
        SceneManager::GetInstance()->ChangeScene("GAMEOVER");
    }
    ImGui::EndDisabled();
}

void GamePlayScene::DrawCameraControlUi()
{
    if (!ImGui::CollapsingHeader("Camera Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (ImGui::Button("Switch to Main Camera")) {
        Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera("maincam");
    }
    if (ImGui::Button("Switch to Sub Camera")) {
        Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera("subcam");
    }

    Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
    EulerTransform cameraTransform = activeCamera->GetTransform();
    if (ImGui::DragFloat3("Camera Position", &cameraTransform.translate.x, 0.01f)) {
        activeCamera->SetTranslate(cameraTransform.translate);
    }
    if (ImGui::DragFloat3("Camera Rotation", &cameraTransform.rotate.x, 0.01f)) {
        activeCamera->SetRotate(cameraTransform.rotate);
    }
}

void GamePlayScene::DrawObjectControlUi()
{
    if (!ImGui::CollapsingHeader("Object3D", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    EulerTransform transform = object3D->GetTransform();
    if (ImGui::DragFloat3("Object Position", &transform.translate.x, 0.01f)) {
        object3D->SetTransform(transform);
    }
    if (ImGui::DragFloat3("Object Rotation", &transform.rotate.x, 0.01f)) {
        object3D->SetTransform(transform);
    }
    if (ImGui::DragFloat3("Object Scale", &transform.scale.x, 0.01f)) {
        object3D->SetTransform(transform);
    }

    Vector4 color = object3D->GetColor();
    if (ImGui::ColorEdit4("Object Color", &color.x)) {
        object3D->SetColor(color);
    }

    if (ImGui::Checkbox("Enable Lighting", &light)) {
        object3D->SetLighting(light);
    }
}

void GamePlayScene::DrawSpriteControlUi()
{
    if (!ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    Vector2 position = sprite->GetPosition();
    if (ImGui::DragFloat2("Sprite Position", &position.x, 0.01f)) {
        sprite->SetPosition(position);
    }

    float rotation = sprite->GetRotation();
    if (ImGui::DragFloat("Sprite Rotation", &rotation, 0.01f)) {
        sprite->SetRotation(rotation);
    }

    Vector2 scale = sprite->GetSize();
    if (ImGui::DragFloat2("Sprite Scale", &scale.x, 0.01f)) {
        sprite->SetSize(scale);
    }

    Vector4 color = sprite->GetColor();
    if (ImGui::ColorEdit4("Sprite Color", &color.x)) {
        sprite->SetColor(color);
    }
}

void GamePlayScene::DrawLightControlUi()
{
    DrawDirectionalLightUi();
    DrawPointLightUi();
    DrawSpotLightUi();
}

void GamePlayScene::DrawDirectionalLightUi()
{
    if (!ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    Vector4 color = object3D->GetDirectionalLight().color;
    Vector3 direction = object3D->GetDirectionalLight().direction;
    float intensity = object3D->GetDirectionalLight().intensity;

    if (ImGui::ColorEdit4("Color", &color.x)) {
        object3D->SetDirectionalLightColor(color);
    }
    if (ImGui::DragFloat3("Direction", &direction.x, 0.01f)) {
        object3D->SetDirectionalLightDirection(direction);
    }
    if (ImGui::DragFloat("Intensity", &intensity, 0.01f)) {
        object3D->SetDirectionalLightIntensity(intensity);
    }
    if (ImGui::Checkbox("Enable DirectionalLight", &directionLight)) {
        object3D->SetDirectionalLightEnable(directionLight);
    }
}

void GamePlayScene::DrawPointLightUi()
{
    if (!ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    Vector4 color = object3D->GetPointLight().color;
    Vector3 position = object3D->GetPointLight().position;
    float intensity = object3D->GetPointLight().intensity;
    float decay = object3D->GetPointLightDecay();
    float radius = object3D->GetPointLightRadius();
    DrawPointLightToggleUi();
    DrawPointLightSettingsUi(color, position, intensity, decay, radius);
}

void GamePlayScene::DrawPointLightToggleUi()
{
    if (ImGui::Checkbox("Enable PointLight", &pointLight)) {
        object3D->SetPointLightEnable(pointLight);
        terrain->SetPointLightEnable(pointLight);
    }
}

void GamePlayScene::DrawPointLightSettingsUi(Vector4& color, Vector3& position, float& intensity, float& decay, float& radius)
{
    if (ImGui::ColorEdit4("pointColor", &color.x)) {
        object3D->SetPointLightColor(color);
    }
    if (ImGui::DragFloat3("pointPosition", &position.x, 0.01f)) {
        object3D->SetPointLightPosition(position);
        terrain->SetPointLightPosition(position);
    }
    if (ImGui::DragFloat("pointIntensity", &intensity, 0.01f)) {
        object3D->SetPointLightIntensity(intensity);
        terrain->SetPointLightIntensity(intensity);
    }
    if (ImGui::DragFloat("pointRadius", &radius, 0.01f)) {
        object3D->SetPointLightRadius(radius);
        terrain->SetPointLightRadius(radius);
    }
    if (ImGui::DragFloat("pointDecay", &decay, 0.01f)) {
        object3D->SetPointLightDecay(decay);
        terrain->SetPointLightDecay(decay);
    }
}

void GamePlayScene::DrawSpotLightUi()
{
    if (!ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    Vector4 color = object3D->GetSpotLight().color;
    Vector3 position = object3D->GetSpotLight().position;
    Vector3 direction = object3D->GetSpotLight().direction;
    float intensity = object3D->GetSpotLight().intensity;
    float distance = object3D->GetSpotLight().distance;
    float decay = object3D->GetSpotLight().decay;
    float coneAngleCos = object3D->GetSpotLight().coneAngleCos;
    float cosFalloffStart = object3D->GetSpotLight().cosFalloffStart;
    DrawSpotLightToggleUi();
    DrawSpotLightBasicSettingsUi(color, position, direction, intensity, distance);
    DrawSpotLightFalloffSettingsUi(decay, coneAngleCos, cosFalloffStart);
}

void GamePlayScene::DrawSpotLightToggleUi()
{
    if (ImGui::Checkbox("Enable SpotLight", &spotLight)) {
        object3D->SetSpotLightEnable(spotLight);
        terrain->SetSpotLightEnable(spotLight);
    }
}

void GamePlayScene::DrawSpotLightBasicSettingsUi(Vector4& color, Vector3& position, Vector3& direction, float& intensity, float& distance)
{
    if (ImGui::ColorEdit4("spotColor", &color.x)) {
        object3D->SetSpotLightColor(color);
        terrain->SetSpotLightColor(color);
    }
    if (ImGui::DragFloat3("spotPosition", &position.x, 0.01f)) {
        object3D->SetSpotLightPosition(position);
        terrain->SetSpotLightPosition(position);
    }
    if (ImGui::DragFloat3("spotDirection", &direction.x, 0.01f)) {
        object3D->SetSpotLightDirection(direction);
        terrain->SetSpotLightDirection(direction);
    }
    if (ImGui::DragFloat("spotIntensity", &intensity, 0.01f)) {
        object3D->SetSpotLightIntensity(intensity);
        terrain->SetSpotLightIntensity(intensity);
    }
    if (ImGui::DragFloat("spotDistance", &distance, 0.01f)) {
        object3D->SetSpotLightDistance(distance);
        terrain->SetSpotLightDistance(distance);
    }
}

void GamePlayScene::DrawSpotLightFalloffSettingsUi(float& decay, float& coneAngleCos, float& cosFalloffStart)
{
    if (ImGui::DragFloat("spotDecay", &decay, 0.01f)) {
        object3D->SetSpotLightDecay(decay);
        terrain->SetSpotLightDecay(decay);
    }
    if (ImGui::DragFloat("spotConeAngleCos", &coneAngleCos, 0.01f)) {
        object3D->SetSpotLightConeAngleCos(coneAngleCos);
        terrain->SetSpotLightConeAngleCos(coneAngleCos);
    }
    if (ImGui::DragFloat("spotCosFalloffStart", &cosFalloffStart, 0.01f)) {
        object3D->SetSpotLightCosFalloffStart(cosFalloffStart);
        terrain->SetSpotLightCosFalloffStart(cosFalloffStart);
    }
}

void GamePlayScene::DrawParticleControlUi()
{
    if (!ImGui::CollapsingHeader("ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Text("ParticleEmitter");
    if (ImGui::Button("Emit")) {
        particleEmitter->Emit();
    }

    Vector3 position = particleEmitter->GetPosition();
    if (ImGui::DragFloat3("Position", &position.x, 0.01f)) {
        particleEmitter->SetPosition(position);
    }
}

void GamePlayScene::DrawEnvironmentMapUi()
{
    if (!ImGui::CollapsingHeader("Environment Map")) return;

    auto skyboxScale = skyBox->GetScale();
    if (ImGui::DragFloat3("Skybox Scale", &skyboxScale.x, 0.01f, 0.0f, 10.0f)) skyBox->SetScale(skyboxScale);

    float reflectionStrength = object3D->GetEnvironmentReflectionStrength();
    float roughness = object3D->GetEnvironmentRoughness();

    if (ImGui::DragFloat("Reflection Strength", &reflectionStrength, 0.01f, 0.0f, 1.0f)) {
        object3D->SetEnvironmentReflectionStrength(reflectionStrength);
        terrain->SetEnvironmentReflectionStrength(reflectionStrength);
    }

    if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
        object3D->SetEnvironmentRoughness(roughness);
        terrain->SetEnvironmentRoughness(roughness);
    }
}

#endif
void GamePlayScene::Draw()
{
    skyBox->Draw();

#pragma region 3Dオブジェクト描画
	// 3Dオブジェクト描画
	Engine::Graphics3D::Object3DCommon::GetInstance()->CommonDraw();
	terrain->Draw();

	Engine::Graphics3D::Object3DCommon::GetInstance()->SkinningCommonDraw();
	object3D->DrawSkinning();
#pragma endregion

    Engine::Particle::ParticleManager::GetInstance()->Draw();

#pragma region スプライト描画
	// スプライト描画
	Engine::Graphics2D::SpriteCommon::GetInstance()->CommonDraw();
    sprite->Draw();
#pragma endregion
}

EulerTransform GamePlayScene::GetPlayerTransform() const
{
    return object3D ? object3D->GetTransform() : EulerTransform{};
}

void GamePlayScene::SetPlayerTransform(const EulerTransform& transform)
{
    if (object3D) {
        object3D->SetTransform(transform);
    }
}

void GamePlayScene::LoadModel()
{
	// 必要なモデルを事前ロード
	Engine::Graphics3D::ModelManager::GetInstance()->LoadModels({
		"axis.obj",
		"plane.gltf",
		"sphere.obj",
		"terrain.obj",
		"animationfly.gltf",
		"sphere.gltf",
		"player.gltf",
		"walk.gltf",
		"testanimation.gltf",
	});
}

}
