#include "engine/editor/ProjectStartupScene.h"
#include <Windows.h>
#include <iostream>
namespace {
void Require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
void Write(const std::filesystem::path& path,const std::string& bytes) {std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Require(!file.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path) {std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
int main() {
    namespace fs=std::filesystem;
    const auto fixture=fs::temp_directory_path()/(L"kubo-startup-settings-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    try {
        const auto root=fixture/L"Project";const auto manifest=root/L"設定.kubo";
        Write(manifest,R"({"name":"Startup","startupScene":"missing-legacy.json","extension":{"keep":true}})");
        const auto target=root/L"Assets/場面/開始.scene";
        Write(target,R"({"version":1,"entities":[]})");
        Write(root/"bad.scene",R"({"version":1,"entities":[{"id":1}]})");
        Write(root/"legacy.json",R"({"version":1,"player":{}})");
        Write(root/"syntax.scene","not JSON");
        Write(root/"oversized.scene",std::string(4*1024*1024+1,' '));
        Write(fixture/"outside.scene",R"({"version":1,"entities":[]})");
        fs::create_directories(fixture/"Unrelated");fs::current_path(fixture/"Unrelated");
        Engine::Base::ProjectContext project;std::string error,relative="retained";
        Require(project.Open(manifest,error),error.c_str());
        const auto original=Read(manifest);
        for(const auto& bad : {fs::path{},fixture/"outside.scene",root/"missing.scene",root/"bad.scene",root/"legacy.json",root/"syntax.scene",root/"oversized.scene",fs::path(L"../outside.scene"),fs::path(L"C:relative")}) {
            Require(!Engine::Editor::SelectProjectStartupScene(project,bad,relative,error) && relative=="retained" && Read(manifest)==original,"Invalid Scene selection changed draft/manifest");
        }
        Require(!Engine::Editor::SelectProjectStartupScene(project,std::string("\xff",1),relative,error),"Malformed UTF-8 Scene selection accepted");
        Require(Engine::Editor::SelectProjectStartupScene(project,target,relative,error) && relative=="Assets/場面/開始.scene",error.c_str());
        Require(Engine::Editor::SelectProjectStartupScene(project,fs::path(L"Assets/場面/開始.scene"),relative,error),"Relative selection followed unrelated cwd");
        auto caseAlias=root.native();for(auto& character:caseAlias) if(character>=L'a' && character<=L'z') character-=L'a'-L'A';
        Require(Engine::Editor::SelectProjectStartupScene(project,fs::path(caseAlias)/L"Assets/場面/開始.scene",relative,error),"Windows case-insensitive project ownership rejected");
        for(const std::string& bad : std::vector<std::string>{"../outside.scene","C:/outside.scene","Assets:stream","folder./bad.scene",std::string("x\0y",3),std::string("\xff",1)}) {
            Require(!project.SaveLaunchSettings({960,540},bad,error) && Read(manifest)==original,"Invalid startup path changed manifest");
        }
        const HANDLE locked=CreateFileW(manifest.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(locked!=INVALID_HANDLE_VALUE,"Lock fixture failed");
        const bool rejected=!project.SaveLaunchSettings({960,540},relative,error);CloseHandle(locked);
        Require(rejected && Read(manifest)==original && project.GetSettings().startupScene=="missing-legacy.json" && project.GetSettings().gameOutput==Engine::Base::ViewExtent{},"Failed launch save partially committed");
        Require(project.SaveLaunchSettings({960,540},relative,error),error.c_str());
        Engine::Base::ProjectContext reopened;
        Require(reopened.Open(manifest,error) && reopened.GetSettings().startupScene==relative && reopened.GetSettings().gameOutput==Engine::Base::ViewExtent{960,540},"Combined launch settings did not reopen");
        Require(crude_json::value::parse(Read(manifest))["extension"]["keep"].get<crude_json::boolean>(),"Launch save lost unknown fields");
        auto external=crude_json::value::parse(Read(manifest));external["name"]="external";Write(manifest,external.dump());
        const auto externalBytes=Read(manifest);
        Require(!project.SaveLaunchSettings({1280,720},"another.scene",error) && Read(manifest)==externalBytes && project.GetSettings().startupScene==relative,"Stale launch save replaced external file/context");
        Require(project.Open(manifest,error) && project.SaveLaunchSettings({1280,720},"",error),"Legacy empty startup compatibility lost");
        Require(project.GetSettings().startupScene.empty(),"Empty startup did not persist");
        fs::current_path(previous);fs::remove_all(fixture);
        std::cout<<"PASS: Unicode project-owned validated generic startup selection, schema/size/path failure isolation, combined atomic launch save, reopen, external-change refusal and legacy compatibility\n";return 0;
    } catch(const std::exception& exception) {fs::current_path(previous);std::error_code cleanup;fs::remove_all(fixture,cleanup);std::cerr<<exception.what()<<'\n';return 1;}
}
