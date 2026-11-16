#include "engine/assets/ExportAssetDependencies.h"
#include <fstream>
#include <iostream>
namespace {
namespace fs=std::filesystem;using namespace Engine::Assets;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Word(std::string& out,uint32_t value){for(int byte=0;byte<4;++byte)out+=static_cast<char>((value>>(byte*8))&255);}
void SetWord(std::string& out,size_t offset,uint32_t value){for(int byte=0;byte<4;++byte)out[offset+byte]=static_cast<char>((value>>(byte*8))&255);}
std::string Glb(std::string json,std::string bin={},bool includeBin=false,bool unknown=false){
    while(json.size()%4)json+=' ';while(bin.size()%4)bin+='\0';
    std::string out;Word(out,0x46546c67);Word(out,2);Word(out,static_cast<uint32_t>(20+json.size()+(includeBin?8+bin.size():0)+(unknown?12:0)));
    Word(out,static_cast<uint32_t>(json.size()));Word(out,0x4e4f534a);out+=json;
    if(includeBin){Word(out,static_cast<uint32_t>(bin.size()));Word(out,0x004e4942);out+=bin;}
    if(unknown){Word(out,4);Word(out,0x12345678);out+="abcd";}return out;
}
void Write(const fs::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;Require(bool(out),"Fixture write failed");}
}
int main(){
    const auto root=fs::temp_directory_path()/(L"kubo-glb-dependencies-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(root/"Assets");const auto model=root/L"Assets/モデル.glb";
        Write(root/L"Assets/色.png","image");Write(root/L"Assets/頂点 bin.bin","vertices");
        const auto good=Glb(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1},{"uri":"頂点%20bin.bin"}],"images":[{"uri":"色.png"}]})","x",true,true);
        Write(model,good);
        auto closure=ValidateExportModelDependencies(root,{L"Assets/モデル.glb"},{"Assets"});
        Require(closure.files==3 && closure.edges==2 && closure.uninspectedModels==0,"GLB embedded/external/Unicode closure failed");
        Write(model,Glb(R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_specular","KHR_materials_ior"],"materials":[{"extensions":{"KHR_materials_ior":{"ior":0},"KHR_materials_specular":{"specularTexture":{"index":0}}}}],"textures":[{"source":0}],"images":[{"uri":"色.png"}]})"));
        closure=ValidateExportModelDependencies(root,{L"Assets/モデル.glb"},{"Assets"});
        Require(closure.files==2 && closure.edges==1,"GLB material extension external image closure failed");
        Write(model,Glb(R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_specular"],"materials":[{"extensions":{"KHR_materials_specular":{"specularColorTexture":{"index":0}}}}],"textures":[{"source":0}],"buffers":[{"byteLength":1}],"bufferViews":[{"buffer":0,"byteLength":1}],"images":[{"bufferView":0,"mimeType":"image/png"}]})","x",true));
        closure=ValidateExportModelDependencies(root,{L"Assets/モデル.glb"},{"Assets"});
        Require(closure.files==1 && closure.edges==0,"GLB material extension embedded image fabricated external dependency");
        const auto reject=[&](std::string bytes,const char* why){Write(model,bytes);bool failed=false;try{ValidateExportModelDependencies(root,{L"Assets/モデル.glb"},{"Assets"});}catch(const std::exception&){failed=true;}Require(failed,why);};
        auto bad=good;SetWord(bad,0,0);reject(bad,"Bad GLB magic accepted");
        bad=good;SetWord(bad,4,1);reject(bad,"GLB v1 accepted");
        bad=good;SetWord(bad,8,static_cast<uint32_t>(bad.size()-1));reject(bad,"Bad total extent accepted");
        bad=good;SetWord(bad,12,3);reject(bad,"Unaligned chunk accepted");
        bad=good;SetWord(bad,12,0xfffffffc);reject(bad,"Overflowing chunk length accepted");
        bad=good;SetWord(bad,16,0x004e4942);reject(bad,"First BIN chunk accepted");
        bad=Glb(R"({"asset":{"version":"2.0"}})");Word(bad,4);Word(bad,0x4e4f534a);bad+="{}  ";SetWord(bad,8,static_cast<uint32_t>(bad.size()));reject(bad,"Duplicate JSON accepted");
        bad=Glb(R"({"asset":{"version":"2.0"}})","abcd",true);Word(bad,0);Word(bad,0x004e4942);SetWord(bad,8,static_cast<uint32_t>(bad.size()));reject(bad,"Duplicate BIN accepted");
        reject(Glb(R"({"asset":{"version":"1.0"}})"),"Wrong JSON asset version accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1}]})"),"Missing embedded BIN accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":5}]})","abcd",true),"Short BIN accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1}]})","abcd",true),"Nonzero BIN padding accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1}]})","12345678",true),"Excess BIN padding accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,AA=="},{"byteLength":1}]})","x",true),"Second implicit buffer accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"images":[{"uri":"absent.png"}]})"),"Missing external image accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"images":[{"uri":"../../escape.png"}]})"),"Escaping URI accepted");
        reject(Glb(R"({"asset":{"version":"2.0"},"extensionsRequired":["unknown"]})"),"Undeclared required extension accepted");
        reject(Glb(std::string(1000,'[')+"0"+std::string(1000,']')),"Deep JSON accepted");
        reject(Glb(std::string(1,static_cast<char>(0xff))),"Malformed JSON UTF-8 accepted");
        reject(Glb(std::string("{}\0 ",4)),"NUL JSON padding accepted");
        reject(Glb(std::string(8*1024*1024+4,' ')),"Oversized JSON accepted");
        Write(model,Glb(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":12582912}]})",std::string(12*1024*1024,'x'),true));
        const auto content=ReadGlbDependencyContent(model);
        Require(content.json.size()<1024 && content.binLength==12*1024*1024,"Large BIN was not skipped with bounded JSON");
        Require(ValidateExportModelDependencies(root,{L"Assets/モデル.glb"},{"Assets"}).files==1,"Large embedded-only GLB rejected");
        bool cancelled=false;try{ReadGlbDependencyContent(model,[]{return true;});}catch(const std::exception&){cancelled=true;}Require(cancelled,"GLB reader ignored cancellation");
        for(const auto& entry:fs::recursive_directory_iterator(root))Require(!entry.path().wstring().ends_with(L".kubo-meta.json"),"Read-only GLB scan created sidecars");
        fs::remove_all(root);std::cout<<"PASS: shared GLB v2 container/JSON/BIN bounds, external Unicode dependencies, malformed refusal, large-BIN streaming and read-only/cancellation\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"; retained: "<<Engine::Base::PathToUtf8(root)<<'\n';return 1;}
}
