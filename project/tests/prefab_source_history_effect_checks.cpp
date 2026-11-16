#include "engine/editor/PrefabSourceHistoryEffect.h"
#include "engine/editor/PrefabApplyPlan.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/EntityDocumentState.h"
#include <fstream>
#include <iostream>
#include <map>
#include <cstdlib>
#include <new>
namespace {bool denySourceReplayAllocations=false;size_t deniedSourceReplayAllocations=0;}
void* operator new(size_t bytes){if(denySourceReplayAllocations){++deniedSourceReplayAllocations;throw std::bad_alloc();}if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
namespace {
namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Scene;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);Require(in.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
PrefabFileIdentity Identity(const fs::path& path){const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);Require(handle!=INVALID_HANDLE_VALUE,"Fixture identity open failed");BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(handle,&info)!=0;CloseHandle(handle);Require(ok,"Fixture identity failed");return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};}
std::map<fs::path,std::string> Snapshot(const fs::path& root){std::map<fs::path,std::string> result;for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),Read(entry.path()));return result;}
// Test-only decorator observes the actual native lease before forwarding End;
// no callback or bypass is added to the production native provider.
class ObservedNativeEffect final:public EntityHistoryEffect {
    bool acquired_=false,undo_=false;
public:
    std::shared_ptr<PrefabSourceHistoryEffect> native;std::function<void(bool)> observe;
    bool observationFailed=false;int observations=0;
    explicit ObservedNativeEffect(std::shared_ptr<PrefabSourceHistoryEffect> effect):EntityHistoryEffect(effect->Bytes()+sizeof(ObservedNativeEffect)),native(std::move(effect)){}
    bool Begin(bool undo,std::string& error) override{
        const bool result=native->Begin(undo,error);
        if(result){acquired_=true;undo_=undo;denySourceReplayAllocations=true;}return result;
    }
    void End() noexcept override{
        denySourceReplayAllocations=false;
        if(acquired_){++observations;try{Require(deniedSourceReplayAllocations==0,"Native replay memory/document publication allocated after Begin");if(observe)observe(undo_);}catch(...){observationFailed=true;}}
        acquired_=false;native->End();
    }
};
struct Child {
    PROCESS_INFORMATION info{};
    Child(const fs::path& root,bool undo){wchar_t executable[32768];Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Child executable missing");std::wstring command=L"\""+std::wstring(executable)+(undo?L"\" --child \"":L"\" --child-redo \"")+root.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Child launch failed");}
    void Kill(){Require(TerminateProcess(info.hProcess,91)!=0 && WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Owned child termination failed");DWORD code=0;Require(GetExitCodeProcess(info.hProcess,&code) && code==91,"Owned child exit mismatch");}
    ~Child(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int ChildMain(const fs::path& root,bool undo){
    Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());Engine::Assets::AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());
    const auto file=root/L"Assets/ソース.prefab";PrefabSourceRecoveryReport receipt;receipt.guid=registry.FindByPath(file)->guid;receipt.source=file;receipt.metadataBytes=Read(Engine::Assets::AssetRegistry::MetadataPath(file));receipt.metadataIdentity=Identity(Engine::Assets::AssetRegistry::MetadataPath(file));
    for(const auto& entry:fs::directory_iterator(root/"Assets/.kubo/prefab-archive"))if(entry.is_directory())receipt.directory=entry.path();
    const auto original=undo?receipt.directory/"original.prefab":file;const auto next=undo?file:receipt.directory/"next.prefab";
    receipt.originalBytes=Read(original);receipt.originalIdentity=Identity(original);receipt.nextBytes=Read(next);receipt.nextIdentity=Identity(next);
    PrefabSourceHistoryEffect effect(project,receipt,[&]{Write(root/"ready.pending","parked");fs::rename(root/"ready.pending",root/"ready");Sleep(INFINITE);});
    Require(effect.Begin(undo,error),error.c_str());return 2;
}
}
int wmain(int argc,wchar_t** argv){
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-history-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        if(argc==3 && (std::wstring(argv[1])==L"--child" || std::wstring(argv[1])==L"--child-redo"))return ChildMain(argv[2],std::wstring(argv[1])==L"--child");
        denySourceReplayAllocations=true;bool allocationProbe=false;try{auto* memory=::operator new(1);::operator delete(memory);}catch(const std::bad_alloc&){allocationProbe=true;}
        denySourceReplayAllocations=false;Require(allocationProbe && deniedSourceReplayAllocations==1,"Native replay allocation probe inactive");deniedSourceReplayAllocations=0;
        for(const auto& scenario:{"success","source-stale","source-replaced","metadata-stale","metadata-replaced","backup-replaced","duplicate-guid","pending","asset-move-pending","world-stale","callback-failure","late-source","unknown-pending","child-interrupted","child-redo-interrupted"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"History","startupScene":"Assets/Main.scene"})");
            Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
            EntityWorld source;source.CreateEntity("Before");source.CreateEntity("Child",1);PrefabValue definition;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());const auto file=root/L"Assets/ソース.prefab";Require(SavePrefabDocument(file,definition,error),error.c_str());
            Engine::Assets::AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());ProjectPrefabRevision revision;Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
            EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory construction;EntityId chosen=0,other=0;
            Require(InstantiatePrefab(world,definition,revision.guid,1,selection,construction,chosen,error) && InstantiatePrefab(world,definition,revision.guid,1,selection,construction,other,error),error.c_str());
            world.Find(chosen)->name="After";world.Find(3)->transform.position.x=7;world.Find(5)->active=false;Require(selection.Assign(world,{3,5},5,error),error.c_str());
            const auto beforeWorld=world.Capture();const auto beforeSelection=selection.Capture();PrefabApplyPlan plan;Require(BuildPrefabApplyPlan(world,chosen,definition,revision.guid,plan,error),error.c_str());
            PrefabSourceRecoveryReport receipt;fs::path archived;
            {PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,plan.source,error) && publication.Publish(error),error.c_str());}
            Require(PrefabSourceRecovery::Inspect(project,revision.guid,receipt,error) && PrefabSourceAcknowledgement::ArchiveResolved(project,revision.guid,receipt,archived,error),error.c_str());receipt.directory=archived;
            const auto pending=root/"Assets/.kubo/prefab-transactions"/revision.guid;
            if(mode=="child-interrupted" || mode=="child-redo-interrupted"){
                const bool undo=mode=="child-interrupted";
                if(!undo){PrefabSourceHistoryEffect prepare(project,receipt);Require(prepare.Begin(true,error),error.c_str());prepare.End();}
                {Child child(root,undo);const auto deadline=GetTickCount64()+5000;while(!fs::exists(root/"ready") && GetTickCount64()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT)Sleep(10);Require(fs::exists(root/"ready"),"Child failed before source park");child.Kill();}
                Require(!fs::exists(file) && Read(archived/"original.prefab")==revision.bytes && Read(archived/"next.prefab")==receipt.nextBytes && Read(revision.metadata)==revision.metadataBytes && fs::exists(pending/"history.json"),"Interrupted replay lost source pair/marker");
                const auto marker=PrefabValue::parse(Read(pending/"history.json"));Require(marker["format"].get<crude_json::string>()=="kubo-prefab-source-history-1" && marker["guid"].get<crude_json::string>()==revision.guid && marker["undo"].get<crude_json::boolean>()==undo && marker["intent"]["originalBytes"].get<crude_json::string>()==revision.bytes,"Interrupted marker lost exact recovery intent");HeldProjectPrefabWriter writer;Require(!writer.Open(project,registry,revision,error),"Interrupted history did not block new writer");continue;
            }
            auto effect=std::make_shared<PrefabSourceHistoryEffect>(project,receipt,[&]{
                if(mode=="callback-failure")throw std::runtime_error("Injected history park failure");
                if(mode=="late-source")Write(file,"foreign source owner");
                if(mode=="unknown-pending")Write(pending/"foreign-data","retain me");
            });
            auto observed=std::make_shared<ObservedNativeEffect>(effect);
            EntityHistory history;EntitySelection nextSelection;nextSelection.Replace(chosen);Require(history.Record(beforeWorld,plan.world,beforeSelection,nextSelection.Capture(),error,observed),error.c_str());history.Commit();world=std::move(plan.world);selection=std::move(nextSelection);
            const auto afterWorld=world.Capture();const auto afterSelection=selection.Capture();const auto saved=root/L"保存.scene";EntityWorld baseline;Require(baseline.Restore(beforeWorld,error) && baseline.Save(saved,error),error.c_str());EntityDocumentState document;document.Opened(baseline,saved);document.Observe(world);Require(document.IsDirty(),"Fixture applied Scene not dirty");
            observed->observe=[&](bool undo){
                Require(document.IsDirty()!=undo && world.Capture().dump()==(undo?beforeWorld:afterWorld).dump() && selection.Capture()==(undo?beforeSelection:afterSelection) && (undo?history.CanRedo():history.CanUndo()),"Native lease ended before World/selection/history/document publication");
                const auto writer=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);const auto code=GetLastError();if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);
                Require(writer==INVALID_HANDLE_VALUE && code==ERROR_SHARING_VIOLATION && fs::exists(pending/"history.json"),"Native source ownership/marker released before document publication");
            };
            if(mode=="source-stale")Write(file,receipt.nextBytes+"\n");
            if(mode=="metadata-stale")Write(revision.metadata,revision.metadataBytes+"\n");
            if(mode=="source-replaced"){Write(root/"replacement",receipt.nextBytes);fs::rename(file,root/"retained-source");fs::rename(root/"replacement",file);}
            if(mode=="metadata-replaced"){Write(root/"replacement",revision.metadataBytes);fs::rename(revision.metadata,root/"retained-metadata");fs::rename(root/"replacement",revision.metadata);}
            if(mode=="backup-replaced"){Write(archived/"foreign-backup",revision.bytes);fs::rename(archived/"original.prefab",archived/"retained-original");fs::rename(archived/"foreign-backup",archived/"original.prefab");}
            if(mode=="duplicate-guid"){Write(root/"Assets/duplicate.prefab",revision.bytes);Write(Engine::Assets::AssetRegistry::MetadataPath(root/"Assets/duplicate.prefab"),revision.metadataBytes);}
            if(mode=="pending"){fs::create_directory(pending);Write(pending/"foreign-owner","retain me");}
            if(mode=="asset-move-pending")Write(root/"Assets/.kubo/asset-move.pending.json","foreign pending owner");
            if(mode=="world-stale")world.Find(chosen)->active=false;
            const auto filesBefore=Snapshot(root);const auto heldWorld=world.Capture().dump();const auto heldBytes=history.StoredBytes();
            if(mode!="success" && mode!="unknown-pending"){
                const auto heldDocument=document;
                Require(!history.Undo(world,selection,error,&document) && document.HasSameState(heldDocument) && world.Capture().dump()==heldWorld && selection.Capture()==afterSelection && history.UndoCount()==1 && history.RedoCount()==0 && history.StoredBytes()==heldBytes,"Failed source replay published World/selection/history/document");
                if(mode=="late-source")Require(Read(file)=="foreign source owner" && Read(archived/"original.prefab")==revision.bytes && Read(archived/"next.prefab")==receipt.nextBytes && fs::exists(pending/"history.json") && effect->HasRetainedPending(),"Late collision lost source/marker");
                else Require(Snapshot(root)==filesBefore,"Rejected/rolled-back replay changed files");continue;
            }
            Require(history.Undo(world,selection,error,&document) && !observed->observationFailed && observed->observations==1,error.c_str());
            Require(world.Capture().dump()==beforeWorld.dump() && selection.Capture()==beforeSelection && !document.IsDirty() && Read(file)==revision.bytes && Identity(file)==revision.fileIdentity && Read(archived/"next.prefab")==receipt.nextBytes && Identity(archived/"next.prefab")==receipt.nextIdentity && Read(revision.metadata)==revision.metadataBytes,"Native source Undo lost bytes/IDs/Scene/selection/dirty");
            if(mode=="unknown-pending"){const auto heldDocument=document;Require(Read(pending/"foreign-data")=="retain me" && effect->HasRetainedPending() && !history.Redo(world,selection,error,&document) && document.HasSameState(heldDocument),"Unknown pending data deleted/ignored or changed document");continue;}
            Require(!fs::exists(pending) && !effect->HasRetainedPending(),"Successful replay retained pending marker");
            Require(history.Redo(world,selection,error,&document) && !observed->observationFailed && observed->observations==2,error.c_str());Require(world.Capture().dump()==afterWorld.dump() && selection.Capture()==afterSelection && document.IsDirty() && Read(file)==receipt.nextBytes && Identity(file)==receipt.nextIdentity && Read(archived/"original.prefab")==revision.bytes && Identity(archived/"original.prefab")==revision.fileIdentity && !fs::exists(pending),"Native source Redo lost source pair/Scene/history/document");
            Require(effect->Begin(true,error),error.c_str());const auto writer=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);Require(writer==INVALID_HANDLE_VALUE && fs::exists(pending/"history.json"),"Effect did not retain write ownership/pending through publication");effect->End();
        }
        fs::remove_all(fixture);std::cout<<"PASS: archived native Prefab source Undo/Redo with actual all-instance World/full selection/history and observed dirty, exact bytes/IDs, held ownership, stale/duplicate/pending isolation, no-replace rollback/collision and owned-child interruption marker retention (not initial Apply/UI/restart recovery)\n";return 0;
    }catch(const std::exception& exception){denySourceReplayAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<" (owned fixture retained)\n";return 1;}
}
