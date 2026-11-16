#include "GamePlayScene.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Sprite.h"
#include "ParticleEmitter.h"
#include "PlayerControlState.h"
#include "Audio.h"
#include "SkyBox.h"
#include "Logger.h"
#include "IconsFontAwesome5.h"
#include "engine/base/AssetPath.h"
#include <imgui.h>
#ifndef KUBO_RUNTIME_ONLY
#include "engine/editor/DocumentShortcuts.h"
#endif
#include <cmath>

namespace Engine::Scene {
namespace {
using J = SceneValue;
J Vec(const Vector3& v) { return crude_json::array{double(v.x), double(v.y), double(v.z)}; }
J Vec(const Vector4& v) { return crude_json::array{double(v.x), double(v.y), double(v.z), double(v.w)}; }
J Vec(const Vector2& v) { return crude_json::array{double(v.x), double(v.y)}; }
float Num(const J& j) { return static_cast<float>(j.get<crude_json::number>()); }
Vector3 V3(const J& j) { return {Num(j[size_t(0)]), Num(j[size_t(1)]), Num(j[size_t(2)])}; }
Vector4 V4(const J& j) { return {Num(j[size_t(0)]), Num(j[size_t(1)]), Num(j[size_t(2)]), Num(j[size_t(3)])}; }
Vector2 V2(const J& j) { return {Num(j[size_t(0)]), Num(j[size_t(1)])}; }
bool Bool(const J& j) { return j.get<crude_json::boolean>(); }
J Transform(const EulerTransform& t)
{
    J j; j["position"] = Vec(t.translate); j["rotation"] = Vec(t.rotate); j["scale"] = Vec(t.scale); return j;
}
EulerTransform ReadTransform(const J& j) { return {V3(j["scale"]), V3(j["rotation"]), V3(j["position"])}; }

J Object(const Engine::Graphics3D::Object3D& o, const char* asset)
{
    J j;
    j["model"] = asset;
    j["transform"] = Transform(o.GetTransform());
    j["color"] = Vec(o.GetColor());
    j["lighting"] = o.GetLighting();
    j["reflection"] = double(o.GetEnvironmentReflectionStrength());
    j["roughness"] = double(o.GetEnvironmentRoughness());
    const auto& d = o.GetDirectionalLight();
    j["directional"]["color"] = Vec(d.color);
    j["directional"]["direction"] = Vec(d.direction);
    j["directional"]["intensity"] = double(d.intensity);
    j["directional"]["enabled"] = d.enable != 0;
    const auto& p = o.GetPointLight();
    j["point"]["color"] = Vec(p.color);
    j["point"]["position"] = Vec(p.position);
    j["point"]["intensity"] = double(p.intensity);
    j["point"]["radius"] = double(p.radius);
    j["point"]["decay"] = double(p.decay);
    j["point"]["enabled"] = p.enable != 0;
    const auto& s = o.GetSpotLight();
    j["spot"]["color"] = Vec(s.color);
    j["spot"]["position"] = Vec(s.position);
    j["spot"]["direction"] = Vec(s.direction);
    j["spot"]["intensity"] = double(s.intensity);
    j["spot"]["distance"] = double(s.distance);
    j["spot"]["decay"] = double(s.decay);
    j["spot"]["cone"] = double(s.coneAngleCos);
    j["spot"]["falloff"] = double(s.cosFalloffStart);
    j["spot"]["enabled"] = s.enable != 0;
    return j;
}

void ApplyObject(Engine::Graphics3D::Object3D& o, const J& j)
{
    o.SetTransform(ReadTransform(j["transform"]));
    o.SetColor(V4(j["color"]));
    o.SetLighting(Bool(j["lighting"]));
    o.SetEnvironmentReflectionStrength(Num(j["reflection"]));
    o.SetEnvironmentRoughness(Num(j["roughness"]));
    auto d = o.GetDirectionalLight();
    const auto& jd = j["directional"];
    d.color = V4(jd["color"]); d.direction = V3(jd["direction"]);
    d.intensity = Num(jd["intensity"]); d.enable = Bool(jd["enabled"]);
    o.SetDirectionalLight(d);
    auto p = o.GetPointLight();
    const auto& jp = j["point"];
    p.color = V4(jp["color"]); p.position = V3(jp["position"]);
    p.intensity = Num(jp["intensity"]); p.radius = Num(jp["radius"]);
    p.decay = Num(jp["decay"]); p.enable = Bool(jp["enabled"]);
    o.SetPointLight(p);
    auto s = o.GetSpotLight();
    const auto& js = j["spot"];
    s.color = V4(js["color"]); s.position = V3(js["position"]); s.direction = V3(js["direction"]);
    s.intensity = Num(js["intensity"]); s.distance = Num(js["distance"]); s.decay = Num(js["decay"]);
    s.coneAngleCos = Num(js["cone"]); s.cosFalloffStart = Num(js["falloff"]); s.enable = Bool(js["enabled"]);
    o.SetSpotLight(s);
}

J Emitter(const Engine::Particle::ParticleEmitter& e)
{
    J j; j["position"] = Vec(e.GetPosition()); j["frequency"] = double(e.GetFrequency());
    j["count"] = double(e.GetCount()); j["group"] = e.GetName(); return j;
}
void ApplyEmitter(Engine::Particle::ParticleEmitter& e, const J& j)
{
    e.SetPosition(V3(j["position"])); e.SetFrequency(Num(j["frequency"]));
    e.SetCount(static_cast<uint32_t>(Num(j["count"])));
}
#ifndef KUBO_RUNTIME_ONLY
bool ToolButton(const char* icon, const char* tooltip)
{
    const bool pressed = ImGui::Button(icon);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tooltip); }
    return pressed;
}
#endif
}

