#include "../engine/assets/AssetRegistry.h"

#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}
}

int main()
{
    using namespace Engine::Assets;
    const auto root = std::filesystem::temp_directory_path() /
        ("kubo-asset-registry-checks-" + std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(root / "Models");
        std::filesystem::create_directories(root / "Textures");
        { std::ofstream(root / "Models" / "cube.gltf") << "{}"; }
        { std::ofstream(root / "Textures" / "checker.png", std::ios::binary) << "texture"; }

        AssetRegistry registry;
        std::string error;
        Require(registry.Scan(root, error), error.c_str());
        Require(registry.GetAssets().size() == 2, "Asset scan count mismatch");
        Require(registry.Search("cube").size() == 1, "Asset search failed");
        Require(registry.Search("", AssetType::Texture).size() == 1, "Asset type filter failed");
		const auto originalGuid = registry.GetAssets().front().guid;
		const auto originalFingerprint = registry.GetAssets().front().fingerprint;
		{ std::ofstream(root / "Models" / "cube.gltf", std::ios::app) << "changed"; }
		Require(registry.Scan(root, error), error.c_str());
		Require(registry.GetAssets().front().guid == originalGuid, "Asset GUID must remain stable after edits");
		Require(registry.GetAssets().front().fingerprint != originalFingerprint, "Asset fingerprint must detect edits");

        AssetRecord imported;
        Require(registry.ImportAsset(root / "Models" / "cube.gltf", root / ".kubo" / "cache", imported, error), error.c_str());
        Require(!imported.guid.empty() && std::filesystem::is_regular_file(imported.cachePath),
            "Asset import metadata was not created");

        const auto database = root / ".kubo" / "assets.json";
        Require(registry.SaveDatabase(database, error), error.c_str());
        AssetRegistry loaded;
        Require(loaded.LoadDatabase(database, error), error.c_str());
        Require(loaded.FindByGuid(imported.guid) != nullptr, "Asset database roundtrip lost GUID");
        const auto controller=root/"locomotion.animfsm";
        const std::string good=R"({"format":"kubo-animator-controller-1","parameters":[],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true}],"transitions":[]}]})";
        {std::ofstream(controller)<<good;}
        Require(AssetRegistry::Classify("Upper.ANIMFSM")==AssetType::AnimatorController,"Controller classification failed");
        AssetRecord controllerRecord;Require(registry.ImportAsset(controller,root/".kubo/cache",controllerRecord,error),error.c_str());
        Require(controllerRecord.type==AssetType::AnimatorController && registry.Search("",AssetType::AnimatorController).size()==1,"Controller typed import/search failed");
        const auto controllerGuid=controllerRecord.guid,controllerFingerprint=controllerRecord.fingerprint;
        Require(registry.SaveDatabase(database,error) && loaded.LoadDatabase(database,error) && loaded.FindByGuid(controllerGuid)->type==AssetType::AnimatorController,"Controller database type lost");
        {std::ofstream(controller)<<"{\"format\":1,\"format\":2}";}
        AssetRecord retained=controllerRecord;
        Require(!registry.ImportAsset(controller,root/".kubo/cache",retained,error) && retained.guid==controllerGuid && retained.fingerprint==controllerFingerprint && registry.FindByGuid(controllerGuid)->fingerprint==controllerFingerprint,"Malformed controller published import result/registry");
        const auto invalid=root/"invalid.animfsm";{std::ofstream(invalid)<<"{}";}
        Require(!registry.ImportAsset(invalid,root/".kubo/cache",retained,error) && !std::filesystem::exists(AssetRegistry::MetadataPath(invalid)),"Invalid controller created identity sidecar");
        {std::ofstream(controller)<<good;}
        Require(registry.ImportAsset(controller,root/".kubo/cache",retained,error) && retained.guid==controllerGuid,"Controller reimport changed stable GUID");
        std::filesystem::remove_all(root);
        std::cout << "PASS: asset scan, classification, search, import metadata, database roundtrip\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
