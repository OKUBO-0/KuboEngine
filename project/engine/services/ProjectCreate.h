#pragma once
#include "NativeCreateJournal.h"
#include "ProjectStarter.h"
#include <atomic>

namespace Engine::Services {
// Hub's blank/generic template. Legacy ProjectContext::Create remains a low-level
// population API; never use it directly against a Hub user's existing content.
class ProjectCreate final {
public:
#if defined(KUBO_PROJECT_CREATE_TEST_HOOK)
    inline static void (*BeforePublish)(const std::filesystem::path&,const std::filesystem::path&)=nullptr;
    inline static void (*AfterPublish)(const std::filesystem::path&)=nullptr;
#endif
    static bool Create(const std::filesystem::path& manifest,Base::ProjectContext& context,std::string& error,
                       ProjectStarter starter = ProjectStarter::Empty) {
        namespace fs=std::filesystem;
        fs::path root,stage,backup;bool ownsStage=false,retired=false,published=false;
        NativeCreateJournal journal;
        try {
            const auto destination=fs::absolute(manifest).lexically_normal();root=destination.parent_path();
            if(root==root.root_path() || _wcsicmp(destination.extension().c_str(),L".kubo"))throw std::runtime_error("Choose a .kubo manifest in a new or empty project directory, not a volume root");
            // Refuse static parent junctions/symlinks. Not a hostile-writer sandbox.
            for(auto directory=root;!directory.empty();directory=directory.parent_path()) {
                const auto attrs=GetFileAttributesW(directory.c_str());
                if(attrs!=INVALID_FILE_ATTRIBUTES && (!(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT)))throw std::runtime_error("Project creation requires native unlinked directories");
                if(directory==directory.root_path())break;
            }
            const auto recovery=NativeCreateJournal::Recover(destination,error,NativeCreateJournal::Kind::Project);
            if(recovery==NativeCreateJournal::Recovery::Published) {
                const auto notice=error;Base::ProjectContext recovered;if(!recovered.Open(destination,error))return false;
                context=std::move(recovered);error=notice;return true;
            }
            if(recovery!=NativeCreateJournal::Recovery::None)return false;
            if(fs::exists(root) && (!fs::is_directory(root) || !fs::is_empty(root)))throw std::runtime_error("Project creation requires a new or empty directory; open or migrate existing content separately");
            fs::create_directories(root.parent_path());
            static std::atomic<unsigned long long> sequence{0};
            const auto token=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
            stage=root.parent_path()/(L".kubo-project-stage-"+token);backup=root.parent_path()/(L".kubo-empty-backup-"+token);
            if(!fs::create_directory(stage))throw std::runtime_error("Cannot exclusively create generic project stage");ownsStage=true;
            journal.Begin(root,destination,stage,backup,NativeCreateJournal::Kind::Project);
            Base::ProjectContext candidate;const auto name=destination.stem().u8string();
            if(!candidate.Create(stage,std::string(name.begin(),name.end()),error))throw std::runtime_error(error);
            if(!PopulateProjectStarter(stage,starter,error))throw std::runtime_error(error);
            journal.ValidateBeforePublication(root,stage);
            if(fs::exists(root)) {
                if(!fs::is_empty(root))throw std::runtime_error("Output became nonempty before publication");
                if(!MoveFileExW(root.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot retain original empty project folder");
                retired=true;journal.ValidateRetired(backup);
                if(!fs::is_empty(backup))throw std::runtime_error("Output became nonempty during publication");
            }
#if defined(KUBO_PROJECT_CREATE_TEST_HOOK)
            if(BeforePublish)BeforePublish(root,stage);
#endif
            // A scanner may briefly hold a newly created directory without
            // FILE_SHARE_DELETE. Retry only transient access/sharing failures;
            // never replace an output or change permissions to force publication.
            for(unsigned attempt=0;;++attempt) {
                if(MoveFileExW(stage.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH))break;
                const auto code=GetLastError();
                if(attempt>=4 || (code!=ERROR_ACCESS_DENIED && code!=ERROR_SHARING_VIOLATION) ||
                    fs::exists(root) || !journal.OwnsStage(stage))
                    throw std::runtime_error("Cannot publish generic project directory without overwriting output (Windows error "+std::to_string(code)+")");
                Sleep(25);
            }
            ownsStage=false;published=true;
#if defined(KUBO_PROJECT_CREATE_TEST_HOOK)
            if(AfterPublish)AfterPublish(root);
#endif
            if(!candidate.Open(root/candidate.GetProjectFile().filename(),error))throw std::runtime_error(error);
            context=std::move(candidate);error.clear();
            if(retired) {std::error_code cleanup;if(!fs::remove(backup,cleanup) || cleanup)error="Project created; original empty folder retained at "+Base::PathToUtf8(backup);}
            if(error.empty() && !journal.Discard())error="Project created; recovery journal retained at "+Base::PathToUtf8(NativeCreateJournal::Path(root));
            return true;
        }catch(const std::exception& failure) {
            error=failure.what();
            if(retired && !published && !MoveFileExW(backup.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH))error+="; original output retained at "+Base::PathToUtf8(backup);
            if(ownsStage) {
                bool safe=false;std::error_code inspection;
                try {safe=stage.parent_path()==root.parent_path() && stage.filename().wstring().starts_with(L".kubo-project-stage-") && journal.OwnsStage(stage);}catch(...){}
                if(safe)for(fs::recursive_directory_iterator it(stage,inspection),end;safe && !inspection && it!=end;it.increment(inspection)) {
                    const auto attrs=GetFileAttributesW(it->path().c_str());if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_REPARSE_POINT))safe=false;
                }
                if(safe && !inspection){std::error_code cleanup;fs::remove_all(stage,cleanup);if(cleanup)error+="; stage retained at "+Base::PathToUtf8(stage);}
                else error+="; stage retained for inspection at "+Base::PathToUtf8(stage);
            }
            if(published){error+="; completed directory retained at "+Base::PathToUtf8(root);if(retired)error+="; original empty folder backup at "+Base::PathToUtf8(backup);}
            std::error_code stageInspection,backupInspection;
            const bool noStage=stage.empty() || !fs::exists(stage,stageInspection),noBackup=backup.empty() || !fs::exists(backup,backupInspection);
            if(!published && noStage && noBackup && !stageInspection && !backupInspection)journal.Discard();
            return false;
        }
    }
};
}