SceneValue GamePlayScene::CaptureScene() const
{
    J j;
    j["version"] = 1.0; j["scene"] = "GAMEPLAY";
    j["objects"]["player"] = Object(*object3D, "walk.gltf");
    j["objects"]["terrain"] = Object(*terrain, "terrain.obj");
    j["cameras"]["main"] = Transform(camera1->GetTransform());
    j["cameras"]["sub"] = Transform(camera2->GetTransform());
    j["cameras"]["subActive"] = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera() == camera2.get();
    j["sprite"]["texture"] = "Resources/textures/common/uvChecker.png";
    j["sprite"]["position"] = Vec(sprite->GetPosition());
    j["sprite"]["size"] = Vec(sprite->GetSize());
    j["sprite"]["rotation"] = double(sprite->GetRotation());
    j["sprite"]["color"] = Vec(sprite->GetColor());
    j["emitters"]["attack"] = Emitter(*particleEmitter);
    j["emitters"]["circle"] = Emitter(*particleEmitter2);
    j["skybox"]["texture"] = skyBox->GetTextureFilePath();
    j["skybox"]["scale"] = Vec(skyBox->GetScale());
    const auto& l = Engine::Graphics3D::Object3DCommon::GetInstance()->GetSceneLight();
    j["sceneLight"]["color"] = Vec(l.color); j["sceneLight"]["direction"] = Vec(l.direction);
    j["sceneLight"]["intensity"] = double(l.intensity); j["sceneLight"]["ambientColor"] = Vec(l.ambientColor);
    j["sceneLight"]["ambientIntensity"] = double(l.ambientIntensity);
    j["sceneLight"]["specularStrength"] = double(l.specularStrength); j["sceneLight"]["enabled"] = l.enable != 0;
    return j;
}

bool GamePlayScene::RestoreScene(const SceneValue& state, std::string& error)
{
    if (!ValidateScene(state, error)) { return false; }
    ApplyScene(state);
    return true;
}

bool GamePlayScene::ValidateScene(const SceneValue& state, std::string& error) const
{
    if (!SceneDocument::Validate(state, sceneSchema_, error)) { return false; }
    for (const char* name : {"attack", "circle"}) {
        const auto& e = state["emitters"][name];
        const double count = e["count"].get<crude_json::number>();
        if (count < 0 || count > 100000 || count != std::floor(count) || Num(e["frequency"]) < 0) {
            error = "Invalid emitter frequency or count"; return false;
        }
    }
    return true;
}

