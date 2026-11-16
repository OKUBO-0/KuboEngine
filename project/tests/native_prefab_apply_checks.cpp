#include "engine/editor/NativePrefabApplyEffect.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/EntityDocumentState.h"
#include <fstream>
#include <iostream>
#include <map>
#include <cstdlib>
namespace {bool denyNativeApplyAllocations=false;size_t deniedNativeApplyAllocations=0;}
void* operator new(size_t bytes){if(denyNativeApplyAllocations){++deniedNativeApplyAllocations;throw std::bad_alloc();}if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
namespace {
namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Scene;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const fs::path& file,const std::string& bytes){std::ofstream out(file,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Owned fixture write failed");}
std::string Read(const fs::path& file){std::ifstream in(file,std::ios::binary);Require(in.good(),"Owned fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
PrefabFileIdentity Identity(const fs::path& file){const auto handle=CreateFileW(file.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);Require(handle!=INVALID_HANDLE_VALUE,"Identity open failed");BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(handle,&info)!=0;CloseHandle(handle);Require(ok,"Identity query failed");return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};}
struct File {std::string bytes;PrefabFileIdentity identity;bool operator==(const File&)const=default;};
std::map<fs::path,File> Snapshot(const fs::path& root){std::map<fs::path,File> result;for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),File{Read(entry.path()),Identity(entry.path())});return result;}
void ReplaceSame(const fs::path& file){Write(file.parent_path()/"replacement",Read(file));fs::rename(file,file.parent_path()/(file.filename().wstring()+L".retained"));fs::rename(file.parent_path()/"replacement",file);}
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Fixture {
    fs::path root,file;Engine::Base::ProjectContext project;Engine::Assets::AssetRegistry registry;ProjectPrefabRevision revision;
    EntityWorld world;EntitySelection selection;EntityHistory history;EntityDocumentState document;EntityId chosen=0,other=0;PrefabApplyPlan plan;std::string error;
    Fixture(const fs::path& directory,bool create):root(directory),file(root/L"Assets/ソース.prefab"){
        if(create){fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"Apply","assetDirectory":"Assets","startupScene":"Assets/Main.scene"})");
            EntityWorld source;source.CreateEntity("Before");source.CreateEntity("Child",1);source.CreateEntity("Deleted",1);PrefabValue definition;
            Require(CapturePrefabSubtree(source,1,definition,error) && SavePrefabDocument(file,definition,error),error.c_str());}
        Require(project.Open(root/"Game.kubo",error) && registry.Scan(root/"Assets",error) && ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
        world.CreateEntity("Outside");EntityHistory construction;
        Require(InstantiatePrefab(world,revision.definition,revision.guid,1,selection,construction,chosen,error) && InstantiatePrefab(world,revision.definition,revision.guid,1,selection,construction,other,error),error.c_str());
        Require(selection.Assign(world,{3,7},7,error),error.c_str());const auto prior=world.Capture();
        world.Find(chosen)->name="After";world.Find(3)->transform.position.x=7;Require(world.DestroyEntity(4),"Chosen delete failed");world.Find(other)->name="Other override";world.Find(6)->active=false;
        Require(history.Record(prior,world,selection.Capture(),selection.Capture(),error) && BuildPrefabApplyPlan(world,chosen,revision.definition,revision.guid,plan,error),error.c_str());
        document.Opened(world,root/L"保存.scene");
    }
};
struct Child {
    PROCESS_INFORMATION info{};
    Child(const fs::path& root,const std::wstring& phase){wchar_t executable[32768];Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Child executable missing");std::wstring command=L"\""+std::wstring(executable)+L"\" --child \""+root.wstring()+L"\" "+phase;STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=0,"Owned child launch failed");}
    void Kill(){Require(TerminateProcess(info.hProcess,91)!=0 && WaitForSingleObject(info.hProcess,5000)==WAIT_OBJECT_0,"Owned child termination failed");DWORD code=0;Require(GetExitCodeProcess(info.hProcess,&code) && code==91,"Owned child exit mismatch");}
    ~Child(){if(info.hProcess){if(WaitForSingleObject(info.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(info.hProcess,92);WaitForSingleObject(info.hProcess,5000);}CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
int ChildMain(const fs::path& root,const std::wstring& phase){
    Fixture fixture(root,false);const auto before=fixture.world.Capture().dump();const auto after=fixture.plan.world.Capture().dump();
    const auto stop=[&]{Write(root/"world-at-boundary",fixture.world.Capture().dump());Write(root/"ready.pending","ready");fs::rename(root/"ready.pending",root/"ready");Sleep(INFINITE);};
    NativePrefabApplyEffect::Hooks hooks;
    if(phase==L"prepared")hooks.afterPrepare=stop;
    if(phase==L"parked")hooks.afterPark=stop;
    if(phase==L"published")hooks.afterPublish=stop;
    if(phase==L"before-archive")hooks.beforeArchive=[&](const fs::path&){Require(fixture.world.Capture().dump()==after && fixture.history.UndoCount()==2,"Child archived before World/history publication");stop();};
    if(phase==L"after-archive")hooks.afterArchive=stop;
    auto effect=std::make_shared<NativePrefabApplyEffect>(fixture.project,fixture.revision,fixture.plan.source,hooks);PrefabApplyAdmission admission;
    Require(admission.Prepare(fixture.world,fixture.chosen,fixture.revision.definition,fixture.revision.guid,fixture.selection,fixture.history,effect,fixture.error,&fixture.document) && admission.Publish(fixture.world,fixture.selection,fixture.history,fixture.error,&fixture.document),fixture.error.c_str());
    Require(before!=after,"Child plan unchanged");return 2;
}
}
int wmain(int argc,wchar_t** argv){
    const auto fixtureRoot=fs::temp_directory_path()/(L"kubo-native-apply-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        if(argc==4 && std::wstring(argv[1])==L"--child")return ChildMain(argv[2],argv[3]);
        denyNativeApplyAllocations=true;bool guardActive=false;try{auto* memory=::operator new(1);::operator delete(memory);}catch(const std::bad_alloc&){guardActive=true;}
        denyNativeApplyAllocations=false;Require(guardActive && deniedNativeApplyAllocations==1,"Native allocation refusal probe inactive");deniedNativeApplyAllocations=0;
        for(const auto* scenario:{"success","allocation","unicode-raw","unknown-files","archive-conflict","archive-callback","archive-reader","archive-stage-collision","late-asset-move",
            "source-stale","source-replaced","source-reader","metadata-stale","metadata-replaced","duplicate-guid","pending-own","pending-other","root-changed","invented-definition",
            "prepare-callback","park-callback","park-collision","park-asset-move","park-root-changed","post-publish-callback","budget","effect-budget","replay-stale","replay-other-pending","replay-reader","child-prepared","child-parked","child-published","child-before-archive","child-after-archive"}) {
            const std::string mode=scenario;Fixture fixture(fixtureRoot/scenario,true);auto& error=fixture.error;auto& world=fixture.world;auto& selection=fixture.selection;auto& history=fixture.history;
            if(mode=="unicode-raw") {
                Write(fixture.file,std::string("\xef\xbb\xbf")+fixture.revision.bytes+"\n");
                Write(fixture.revision.metadata,"{\"version\":1,\"guid\":\""+fixture.revision.guid+"\",\"label\":\"日本語🚀\"}\n");
                Require(ReadProjectPrefabRevision(fixture.project,fixture.registry,fixture.revision.guid,fixture.revision,error),error.c_str());
            }
            if(mode=="invented-definition") {
                fixture.revision.definition["world"]["entities"][size_t(0)]["active"]=false;
                Require(BuildPrefabApplyPlan(world,fixture.chosen,fixture.revision.definition,fixture.revision.guid,fixture.plan,error),error.c_str());
            }
            const auto before=world.Capture().dump();const auto beforeSelection=selection.Capture();const auto beforeHistory=history;
            const auto pending=fixture.root/"Assets/.kubo/prefab-transactions"/fixture.revision.guid;
            const auto nextBytes=fixture.plan.source.dump();
            if(mode.starts_with("child-")) {
                const auto phase=std::wstring(mode.begin()+6,mode.end());
                {Child child(fixture.root,phase);const auto deadline=GetTickCount64()+10000;while(!fs::exists(fixture.root/"ready") && GetTickCount64()<deadline && WaitForSingleObject(child.info.hProcess,0)==WAIT_TIMEOUT)Sleep(10);
                    Require(fs::exists(fixture.root/"ready"),"Child did not reach native Apply boundary");PrefabSourceRecoveryReport held;
                    Require(!PrefabSourceRecovery::Inspect(fixture.project,fixture.revision.guid,held,error),"Live child source ownership was bypassed");child.Kill();}
                const bool afterWorld=phase==L"before-archive" || phase==L"after-archive";
                Require(Read(fixture.root/"world-at-boundary")== (afterWorld?fixture.plan.world.Capture().dump():before),"Child World publication boundary incorrect");
                if(phase==L"after-archive") {
                    Require(!fs::exists(pending) && Read(fixture.file)==nextBytes,"Acknowledged child lost current source/retained archive");ProjectPrefabRevision fresh;
                    Require(ReadProjectPrefabRevision(fixture.project,fixture.registry,fixture.revision.guid,fresh,error),error.c_str());HeldProjectPrefabWriter writer;Require(writer.Open(fixture.project,fixture.registry,fresh,error),error.c_str());continue;
                }
                PrefabSourceRecoveryReport report;Require(PrefabSourceRecovery::Inspect(fixture.project,fixture.revision.guid,report,error),error.c_str());
                const auto state=phase==L"prepared"?PrefabSourceRecoveryState::Prepared:phase==L"parked"?PrefabSourceRecoveryState::Parked:PrefabSourceRecoveryState::Published;
                Require(report.state==state && report.originalBytes==fixture.revision.bytes && report.nextBytes==nextBytes && report.metadataIdentity==fixture.revision.metadataIdentity,"Interrupted initial transaction lost native recovery intent");
                Require(PrefabSourceRecovery::RestoreOriginal(fixture.project,fixture.revision.guid,report,error) && Read(fixture.file)==fixture.revision.bytes && Identity(fixture.file)==fixture.revision.fileIdentity,error.c_str());continue;
            }
            Handle reader;fs::path occupied;bool sawWorldPublication=false,sawWriteOwnership=false;
            NativePrefabApplyEffect::Hooks hooks;
            hooks.afterPrepare=[&]{if(mode=="prepare-callback")throw std::runtime_error("Injected prepare callback");};
            hooks.afterPark=[&]{if(mode=="park-callback")throw std::runtime_error("Injected park callback");if(mode=="park-collision"){Write(fixture.file,"foreign source owner");throw std::runtime_error("Injected source collision");}
                if(mode=="park-asset-move")Write(fixture.root/"Assets/.kubo/asset-move.pending.json","foreign move intent");
                if(mode=="park-root-changed"){fs::create_directory(fixture.root/"OtherAssets");Write(fixture.root/"Game.kubo",R"({"name":"Apply","assetDirectory":"OtherAssets","startupScene":"Assets/Main.scene"})");}};
            hooks.afterPublish=[&]{Require(world.Capture().dump()==before && history.HasSameState(beforeHistory),"Native source returned after changing live World/history");if(mode=="post-publish-callback")throw std::runtime_error("Injected post-source callback");if(mode=="allocation")denyNativeApplyAllocations=true;};
            hooks.beforeCompletion=[&]{denyNativeApplyAllocations=false;if(mode=="allocation")Require(deniedNativeApplyAllocations==0 && world.Capture().dump()==fixture.plan.world.Capture().dump() && history.UndoCount()==2,"Native source success to World/history publication allocated");};
            hooks.beforeArchive=[&](const fs::path& destination){
                sawWorldPublication=world.Capture().dump()==fixture.plan.world.Capture().dump() && selection.Ids()==std::vector<EntityId>{3} && history.UndoCount()==2 && fixture.document.IsDirty();
                const auto writer=CreateFileW(fixture.file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);const auto code=GetLastError();if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);sawWriteOwnership=writer==INVALID_HANDLE_VALUE && code==ERROR_SHARING_VIOLATION;
                if(mode=="unknown-files")Write(pending/"unknown-data","preserve me");
                if(mode=="archive-conflict"){occupied=destination;fs::create_directory(destination);Write(destination/"foreign-owner","retain destination");}
                if(mode=="archive-callback")throw std::runtime_error("Injected archive callback");
                if(mode=="archive-reader"){reader.value=CreateFileW((pending/"pending.json").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);Require(reader.value!=INVALID_HANDLE_VALUE,"Archive reader fixture failed");}
                if(mode=="archive-stage-collision")Write(pending/"next.prefab","foreign stage owner");
                if(mode=="late-asset-move")Write(fixture.root/"Assets/.kubo/asset-move.pending.json","foreign move intent");
            };
            auto effect=std::make_shared<NativePrefabApplyEffect>(fixture.project,fixture.revision,fixture.plan.source,hooks);
            if(mode=="source-stale")Write(fixture.file,fixture.revision.bytes+"\n");
            if(mode=="source-replaced")ReplaceSame(fixture.file);
            if(mode=="metadata-stale")Write(fixture.revision.metadata,fixture.revision.metadataBytes+"\n");
            if(mode=="metadata-replaced")ReplaceSame(fixture.revision.metadata);
            if(mode=="source-reader") {
                {HeldProjectPrefabWriter preseed;Require(preseed.Open(fixture.project,fixture.registry,fixture.revision,error),error.c_str());}
                reader.value=CreateFileW(fixture.file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(reader.value!=INVALID_HANDLE_VALUE,"Source reader fixture failed");
            }
            if(mode=="duplicate-guid"){Write(fixture.root/"Assets/duplicate.prefab",fixture.revision.bytes);Write(Engine::Assets::AssetRegistry::MetadataPath(fixture.root/"Assets/duplicate.prefab"),fixture.revision.metadataBytes);}
            if(mode=="pending-own" || mode=="pending-other"){const auto path=mode=="pending-own"?pending:pending.parent_path()/"fedcba9876543210fedcba9876543210";fs::create_directories(path);Write(path/"foreign-data","preserve pending");}
            if(mode=="root-changed"){fs::create_directory(fixture.root/"OtherAssets");Write(fixture.root/"Game.kubo",R"({"name":"Apply","assetDirectory":"OtherAssets","startupScene":"Assets/Main.scene"})");}
            if(mode=="budget")history=EntityHistory(100,1);
            if(mode=="effect-budget")history=EntityHistory(100,effect->Bytes()-1);
            const auto admittedHistory=history;const auto files=Snapshot(fixture.root);PrefabApplyAdmission admission;
            const bool admitted=admission.Prepare(world,fixture.chosen,fixture.revision.definition,fixture.revision.guid,selection,history,effect,error,&fixture.document);
            if(mode=="budget" || mode=="effect-budget"){Require(!admitted && Snapshot(fixture.root)==files && history.HasSameState(admittedHistory) && effect->CompletionState()==NativePrefabApplyEffect::Completion::NotStarted,"Budget refusal wrote native files/changed history");continue;}
            Require(admitted,error.c_str());const bool published=admission.Publish(world,selection,history,error,&fixture.document);
            if(mode=="source-stale" || mode=="source-replaced" || mode=="source-reader" || mode=="metadata-stale" || mode=="metadata-replaced" || mode=="duplicate-guid" || mode=="pending-own" || mode=="pending-other" || mode=="root-changed" || mode=="invented-definition") {
                Require(!published && world.Capture().dump()==before && selection.Capture()==beforeSelection && history.HasSameState(beforeHistory) && Snapshot(fixture.root)==files,"Stale native admission changed files/Scene/history");continue;
            }
            if(mode=="prepare-callback" || mode=="park-callback" || mode=="park-collision" || mode=="park-asset-move" || mode=="park-root-changed" || mode=="post-publish-callback") {
                Require(!published && world.Capture().dump()==before && selection.Capture()==beforeSelection && history.HasSameState(beforeHistory) && effect->HasRetainedPending() && fs::exists(pending/"pending.json"),"Failed initial native effect lost pending or changed World/history");
                Require(Read(fixture.revision.metadata)==fixture.revision.metadataBytes && Identity(fixture.revision.metadata)==fixture.revision.metadataIdentity,"Initial failure changed metadata");
                if(mode=="park-collision")Require(Read(fixture.file)=="foreign source owner" && Read(pending/"original.prefab")==fixture.revision.bytes && Read(pending/"next.prefab")==nextBytes,"Initial rollback overwrote foreign data");
                else Require(Read(fixture.file)==(mode=="post-publish-callback"?nextBytes:fixture.revision.bytes),"Failed initial effect source state wrong");continue;
            }
            Require(published && sawWorldPublication && sawWriteOwnership && Read(fixture.file)==nextBytes && Read(fixture.revision.metadata)==fixture.revision.metadataBytes && Identity(fixture.revision.metadata)==fixture.revision.metadataIdentity,"Native initial Apply lost World-before-archive/ownership/source/metadata");
            const auto after=world.Capture().dump();const auto afterSelection=selection.Capture();const auto nextId=Identity(fixture.file);
            if(mode.starts_with("archive-") || mode=="late-asset-move") {
                Require(effect->HasRetainedPending() && effect->CompletionState()==NativePrefabApplyEffect::Completion::Pending && fs::exists(pending/"pending.json"),"Archive failure hidden/transaction deleted");const auto heldHistory=history;
                Require(!history.Undo(world,selection,error) && world.Capture().dump()==after && selection.Capture()==afterSelection && history.HasSameState(heldHistory),"Unacknowledged initial Apply replayed history");
                if(mode=="archive-conflict")Require(Read(occupied/"foreign-owner")=="retain destination","Archive collision overwrote foreign destination");
                if(mode=="archive-stage-collision")Require(Read(pending/"next.prefab")=="foreign stage owner","Archive validation deleted foreign stage");continue;
            }
            Require(!effect->HasRetainedPending() && effect->CompletionState()==NativePrefabApplyEffect::Completion::Archived && !fs::exists(pending) && Read(effect->ArchivePath()/"original.prefab")==fixture.revision.bytes && Identity(effect->ArchivePath()/"original.prefab")==fixture.revision.fileIdentity,"Initial Apply acknowledgement lost original file identity/archive");
            if(mode=="unknown-files")Require(Read(effect->ArchivePath()/"unknown-data")=="preserve me","Initial Apply archive deleted unknown data");
            if(mode=="replay-stale"){Write(fixture.file,nextBytes+"\n");const auto heldHistory=history;Require(!history.Undo(world,selection,error) && world.Capture().dump()==after && selection.Capture()==afterSelection && history.HasSameState(heldHistory),"Initial Apply stale source replay published Scene");continue;}
            if(mode=="replay-other-pending" || mode=="replay-reader") {
                if(mode=="replay-other-pending"){const auto foreign=pending.parent_path()/"fedcba9876543210fedcba9876543210";fs::create_directory(foreign);Write(foreign/"foreign-data","preserve pending");}
                else {reader.value=CreateFileW(fixture.file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(reader.value!=INVALID_HANDLE_VALUE,"Replay reader fixture failed");}
                const auto heldHistory=history;const auto heldFiles=Snapshot(fixture.root);
                Require(!history.Undo(world,selection,error) && world.Capture().dump()==after && selection.Capture()==afterSelection && history.HasSameState(heldHistory) && Snapshot(fixture.root)==heldFiles,"Pending/reader replay refusal changed native files/Scene/history");continue;
            }
            EntityWorld saved;Require(saved.Restore(PrefabValue::parse(before),error),error.c_str());EntityDocumentState document;document.Opened(saved,fixture.root/L"保存.scene");document.Observe(world);Require(document.IsDirty(),"Initial Apply not observed dirty");
            Require(history.Undo(world,selection,error,&document) && world.Capture().dump()==before && selection.Capture()==beforeSelection && Read(fixture.file)==fixture.revision.bytes && Identity(fixture.file)==fixture.revision.fileIdentity && Read(effect->ArchivePath()/"next.prefab")==nextBytes && Identity(effect->ArchivePath()/"next.prefab")==nextId,error.c_str());Require(!document.IsDirty(),"Initial Apply Undo did not restore dirty baseline");
            Require(history.Redo(world,selection,error,&document) && world.Capture().dump()==after && selection.Capture()==afterSelection && Read(fixture.file)==nextBytes && Identity(fixture.file)==nextId,error.c_str());Require(document.IsDirty() && !effect->HasRetainedPending(),"Initial Apply Redo dirty/pending wrong");
            const auto complete=Snapshot(fixture.root);effect->End();effect->End();Require(Snapshot(fixture.root)==complete,"Repeated End changed acknowledged data");
        }
        {
            Fixture fixture(fixtureRoot/"field-native",true);auto& error=fixture.error;
            PrefabOverrideReport report;Require(InspectPrefabOverrides(fixture.world,fixture.chosen,report,error),error.c_str());
            const auto field=std::find_if(report.entries.begin(),report.entries.end(),[](const auto& entry){return entry.local==2 && entry.path=="/transform/position";});Require(field!=report.entries.end(),"Native field fixture has no target");
            std::unique_ptr<PrefabApplyPlan> expected;Require(BuildPrefabFieldApplyPlan(fixture.world,fixture.chosen,fixture.revision.definition,fixture.revision.guid,*field,expected,error),error.c_str());
            const auto before=fixture.world.Capture().dump();const auto selected=fixture.selection.Capture();const auto history=fixture.history;bool heldAfterPublication=false;
            NativePrefabApplyEffect::Hooks hooks;
            hooks.afterPublish=[&]{Require(fixture.world.Capture().dump()==before && fixture.history.HasSameState(history),"Native field source changed Scene before admission");denyNativeApplyAllocations=true;};
            hooks.beforeCompletion=[&]{denyNativeApplyAllocations=false;Require(deniedNativeApplyAllocations==0 && fixture.world.Capture().dump()==expected->world.Capture().dump() && fixture.selection.Capture()==selected && fixture.history.UndoCount()==2 && fixture.document.IsDirty(),"Native field allocated/lost full selection/dirty before source End");
                const auto writer=CreateFileW(fixture.file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);const auto code=GetLastError();if(writer!=INVALID_HANDLE_VALUE)CloseHandle(writer);heldAfterPublication=writer==INVALID_HANDLE_VALUE && code==ERROR_SHARING_VIOLATION;};
            deniedNativeApplyAllocations=0;auto effect=std::make_shared<NativePrefabApplyEffect>(fixture.project,fixture.revision,expected->source,hooks);
            PrefabApplyAdmission admission;Require(admission.PrepareField(fixture.world,fixture.chosen,fixture.revision.definition,fixture.revision.guid,*field,fixture.selection,fixture.history,effect,error,&fixture.document) && fixture.world.Capture().dump()==before && fixture.history.HasSameState(history) && !fixture.document.IsDirty(),error.c_str());
            Require(admission.Publish(fixture.world,fixture.selection,fixture.history,error,&fixture.document) && heldAfterPublication && Read(fixture.file)==expected->source.dump() && !effect->HasRetainedPending(),error.c_str());
            Require(fixture.world.Find(fixture.chosen)->name=="After" && fixture.world.Find(fixture.other)->name=="Other override" && fixture.world.Find(7) && fixture.selection.Capture()==selected,"Native field Apply changed unselected names/structure/full selection");
            const auto after=fixture.world.Capture().dump();const auto nextId=Identity(fixture.file);
            Require(fixture.history.Undo(fixture.world,fixture.selection,error,&fixture.document) && fixture.world.Capture().dump()==before && fixture.selection.Capture()==selected && !fixture.document.IsDirty() && Read(fixture.file)==fixture.revision.bytes && Identity(fixture.file)==fixture.revision.fileIdentity,error.c_str());
            Require(fixture.history.Redo(fixture.world,fixture.selection,error,&fixture.document) && fixture.world.Capture().dump()==after && fixture.selection.Capture()==selected && fixture.document.IsDirty() && Read(fixture.file)==expected->source.dump() && Identity(fixture.file)==nextId,error.c_str());
            std::cout<<"PASS: actual native single-field Apply prepared publication, source-only field change/unselected names/structure, full selection/dirty under writer exclusion without post-source allocation, acknowledged archive and exact source identity/Scene Undo/Redo (not Game field Apply UI)\n";
        }
        fs::remove_all(fixtureRoot);std::cout<<"PASS: actual native initial Prefab Apply/all-instance World/full selection/history/observed dirty, held ownership until post-World whole-directory archive, exact file identities/Undo/Redo, stale/budget/refusal, retained pending/unknown/collision data and five owned-child interruption boundaries (not Game Apply UI/physical crash QA)\n";return 0;
    }catch(const std::exception& exception){denyNativeApplyAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<" (owned fixture retained: "<<Engine::Base::PathToUtf8(fixtureRoot)<<")\n";return 1;}
}
