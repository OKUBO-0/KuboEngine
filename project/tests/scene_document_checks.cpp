#include "../engine/scene/core/SceneDocument.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool ok, const char* message) { if (!ok) { throw std::runtime_error(message); } }
}
int main()
{
    using namespace Engine::Scene;
    const auto directory = std::filesystem::temp_directory_path() / ("kubo-scene-checks-" + std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    try {
        const auto path = directory / L"日本語.scene";
        const auto autosave=SceneDocument::AutosavePath(path);
        Require(autosave==directory/L"日本語.scene.autosave","Autosave path used a narrow filename conversion");
        SceneValue schema;
        schema["version"] = 1.0; schema["scene"] = "GAMEPLAY";
        schema["transform"]["position"] = crude_json::array{0.0, 0.0, 0.0};
        schema["enabled"] = true;
        auto edited = schema;
        edited["transform"]["position"][size_t(0)] = double(1.2345678f);
        edited["enabled"] = false;
        std::string error;
        std::filesystem::create_directories(directory);
        auto existingTemporary=path;existingTemporary+=L".tmp";
        {std::ofstream temporary(existingTemporary);temporary<<"other writer";}
        Require(SceneDocument::Save(path, edited, schema, error), error.c_str());
        {std::ifstream temporary(existingTemporary);std::string text;std::getline(temporary,text);Require(text=="other writer","Scene save touched an unrelated legacy temporary file");}
        SceneValue result;
        Require(SceneDocument::Load(path, schema, result, error), error.c_str());
        Require(result.dump() == edited.dump(), "Saving must preserve exact finite float values");
        Require(SceneDocument::Save(autosave,edited,schema,error) && SceneDocument::Load(autosave,schema,result,error),"Native Unicode autosave roundtrip failed");
        const auto preserved = result.dump();
        for (int failure = 0; failure < 5; ++failure) {
            auto invalid = edited;
            if (failure == 0) { invalid["version"] = 2.0; }
            if (failure == 1) { invalid.erase("enabled"); }
            if (failure == 2) { invalid["enabled"] = 1.0; }
            if (failure == 3) { invalid["transform"]["position"] = crude_json::array{1.0}; }
            if (failure == 4) { invalid["transform"]["position"][size_t(0)] = std::numeric_limits<double>::infinity(); }
            Require(!SceneDocument::Save(path, invalid, schema, error), "Invalid save must fail");
            Require(SceneDocument::Load(path, schema, result, error), "Invalid save must preserve the existing file");
            Require(result.dump() == preserved, "Existing scene was modified by a rejected save");
        }
        const auto broken = directory / "broken.json";
        { std::ofstream out(broken); out << "{ broken"; }
        Require(!SceneDocument::Load(broken, schema, result, error), "Malformed JSON must fail");
        Require(result.dump() == preserved, "Failed load must not modify the output");
        Require(!SceneDocument::Load(directory / "missing.json", schema, result, error), "Missing file must fail");
        std::filesystem::create_directory(directory / "blocked.json");
        Require(!SceneDocument::Save(directory / "blocked.json", edited, schema, error), "Failed replacement must be reported");
        Require(!std::filesystem::exists(directory / "blocked.json.tmp"), "Temporary file must be cleaned after replacement failure");
        for(const auto& entry:std::filesystem::directory_iterator(directory)) Require(entry.path().filename().native().find(L".kubo-save-")==std::wstring::npos,"Scene owned temporary leaked");
        std::filesystem::remove_all(directory);
        std::cout << "PASS: scene roundtrip, schema validation, failed load isolation, atomic replacement failure\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