void GamePlayScene::ApplyScene(const SceneValue& state)
{
    ApplyObject(*object3D, state["objects"]["player"]);
    ApplyObject(*terrain, state["objects"]["terrain"]);
    const auto main = ReadTransform(state["cameras"]["main"]);
    const auto sub = ReadTransform(state["cameras"]["sub"]);
    camera1->SetTranslate(main.translate); camera1->SetRotate(main.rotate);
    camera2->SetTranslate(sub.translate); camera2->SetRotate(sub.rotate);
    camera1->Update(); camera2->Update();
    Engine::CameraSystem::CameraManager::GetInstance()->SetActiveCamera(Bool(state["cameras"]["subActive"]) ? "subcam" : "maincam");
    sprite->SetPosition(V2(state["sprite"]["position"])); sprite->SetSize(V2(state["sprite"]["size"]));
    sprite->SetRotation(Num(state["sprite"]["rotation"])); sprite->SetColor(V4(state["sprite"]["color"]));
    ApplyEmitter(*particleEmitter, state["emitters"]["attack"]);
    ApplyEmitter(*particleEmitter2, state["emitters"]["circle"]);
    skyBox->SetScale(V3(state["skybox"]["scale"]));
    auto* common = Engine::Graphics3D::Object3DCommon::GetInstance();
    auto l = common->GetSceneLight(); const auto& jl = state["sceneLight"];
    l.color = V4(jl["color"]); l.direction = V3(jl["direction"]); l.intensity = Num(jl["intensity"]);
    l.ambientColor = V4(jl["ambientColor"]); l.ambientIntensity = Num(jl["ambientIntensity"]);
    l.specularStrength = Num(jl["specularStrength"]); l.enable = Bool(jl["enabled"]); common->SetSceneLight(l);
    light = object3D->GetLighting(); directionLight = object3D->GetDirectionalLight().enable != 0;
    pointLight = object3D->GetPointLight().enable != 0; spotLight = object3D->GetSpotLight().enable != 0;
    object3D->Update(0.0f); terrain->Update(0.0f); sprite->Update(); skyBox->Update();
}

bool GamePlayScene::SaveScene(const std::filesystem::path& path)
{
    if (playback_ != Playback::Editing && !playSnapshot_.is_null()) {
        sceneFileStatus_ = "Stop playback before saving the scene"; return false;
    }
    std::string error;
    const auto state = CaptureScene();
    const bool ok = ValidateScene(state, error) && SceneDocument::Save(path, state, sceneSchema_, error);
    if (ok && path == scenePath_) {
        sceneDirty_ = false;
        std::error_code ignored;
        std::filesystem::remove(SceneDocument::AutosavePath(scenePath_), ignored);
        recoveryAvailable_ = false;
    }
    sceneFileStatus_ = ok ? "Scene saved" : "Save failed: " + error;
    Engine::Base::Logger::Log(sceneFileStatus_, ok ? Engine::Base::Logger::Level::Info : Engine::Base::Logger::Level::Error, path);
    return ok;
}

bool GamePlayScene::LoadScene(const std::filesystem::path& path)
{
    if (playback_ != Playback::Editing && !playSnapshot_.is_null()) {
        sceneFileStatus_ = "Stop playback before loading the scene"; return false;
    }
    SceneValue state; std::string error;
    const bool ok = SceneDocument::Load(path, sceneSchema_, state, error) && RestoreScene(state, error);
    if (ok) { sceneDirty_ = path != scenePath_; history_.Clear(); historyAction_ = true; }
    sceneFileStatus_ = ok ? "Scene loaded" : "Load failed: " + error;
    Engine::Base::Logger::Log(sceneFileStatus_, ok ? Engine::Base::Logger::Level::Info : Engine::Base::Logger::Level::Error, path);
    return ok;
}

