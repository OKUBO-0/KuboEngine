#include "engine/base/ProjectContext.h"
#include "engine/base/ProjectPaths.h"
#include "externals/imgui_node_editor/crude_json.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void Require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
void Write(const std::filesystem::path& path,const std::string& bytes) {std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Require(!file.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path) {std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
}
int main() {
    namespace fs=std::filesystem;using Engine::Base::ProjectContext;
    const auto fixture=fs::temp_directory_path()/(L"kubo-project-path-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    try {
        fs::create_directories(fixture/L"素材/モデル");fs::create_directories(fixture/L"素材/場面");fs::create_directories(fixture/"Unrelated");
        const auto manifest=fixture/L"日本語.KUBO";
        const crude_json::value valid=crude_json::object{{"name","日本語"},{"engineVersion","0.1.0"},{"startupScene","素材/場面/開始.scene"},{"assetDirectory","素材/モデル"}};
        Write(manifest,"\xef\xbb\xbf"+valid.dump());
        const auto scene=fixture/L"素材/場面/開始.scene";Write(scene,"{\"version\":1,\"entities\":[]}");
        fs::current_path(fixture/"Unrelated");ProjectContext context;std::string error;
        Engine::Base::ProjectPaths projectPaths(fixture),legacyPaths;
        fs::current_path(previous);
        Require(projectPaths.Root()==fixture && projectPaths.InputBindings()==fixture/"UserSettings/input_bindings.json","Project settings followed a changed cwd");
        Require(legacyPaths.Root()==fixture/"Unrelated" && legacyPaths.InputBindings()==fixture/"Unrelated/UserSettings/input_bindings.json","Legacy paths were not snapshotted");
        fs::current_path(fixture/"Unrelated");
        Require(context.Open(manifest,error),error.c_str());
        Require(context.GetSettings().gameOutput==Engine::Base::ViewExtent{},"Legacy manifest lost default Game output");
        Require(context.ResolvePath(context.GetSettings().startupScene)==scene,"Manifest startup path depended on ACP/cwd");
        Require(context.ResolveAssetPath("cube.obj")==fixture/L"素材/モデル/cube.obj","Custom Unicode asset root depended on ACP");
        Require(context.ResolveAssetPath("素材/モデル/cube.obj")==fixture/L"素材/モデル/cube.obj","Nested asset root was doubled");
        Require(context.ResolveAssetPath("素材/モデリング/cube.obj")==fixture/L"素材/モデル/素材/モデリング/cube.obj","Asset root used text prefix instead of components");
        Require(context.ResolveAssetPath("Resources/sky.dds")==fixture/L"Resources/sky.dds","Legacy Resources compatibility lost");
        Require(context.ResolvePath(fs::path(L"C:/outside/file.scene"))==fs::path(L"C:/outside/file.scene"),"Explicit native absolute path compatibility lost");
        for(const auto& bad:{"../outside", "C:relative", ".. /outside", "folder./asset", "folder /asset"}) {
            bool rejected=false;try {(void)context.ResolvePath(bad);}catch(const std::exception&) {rejected=true;}Require(rejected,"Unsafe/ambiguous relative path accepted");
        }
        const auto badManifest=fixture/"Bad.kubo";
        for(const auto& bad:{"../outside", "Assets/../../outside", "C:/outside", "C:relative", "\\\\server\\share", ".. /outside", ".", "", "Assets:stream"}) {
            auto candidate=valid;candidate["assetDirectory"]=bad;Write(badManifest,candidate.dump());
            Require(!context.Open(badManifest,error) && context.GetProjectFile()==manifest && context.GetSettings().assetDirectory=="素材/モデル","Invalid asset directory replaced open context");
        }
        for(const auto& field:{"name","engineVersion","startupScene","assetDirectory"}) {
            auto candidate=valid;candidate[field]=std::string("x\0y",3);Write(badManifest,candidate.dump());
            Require(!context.Open(badManifest,error) && context.GetProjectFile()==manifest,"NUL manifest field accepted");
            candidate=valid;candidate[field]=std::string("\xff",1);Write(badManifest,candidate.dump());
            Require(!context.Open(badManifest,error) && context.GetProjectFile()==manifest,"Malformed UTF-8 manifest field accepted");
        }
        auto normalized=valid;normalized["assetDirectory"]="Content/./Data";normalized["startupScene"]="Assets/../素材/場面/開始.scene";Write(badManifest,normalized.dump());
        Require(context.Open(badManifest,error) && context.GetSettings().assetDirectory=="Content/Data" && context.GetSettings().startupScene=="素材/場面/開始.scene","Safe path normalization failed");
        Require(context.ResolveAssetPath("content/data/model.obj")==fixture/L"content/data/model.obj","Windows case-insensitive asset prefix failed");
        normalized["startupScene"]="";Write(badManifest,normalized.dump());Require(context.Open(badManifest,error) && context.GetSettings().startupScene.empty(),"Legacy empty startup fallback rejected");
        auto configured=valid;configured["gameOutput"]=crude_json::object{{"width",512.0},{"height",768.0}};Write(badManifest,configured.dump());
        Require(context.Open(badManifest,error) && context.GetSettings().gameOutput==Engine::Base::ViewExtent{512,768},"Configured Game output did not load");
        for(const auto& invalidOutput : {"null","[]","{}","{\"width\":1}","{\"width\":true,\"height\":1}","{\"width\":0,\"height\":1}","{\"width\":1.5,\"height\":2}","{\"width\":4097,\"height\":2}","{\"width\":4096,\"height\":4096}"}) {
            auto rejected=configured;rejected["gameOutput"]=crude_json::value::parse(invalidOutput);Write(manifest,rejected.dump());
            Require(!context.Open(manifest,error) && context.GetProjectFile()==badManifest && context.GetSettings().gameOutput==Engine::Base::ViewExtent{512,768},"Invalid Game output replaced open context");
        }
        ProjectContext unopened;
        Require(!unopened.SaveGameOutput({640,480},error),"Unopened context saved settings");
        configured["customExtension"]=crude_json::object{{"text","保持"},{"enabled",true}};
        configured["gameOutput"]["futureOption"]="preserve";
        const auto original="\xef\xbb\xbf"+configured.dump();Write(badManifest,original);
        Require(context.Open(badManifest,error),error.c_str());
        for(const auto extent : {Engine::Base::ViewExtent{0,720},Engine::Base::ViewExtent{4096,4096},Engine::Base::ViewExtent{4097,1}}) {
            Require(!context.SaveGameOutput(extent,error) && Read(badManifest)==original && context.GetSettings().gameOutput==Engine::Base::ViewExtent{512,768},"Invalid settings save modified context/file");
        }
        const HANDLE locked=CreateFileW(badManifest.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(locked!=INVALID_HANDLE_VALUE,"Cannot lock manifest fixture");
        const bool lockRejected=!context.SaveGameOutput({640,480},error);CloseHandle(locked);
        Require(lockRejected && Read(badManifest)==original && context.GetSettings().gameOutput==Engine::Base::ViewExtent{512,768},"Replacement failure modified settings/file");
        Require(context.SaveGameOutput({640,480},error),error.c_str());
        const auto savedBytes=Read(badManifest);Require(savedBytes.starts_with("\xef\xbb\xbf"),"Settings save removed BOM");
        const auto saved=crude_json::value::parse(savedBytes.substr(3));
        Require(saved["customExtension"]==configured["customExtension"] && saved["gameOutput"]["futureOption"]==configured["gameOutput"]["futureOption"],"Settings save removed extension fields");
        ProjectContext reopened;Require(reopened.Open(badManifest,error) && reopened.GetSettings().gameOutput==Engine::Base::ViewExtent{640,480},"Saved settings did not survive reopening");
        auto external=saved;external["name"]="external change";Write(badManifest,external.dump());
        const auto externalBytes=Read(badManifest);
        Require(!context.SaveGameOutput({960,540},error) && Read(badManifest)==externalBytes && context.GetSettings().gameOutput==Engine::Base::ViewExtent{640,480},"Stale settings silently overwrote external changes");
        Require(context.Open(badManifest,error) && context.SaveGameOutput({960,540},error),error.c_str());
        Require(crude_json::value::parse(Read(badManifest))["name"].get<crude_json::string>()=="external change","Reloaded save lost external field");
        fs::remove(badManifest);
        Require(!context.SaveGameOutput({1280,720},error) && !fs::exists(badManifest),"Settings save recreated missing manifest");
        for(const auto& entry : fs::directory_iterator(fixture)) Require(entry.path().filename().native().find(L".kubo-save-")==std::wstring::npos,"Settings save leaked owned temporary");
        fs::current_path(previous);fs::remove_all(fixture);
        std::cout<<"PASS: UTF-8/BOM manifest, Unicode nested paths independent of ACP/cwd, component prefixes, traversal/root/ambiguous field rejection, failed-open isolation and legacy paths\n";return 0;
    } catch(const std::exception& exception) {fs::current_path(previous);std::error_code cleanup;fs::remove_all(fixture,cleanup);std::cerr<<exception.what()<<'\n';return 1;}
}
