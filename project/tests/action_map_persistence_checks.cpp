#include "../engine/input/ActionMap.h"
#include <Windows.h>
#include <limits>
#include <fstream>
#include <iostream>
#include <stdexcept>
void Require(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
int main()
{
    using Engine::InputSystem::ActionMap;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-action-map-check-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(fixture);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{fixture};
    const auto path = fixture / L"日本語.json";
    ActionMap map; map.Bind("move", 17, {1.0f, 0.0f, 0.0f}); map.Bind("start", 57);
    std::string error; Require(map.Save(path, error), error.c_str());
    auto temporary=path;temporary+=L".tmp";{std::ofstream other(temporary);other<<"other writer";}
    Require(map.Save(path,error),error.c_str());
    {std::ifstream other(temporary);std::string text;std::getline(other,text);Require(text=="other writer","Bindings save overwrote unrelated temporary file");}
    ActionMap loaded; Require(loaded.Load(path, error), error.c_str());
    Require(loaded.BindingCount("move") == 1 && loaded.BindingCount("start") == 1, "Binding roundtrip failed");
    ActionMap empty;Require(empty.Save(path,error) && loaded.Load(path,error) && loaded.Bindings().empty(),"Cleared binding map did not roundtrip");
    Require(map.Save(path,error) && loaded.Load(path,error),"Cannot restore bindings fixture");
    for(int failure=0;failure<3;++failure) {
        ActionMap invalid;if(failure==0)invalid.Bind("invalid",256);if(failure==1)invalid.Bind("",17);if(failure==2)invalid.Bind("invalid",17,{std::numeric_limits<float>::infinity(),0,0});
        Require(!invalid.Save(path,error),"Unsupported native binding overwrote saved configuration");
        Require(loaded.Load(path,error) && loaded.BindingCount("move")==1,"Rejected binding save changed existing file");
    }
    { std::ofstream out(path); out << "{ broken"; }
    Require(!loaded.Load(path, error), "Malformed bindings must fail");
    Require(loaded.BindingCount("move") == 1 && loaded.BindingCount("start") == 1, "Failed load changed current bindings");
    for(const auto& invalid:{"{\"version\":1,\"actions\":{\"x\":[{\"key\":256,\"contribution\":[0,0,1]}]}}",
        "{\"version\":1,\"actions\":{\"x\":[{\"key\":-1,\"contribution\":[0,0,1]}]}}",
        "{\"version\":1,\"actions\":{\"x\":[{\"key\":1.5,\"contribution\":[0,0,1]}]}}",
        "{\"version\":1,\"actions\":{\"x\":[{\"key\":17,\"contribution\":[1e99,0,1]}]}}",
        "{\"version\":1,\"actions\":{\"x\":[{\"key\":17,\"contribution\":[\"bad\",0,1]}]}}"}) {
        { std::ofstream out(path);out<<invalid; }
        Require(!loaded.Load(path,error) && loaded.BindingCount("move")==1 && loaded.BindingCount("start")==1,"Invalid binding crossed numeric boundary or modified old map");
    }
    const auto blocked = fixture / "blocked.json";
    std::filesystem::create_directory(blocked);
    Require(!map.Save(blocked, error), "Failed replacement must be reported");
    Require(!std::filesystem::exists(blocked.string() + ".tmp"), "Temporary binding file must be cleaned");
    for(const auto& entry:std::filesystem::directory_iterator(fixture)) Require(entry.path().filename().native().find(L".kubo-save-")==std::wstring::npos,"Binding owned temporary leaked");
    std::cout << "PASS: ActionMap JSON roundtrip and failed-load isolation\n";
}
