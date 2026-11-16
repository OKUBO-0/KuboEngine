#include "engine/editor/AnimatorControllerSelection.h"
#include <fstream>
#include <iostream>
namespace {
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Fixture write failed");}
}
int main(){namespace fs=std::filesystem;using namespace Engine;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-selection-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"Controller selection","startupScene":"Assets/Main.scene"})");
        const auto source=root/L"Assets/移動##controller.animfsm";
        const std::string good=R"({"format":"kubo-animator-controller-1","parameters":[],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true}],"transitions":[]}]})";
        Write(source,good);Write(root/"Assets/wrong.lua","return {}");
        Base::ProjectContext project;Assets::AssetRegistry registry;std::string error;
        Need(project.Open(root/"Game.kubo",error) && registry.Scan(root/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(source)->guid;
        AnimationSystem::AnimatorComponent current,output;current.clip="legacy";current.speed=0.5f;current.loop=false;current.playOnAwake=false;
        Need(Editor::PrepareAnimatorControllerSelection(project,registry,current,guid,output,error),error.c_str());
        Need(output.controllerGuid==guid && output.clip==current.clip && output.speed==0.5f && !output.loop && !output.playOnAwake,"Selection changed other authored fields");
        const auto retained=AnimationSystem::EncodeAnimator(output).dump();
        const auto refuse=[&](const std::string& selection){Need(!Editor::PrepareAnimatorControllerSelection(project,registry,current,selection,output,error) && AnimationSystem::EncodeAnimator(output).dump()==retained,"Refused selection changed output");};
        refuse("../invalid");refuse(std::string(32,'a'));refuse(registry.FindByPath(root/"Assets/wrong.lua")->guid);
        Write(source,"{}");refuse(guid);Write(source,good);
        const auto metadata=Assets::AssetRegistry::MetadataPath(source);fs::rename(metadata,metadata.wstring()+L".saved");
        refuse(guid);Need(!fs::exists(metadata),"Selection generated missing identity");fs::rename(metadata.wstring()+L".saved",metadata);
        Base::ProjectContext closed;
        Need(Editor::PrepareAnimatorControllerSelection(closed,registry,output,"",output,error) && output.controllerGuid.empty() && output.clip==current.clip,"Clear/in-place selection failed");
        Need(Editor::PrepareAnimatorControllerSelection(project,registry,output,guid,output,error) && output.controllerGuid==guid,"In-place selected settings failed");
        auto invalid=current;invalid.speed=-1;
        Need(!Editor::PrepareAnimatorControllerSelection(project,registry,invalid,"",output,error) && output.controllerGuid==guid,"Invalid current settings accepted");
        std::cout<<"PASS: production Inspector Controller selection/clear/in-place ownership/other-field preservation/type+GUID+graph+metadata refusal; no physical UI or Model compatibility claim\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
