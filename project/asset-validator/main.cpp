#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "engine/assets/ExportAssetDependencies.h"
#include "engine/assets/ExportPrefabDependencies.h"
#include <iostream>
#include <limits>

namespace {
std::string ReadRequest(const std::filesystem::path& path) {
    struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}} file;
    file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if(file.value==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) || info.nFileSizeHigh ||
        !info.nFileSizeLow || info.nFileSizeLow>4*1024*1024) throw std::runtime_error("Invalid validator request file");
    std::string bytes(info.nFileSizeLow,'\0');DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),info.nFileSizeLow,&read,nullptr) || read!=info.nFileSizeLow)
        throw std::runtime_error("Incomplete validator request");
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0)==0 || bytes.find('\0')!=std::string::npos)
        throw std::runtime_error("Invalid validator UTF-8 request");
    size_t depth=0;bool quoted=false,escaped=false;
    for(const char c:bytes) {
        if(quoted) {if(escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='"') quoted=false;}
        else if(c=='"') quoted=true;
        else if(c=='{' || c=='[') {if(++depth>16) throw std::runtime_error("Validator request nesting limit");}
        else if(c=='}' || c==']') {if(!depth) throw std::runtime_error("Invalid validator JSON structure");--depth;}
    }
    return bytes;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3 || std::wstring(argv[1])!=L"--request") throw std::runtime_error("Usage: KuboAssetValidator --request <UTF-8 JSON file>");
        const auto request=crude_json::value::parse(ReadRequest(argv[2]));
        if(!request.is_object() || !request.contains("format") || !request["format"].is_string() ||
            (request["format"].get<crude_json::string>()!="kubo-export-dependencies-1" && request["format"].get<crude_json::string>()!="kubo-export-dependencies-2" && request["format"].get<crude_json::string>()!="kubo-export-dependencies-3" && request["format"].get<crude_json::string>()!="kubo-export-dependencies-4" && request["format"].get<crude_json::string>()!="kubo-export-dependencies-5") ||
            !request.contains("root") || !request["root"].is_string() ||
            !request.contains("models") || !request["models"].is_array() ||
            !request.contains("copyRoots") || !request["copyRoots"].is_array()) throw std::runtime_error("Invalid validator request schema");
        const auto paths=[&](const char* field) {
            std::vector<std::filesystem::path> result;
            for(const auto& item:request[field].get<crude_json::array>()) {
                if(!item.is_string() || result.size()>=65536) throw std::runtime_error("Invalid validator path array");
                result.push_back(Engine::Base::PathFromUtf8(item.get<crude_json::string>()));
            }
            return result;
        };
        const auto rootText=request["root"].get<crude_json::string>();
        if(rootText.empty() || rootText.find('\0')!=std::string::npos) throw std::runtime_error("Invalid validator root text");
        const auto root=Engine::Base::PathFromUtf8(rootText);
        if(!root.is_absolute() || root.native().size()>32767) throw std::runtime_error("Validator root must be a bounded absolute path");
        const auto copyRoots=paths("copyRoots");
        const auto closure=Engine::Assets::ValidateExportModelDependencies(root,paths("models"),copyRoots);
        const bool withMaterials=request["format"].get<crude_json::string>()=="kubo-export-dependencies-5";
        const bool withUI=withMaterials || request["format"].get<crude_json::string>()=="kubo-export-dependencies-4";
        const bool withPrefabs=withUI || request["format"].get<crude_json::string>()=="kubo-export-dependencies-3";
        if(!withPrefabs && request.contains("prefabs"))throw std::runtime_error("Prefab references require validator format 3");
        if(withPrefabs || request["format"].get<crude_json::string>()=="kubo-export-dependencies-2") {
            if(!request.contains("project") || !request["project"].is_string() || !request.contains("controllers") || !request["controllers"].is_array())
                throw std::runtime_error("Controller validator request requires project and GUIDs");
            const auto project=Engine::Base::PathFromUtf8(request["project"].get<crude_json::string>());
            if(!project.is_absolute() || project.native().size()>32767 || project.native().find(L'\0')!=std::wstring::npos ||
                std::filesystem::canonical(project.parent_path())!=std::filesystem::canonical(root))throw std::runtime_error("Controller validator project/root mismatch");
            std::vector<std::string> guids;
            for(const auto& value:request["controllers"].get<crude_json::array>()) {
                if(!value.is_string() || guids.size()>=65536)throw std::runtime_error("Invalid Controller GUID array");
                guids.push_back(value.get<crude_json::string>());
            }
            const auto count=Engine::Assets::ValidateExportControllerDependencies(project,guids,copyRoots);
            std::cout<<"Controller dependencies verified: "<<count<<" GUID source/metadata pairs (Model clip compatibility is runtime admission)\n";
            if(withPrefabs){
                if(!request.contains("prefabs") || !request["prefabs"].is_array())throw std::runtime_error("Prefab validator requires inventory");
                const auto prefab=Engine::Assets::ValidateExportPrefabDependencies(project,paths("prefabs"),copyRoots);
                std::cout<<"Prefab dependencies verified: "<<prefab.prefabs<<" templates, "<<prefab.models.files<<" Model files, "<<prefab.models.edges<<" inspected Model edges\n";
                std::cout<<"Prefab Sprite dependencies verified: "<<prefab.sprites.sprites<<" assigned references, "<<prefab.sprites.textures<<" texture files summed per template, "<<prefab.sprites.unassigned<<" unassigned drafts\n";
                std::cout<<"Prefab Particle dependencies verified: "<<prefab.particles.emitters<<" assigned references, "<<prefab.particles.textures<<" texture files summed per template, "<<prefab.particles.unassigned<<" unassigned drafts\n";
                if(withMaterials)std::cout<<"Prefab Material dependencies verified: "<<prefab.materials<<" shared assets summed per template\n";
                if(prefab.opaqueScripts)std::cout<<"Warning: "<<prefab.opaqueScripts<<" GUID-less Prefab Script keys are opaque native callback names; file resolution/execution is not verified\n";
                if(prefab.models.uninspectedModels)std::cout<<"Warning: "<<prefab.models.uninspectedModels<<" Prefab Model formats have no external-dependency parser\n";
            }
            if(withUI){
                const auto sprites=Engine::Assets::ValidateExportStartupSpriteDependencies(project,copyRoots);
                std::cout<<"Startup Sprite dependencies verified: "<<sprites.sprites<<" assigned references, "<<sprites.textures<<" texture files, "<<sprites.unassigned<<" unassigned drafts\n";
                const auto render=Engine::Assets::ValidateExportStartupRenderDependencies(project,copyRoots,withMaterials);
                const auto& particles=render.particles;
                if(withMaterials)std::cout<<"Startup Material dependencies verified: "<<render.materials<<" shared assets\n";
                std::cout<<"Startup Particle dependencies verified: "<<particles.emitters<<" assigned references, "<<particles.textures<<" texture files, "<<particles.unassigned<<" unassigned drafts\n";
            }
        } else if(request.contains("controllers"))throw std::runtime_error("Controller references require validator format 2");
        std::cout<<"Model dependencies verified: "<<closure.files<<" files, "<<closure.edges<<" inspected edges\n";
        if(closure.uninspectedModels) std::cout<<"Warning: "<<closure.uninspectedModels<<" Model formats have no external-dependency parser; full graph is not verified\n";
        return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
