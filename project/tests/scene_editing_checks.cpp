#include "GamePlayScene.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <cmath>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}
bool Equivalent(const Engine::Scene::SceneValue& a, const Engine::Scene::SceneValue& b)
{
    if (a.type() != b.type()) { return false; }
    if (a.is_number()) { return std::abs(a.get<crude_json::number>() - b.get<crude_json::number>()) < 0.000001; }
    if (a.is_object()) {
        for (const auto& [key, v] : a.get<crude_json::object>()) {
            if (!b.contains(key) || !Equivalent(v, b[key])) { return false; }
        }
        return a.get<crude_json::object>().size() == b.get<crude_json::object>().size();
    }
    if (a.is_array()) {
        const auto& aa = a.get<crude_json::array>(); const auto& bb = b.get<crude_json::array>();
        if (aa.size() != bb.size()) { return false; }
        for (size_t i = 0; i < aa.size(); ++i) { if (!Equivalent(aa[i], bb[i])) { return false; } }
        return true;
    }
    return a.dump() == b.dump();
}
}

void CheckSceneEditing(Engine::Scene::GamePlayScene& scene)
{
    const auto original = scene.CaptureScene();
    auto edited = original;
    edited["objects"]["player"]["transform"]["position"][size_t(0)] = 2.5;
    edited["objects"]["terrain"]["color"][size_t(1)] = 0.25;
    edited["cameras"]["subActive"] = true;
    edited["cameras"]["sub"]["position"][size_t(1)] = 8.0;
    edited["sprite"]["rotation"] = 0.5;
    edited["emitters"]["attack"]["count"] = 8.0;
    edited["sceneLight"]["intensity"] = 0.75;
    std::string error;
    Require(scene.RestoreScene(edited, error), error.c_str());
    Require(Equivalent(scene.CaptureScene(), edited), "Live scene did not apply all authored values");
    const auto path=scene.GetScenePath();
    Require(path.is_absolute(),"Live sample document path is not fixed");
    const auto previous=std::filesystem::current_path();
    struct RestoreDirectory {std::filesystem::path path;~RestoreDirectory(){std::error_code ignored;std::filesystem::current_path(path,ignored);}} restoreDirectory{previous};
    const auto unrelated=path.parent_path()/"Unrelated";std::filesystem::create_directories(unrelated);std::filesystem::current_path(unrelated);
    const auto autosave=Engine::Scene::SceneDocument::AutosavePath(path);
    Require(scene.SaveScene(autosave),"Live native autosave save failed");
    Require(scene.RestoreScene(original,error) && scene.LoadScene(autosave),"Live native autosave recovery failed");
    Require(Equivalent(scene.CaptureScene(),edited),"Live autosave recovery lost authored values");
    Require(scene.SaveScene(path), "Live scene save failed");
    Require(!std::filesystem::exists(autosave),"Primary save did not remove its native autosave companion");
    Require(!std::filesystem::exists(unrelated/"UserScenes"),"Live sample save followed changed cwd");
    Require(scene.RestoreScene(original, error), error.c_str());
    Require(scene.LoadScene(path), "Live scene load failed");
    Require(Equivalent(scene.CaptureScene(), edited), "Live scene roundtrip lost authored values");
    auto invalid = edited;
    invalid["emitters"]["attack"]["count"] = -1.0;
    Require(!scene.RestoreScene(invalid, error), "Invalid emitter count must be rejected");
    Require(Equivalent(scene.CaptureScene(), edited), "Invalid scene changed live objects");
    Require(scene.RestoreScene(original, error), error.c_str());
    Require(scene.GetPlayback() == Engine::Scene::GamePlayScene::Playback::Editing, "Scene must start in editing mode");
    Require(scene.Play(), "Play must enter playing mode");
    Require(scene.GetPlayback() == Engine::Scene::GamePlayScene::Playback::Playing, "Play state was not entered");
    Require(scene.SimulationDeltaTime(0.25f) == 0.25f, "Playing mode must advance simulation time");
    auto runtimeEdit = original;
    runtimeEdit["objects"]["player"]["transform"]["position"][size_t(0)] = 99.0;
    Require(scene.RestoreScene(runtimeEdit, error), error.c_str());
    scene.Pause();
    Require(scene.GetPlayback() == Engine::Scene::GamePlayScene::Playback::Paused, "Pause state was not entered");
    Require(scene.SimulationDeltaTime(0.25f) == 0.0f, "Paused mode must freeze simulation time");
    scene.Step();
    Require(scene.SimulationDeltaTime(0.25f) > 0.0f, "Step must advance one simulation frame");
    Require(scene.Stop(), "Stop must return to editing mode");
    Require(scene.GetPlayback() == Engine::Scene::GamePlayScene::Playback::Editing, "Stop did not return to editing mode");
    Require(Equivalent(scene.CaptureScene(), original), "Stop did not restore the edit snapshot");
    scene.RecordEdit(original, edited);
    Require(scene.RestoreScene(edited, error), error.c_str());
    Require(scene.UndoEdit(), "Live undo failed");
    Require(Equivalent(scene.CaptureScene(), original), "Undo did not restore authored state");
    Require(scene.RedoEdit(), "Live redo failed");
    Require(Equivalent(scene.CaptureScene(), edited), "Redo did not restore authored state");
    Require(scene.RestoreScene(original, error), error.c_str());
    std::filesystem::current_path(previous);
    std::cout<<"PASS: missing Unicode startup Scene path retained; native autosave recovery/save cleanup and changed-cwd isolation\n";
    std::cout << "PASS: live scene full snapshot roundtrip and transactional validation\n";
}
