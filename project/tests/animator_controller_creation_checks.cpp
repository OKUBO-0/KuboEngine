#include "engine/editor/AnimatorControllerCreation.h"
#include "engine/editor/AnimatorControllerSession.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Creation fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Need(stream.good(),"Creation fixture read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-create-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const char* scenario:{"success","rollback","foreign-source","source-before","meta-before","rollback-occupied","reentrant","move","invalid","outside","reserved","missing-parent","occupied-source","occupied-meta","competing","device-name","stream-name","wrong-extension","invalid-utf8","move-before"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets/Graphs");Write(root/"entry.kubo",R"({"name":"Controller Creation","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
            AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),error.c_str());const auto original=root/"Assets/original.animfsm";const auto oldBytes=document.Text();Write(original,oldBytes);
            AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto oldGuid=registry.FindByPath(original)->guid;const auto oldMeta=Read(AssetRegistry::MetadataPath(original));
            auto state=document.Definition().layers[0].states[0];state.name="Created ### state";Need(AnimatorControllerEdits(document).ReplaceState(0,0,state,error),error.c_str());
            std::string relative="Graphs/新規.animfsm";if(mode=="outside")relative="../outside.animfsm";if(mode=="reserved")relative=".kubo/forbidden.animfsm";if(mode=="missing-parent")relative="Missing/new.animfsm";
            if(mode=="device-name")relative="Graphs/CON.animfsm";if(mode=="stream-name")relative="Graphs/new.animfsm:stream";if(mode=="wrong-extension")relative="Graphs/new.lua";
            const auto destination=root/"Assets"/Engine::Base::PathFromUtf8(relative);const auto meta=AssetRegistry::MetadataPath(destination);
            if(mode=="invalid-utf8")relative=std::string(1,char(0xff));
            if(mode=="move-before"){fs::create_directory(root/"Assets/.kubo");Write(root/"Assets/.kubo/asset-move.pending.json","{}");}
            if(mode=="occupied-source")Write(destination,"foreign source");if(mode=="occupied-meta")Write(meta,"foreign metadata");
            fs::path directory;AnimatorControllerRevision published;bool success=false;
            {
                AnimatorControllerCreation creation;const bool prepared=creation.Prepare(project,relative,mode=="invalid"?"{}":document.Text(),error);
                if(mode=="invalid" || mode=="outside" || mode=="reserved" || mode=="missing-parent" || mode=="occupied-source" || mode=="occupied-meta" || mode=="device-name" || mode=="stream-name" || mode=="wrong-extension" || mode=="invalid-utf8" || mode=="move-before"){
                    Need(!prepared && creation.Directory().empty(),"Invalid/occupied creation prepared files");
                    if(mode=="occupied-source")Need(Read(destination)=="foreign source" && !fs::exists(meta),"Occupied source modified");
                    if(mode=="occupied-meta")Need(Read(meta)=="foreign metadata" && !fs::exists(destination),"Occupied metadata modified");continue;
                }
                Need(prepared && !creation.IsPublished() && !creation.PublishedRevision(),error.c_str());directory=creation.Directory();
                Need(!creation.Prepare(project,"Graphs/other.animfsm",document.Text(),error),"Creation reused after Prepare");
                Need(!fs::exists(destination) && !fs::exists(meta),"Prepare published pair early");
                auto foreign=CreateFileW((directory/"source.stage").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(foreign!=INVALID_HANDLE_VALUE){CloseHandle(foreign);throw std::runtime_error("Stage ownership permitted write");}
                if(mode=="competing"){AnimatorControllerCreation other;Need(!other.Prepare(project,"Graphs/other.animfsm",document.Text(),error) && other.Directory().empty(),"Competing creation bypassed project lock");}
                if(mode=="source-before")Write(destination,"foreign source");if(mode=="meta-before")Write(meta,"foreign metadata");
                const auto hook=[&]{
                    if(mode=="rollback")throw std::runtime_error("after metadata injection");
                    if(mode=="foreign-source")Write(destination,"foreign source");
                    if(mode=="rollback-occupied"){Write(directory/"metadata.stage","foreign stage");throw std::runtime_error("occupied rollback injection");}
                    if(mode=="move")Write(root/"Assets/.kubo/asset-move.pending.json","{}");
                    if(mode=="reentrant"){std::string nested;Need(!creation.Publish(nested) && creation.HasPublishedMetadata(),"Reentrant creation rolled back active publisher");}
                };
                success=creation.Publish(error,hook);
                Need(success==(mode=="success" || mode=="reentrant" || mode=="competing"),"Creation wrong publish result");
                Need(!creation.Publish(error),"Creation repeated without recovery");
                if(success){Need(creation.HasPublishedMetadata() && creation.PublishedRevision(),"New pair revision missing");published=*creation.PublishedRevision();}
                if(mode=="rollback-occupied")Need(creation.HasPublishedMetadata(),"Failed rollback lost metadata location status");
            }
            const auto journal=crude_json::value::parse(Read(directory/"pending.json"));
            Need(journal["format"].get<crude_json::string>()=="kubo-controller-creation-1" && journal["sourceBytes"].get<crude_json::string>()==document.Text() && Read(original)==oldBytes,"Creation intent/source preservation failed");
            Need(Read(AssetRegistry::MetadataPath(original))==oldMeta,"Creation changed original metadata");
            if(success){
                Need(published.guid.size()==32 && published.guid!=oldGuid && published.source==destination && Read(destination)==document.Text() && Read(meta)==published.metadataBytes && !fs::exists(directory/"source.stage") && !fs::exists(directory/"metadata.stage"),"Created pair/GUID/stages incorrect");
                AssetRegistry fresh;Need(fresh.Scan(root/"Assets",error,AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());Need(fresh.FindByPath(destination)->guid==published.guid,"New GUID registration mismatch");
                {HeldAnimatorControllerAsset read;Need(!read.Open(project,fresh,published.guid,error,&published),"Pending pair admitted through normal reader");}
                HeldAnimatorControllerWriter writer;Need(!writer.Open(project,fresh,published,error),"Unacknowledged new pair allowed writer");
                AnimatorControllerSession session;Need(!session.Open(project,fresh,published.guid,error),"Normal session opened pending creation");
                ControllerCreationReport report;Need(AnimatorControllerCreationRecovery::Inspect(project,published.guid,report,error),error.c_str());fs::path archive;
                Need(AnimatorControllerCreationRecovery::ArchiveResolved(project,published.guid,report,archive,error),error.c_str());
                {HeldAnimatorControllerAsset read;Need(read.Open(project,fresh,published.guid,error,&published),"Resolved pair did not admit through real loader/native identities");}
            }else{
                Need(Read(directory/"source.stage")==document.Text(),"Rejected creation lost source stage");
                if(mode=="foreign-source" || mode=="source-before")Need(Read(destination)=="foreign source","Foreign source overwritten");else Need(!fs::exists(destination),"Rejected creation published source");
                if(mode=="meta-before")Need(Read(meta)=="foreign metadata","Foreign metadata overwritten");
                else if(mode=="rollback-occupied")Need(Read(directory/"metadata.stage")=="foreign stage" && Read(meta)==journal["metadataBytes"].get<crude_json::string>(),"Occupied rollback lost owned metadata/foreign stage");
                else Need(!fs::exists(meta) && Read(directory/"metadata.stage")==journal["metadataBytes"].get<crude_json::string>(),"Rejected creation failed metadata restoration");
            }
        }
        std::cout<<"CREATION_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: new Controller pair/random GUID/staged immutable intent/no-overwrite/metadata-first rollback/foreign+occupied preservation/UTF+path+root guards/reentry/project lock/real registry+loader admission/pending writer+Save guards (creation recovery+ack+GUI SaveAs pending)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained creation fixture: "<<fixture<<'\n';return 1;}
}
