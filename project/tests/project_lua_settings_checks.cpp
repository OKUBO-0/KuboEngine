#include "engine/base/ProjectContext.h"
#include "externals/imgui_node_editor/crude_json.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;file.close();Require(!file.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
bool Same(Engine::Scripting::LuaResourceLimits a,Engine::Scripting::LuaResourceLimits b){return a.memoryBytes==b.memoryBytes && a.states==b.states && a.frameInstructions==b.frameInstructions;}
}
int main(){
    using Engine::Base::ProjectContext;using Engine::Scripting::LuaResourceLimits;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-project-lua-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        std::filesystem::create_directories(fixture);const auto path=fixture/L"設定.kubo";
        crude_json::value manifest=crude_json::object{{"name","日本語"},{"startupScene","Assets/Main.scene"},{"assetDirectory","Assets"},{"extension",crude_json::object{{"keep",true}}}};
        Write(path,"\xef\xbb\xbf"+manifest.dump());ProjectContext project;std::string error;
        Require(project.Open(path,error) && Same(project.GetSettings().luaLimits,{}),"Legacy defaults lost");
        const LuaResourceLimits chosen{12345678,19,234567};Require(project.SaveLuaLimits(chosen,error),error.c_str());
        auto bytes=Read(path);Require(bytes.starts_with("\xef\xbb\xbf"),"BOM lost");auto saved=crude_json::value::parse(bytes.substr(3));
        Require(!saved.contains("gameOutput") && saved["extension"]["keep"].get<crude_json::boolean>() && saved["startupScene"]==manifest["startupScene"] && Same(project.GetSettings().luaLimits,chosen),"Lua-only save changed unrelated settings");
        saved["luaLimits"]["unknown"]=crude_json::object{{"keep",true}};saved["gameOutput"]=crude_json::object{{"width",1280.0},{"height",720.0},{"keep",true}};Write(path,"\xef\xbb\xbf"+saved.dump());
        Require(project.Open(path,error),error.c_str());const LuaResourceLimits maximum{1073741824,4096,100000000};
        Require(project.SaveLaunchSettings({640,480},"Assets/別.scene",maximum,error),error.c_str());ProjectContext reopened;Require(reopened.Open(path,error) && Same(reopened.GetSettings().luaLimits,maximum) && reopened.GetSettings().gameOutput==Engine::Base::ViewExtent{640,480},"Combined settings reopen mismatch");
        bytes=Read(path);saved=crude_json::value::parse(bytes.substr(3));Require(saved["luaLimits"]["unknown"]["keep"].get<crude_json::boolean>() && saved["gameOutput"]["keep"].get<crude_json::boolean>(),"Nested extensions lost");
        Require(project.SaveLaunchSettings({800,600},"Assets/Main.scene",error) && Same(project.GetSettings().luaLimits,maximum),"Legacy SaveLaunchSettings changed Lua limits");
        const auto before=Read(path);const auto beforeExtent=project.GetSettings().gameOutput;
        for(const auto bad:{LuaResourceLimits{0,1,1},LuaResourceLimits{1073741825,1,1},LuaResourceLimits{1,0,1},LuaResourceLimits{1,4097,1},LuaResourceLimits{1,1,0},LuaResourceLimits{1,1,100000001},LuaResourceLimits{(std::numeric_limits<size_t>::max)(),1,1}}){
            Require(!project.SaveLaunchSettings({320,240},"Assets/new.scene",bad,error) && Read(path)==before && Same(project.GetSettings().luaLimits,maximum) && project.GetSettings().gameOutput==beforeExtent,"Invalid Lua combined save published state");}
        auto valid=crude_json::value::parse(before.substr(3));
        for(const auto& field:{"memoryBytes","states","frameInstructions"}){
            for(const auto& value:{crude_json::value{0.0},crude_json::value{-1.0},crude_json::value{1.5},crude_json::value{true},crude_json::value{"1"},crude_json::value{nullptr},crude_json::value{1e100}}){
                auto bad=valid;bad["luaLimits"][field]=value;Write(path,bad.dump());Require(!project.Open(path,error) && Same(project.GetSettings().luaLimits,maximum) && project.GetSettings().gameOutput==beforeExtent,"Invalid manifest replaced previous context");}
        }
        for(const auto& limits:{crude_json::value{nullptr},crude_json::value{true},crude_json::value{crude_json::array{}},crude_json::value{crude_json::object{{"memoryBytes",1.0}}}}){auto bad=valid;bad["luaLimits"]=limits;Write(path,bad.dump());Require(!project.Open(path,error) && Same(project.GetSettings().luaLimits,maximum),"Malformed limits accepted");}
        Write(path,before);Require(project.Open(path,error),error.c_str());auto foreign=valid;foreign["foreign"]=true;Write(path,foreign.dump());const auto external=Read(path);
        Require(!project.SaveLuaLimits(chosen,error) && Read(path)==external && Same(project.GetSettings().luaLimits,maximum),"Stale snapshot overwrote external changes");
        Require(project.Open(path,error) && project.SaveLuaLimits({1,1,1},error),error.c_str());Require(Same(project.GetSettings().luaLimits,{1,1,1}),"Minimum limits rejected");
        std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cout<<"PASS: project Lua limits defaults/schema/bounds, BOM/nested extension preservation, Lua-only/combined/legacy save, invalid/stale failure isolation and Unicode reopen\n";return 0;
    }catch(const std::exception& exception){std::error_code cleanup;std::filesystem::remove_all(fixture,cleanup);std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