#ifndef KUBO_RUNTIME_ONLY
void GamePlayScene::DrawSceneDocumentUi()
{
    if (ToolButton(ICON_FA_PLAY "##Play", "Play / Resume")) { Play(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(playback_ != Playback::Playing);
    if (ToolButton(ICON_FA_PAUSE "##Pause", "Pause")) { Pause(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(playback_ == Playback::Editing);
    if (ToolButton(ICON_FA_STOP "##Stop", "Stop")) { Stop(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(playback_ != Playback::Paused);
    if (ToolButton(ICON_FA_STEP_FORWARD "##Step", "Step one frame")) { Step(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::TextUnformatted(playback_ == Playback::Editing ? "Editing" : playback_ == Playback::Playing ? "Playing" : "Paused");
    ImGui::BeginDisabled(playback_ != Playback::Editing);
    const auto shortcut=Engine::Editor::DocumentShortcuts::Take("シーン固有デバッグ",playback_==Playback::Editing);
    if(shortcut==Engine::Editor::DocumentShortcuts::Command::Undo)UndoEdit();
    else if(shortcut==Engine::Editor::DocumentShortcuts::Command::Redo)RedoEdit();
    ImGui::BeginDisabled(!history_.CanUndo());
    if (ToolButton(ICON_FA_UNDO "##UndoScene", "Undo")) { UndoEdit(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanRedo());
    if (ToolButton(ICON_FA_REDO "##RedoScene", "Redo")) { RedoEdit(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ToolButton(ICON_FA_SAVE "##SaveScene", "Save scene")) { SaveScene(scenePath_); }
    ImGui::SameLine();
    if (ToolButton(ICON_FA_FOLDER_OPEN "##LoadScene", "Reload saved scene")) { LoadScene(scenePath_); }
    if (sceneDirty_) { ImGui::SameLine(); ImGui::TextUnformatted("Modified"); }
    if (recoveryAvailable_) {
        if (ImGui::Button("Recover autosave")) {
            if (LoadScene(SceneDocument::AutosavePath(scenePath_))) { recoveryAvailable_ = false; }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard autosave")) {
            std::error_code error;
            std::filesystem::remove(SceneDocument::AutosavePath(scenePath_), error);
            if (!error) { recoveryAvailable_ = false; }
            else { sceneFileStatus_ = error.message(); }
        }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", Engine::Base::PathToUtf8(scenePath_).c_str());
    if (!sceneFileStatus_.empty()) { ImGui::TextWrapped("%s", sceneFileStatus_.c_str()); }
}

void GamePlayScene::DrawHierarchyUi()
{
    const std::pair<const char*, Selection> entries[] = {
        {"Player / walk.gltf", Selection::Player}, {"Terrain / terrain.obj", Selection::Terrain},
        {"Main Camera", Selection::MainCamera}, {"Sub Camera", Selection::SubCamera},
        {"Sprite / uvChecker.png", Selection::Sprite}, {"Emitter / Attack", Selection::AttackEmitter},
        {"Emitter / Circle", Selection::CircleEmitter},
    };
    for (const auto& [label, selection] : entries) {
        if (ImGui::Selectable(label, selection_ == selection)) { selection_ = selection; }
    }
}

void GamePlayScene::DrawInspectorUi()
{
    ImGui::Separator();
    auto drawTransform = [](EulerTransform transform, const char* id, auto&& apply) {
        if (ImGui::DragFloat3((std::string("Position##") + id).c_str(), &transform.translate.x, 0.01f)) { apply(transform); }
        if (ImGui::DragFloat3((std::string("Rotation##") + id).c_str(), &transform.rotate.x, 0.01f)) { apply(transform); }
        if (ImGui::DragFloat3((std::string("Scale##") + id).c_str(), &transform.scale.x, 0.01f)) { apply(transform); }
    };
    switch (selection_) {
    case Selection::Player:
        ImGui::TextUnformatted("Player");
        drawTransform(object3D->GetTransform(), "player", [&](const EulerTransform& t) { object3D->SetTransform(t); });
        break;
    case Selection::Terrain:
        ImGui::TextUnformatted("Terrain");
        drawTransform(terrain->GetTransform(), "terrain", [&](const EulerTransform& t) { terrain->SetTransform(t); });
        break;
    case Selection::MainCamera:
        ImGui::TextUnformatted("Main Camera");
        drawTransform(camera1->GetTransform(), "main_camera", [&](const EulerTransform& t) { camera1->SetTranslate(t.translate); camera1->SetRotate(t.rotate); });
        break;
    case Selection::SubCamera:
        ImGui::TextUnformatted("Sub Camera");
        drawTransform(camera2->GetTransform(), "sub_camera", [&](const EulerTransform& t) { camera2->SetTranslate(t.translate); camera2->SetRotate(t.rotate); });
        break;
    case Selection::Sprite: {
        ImGui::TextUnformatted("Sprite");
        Vector2 position = sprite->GetPosition(), size = sprite->GetSize();
        float rotation = sprite->GetRotation();
        if (ImGui::DragFloat2("Position##sprite", &position.x, 0.01f)) { sprite->SetPosition(position); }
        if (ImGui::DragFloat2("Size##sprite", &size.x, 0.01f)) { sprite->SetSize(size); }
        if (ImGui::DragFloat("Rotation##sprite", &rotation, 0.01f)) { sprite->SetRotation(rotation); }
        break;
    }
    case Selection::AttackEmitter:
    case Selection::CircleEmitter: {
        auto* emitter = selection_ == Selection::AttackEmitter ? particleEmitter.get() : particleEmitter2.get();
        ImGui::TextUnformatted(selection_ == Selection::AttackEmitter ? "Attack Emitter" : "Circle Emitter");
        Vector3 position = emitter->GetPosition(); float frequency = emitter->GetFrequency();
        int count = static_cast<int>(emitter->GetCount());
        if (ImGui::DragFloat3("Position##emitter", &position.x, 0.01f)) { emitter->SetPosition(position); }
        if (ImGui::DragFloat("Frequency##emitter", &frequency, 0.01f, 0.001f, 60.0f)) { emitter->SetFrequency(frequency); }
        if (ImGui::DragInt("Count##emitter", &count, 1, 0, 100000)) { emitter->SetCount(static_cast<uint32_t>(count)); }
        break;
    }
    }
}

#endif
void GamePlayScene::RecordEdit(const SceneValue& before, const SceneValue& after, bool finish)
{
    if (before.dump() != after.dump()) { history_.Record(before, after); sceneDirty_ = true; }
    if (finish) { history_.Commit(); }
}

bool GamePlayScene::UndoEdit()
{
    if (playback_ != Playback::Editing) { return false; }
    history_.Commit();
    if (!history_.CanUndo()) { return false; }
    std::string error;
    if (!RestoreScene(history_.UndoValue(CaptureScene()), error)) { sceneFileStatus_ = error; return false; }
    history_.DidUndo(); historyAction_ = true; sceneDirty_ = true;
    return true;
}

bool GamePlayScene::RedoEdit()
{
    if (playback_ != Playback::Editing) { return false; }
    if (!history_.CanRedo()) { return false; }
    std::string error;
    if (!RestoreScene(history_.RedoValue(CaptureScene()), error)) { sceneFileStatus_ = error; return false; }
    history_.DidRedo(); historyAction_ = true; sceneDirty_ = true;
    return true;
}

float GamePlayScene::SimulationDeltaTime(float realDeltaTime) const
{
    if (playback_ == Playback::Playing) { return realDeltaTime; }
    return stepPending_ ? 1.0f / 60.0f : 0.0f;
}

bool GamePlayScene::Play()
{
    if (playback_ == Playback::Playing) { return false; }
    if (playback_ == Playback::Editing) {
        std::string error;
        auto snapshot = CaptureScene();
        if (!ValidateScene(snapshot, error)) { sceneFileStatus_ = error; return false; }
        history_.Commit(); playSnapshot_ = std::move(snapshot);
        authoredDirty_ = sceneDirty_; numberBeforePlay_ = number;
        object3D->ResetAnimationTime(); terrain->ResetAnimationTime();
        particleEmitter->SetFrequencyTime(0.0f); particleEmitter2->SetFrequencyTime(0.0f);
        Engine::Particle::ParticleManager::GetInstance()->ClearParticles("Particle1");
        Engine::Particle::ParticleManager::GetInstance()->ClearParticles("Particle2");
    }
    playback_ = Playback::Playing; stepPending_ = false; historyAction_ = true;
    Engine::AudioSystem::Audio::GetInstance()->ResumeAudio();
    return true;
}

void GamePlayScene::Pause()
{
    if (playback_ == Playback::Playing) {
        playback_ = Playback::Paused;
        Engine::AudioSystem::Audio::GetInstance()->PauseAudio();
    }
}

bool GamePlayScene::Stop()
{
    if (playback_ == Playback::Editing || playSnapshot_.is_null()) { return false; }
    std::string error;
    if (!RestoreScene(playSnapshot_, error)) { sceneFileStatus_ = error; return false; }
    playback_ = Playback::Editing; stepPending_ = false; historyAction_ = true;
    sceneDirty_ = authoredDirty_; number = numberBeforePlay_;
    controlState_ = std::make_unique<IdlePlayerControlState>();
    object3D->ResetAnimationTime(); terrain->ResetAnimationTime();
    object3D->Update(0.0f); terrain->Update(0.0f);
    particleEmitter->SetFrequencyTime(0.0f); particleEmitter2->SetFrequencyTime(0.0f);
    Engine::Particle::ParticleManager::GetInstance()->ClearParticles("Particle1");
    Engine::Particle::ParticleManager::GetInstance()->ClearParticles("Particle2");
    Engine::AudioSystem::Audio::GetInstance()->StopAudio();
    playSnapshot_ = SceneValue{};
    return true;
}

void GamePlayScene::UpdateAutosave(float deltaTime)
{
    if (!sceneDirty_ || recoveryAvailable_) { return; }
    autosaveTimer_ += deltaTime;
    if (autosaveTimer_ < 30.0f) { return; }
    autosaveTimer_ = 0.0f;
    SaveScene(SceneDocument::AutosavePath(scenePath_));
}
}
