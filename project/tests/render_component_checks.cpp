#include "engine/scene/entity/EntityWorld.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){using namespace Engine::Scene;try{
    EntityWorld world;const auto id=world.CreateEntity("Rendering");
    const auto legacy=world.Capture();
    Need(!legacy["entities"][0].contains("light") && !legacy["entities"][0].contains("material"),"Legacy capture gained render components");
    EntityWorld restored;std::string error;Need(restored.Restore(legacy,error),error.c_str());
    Need(!restored.Find(id)->hasLight && !restored.Find(id)->hasMaterial && !restored.Find(id)->material.lighting,"Legacy default became lit");
    LightComponent light;MaterialComponent material;
    Need(light.IsValid() && material.IsValid(),"Invalid default components");
    Need(DecodeLight(crude_json::object{},light) && DecodeMaterial(crude_json::object{},material),"Empty/default object refused");
    for(const auto kind:{LightKind::Directional,LightKind::Point,LightKind::Spot}){
        light.kind=kind;light.enabled=false;light.color={0.2f,0.4f,0.8f};light.intensity=42;light.range=75;light.decay=3;
        light.innerAngle=0;light.outerAngle=MaximumLightAngle;
        Need(world.SetLight(id,light),"Light setter failed");
        material.lighting=true;material.color={0.3f,0.6f,0.9f,0.5f};material.shininess=128;
        material.roughness=0.4f;material.reflectionStrength=0.7f;material.textureInfluence=0.2f;
        Need(world.SetMaterial(id,material),"Material setter failed");
        const auto captured=world.Capture();Need(restored.Restore(captured,error) && restored.Capture().dump()==captured.dump(),"Render component roundtrip lost fields");
        Need(restored.Find(id)->hasLight && restored.Find(id)->light.kind==kind && !restored.Find(id)->light.enabled && restored.Find(id)->hasMaterial,"Light presence/type or disabled state lost");
    }
    const auto before=world.Capture().dump();const auto high=world.AllocationHighWatermark();
    auto invalidLight=light;invalidLight.kind=static_cast<LightKind>(99);
    Need(!world.SetLight(id,invalidLight),"Unknown native kind accepted");
    {auto retained=light;Need(!DecodeLight(EncodeLight(invalidLight),retained) && EncodeLight(retained).dump()==EncodeLight(light).dump(),"Unknown native kind silently serialized as Spot");}
    invalidLight=light;invalidLight.intensity=std::numeric_limits<float>::infinity();Need(!world.SetLight(id,invalidLight),"Infinite light accepted");
    auto invalidMaterial=material;invalidMaterial.color.w=-1;Need(!world.SetMaterial(id,invalidMaterial),"Invalid native material accepted");
    Need(!world.SetLight(999,light) && !world.SetMaterial(999,material) && world.Capture().dump()==before,"Refused setter changed World");
    const auto rejectLight=[&](const SceneValue& value){
        auto output=light;const auto previous=EncodeLight(output).dump();Need(!DecodeLight(value,output) && EncodeLight(output).dump()==previous,"Invalid Light replaced decode output");
        auto document=world.Capture();document["entities"][0]["light"]=value;
        Need(!world.Restore(document,error) && !error.empty() && world.Capture().dump()==before && world.AllocationHighWatermark()==high,"Invalid Light published World/IDs");
    };
    rejectLight(nullptr);rejectLight(true);rejectLight(crude_json::array{});
    for(const auto* type:{"sphere","POINT",""}){auto v=EncodeLight(light);v["type"]=type;rejectLight(v);}
    {auto v=EncodeLight(light);v["type"]=1.0;rejectLight(v);v=EncodeLight(light);v["enabled"]=1.0;rejectLight(v);v=EncodeLight(light);v["unknown"]=true;rejectLight(v);}
    for(const auto* field:{"intensity","range","decay","innerAngle","outerAngle"}){
        auto v=EncodeLight(light);v[field]="1";rejectLight(v);
        v[field]=-1e-100;rejectLight(v);v[field]=std::numeric_limits<double>::infinity();rejectLight(v);
    }
    {auto v=EncodeLight(light);v["color"]=crude_json::array{1.0,1.0};rejectLight(v);v["color"]=crude_json::array{1.0,-1e-100,1.0};rejectLight(v);v["color"]=crude_json::array{1.0,1.01,1.0};rejectLight(v);}
    {auto v=EncodeLight(light);v["innerAngle"]=1.0;v["outerAngle"]=0.5;rejectLight(v);v=EncodeLight(light);v["range"]=0.0;rejectLight(v);}
    const auto rejectMaterial=[&](const SceneValue& value){
        auto output=material;const auto previous=EncodeMaterial(output).dump();Need(!DecodeMaterial(value,output) && EncodeMaterial(output).dump()==previous,"Invalid Material replaced decode output");
        auto document=world.Capture();document["entities"][0]["material"]=value;
        Need(!world.Restore(document,error) && world.Capture().dump()==before && world.AllocationHighWatermark()==high,"Invalid Material published World/IDs");
    };
    rejectMaterial(nullptr);rejectMaterial(false);rejectMaterial(crude_json::array{});
    for(const auto* field:{"shininess","roughness","reflectionStrength","textureInfluence"}){
        auto v=EncodeMaterial(material);v[field]=true;rejectMaterial(v);v[field]=-1e-100;rejectMaterial(v);
        v[field]=std::numeric_limits<double>::quiet_NaN();rejectMaterial(v);
    }
    {auto v=EncodeMaterial(material);v["guid"]="";rejectMaterial(v);v["guid"]="not-an-asset";rejectMaterial(v);}
    {auto v=EncodeMaterial(material);v["lighting"]=0.0;rejectMaterial(v);v=EncodeMaterial(material);v["color"]=crude_json::array{1.0,1.0,1.0};rejectMaterial(v);v=EncodeMaterial(material);v["roughness"]=1.01;rejectMaterial(v);v=EncodeMaterial(material);v["shininess"]=0.0;rejectMaterial(v);v=EncodeMaterial(material);v["unknown"]=true;rejectMaterial(v);}
    world.RemoveLight(id);world.RemoveMaterial(id);
    Need(world.Capture().dump()==legacy.dump(),"Removal failed to restore omitted legacy shape");
    auto environment=SceneEnvironment::Night();
    environment.postEffect=PostEffectType::Vignette;
    Need(world.SetEnvironment(environment),"Night profile rejected");
    const auto night=world.Capture();
    Need(night["version"].get<double>()==3 && restored.Restore(night,error) &&
        restored.Capture().dump()==night.dump(),"Scene environment roundtrip missed version or settings");
    auto rejected=night;
    rejected["environment"]["exposure"]=9.0;
    Need(!restored.Restore(rejected,error) && restored.Capture().dump()==night.dump(),"Malformed environment replaced World");
    rejected=night;
    rejected["environment"]["postEffect"]="unsupported";
    Need(!restored.Restore(rejected,error) && restored.Capture().dump()==night.dump(),"Unknown effect replaced World");
    Need(restored.Restore(legacy,error) && restored.GetEnvironment().IsDefault(),"Legacy Scene retained environment from the previous Scene");
    world.Swap(restored);
    Need(world.GetEnvironment().IsDefault() && restored.Capture().dump()==night.dump(),"World Swap dropped scene-wide settings");
    restored.Clear();
    Need(restored.GetEnvironment().IsDefault() && !restored.Capture().contains("environment"),"New World retained environment");
    std::cout<<"PASS: authored Directional/Point/Spot and Material roundtrip/default-unlit/removal, strict type/finite/range/cone/color/schema validation and refusal preserving World/IDs/output (not rendering)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
