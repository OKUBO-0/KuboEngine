#pragma once
#include "ControllerPendingTransactions.h"
#include "AnimatorControllerAcknowledgement.h"
#include <type_traits>

namespace Engine::Editor {
struct ControllerRecordFile {
    std::string name,bytes;
    Assets::AnimatorControllerRevision::Identity identity;
    bool operator==(const ControllerRecordFile&) const = default;
};
struct ControllerRecordReport {
    ControllerPendingFamily family=ControllerPendingFamily::Publication;
    std::filesystem::path root,directory;
    std::string guid,stageError;
    Assets::AnimatorControllerRevision::Identity directoryIdentity;
    std::vector<ControllerRecordFile> files;
    bool hasIntent=false,hasStage=false,validStage=false;
    bool operator==(const ControllerRecordReport&) const = default;
};
// Read-only diagnosis, NOT source restoration, quarantine or new-asset admission.
// Detached bytes/native identities are only an observation: any future action
// must acquire the same ownership and compare the full report again.
class ControllerRecordInspection final {
    friend class ControllerRecordSalvage;
    using Recovery=AnimatorControllerRecovery;
    static bool Within(const std::filesystem::path& path,const std::filesystem::path& root){
        auto part=path.begin();for(const auto& component:root){if(part==path.end() || _wcsicmp(part->c_str(),component.c_str()))return false;++part;}return part!=path.end();
    }
    static std::vector<std::filesystem::path> Children(const std::filesystem::path& directory){
        std::vector<std::filesystem::path> result;
        for(const auto& child:std::filesystem::directory_iterator(directory)){
            if(result.size()==MaxFiles)throw std::runtime_error("Controller record exceeds inspection file limit");
            const auto attributes=Assets::ControllerPendingDetail::Attributes(child.path());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
                throw std::runtime_error("Controller record child is absent, nested or linked; preserve for manual review");
            result.push_back(child.path());
        }
        std::sort(result.begin(),result.end());return result;
    }
public:
    static constexpr size_t MaxFiles=128,MaxFileBytes=2*1024*1024,MaxTotalBytes=8*1024*1024;
private:
    using Action=std::function<void(const ControllerRecordReport&,HANDLE,const std::function<void()>&,const std::function<void()>&)>;
    static bool Run(const Base::ProjectContext& project,ControllerPendingFamily family,const std::string& guid,
                    ControllerRecordReport& output,std::string& error,const Action& action={},const std::filesystem::path& archive={}){
        static_assert(std::is_nothrow_move_assignable_v<ControllerRecordReport>);
        try{
            if(!project.IsOpen() || guid.empty() || !AnimationSystem::AnimatorComponent::ValidControllerGuid(guid) ||
               (family!=ControllerPendingFamily::Publication && family!=ControllerPendingFamily::Creation) ||
               (family==ControllerPendingFamily::Creation && guid.size()!=32))
                throw std::runtime_error("Record diagnosis requires open project/family/valid GUID");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            Base::ProjectContext fresh;
            if(!fresh.Open(project.GetProjectFile(),error))return false;
            if(std::filesystem::canonical(fresh.ResolvePath(fresh.GetSettings().assetDirectory))!=root ||
               !Within(root,std::filesystem::canonical(project.GetRootPath())))
                throw std::runtime_error("Controller record project/root changed");
            const auto storage=root/".kubo";
            const auto parent=storage/(family==ControllerPendingFamily::Publication?"controller-transactions":"controller-creations");
            if(!Assets::ControllerPendingDetail::Directory(storage) ||
               (archive.empty() && (!Assets::ControllerPendingDetail::Directory(parent) || !Within(std::filesystem::canonical(parent),root))) ||
               std::filesystem::exists(storage/"asset-move.pending.json"))
                throw std::runtime_error("Controller record storage invalid or asset move pending");
            if(family==ControllerPendingFamily::Publication && !Assets::ControllerPendingDetail::Directory(storage/"controller-writers"))
                throw std::runtime_error("Controller record writer storage missing");
            const auto lockPath=family==ControllerPendingFamily::Publication?storage/"controller-writers"/(guid+".lock"):storage/"controller-create.lock";
            auto lock=Recovery::Observe(lockPath,root,0,GENERIC_READ|GENERIC_WRITE);
            if(!lock.file)throw std::runtime_error("Controller record writer lock missing; preserve record");
            auto directory=parent/guid;
            if(!archive.empty()){
                const auto quarantine=storage/"controller-quarantine";
                const std::string prefix=(family==ControllerPendingFamily::Publication?"publication-":"creation-")+guid+"-";
                if(!archive.is_absolute() || archive.lexically_normal()!=archive || archive.parent_path()!=quarantine ||
                   !Base::PathToUtf8(archive.filename()).starts_with(prefix) || !Assets::ControllerPendingDetail::Directory(quarantine) ||
                   std::filesystem::canonical(quarantine)!=quarantine || !Within(quarantine,root))
                    throw std::runtime_error("Salvage archive must be an ordinary matching quarantine child");
                directory=archive;
            }
            AnimatorControllerAcknowledgement::Directory ownedDirectory(directory,root);
            BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(ownedDirectory.handle,&info))throw std::runtime_error("Cannot identify Controller record directory");
            ControllerRecordReport candidate;candidate.family=family;candidate.root=root;candidate.directory=directory;candidate.guid=guid;
            candidate.directoryIdentity={info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};
            const auto children=Children(directory);
            std::vector<Recovery::Observed> held;held.reserve(children.size());size_t total=0;
            for(const auto& child:children){
                auto observed=Recovery::Observe(child,root,MaxFileBytes);
                if(!observed.file || observed.bytes.size()>MaxTotalBytes-total)throw std::runtime_error("Controller record disappeared or byte budget exceeded");
                total+=observed.bytes.size();
                ControllerRecordFile file{Base::PathToUtf8(child.filename()),observed.bytes,observed.identity};
                if(file.name=="pending.json")candidate.hasIntent=true;
                const auto stage=family==ControllerPendingFamily::Publication?"next.source":"source.stage";
                if(file.name==stage){
                    candidate.hasStage=true;AnimationSystem::Controller::Graph graph;
                    candidate.validStage=AnimationSystem::Controller::TextCodec::Decode(file.bytes,graph,candidate.stageError);
                }
                candidate.files.push_back(std::move(file));held.push_back(std::move(observed));
            }
            const auto revalidate=[&]{
                if(Children(directory)!=children || std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory))!=root ||
                   std::filesystem::exists(storage/"asset-move.pending.json"))throw std::runtime_error("Controller record namespace/root/move changed");
                for(size_t i=0;i<children.size();++i)Recovery::Revalidate(held[i],children[i],root,MaxFileBytes);
                Base::ProjectContext finalProject;
                std::string manifestError;if(!finalProject.Open(project.GetProjectFile(),manifestError))throw std::runtime_error(manifestError);
                if(std::filesystem::canonical(finalProject.ResolvePath(finalProject.GetSettings().assetDirectory))!=root)
                    throw std::runtime_error("Controller record manifest root changed before publication");
            };
            revalidate();
            if(action)action(candidate,ownedDirectory.handle,revalidate,[&]{held.clear();});
            output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
public:
    static bool Inspect(const Base::ProjectContext& project,ControllerPendingFamily family,const std::string& guid,
                        ControllerRecordReport& output,std::string& error){return Run(project,family,guid,output,error);}
    static bool InspectArchive(const Base::ProjectContext& project,ControllerPendingFamily family,const std::string& guid,
                               const std::filesystem::path& archive,ControllerRecordReport& output,std::string& error){
        if(archive.empty()){error="Select a quarantine archive";return false;}
        return Run(project,family,guid,output,error,{},archive);
    }
    // Explicit preservation only. Canonical known intents belong to normal
    // recovery; incomplete/opaque records never authorize source restoration.
    static bool Quarantine(const Base::ProjectContext& project,const ControllerRecordReport& expected,
                           std::filesystem::path& archived,std::string& error,
                           const std::function<void(const std::filesystem::path&)>& beforeRename={}){
        std::filesystem::path destination;ControllerRecordReport unused;
        const bool success=Run(project,expected.family,expected.guid,unused,error,
            [&](const ControllerRecordReport& report,HANDLE directory,const std::function<void()>& revalidate,const std::function<void()>& releaseChildren){
                if(report!=expected)throw std::runtime_error("Controller record changed; diagnose again before quarantine");
                for(const auto& file:report.files)if(file.name=="pending.json"){
                    crude_json::value intent;bool canonical=false;
                    try{intent=Recovery::ParseIntent(file.bytes,report.family==ControllerPendingFamily::Publication?9:7);canonical=true;}
                    catch(const std::bad_alloc&){throw;}catch(const std::exception&){}
                    const char* format=report.family==ControllerPendingFamily::Publication?"kubo-controller-source-publication-1":"kubo-controller-creation-1";
                    if(canonical && intent.contains("format") && intent["format"].is_string() && intent["format"].get<crude_json::string>()==format &&
                       intent.contains("guid") && intent["guid"].is_string() && intent["guid"].get<crude_json::string>()==report.guid)
                        throw std::runtime_error("Known canonical Controller intent requires normal recovery, not partial quarantine");
                }
                const auto storage=report.root/".kubo",parent=storage/"controller-quarantine";
                if(!Within(std::filesystem::weakly_canonical(parent),report.root))throw std::runtime_error("Quarantine parent escaped");
                std::filesystem::create_directories(parent);
                if(!Assets::ControllerPendingDetail::Directory(parent) || !Within(std::filesystem::canonical(parent),report.root))throw std::runtime_error("Quarantine parent linked/changed");
                static std::atomic<unsigned long long> sequence{0};
                destination=parent/((report.family==ControllerPendingFamily::Publication?"publication-":"creation-")+report.guid+"-"+
                    std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));
                if(beforeRename)beforeRename(destination);
                if(!Assets::ControllerPendingDetail::Directory(parent) || !Within(std::filesystem::canonical(parent),report.root))throw std::runtime_error("Quarantine parent changed before rename");
                revalidate();
                // Directory + writer lock stay owned. Windows requires child
                // handles released; this is not a hostile child-name sandbox.
                releaseChildren();AnimatorControllerPublication::Rename(directory,destination);
            });
        if(success)archived.swap(destination);return success;
    }
};
}
