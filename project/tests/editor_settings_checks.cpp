#include "engine/base/ProjectPaths.h"
#include "engine/editor/EditorSettingsFile.h"
#include "engine/editor/NativeBuildSettings.h"
#include <iostream>
#include <stdexcept>

void Require(bool condition,const char* reason) {if(!condition) throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && std::wstring(argv[1])==L"--lock-probe") {
        Engine::Editor::NativeBuildSettings settings;Engine::Editor::NativeBuildSettingsSnapshot snapshot;std::string error;
        if(!Engine::Editor::NativeBuildSettingsFile::Load(argv[2],settings,snapshot,error)) return 2;
        if(Engine::Editor::NativeBuildSettingsFile::SaveChecked(argv[2],settings,snapshot,error)) return 3;
        return error.find("busy")!=std::string::npos ? 0 : 4;
    }
    namespace fs=std::filesystem;using Engine::Editor::EditorSettingsFile;
    const auto fixture=fs::temp_directory_path()/(L"kubo-editor-settings-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto previous=fs::current_path();
    try {
        fs::create_directories(fixture/"Other");
        const Engine::Base::ProjectPaths paths(fixture),other(fixture/"Other");
        std::string error,text="retained";
        const auto legacy=fixture/"imgui.ini";
        Require(EditorSettingsFile::Read(paths.EditorLayout(),legacy,text,error) && text.empty(),"Missing settings did not use empty defaults");
        const std::string original="[Window][日本語]\nPos=10,20\nSize=300,400\n";
        Require(EditorSettingsFile::Write(legacy,original,error),error.c_str());
        Require(EditorSettingsFile::Read(paths.EditorLayout(),legacy,text,error) && text==original,"Legacy settings were not read");
        fs::current_path(fixture/"Other");
        const auto updated=original+"Collapsed=1\n";
        Require(EditorSettingsFile::Write(paths.EditorLayout(),updated,error),error.c_str());
        Require(EditorSettingsFile::Read(paths.EditorLayout(),legacy,text,error) && text==updated,"New settings did not override legacy");
        Require(EditorSettingsFile::Read(legacy,{},text,error) && text==original,"Migration modified legacy source");
        Require(!fs::exists(other.EditorLayout()),"Settings save followed changed cwd");
        for(const auto& invalid:{std::string("x\0y",3),std::string("\xff",1),std::string(EditorSettingsFile::Limit+1,'x')}) {
            Require(!EditorSettingsFile::Write(paths.EditorLayout(),invalid,error),"Invalid settings write accepted");
            Require(EditorSettingsFile::Read(paths.EditorLayout(),legacy,text,error) && text==updated,"Rejected save changed existing settings");
        }
        {std::ofstream bad(paths.EditorWindows(),std::ios::binary);bad<<std::string("\xff",1);}
        text="retained";Require(!EditorSettingsFile::Read(paths.EditorWindows(),legacy,text,error) && text=="retained","Malformed settings fell back to legacy or changed output");
        const auto handle=CreateFileW(paths.EditorLayout().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(handle!=INVALID_HANDLE_VALUE,"Cannot lock settings fixture");
        const bool written=EditorSettingsFile::Write(paths.EditorLayout(),"replacement",error);CloseHandle(handle);
        Require(!written,"Locked target replacement succeeded");
        Require(EditorSettingsFile::Read(paths.EditorLayout(),legacy,text,error) && text==updated,"Failed atomic replacement lost existing settings");
        for(const auto& entry:fs::directory_iterator(paths.EditorLayout().parent_path()))
            Require(entry.path().filename().native().find(L".kubo-save-")==std::wstring::npos,"Owned temporary file leaked");
        using Engine::Editor::NativeBuildSettings;
        using Engine::Editor::NativeBuildSettingsFile;
        NativeBuildSettings native;
        Engine::Editor::NativeBuildSettingsSnapshot initialSnapshot;
        Require(NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),native,initialSnapshot,error) && !initialSnapshot.exists && native.output=="Builds/Export" && !native.native,"Missing native settings defaults failed");
        native={true,true,"C:/SDK/日本語 & '$","Native/Game.vcxproj","Builds/日本語"};
        Require(NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),native,initialSnapshot,error) && initialSnapshot.exists,error.c_str());
        NativeBuildSettings reopened;
        Require(NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,error) && reopened.sdk==native.sdk && reopened.project==native.project && reopened.output==native.output && reopened.native && reopened.debug,"Native settings restart roundtrip failed");
        Require(!fs::exists(other.NativeBuildSettings()),"Native settings followed changed cwd");
        std::string good;
        Require(EditorSettingsFile::Read(paths.NativeBuildSettings(),{},good,error),error.c_str());
        auto extended=crude_json::value::parse(good);extended["future"]="preserved";
        Require(EditorSettingsFile::Write(paths.NativeBuildSettings(),extended.dump(),error),error.c_str());
        native.debug=false;Require(NativeBuildSettingsFile::Save(paths.NativeBuildSettings(),native,error),error.c_str());
        Require(EditorSettingsFile::Read(paths.NativeBuildSettings(),{},good,error) && crude_json::value::parse(good)["future"].get<crude_json::string>()=="preserved","Native save dropped unknown fields");
        for(const auto& invalid:{std::string("{}broken"),std::string("{\"version\":2}"),std::string("{\"native\":1}"),std::string("{\"sdk\":\"bad\\u0000path\"}"),std::string(40,'[')+std::string(40,']'),std::string(NativeBuildSettingsFile::Limit+1,' ')}) {
            Require(EditorSettingsFile::Write(paths.NativeBuildSettings(),invalid,error),error.c_str());
            reopened.sdk="retained";
            Require(!NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,error) && reopened.sdk=="retained","Malformed native settings changed draft");
            Require(!NativeBuildSettingsFile::Save(paths.NativeBuildSettings(),native,error),"Save overwrote malformed existing native settings");
            std::string bytes;Require(EditorSettingsFile::Read(paths.NativeBuildSettings(),{},bytes,error) && bytes==invalid,"Failed native save changed malformed settings");
        }
        Require(EditorSettingsFile::Write(paths.NativeBuildSettings(),good,error),error.c_str());
        const auto nativeLock=CreateFileW(paths.NativeBuildSettings().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(nativeLock!=INVALID_HANDLE_VALUE,"Cannot lock native settings fixture");
        const bool nativeWritten=NativeBuildSettingsFile::Save(paths.NativeBuildSettings(),native,error);CloseHandle(nativeLock);
        Require(!nativeWritten,"Locked native settings overwritten");
        std::string preserved;Require(EditorSettingsFile::Read(paths.NativeBuildSettings(),{},preserved,error) && preserved==good,"Failed native replacement lost bytes");
        using Engine::Editor::NativeBuildSettingsSnapshot;
        NativeBuildSettingsSnapshot baseline,second;
        Require(NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,baseline,error),error.c_str());
        Require(NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,second,error),error.c_str());
        auto draft=reopened;draft.output="Builds/first-writer";
        Require(NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error),error.c_str());
        Require(NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error),"Successful save did not advance snapshot");
        const auto staleBytes=second.bytes;auto staleDraft=reopened;staleDraft.output="Builds/stale-writer";
        Require(!NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),staleDraft,second,error) && error.find("changed externally")!=std::string::npos,"Stale Editor overwrote another writer");
        Require(second.bytes==staleBytes && staleDraft.output=="Builds/stale-writer","Conflict changed snapshot/draft");
        Require(EditorSettingsFile::Read(paths.NativeBuildSettings(),{},preserved,error) && preserved==baseline.bytes,"Conflict changed saved settings");
        auto external=crude_json::value::parse(baseline.bytes);external["externalUnknown"]="do not drop";
        Require(EditorSettingsFile::Write(paths.NativeBuildSettings(),external.dump(),error),error.c_str());
        Require(!NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error),"Unknown-field external change ignored");
        Require(NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,baseline,error),error.c_str());
        Require(NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error) && crude_json::value::parse(baseline.bytes)["externalUnknown"].get<crude_json::string>()=="do not drop","Reload/save dropped external fields");
        Require(!NativeBuildSettingsFile::SaveChecked(other.NativeBuildSettings(),draft,baseline,error) && !fs::exists(other.NativeBuildSettings()),"Snapshot accepted another project path");
        auto lockPath=paths.NativeBuildSettings();lockPath+=L".lock";
        const auto cooperativeLock=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(cooperativeLock!=INVALID_HANDLE_VALUE,"Cannot hold cooperating writer lock");
        const bool busyAccepted=NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error);
        const auto busyError=error;
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        std::wstring command=L"\""+std::wstring(self)+L"\" --lock-probe \""+paths.NativeBuildSettings().wstring()+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        const bool started=CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
        DWORD childResult=1;bool childFinished=false;
        if(started) {
            CloseHandle(child.hThread);childFinished=WaitForSingleObject(child.hProcess,10000)==WAIT_OBJECT_0;
            if(!childFinished) {TerminateProcess(child.hProcess,9);WaitForSingleObject(child.hProcess,5000);}
            GetExitCodeProcess(child.hProcess,&childResult);CloseHandle(child.hProcess);
        }
        CloseHandle(cooperativeLock);
        Require(!busyAccepted && busyError.find("busy")!=std::string::npos && started && childFinished && childResult==0,"Concurrent same/other-process save was not excluded");
        Require(NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error),"Released writer lock blocked retry");
        Require(EditorSettingsFile::Write(lockPath,"keep user bytes",error),error.c_str());
        Require(!NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error),"Nonempty lock accepted");
        Require(EditorSettingsFile::Read(lockPath,{},preserved,error) && preserved=="keep user bytes","Nonempty lock changed");
        Require(EditorSettingsFile::Write(lockPath,"",error),error.c_str());
        const auto checkedTargetLock=CreateFileW(paths.NativeBuildSettings().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(checkedTargetLock!=INVALID_HANDLE_VALUE,"Cannot lock checked target");
        const auto beforeFailed=baseline.bytes;draft.debug=!draft.debug;
        const bool checkedWritten=NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error);CloseHandle(checkedTargetLock);
        Require(!checkedWritten && baseline.bytes==beforeFailed,"Failed replacement advanced snapshot");
        Require(NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error),"Failed replacement prevented retry");
        fs::remove(paths.NativeBuildSettings()); // Exact test-owned fixture only.
        Require(!NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),draft,baseline,error) && !fs::exists(paths.NativeBuildSettings()),"External deletion silently recreated settings");
        Require(NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,baseline,error) && !baseline.exists,"Deleted file did not load absence");
        Require(NativeBuildSettingsFile::Save(paths.NativeBuildSettings(),draft,error),error.c_str());
        Require(!NativeBuildSettingsFile::SaveChecked(paths.NativeBuildSettings(),reopened,baseline,error),"Missing baseline overwrote externally created file");
        const auto beforeInvalid=baseline.path;
        Require(EditorSettingsFile::Write(paths.NativeBuildSettings(),"broken",error),error.c_str());
        reopened.sdk="retained";
        Require(!NativeBuildSettingsFile::Load(paths.NativeBuildSettings(),reopened,baseline,error) && reopened.sdk=="retained" && baseline.path==beforeInvalid && !baseline.exists,"Failed reload changed baseline/draft");
        std::cout<<"PASS: checked native settings stale/unknown-field/deletion/creation/path conflicts, snapshot advancement/failure preservation and cross-process writer lock/retry\n";
        std::cout<<"PASS: local native settings Unicode roundtrip, cwd isolation, unknown-field preservation, malformed/deep/version refusal and locked-file preservation\n";
        fs::current_path(previous);fs::remove_all(fixture);
        std::cout<<"PASS: native Unicode editor settings, bounded UTF-8, legacy fallback/new precedence, changed-cwd isolation, exclusive atomic writes and locked-target preservation\n";return 0;
    } catch(const std::exception& exception) {fs::current_path(previous);std::error_code cleanup;fs::remove_all(fixture,cleanup);std::cerr<<exception.what()<<'\n';return 1;}
}
