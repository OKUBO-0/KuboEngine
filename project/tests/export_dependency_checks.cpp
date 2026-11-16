#include "engine/assets/ExportAssetDependencies.h"
#include <fstream>
#include <iostream>
namespace {
namespace fs=std::filesystem;
using namespace Engine::Assets;
void Require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void Write(const fs::path& path,const std::string& text) {fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<text;Require(bool(file),"Write fixture failed");}
bool Reject(const fs::path& root,const char* model,const char* reason) {
    try {ValidateExportModelDependencies(root,{Engine::Base::PathFromUtf8(model)},{"Content"});}
    catch(const std::exception& exception) {return std::string(exception.what()).find(reason)!=std::string::npos;}
    return false;
}
}
int main() {
    const auto root=fs::temp_directory_path()/(L"kubo-export-dependencies-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root/"Content/Models");
        const auto model=root/"Content/Models/model.obj",material=root/L"Content/Models/材質.mtl",image=root/L"Content/Models/色 #1.png";
        Write(model,"mtllib 材質.mtl 材質.mtl # duplicate\nv 0 0 0\n");Write(material,"map_Kd \"色 #1.png\"\n");Write(image,"image");
        const auto graph=ValidateExportModelDependencies(root,{"Content/Models/model.obj"},{"Content"});
        Require(graph.files==3 && graph.edges==2,"Shared OBJ/MTL transitive closure failed");
        Require(ExportPathWithin(Engine::Base::PathFromUtf8(Engine::Base::PathToUtf8(image)),root/"Content"),"Generic/native slash comparison failed");
        Require(!ExportPathWithin(root/"ContentExtra/file.png",root/"Content"),"Sibling root-prefix collision accepted");
        for(const auto& entry:fs::recursive_directory_iterator(root)) Require(!entry.path().wstring().ends_with(L".kubo-meta.json"),"Read-only validation created metadata");
        Write(material,"map_Kd missing.png\n");Require(Reject(root,"Content/Models/model.obj","dependency missing"),"Missing Texture was permitted");
        Write(material,"map_Kd -s 1 1 1 image.png\n");Require(Reject(root,"Content/Models/model.obj","inspection failed"),"Unsupported MTL options passed");
        Write(material,"map_Kd ../../outside.png\n");Write(root/"outside.png","outside copied roots");
        Require(Reject(root,"Content/Models/model.obj","outside copied"),"Uncopied external Texture passed");
        Write(material,"map_Kd ../../../escape.png\n");Require(Reject(root,"Content/Models/model.obj","inspection failed"),"Project escape passed");
        Write(material,"map_Kd \"色 #1.png\"\n");
        // Unrelated inspection issues do not poison an otherwise valid closure.
        Write(root/"Content/unrelated.gltf","{");
        Require(ValidateExportModelDependencies(root,{"Content/Models/model.obj"},{"Content"}).files==3,"Unrelated graph diagnostic blocked closure");
        const auto gltf=root/"Content/Models/model.gltf";
        Write(root/L"Content/Models/頂点 bin.bin","vertices");
        Write(gltf,"{\"buffers\":[{\"uri\":\"頂点%20bin.bin\"},{\"uri\":\"data:application/octet-stream;base64,AAAA\"}],\"images\":[{\"uri\":\"色%20%231.png\"}]}");
        Require(Reject(root,"Content/Models/model.gltf","Unsupported decoded glTF URI"),"Unsupported decoded URI was not refused like Editor parser");
        Write(gltf,"{\"buffers\":[{\"uri\":\"頂点%20bin.bin\"},{\"uri\":\"data:application/octet-stream;base64,AAAA\"}],\"images\":[{\"bufferView\":0}]}");
        const auto gltfGraph=ValidateExportModelDependencies(root,{"Content/Models/model.gltf"},{"Content"});
        Require(gltfGraph.files==2 && gltfGraph.edges==1,"glTF Unicode URI/embedded dependencies failed");
        Write(gltf,"{\"buffers\":[{\"uri\":\"missing.bin\"}]}");Require(Reject(root,"Content/Models/model.gltf","dependency missing"),"Missing glTF buffer passed");
        Write(gltf,"{\"extensionsUsed\":[\"unknown\"]}");Require(Reject(root,"Content/Models/model.gltf","inspection failed"),"Unknown glTF extensions passed");
        Write(root/"Content/Models/specular.png","fixture texture");
        const auto extended=crude_json::value::parse(R"({"extensionsUsed":["KHR_materials_ior","KHR_materials_specular"],"extensionsRequired":["KHR_materials_ior"],"materials":[{"extensions":{"KHR_materials_ior":{"ior":1.5},"KHR_materials_specular":{"specularFactor":0.5,"specularColorFactor":[2,0,1],"specularTexture":{"index":0},"specularColorTexture":{"index":0,"texCoord":1}}}}],"textures":[{"source":0}],"images":[{"uri":"specular.png"}]})");
        Write(gltf,extended.dump());
        Require(ValidateExportModelDependencies(root,{"Content/Models/model.gltf"},{"Content"}).files==2,"Known material extension texture closure failed");
        for(double ior:{0.0,1.0,2.5}) {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_ior"]["ior"]=ior;Write(gltf,doc.dump());Require(ValidateExportModelDependencies(root,{"Content/Models/model.gltf"},{"Content"}).edges==1,"Valid IOR rejected");}
        auto bad=[&](crude_json::value doc,const char* why){Write(gltf,doc.dump());Require(Reject(root,"Content/Models/model.gltf","inspection failed"),why);};
        {auto doc=extended;doc["extensionsUsed"]=crude_json::value::parse(R"(["KHR_materials_ior","KHR_materials_ior"])");bad(doc,"Duplicate extensions accepted");}
        {auto doc=extended;doc["extensionsRequired"]=crude_json::value::parse(R"(["KHR_materials_specular","KHR_materials_specular"])");bad(doc,"Duplicate required extensions accepted");}
        {auto doc=extended;doc["extensionsUsed"]=crude_json::value::parse(R"(["KHR_materials_specular"])");bad(doc,"Required extension outside used accepted");}
        {auto doc=extended;doc["extensionsUsed"]=1.0;bad(doc,"Malformed declaration accepted");}
        {auto doc=extended;doc["extensionsUsed"]=crude_json::value::parse(R"([true])");bad(doc,"Non-string extension accepted");}
        {auto doc=extended;doc["materials"][0]["extensions"]["VENDOR_unknown"]=crude_json::object{};bad(doc,"Undeclared extension payload accepted");}
        {auto doc=extended;doc["extensions"]=doc["materials"][0]["extensions"];bad(doc,"Known extension at root accepted");}
        {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_ior"]=false;bad(doc,"Invalid extension payload accepted");}
        for(double ior:{-1.0,0.5}) {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_ior"]["ior"]=ior;bad(doc,"Invalid IOR accepted");}
        for(double factor:{-1.0,1.1}) {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_specular"]["specularFactor"]=factor;bad(doc,"Invalid specular factor accepted");}
        {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_specular"]["specularColorFactor"]=crude_json::value::parse("[1,1]");bad(doc,"Invalid specular color length accepted");}
        {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_specular"]["specularColorFactor"]=crude_json::value::parse("[1,-1,1]");bad(doc,"Negative specular color accepted");}
        for(double index:{-1.0,0.5,1.0}) {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_specular"]["specularTexture"]["index"]=index;bad(doc,"Invalid texture index accepted");}
        {auto doc=extended;doc["textures"][0]["source"]=1.0;bad(doc,"Missing image index accepted");}
        {auto doc=extended;doc["images"][0]=crude_json::object{};bad(doc,"Missing image storage accepted");}
        {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_specular"]["specularTexture"]["uri"]="unknown.png";bad(doc,"Uninspected extension texture field accepted");}
        {auto doc=extended;doc["materials"][0]["extensions"]["KHR_materials_specular"]["specularTexture"]["extensions"]=crude_json::value::parse(R"({"VENDOR_unknown":{"uri":"hidden.png"}})");bad(doc,"Nested undeclared extension accepted");}
        {auto doc=extended;doc["extras"]=crude_json::value::parse(R"({"extensions":{"custom-metadata":{"uri":"not-a-glTF-dependency"}}})");Write(gltf,doc.dump());Require(ValidateExportModelDependencies(root,{"Content/Models/model.gltf"},{"Content"}).files==2,"App metadata wrongly treated as glTF extension");}
        {auto doc=extended;doc["images"][0]["uri"]="missing-specular.png";Write(gltf,doc.dump());Require(Reject(root,"Content/Models/model.gltf","dependency missing"),"Missing extension image passed");}
        {auto doc=extended;doc["images"][0]["uri"]="../../../escape.png";bad(doc,"Extension texture root escape passed");}
        Write(gltf,std::string(1000,'[')+"0"+std::string(1000,']'));Require(Reject(root,"Content/Models/model.gltf","structure limit"),"Deep glTF passed");
        Write(gltf,std::string(8*1024*1024+1,' '));Require(Reject(root,"Content/Models/model.gltf","8 MiB"),"Oversized source passed");
        Write(root/"Content/unsupported.fbx","fixture only");
        Require(ValidateExportModelDependencies(root,{"Content/unsupported.fbx"},{"Content"}).uninspectedModels==1,"Unknown parser format was falsely marked inspected");
        Write(root/"Content/legacy.dae","fixture only");
        Require(ValidateExportModelDependencies(root,{"Content/legacy.dae"},{"Content"}).uninspectedModels==1,"Legacy path-only Model format was falsely marked inspected");
        // Existing malformed identities must not reach the recursive parser.
        Write(fs::path(model.wstring()+L".kubo-meta.json"),std::string(1000,'[')+"0"+std::string(1000,']'));
        Require(Reject(root,"Content/Models/model.obj","Asset JSON structure limit"),"Deep metadata reached registry parser");
        Write(fs::path(model.wstring()+L".kubo-meta.json"),std::string(1,static_cast<char>(0xff)));
        Require(Reject(root,"Content/Models/model.obj","UTF-8"),"Malformed metadata UTF-8 accepted");
        fs::remove_all(root);
        std::cout<<"PASS: shared read-only OBJ/MTL/glTF export closure, missing/uncopied/unsafe dependencies, bounded diagnostics and unsupported-format accounting\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<"; retained fixture: "<<Engine::Base::PathToUtf8(root)<<'\n';return 1;}
}
