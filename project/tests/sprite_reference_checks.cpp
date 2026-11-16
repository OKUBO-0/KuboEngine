#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "engine/assets/SpriteReference.h"
#include <iostream>
#include <fstream>
static void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream output(path,std::ios::binary);output<<bytes;output.close();Need(!output.fail(),"Reference fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Need(input.good(),"Reference fixture read failed");return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
int main(){using namespace Engine;std::filesystem::path fixture;const auto previous=std::filesystem::current_path();try{
    fixture=std::filesystem::temp_directory_path()/(L"kubo-sprite-reference-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Need(std::filesystem::create_directory(fixture),"Refuse existing reference fixture");std::filesystem::create_directories(fixture/L"Assets/画像");std::filesystem::create_directory(fixture/"Unrelated");
    const auto path=fixture/L"Assets/画像/白.png",script=fixture/"Assets/script.lua";
    // No image decoding is claimed by this reference/identity test.
    Write(path,"reference-unit-not-an-image");Write(script,"return {}\n");
    Assets::AssetRegistry registry;std::string error;Need(registry.Scan(fixture/"Assets",error),error.c_str());
    const auto* image=registry.FindByPath(path);const auto* lua=registry.FindByPath(script);Need(image && lua && image->type==Assets::AssetType::Texture,"Reference registry setup");
    const auto guid=image->guid,scriptGuid=lua->guid;
    Scene::SpriteComponent sprite;sprite.assetPath="Assets/画像/白.png";
    const auto expected=std::filesystem::canonical(path);
    Need(Assets::ResolveSpriteReference(sprite,registry,fixture,error)==expected && error.empty(),"Unicode path reference");
    std::filesystem::current_path(fixture/"Unrelated");Need(Assets::ResolveSpriteReference(sprite,registry,fixture,error)==expected,"cwd redirected Sprite");std::filesystem::current_path(previous);
    sprite.assetGuid=guid;sprite.assetPath="Assets/stale-display-hint.png";
    Need(Assets::ResolveSpriteReference(sprite,registry,fixture,error)==expected,"GUID did not override stale hint");
    const auto refused=[&](const Scene::SpriteComponent& input,const char* message){Need(Assets::ResolveSpriteReference(input,registry,fixture,error).empty() && !error.empty(),message);};
    sprite.assetPath="Assets/画像/白.png";sprite.assetGuid=std::string(16,'f');refused(sprite,"Unknown GUID fell back to hint");
    sprite.assetGuid=scriptGuid;refused(sprite,"Script GUID accepted as Sprite texture");
    sprite.assetGuid=guid;
    const auto metadata=Assets::AssetRegistry::MetadataPath(path);const auto metadataBytes=Read(metadata);
    auto changed=Scene::SceneValue::parse(metadataBytes);changed["guid"]=std::string(16,'e');Write(metadata,changed.dump());refused(sprite,"Stale GUID accepted replacement identity");Write(metadata,metadataBytes);
    Need(Assets::ResolveSpriteReference(sprite,registry,fixture,error)==expected,"Metadata repair did not recover");
    sprite.assetGuid.clear();sprite.assetPath="Assets/script.lua";refused(sprite,"Nontexture path admitted");sprite.assetPath="../escape.png";refused(sprite,"Parent path admitted");
    sprite.assetPath.clear();refused(sprite,"Unassigned draft resolved implicit image");
    // A GUID registry may belong to another project. Its valid live identity
    // must still not escape the explicitly supplied owning root.
    sprite.assetGuid=guid;Need(Assets::ResolveSpriteReference(sprite,registry,fixture/"Unrelated",error).empty() && !error.empty(),"Foreign registry escaped owner root");
    sprite.assetGuid=guid;
    const auto source=Read(path);std::filesystem::remove(path);refused(sprite,"Deleted image resolved");Write(path,source);
    std::filesystem::current_path(previous);std::filesystem::remove_all(fixture);fixture.clear();
    std::cout<<"PASS: Sprite project-relative Unicode/cwd-independent reference, authoritative live GUID/stale hint, wrong-type/missing/replaced/deleted/foreign-root/unassigned refusal and metadata repair (not image decoding or concurrent-writer safety)\n";return 0;
}catch(const std::exception& exception){std::filesystem::current_path(previous);std::cerr<<"FAIL: "<<exception.what()<<'\n';if(!fixture.empty())std::cerr<<"SPRITE_REFERENCE_FIXTURE_RETAINED="<<fixture<<'\n';return 1;}}
