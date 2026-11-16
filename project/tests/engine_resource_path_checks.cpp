#include "engine/base/EngineResourcePath.h"
#include <fstream>
#include <iostream>

namespace {
void Require(bool ok,const char* reason) {if(!ok) throw std::runtime_error(reason);}
}
int main() {
    namespace fs=std::filesystem;using Engine::Base::ResolveEngineResource;
    const auto previous=fs::current_path();
    const auto fixture=fs::temp_directory_path()/(L"kubo-engine-resource-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        Require(!fs::exists(fixture),"Fixture collision");fs::create_directories(fixture/"Resources/Shaders");
        struct Cleanup {fs::path cwd,root;~Cleanup(){std::error_code error;fs::current_path(cwd,error);fs::remove_all(root,error);}} cleanup{previous,fixture};
        {std::ofstream shadow(fixture/"Resources/Shaders/owned-test.hlsl");shadow<<"invalid shadow shader";Require(bool(shadow),"Fixture write failed");}
        wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
        Require(length>0 && length<32768,"Cannot resolve executable");const auto installed=fs::path(executable).parent_path();
        fs::current_path(fixture);
        Require(ResolveEngineResource("Resources/Shaders/owned-test.hlsl")==installed/"Resources/Shaders/owned-test.hlsl","Engine resource was shadowed by cwd file");
        Require(ResolveEngineResource("./EditorResources/Fonts/../Fonts/fa-solid-900.ttf")==installed/"EditorResources/Fonts/fa-solid-900.ttf","Font path did not normalize in installation");
        Require(ResolveEngineResource(fixture/"Resources/Shaders/owned-test.hlsl")==fixture/"Resources/Shaders/owned-test.hlsl","Explicit absolute override was redirected");
        for(const auto& rejected : {fs::path(),fs::path(L"../outside.hlsl"),fs::path(L"Resources/../../outside.hlsl"),fs::path(L"C:drive-relative.hlsl"),fs::path(std::wstring(L"wrong\0suffix",12))}) {
            bool denied=false;try {ResolveEngineResource(rejected);} catch(const std::invalid_argument&) {denied=true;}
            Require(denied,"Invalid installation-relative resource was accepted");
        }
        std::cout<<"PASS: installation-owned engine resources independent of cwd shadow files, normalized fonts, explicit absolute overrides and invalid path rejection\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
