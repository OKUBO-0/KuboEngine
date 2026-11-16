#pragma once
#include "engine/base/ProjectContext.h"
#include "engine/editor/NativeBuildSettings.h"
#include <atomic>
#include "NativeCreateJournal.h"

namespace Engine::Services {
// Trusted SDK template creation, not compilation or an untrusted-code sandbox.
class NativeProjectCreate final {
public:
#if defined(KUBO_NATIVE_CREATE_TEST_HOOK)
    // Deterministic publication failures; absent from shipped Launcher builds.
    inline static void (*BeforePublish)(const std::filesystem::path&,const std::filesystem::path&)=nullptr;
    inline static void (*AfterPublish)(const std::filesystem::path&)=nullptr;
#endif
    static bool Create(const std::filesystem::path& manifest, const std::filesystem::path& sdk,
        Engine::Base::ProjectContext& context, std::string& error) {
        std::filesystem::path stage,root,backup;
        bool ownsStage=false,movedEmpty=false,published=false;
        NativeCreateJournal journal;
        try {
            const auto destination=std::filesystem::absolute(manifest).lexically_normal();
            root=destination.parent_path();
            if(root==root.root_path()) throw std::runtime_error("Do not create a project at a volume root");
            const auto recovery=NativeCreateJournal::Recover(destination,error);
            if(recovery==NativeCreateJournal::Recovery::Published) {
                const auto notice=error;Engine::Base::ProjectContext recovered;
                if(!recovered.Open(destination,error)) return false;
                context=std::move(recovered);error=notice;return true;
            }
            if(recovery!=NativeCreateJournal::Recovery::None) return false;
            const auto sdkRoot=std::filesystem::absolute(sdk).lexically_normal();
            if(_wcsicmp(destination.extension().c_str(),L".kubo")!=0) throw std::runtime_error("Choose a .kubo manifest in a new or empty project directory");
            if(std::filesystem::exists(root) && (!std::filesystem::is_directory(root) || !std::filesystem::is_empty(root)))
                throw std::runtime_error("Native project creation requires a new or empty directory");
            const auto rootText=std::filesystem::weakly_canonical(root).wstring();
            const auto sdkText=std::filesystem::weakly_canonical(sdkRoot).wstring();
            const auto sdkPrefix=sdkText+L"\\";
            if(_wcsicmp(rootText.c_str(),sdkText.c_str())==0 || _wcsnicmp(rootText.c_str(),sdkPrefix.c_str(),sdkPrefix.size())==0)
                throw std::runtime_error("Do not create a game inside its SDK");
            std::string source,project,metadata;
            const auto read=[&](const std::filesystem::path& path,std::string& text) {
                if(!std::filesystem::is_regular_file(path) || !Engine::Editor::EditorSettingsFile::Read(path,{},text,error))
                    throw std::runtime_error("Cannot read SDK template/input "+Engine::Base::PathToUtf8(path)+": "+error);
            };
            read(sdkRoot/"examples/Starter/Starter.cpp",source);
            read(sdkRoot/"examples/Starter/Starter.vcxproj",project);
            read(sdkRoot/"sdk-package.json",metadata);
            if(metadata.starts_with("\xEF\xBB\xBF")) metadata.erase(0,3);
            size_t depth=0;bool quoted=false,escaped=false;
            for(const char c:metadata) {
                if(quoted) {if(escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='\"') quoted=false;}
                else if(c=='\"') quoted=true;
                else if(c=='{' || c=='[') {if(++depth>32) throw std::runtime_error("SDK manifest nesting exceeds 32");}
                else if((c=='}' || c==']') && depth) --depth;
            }
            const auto sdkMetadata=crude_json::value::parse(metadata);
            // The build command validates archive hashes and the toolchain later.
            if(!sdkMetadata.is_object() || !sdkMetadata.contains("format") || !sdkMetadata["format"].is_string() ||
                sdkMetadata["format"].get<crude_json::string>()!="kubo-runtime-sdk-1" || !std::filesystem::is_regular_file(sdkRoot/"KuboRuntime.props") ||
                !std::filesystem::is_regular_file(sdkRoot/"tools/build_native_game.ps1") ||
                !std::filesystem::is_regular_file(sdkRoot/"tools/package_project.ps1") || source.empty() || project.empty())
                throw std::runtime_error("Select a trusted Runtime SDK with native build/export templates");
            Engine::Editor::NativeBuildSettings settings;
            const auto utf8=sdkRoot.u8string();settings.sdk.assign(utf8.begin(),utf8.end());
            settings.native=true;settings.project="Native/Starter.vcxproj";settings.output="Builds/Native";
            std::filesystem::create_directories(root.parent_path());
            static std::atomic<unsigned long long> sequence{0};
            const auto token=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
            stage=root.parent_path()/(L".kubo-native-stage-"+token);
            backup=root.parent_path()/(L".kubo-empty-backup-"+token);
            if(!std::filesystem::create_directory(stage)) throw std::runtime_error("Cannot exclusively create native project stage");
            ownsStage=true;
            journal.Begin(root,destination,stage,backup);
            if(!Engine::Editor::NativeBuildSettingsFile::Save(stage/"UserSettings/native-build.json",settings,error)) throw std::runtime_error(error);
            const auto write=[&](const std::filesystem::path& path,const std::string& text) {
                if(!Engine::Base::AtomicFile::Write(path,text,Engine::Editor::EditorSettingsFile::Limit,error,false)) throw std::runtime_error(error);
            };
            write(stage/"Native/Starter.cpp",source);write(stage/"Native/Starter.vcxproj",project);
            write(stage/".gitignore","Library/\nBuild/\nBuilds/\nNative/bin/\nNative/obj/\nUserSettings/native-build.json\nUserSettings/native-build.json.lock\nUserSettings/ControllerLayouts/\n");
            Engine::Base::ProjectContext candidate;
            const auto name=destination.stem().u8string();
            if(!candidate.Create(stage,std::string(name.begin(),name.end()),error)) throw std::runtime_error(error);
            // Empty existing folders cannot be replaced with a directory rename
            // on Windows. Retain the original folder as a recoverable sibling.
            journal.ValidateBeforePublication(root,stage);
            if(std::filesystem::exists(root)) {
                const auto attributes=GetFileAttributesW(root.c_str());
                if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                    !std::filesystem::is_directory(root) || !std::filesystem::is_empty(root))
                    throw std::runtime_error("Native output changed or is a linked directory; publication refused");
                if(!MoveFileExW(root.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot retain empty output directory before publication");
                movedEmpty=true;
                journal.ValidateRetired(backup);
                if(!std::filesystem::is_empty(backup)) throw std::runtime_error("Output became nonempty during publication");
            }
#if defined(KUBO_NATIVE_CREATE_TEST_HOOK)
            if(BeforePublish) BeforePublish(root,stage);
#endif
            if(!MoveFileExW(stage.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot publish native project directory without overwriting output");
            ownsStage=false;published=true;
#if defined(KUBO_NATIVE_CREATE_TEST_HOOK)
            if(AfterPublish) AfterPublish(root);
#endif
            // Retarget context paths after publication; never expose stage paths.
            if(!candidate.Open(root/candidate.GetProjectFile().filename(),error)) throw std::runtime_error(error);
            context=std::move(candidate);error.clear();
            if(movedEmpty) {
                std::error_code cleanup;
                if(!std::filesystem::remove(backup,cleanup) || cleanup)
                    error="Project created; original output backup retained at "+Engine::Base::PathToUtf8(backup);
            }
            if(error.empty() && !journal.Discard()) error="Project created; recovery journal retained at "+Engine::Base::PathToUtf8(NativeCreateJournal::Path(root));
            return true;
        } catch(const std::exception& e) {
            error=e.what();
            if(movedEmpty && !published) {
                if(!MoveFileExW(backup.c_str(),root.c_str(),MOVEFILE_WRITE_THROUGH))
                    error+="; original output retained at "+Engine::Base::PathToUtf8(backup);
            }
            if(ownsStage) {
                // Validate the exact owned sibling and refuse recursive cleanup
                // if a link was introduced; do not touch user output/backup.
                const auto stageAttributes=GetFileAttributesW(stage.c_str());
                bool safe=stage.parent_path()==root.parent_path() && stage.filename().wstring().starts_with(L".kubo-native-stage-") &&
                    stageAttributes!=INVALID_FILE_ATTRIBUTES && !(stageAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
                std::error_code inspect;
                if(safe) {
                    for(std::filesystem::recursive_directory_iterator it(stage,inspect),end;safe && !inspect && it!=end;it.increment(inspect)) {
                        const auto attributes=GetFileAttributesW(it->path().c_str());
                        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) safe=false;
                    }
                }
                if(safe && !inspect) {std::error_code cleanup;std::filesystem::remove_all(stage,cleanup);if(cleanup) error+="; stage retained at "+Engine::Base::PathToUtf8(stage);}
                else error+="; stage retained for inspection at "+Engine::Base::PathToUtf8(stage);
            }
            if(published) {
                error+="; completed directory retained at "+Engine::Base::PathToUtf8(root);
                if(movedEmpty) error+="; original empty directory backup at "+Engine::Base::PathToUtf8(backup);
            }
            std::error_code stageInspection,backupInspection;
            const bool stageRemains=!stage.empty() && std::filesystem::exists(stage,stageInspection);
            const bool backupRemains=!backup.empty() && std::filesystem::exists(backup,backupInspection);
            if(!published && !stageRemains && !backupRemains && !stageInspection && !backupInspection) journal.Discard();
            return false;
        }
    }
};
}
