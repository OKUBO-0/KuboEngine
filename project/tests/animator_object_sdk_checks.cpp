#include "Framework.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "Model.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "AssetPath.h"
#include "AnimatorControllerAsset.h"
#include "layered_object_fixture.h"
#include <fstream>
#include <d3d12sdklayers.h>
#include <Windows.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
static void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
class Probe final : public Engine::Base::Framework {
    std::unique_ptr<Engine::Graphics3D::Object3D> first_, second_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    bool checked_ = false, retired_ = false;
    void VerifyController(float step) {
        using namespace Engine;
        const auto root=GetProjectRoot();const auto file=root/"Resources/sdk-controller.animfsm";
        const auto write=[&](const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Require(!out.fail(),"SDK Controller write failed");};
        write(root/"ControllerObject.kubo",R"({"name":"SDK Object Controller","assetDirectory":"Resources","startupScene":"entry.scene"})");
        const std::string text=R"({"format":"kubo-animator-controller-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"enabled","type":"bool","default":false},{"name":"go","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":1,"loop":true},{"name":"Run","clip":"verified_walk","speed":2,"loop":true}],"transitions":[{"from":"Idle","to":"Run","blendSeconds":0.1,"exitTime":null,"reenter":false,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"enabled","op":"eq","value":true},{"parameter":"go","op":"set","value":true}]}]}]})";
        const std::string guid(32,'d');write(file,text);write(Assets::AssetRegistry::MetadataPath(file),"{\"version\":1,\"guid\":\""+guid+"\"}");
        Base::ProjectContext project;Assets::AssetRegistry registry;std::string error;
        Require(project.Open(root/"ControllerObject.kubo",error) && registry.Scan(root/"Resources",error,Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
        Require(first_->BindAnimationControllerAsset(project,registry,guid,error) && second_->BindAnimationControllerAsset(project,registry,guid,error),error.c_str());
        const auto revision=*first_->GetControllerAssetRevision();
        Require(revision.guid==guid && revision.contentHash.size()==64 && first_->GetControllerState()=="Idle","SDK GUID revision missing");
        Require(first_->SetControllerFloat("speed",1) && first_->SetControllerBool("enabled",true) && first_->SetControllerTrigger("go") && !first_->SetControllerBool("speed",true),"SDK typed Controller writes failed");
        Require(first_->SetControllerPlaybackRate(0),"SDK Controller Pause refused");first_->Update(step);
        Require(first_->GetControllerState()=="Idle" && first_->GetAnimationTime()==0,"SDK paused Controller consumed transition");
        Require(first_->SetControllerPlaybackRate(0.5f),"SDK Controller rate refused");first_->Update(step);
        Require(first_->GetControllerState()=="Run" && first_->IsAnimationBlending() && std::abs(first_->GetAnimationTime()-step)<1e-5f && second_->GetControllerState()=="Idle" && second_->GetAnimationTime()==0,"SDK Controller real transition/rate/peer failed");
        const auto retained=first_->GetAnimationTime();auto missing=crude_json::value::parse(text);missing["layers"][0]["states"][1]["clip"]="sdk-missing";write(file,missing.dump());
        Require(!first_->BindAnimationControllerAsset(project,registry,guid,error) && first_->GetControllerState()=="Run" && first_->GetAnimationTime()==retained && first_->GetControllerAssetRevision()->contentHash==revision.contentHash,"SDK incompatible GUID rebind changed live owner");
        auto overflow=crude_json::value::parse(text);overflow["layers"][0]["states"][1]["speed"]=double((std::numeric_limits<float>::max)());write(file,overflow.dump());
        Require(!first_->BindAnimationControllerAssetAtRate(project,registry,guid,2,false,error) && first_->GetControllerState()=="Run" &&
            first_->GetAnimationTime()==retained && first_->IsAnimationBlending() && first_->GetControllerAssetRevision()->contentHash==revision.contentHash &&
            second_->GetControllerState()=="Idle", "Paused future-rate overflow changed old graph/clock/blend/revision/peer");
        auto changed=crude_json::value::parse(text);changed["layers"][0]["states"][0]["speed"]=0.5;write(file,changed.dump());
        Require(!first_->BindAnimationControllerAsset(project,registry,guid,error,&revision) && first_->GetControllerState()=="Run","SDK stale revision rebind admitted");
        Require(first_->BindAnimationControllerAsset(project,registry,guid,error) && first_->GetControllerState()=="Idle" && first_->GetControllerAssetRevision()->contentHash!=revision.contentHash,"SDK explicit Controller reload failed");
        Require(first_->BindAnimationControllerAssetAtRate(project,registry,guid,.5f,false,error) && first_->GetControllerState()=="Idle" && first_->GetAnimationTime()==0,"Rate-aware paused reload failed");
        first_->Update(step);Require(first_->GetAnimationTime()==0,"Reload lost Pause");
        first_->SetAnimationClip("default");Require(first_->GetControllerState().empty() && !first_->GetControllerAssetRevision(),"SDK clip detach retained Controller");
        std::cout<<"PASS: SDK-only Object3D Controller GUID/revision/typed parameters/Pause/rate/real blend/peer/refusal/reload/detach\n";
    }
public:
    void Draw() override {}
    void Initialize() override {
        // Probe policy: inspect GPU messages even when linking the normal Release
        // Runtime, whose production build does not enable the debug layer.
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;
        Require(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Probe GPU debug layer unavailable");
        debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);
        Framework::Initialize();
        ShowWindow(winApp->GetHwnd(), SW_HIDE);
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))), "GPU debug queue unavailable");
        auto* models = Engine::Graphics3D::ModelManager::GetInstance();
        auto* common = Engine::Graphics3D::Object3DCommon::GetInstance();
        const auto root = Engine::Base::PathToUtf8(GetProjectRoot() / "Resources");
        first_ = Engine::Graphics3D::Object3D::Create(common, "walk.gltf", root);
        const auto sharedCount = models->GetCachedModelCount();
        second_ = Engine::Graphics3D::Object3D::Create(common, "walk.gltf", root);
        Require(models->GetCachedModelCount() == sharedCount && first_->CanDrawSkinning() && second_->CanDrawSkinning(), "Objects failed shared model/skinning ownership");
        auto* model = models->GetOrLoadModel("walk.gltf", root);
        const auto* clip = model->FindAnimationClip("default");
        Require(clip && std::isfinite(clip->duration) && clip->duration > 0, "Actual animation clip missing");
        const auto names=model->GetAnimationClipNames();
        Require(!names.empty() && names.front()=="default" && first_->GetAnimationClipNames()==names && second_->GetAnimationClipNames()==names,"SDK Model/Object3D clip lists disagree");
        const float step = clip->duration / 16;
        const auto timeMatches = [step](float actual, float expected) { return std::abs(actual - expected) < (std::max)(1e-5f, step * 1e-4f); };
        first_->SetAnimationLoop(false); second_->SetAnimationLoop(false);
        first_->SetAnimationSpeed(1); second_->SetAnimationSpeed(2);
        first_->Update(step); second_->Update(step);
        Require(timeMatches(first_->GetAnimationTime(), step) && timeMatches(second_->GetAnimationTime(), 2 * step), "Actual object clocks/speeds not independent");
        first_->SetAnimationClip("default");
        Require(timeMatches(first_->GetAnimationTime(), step), "Same clip reset actual clock");
        first_->SetAnimationSpeed(0); first_->Update(step); second_->Update(step);
        Require(timeMatches(first_->GetAnimationTime(), step) && timeMatches(second_->GetAnimationTime(), 4 * step), "Pause changed first clock or halted second");
        const auto otherTime = second_->GetAnimationTime();
        first_->ResetAnimationTime();
        Require(first_->GetAnimationTime() == 0 && second_->GetAnimationTime() == otherTime, "Reset changed another object");
        first_->SetAnimationSpeed(1); first_->Update(step);
        Require(timeMatches(first_->GetAnimationTime(), step), "Reset/resume failed");
        Require(models->LoadAnimationClipFromResourceRoot(root, "walk.gltf", "verified_walk", "walk.gltf"), "Actual clip alias load failed");
        Require(first_->HasAnimationClip("verified_walk") && second_->HasAnimationClip("verified_walk"), "Shared loaded clip lookup failed");
        const auto expanded=model->GetAnimationClipNames();
        Require(std::find(expanded.begin(),expanded.end(),"verified_walk")!=expanded.end() && first_->GetAnimationClipNames()==expanded && second_->GetAnimationClipNames()==expanded,"SDK clip list did not reflect shared alias load");
        std::cout<<"SDK_OBJECT_CLIP_CATALOG_NAMES="<<names.size()<<" EXPANDED="<<expanded.size()<<'\n';
        first_->SetAnimationClip("verified_walk");
        Require(first_->GetAnimationTime() == 0 && second_->GetAnimationTime() == otherTime, "New clip reset wrong owner");
        first_->Update(step);
        const auto retainedTime = first_->GetAnimationTime();
        Require(!first_->HasAnimationClip("owned_missing_clip"), "Missing clip lookup silently succeeded");
        // Probe-level admission only: do not invoke low-level default fallback.
        Require(first_->GetAnimationTime() == retainedTime && second_->GetAnimationTime() == otherTime, "Lookup changed playback");
        first_->Update(clip->duration * 2);
        const auto* selected = model->FindAnimationClip("verified_walk");
        Require(selected && timeMatches(first_->GetAnimationTime(), selected->duration), "Actual nonloop did not clamp");
        first_->SetAnimationLoop(true); first_->ResetAnimationTime(); first_->Update(selected->duration + step);
        Require(timeMatches(first_->GetAnimationTime(), step), "Actual loop did not wrap");
        first_->Update(0);
        Require(timeMatches(first_->GetAnimationTime(), step), "Zero simulation delta advanced time");
        first_->ResetAnimationTime();first_->Update(step);
        Require(!first_->BlendToAnimationClip("owned_missing_clip",step) && timeMatches(first_->GetAnimationTime(),step),"SDK blend missing admission changed clock");
        Require(first_->BlendToAnimationClip("verified_walk",2*step),"SDK blend admission failed");
        first_->SetAnimationSpeed(0);first_->Update(step);
        Require(first_->IsAnimationBlending() && first_->GetAnimationTime()==0,"SDK paused blend advanced");
        first_->SetAnimationSpeed(1);first_->Update(step);
        QuaternionTransform before{},after{};Require(first_->GetAnimationJointTransform(0,before),"SDK blend joint missing");
        Require(first_->BlendToAnimationClip("default",step),"SDK blend interruption refused");first_->Update(0);
        Require(first_->GetAnimationJointTransform(0,after) && std::abs(before.translate.x-after.translate.x)<1e-5f && std::abs(before.rotate.w-after.rotate.w)<1e-5f,"SDK interruption snapped local pose");
        first_->Update(step);
        Require(!first_->IsAnimationBlending() && timeMatches(first_->GetAnimationTime(),step) && second_->GetAnimationTime()==otherTime,"SDK blend completion/peer isolation failed");
        Require(first_->BlendToAnimationClip("default",step),"SDK reset blend setup failed");first_->ResetAnimationTime();
        Require(!first_->IsAnimationBlending(),"SDK reset retained transition");
        std::cout<<"PASS: SDK-only Object3D blend missing admission/pause/snapshot interruption/target clock/peer/reset\n";
        VerifyController(step);
        VerifyLayeredObject(common,model,root,step);
        checked_ = true; endRequest_ = true;
    }
    void Finalize() override {
        if (dxCommon) dxCommon->WaitForAllFrames();
        first_.reset(); second_.reset();
        Framework::Finalize(); retired_ = true;
    }
    void Verify() const {
        Require(checked_ && retired_, "Probe did not complete owned cleanup");
        for (UINT64 i = 0; i < messages_->GetNumStoredMessages(); ++i) {
            SIZE_T size = 0; Require(SUCCEEDED(messages_->GetMessage(i, nullptr, &size)), "GPU message size read failed");
            std::vector<unsigned char> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            Require(SUCCEEDED(messages_->GetMessage(i, message, &size)), "GPU message read failed");
            Require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR && message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION, "GPU debug error during object/retirement checks");
        }
        std::cout << "PASS: SDK-only real-device Object3D shared-model independent clocks, actual clip alias/new-clip reset, pause/resume/reset/loop/clamp/zero-delta, GPU errors0 and owned retirement (no draw/pixel/Scene/Inspector/Lua integration; missing lookup admission is probe-only)\n";
    }
};
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try { Probe probe; probe.Run(); probe.Verify(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
