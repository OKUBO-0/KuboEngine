#pragma once
#include "../assets/AnimatorControllerAsset.h"
#include "../animation/AnimatorComponent.h"
#include <algorithm>

namespace Engine::Editor {
enum class ControllerPendingFamily { Publication, Creation };
enum class ControllerPendingKind { Intent, Incomplete, Unknown };
struct ControllerPendingEntry {
    std::filesystem::path directory;std::string name,guid;
    ControllerPendingFamily family=ControllerPendingFamily::Publication;
    ControllerPendingKind kind=ControllerPendingKind::Unknown;
};
struct ControllerArchiveEntry {
    std::filesystem::path directory;
    std::string name,guid;
    ControllerPendingFamily family=ControllerPendingFamily::Publication;
    bool diagnosable=false;
    bool operator==(const ControllerArchiveEntry&) const = default;
};
// Read-only namespace inventory. Marker presence is NOT intent validity or
// mutation authority; recovery must acquire ownership and inspect again.
class ControllerPendingTransactions final {
    static bool Within(const std::filesystem::path& path,const std::filesystem::path& root){auto part=path.begin();for(const auto& component:root){if(part==path.end() || _wcsicmp(part->c_str(),component.c_str()))return false;++part;}return part!=path.end();}
public:
    static constexpr size_t MaxEntries=128;
    // Namespace observation only: a recognized filename is not valid content
    // or mutation authority. InspectArchive must re-own and diagnose selection.
    static bool ScanArchives(const Base::ProjectContext& project,std::vector<ControllerArchiveEntry>& output,std::string& error){
        try{
            if(!project.IsOpen())throw std::runtime_error("Controller archive inventory requires open project");
            const auto root=std::filesystem::weakly_canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            Base::ProjectContext fresh;if(!fresh.Open(project.GetProjectFile(),error))return false;
            if(root!=std::filesystem::weakly_canonical(fresh.ResolvePath(fresh.GetSettings().assetDirectory)) || !Within(root,std::filesystem::canonical(project.GetRootPath())))
                throw std::runtime_error("Controller archive inventory root changed/escaped");
            std::vector<ControllerArchiveEntry> candidate;
            const auto storage=root/".kubo",parent=storage/"controller-quarantine";
            if(!Assets::ControllerPendingDetail::Directory(root) || !Assets::ControllerPendingDetail::Directory(storage) || !Assets::ControllerPendingDetail::Directory(parent)){
                output=std::move(candidate);error.clear();return true;
            }
            if(!Within(std::filesystem::canonical(parent),root))throw std::runtime_error("Controller archive parent escaped");
            for(const auto& item:std::filesystem::directory_iterator(parent)){
                if(candidate.size()==MaxEntries)throw std::runtime_error("Controller archive inventory exceeds 128 entries; preserve for review");
                ControllerArchiveEntry entry;entry.directory=item.path();entry.name=Base::PathToUtf8(item.path().filename());
                const auto attributes=Assets::ControllerPendingDetail::Attributes(item.path());
                if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Controller archive entry changed");
                std::string_view rest=entry.name;bool recognized=false;
                if(rest.starts_with("publication-")){rest.remove_prefix(12);entry.family=ControllerPendingFamily::Publication;recognized=true;}
                else if(rest.starts_with("creation-")){rest.remove_prefix(9);entry.family=ControllerPendingFamily::Creation;recognized=true;}
                if(recognized && (attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){
                    const auto split=rest.find('-');
                    if(split!=std::string_view::npos){
                        auto guid=std::string(rest.substr(0,split));rest.remove_prefix(split+1);
                        bool suffix=true;
                        for(size_t i=0;i<3;++i){
                            const auto end=rest.find('-');const auto number=rest.substr(0,end);
                            if(number.empty() || number.size()>20 || (i<2 && end==std::string_view::npos) || (i==2 && end!=std::string_view::npos))suffix=false;
                            for(char c:number)if(c<'0' || c>'9')suffix=false;
                            if(end!=std::string_view::npos)rest.remove_prefix(end+1);
                        }
                        if(suffix && !guid.empty() && AnimationSystem::AnimatorComponent::ValidControllerGuid(guid) &&
                           (entry.family==ControllerPendingFamily::Publication || guid.size()==32)){
                            entry.guid=std::move(guid);entry.diagnosable=true;
                        }
                    }
                }
                candidate.push_back(std::move(entry));
            }
            std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.name<b.name;});
            output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    static bool Scan(const Base::ProjectContext& project,std::vector<ControllerPendingEntry>& output,std::string& error){
        try{
            if(!project.IsOpen())throw std::runtime_error("Controller inventory requires open project");
            const auto root=std::filesystem::weakly_canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!Within(root,std::filesystem::canonical(project.GetRootPath())))throw std::runtime_error("Controller inventory root escapes project");
            std::vector<ControllerPendingEntry> candidate;
            if(!Assets::ControllerPendingDetail::Directory(root)){output=std::move(candidate);error.clear();return true;}
            const auto storage=root/".kubo";
            if(!Assets::ControllerPendingDetail::Directory(storage)){output=std::move(candidate);error.clear();return true;}
            for(const auto family:{ControllerPendingFamily::Publication,ControllerPendingFamily::Creation}){
                const auto parent=storage/(family==ControllerPendingFamily::Publication?"controller-transactions":"controller-creations");
                if(!Assets::ControllerPendingDetail::Directory(parent))continue;
                if(!Within(std::filesystem::canonical(parent),root))throw std::runtime_error("Controller pending parent escaped");
                for(const auto& item:std::filesystem::directory_iterator(parent)){
                    if(candidate.size()==MaxEntries)throw std::runtime_error("Controller inventory exceeds 128 entries; preserve records for manual review");
                    ControllerPendingEntry entry;entry.family=family;entry.directory=item.path();entry.name=Base::PathToUtf8(item.path().filename());
                    if(!entry.name.empty() && AnimationSystem::AnimatorComponent::ValidControllerGuid(entry.name) && (family==ControllerPendingFamily::Publication || entry.name.size()==32))entry.guid=entry.name;
                    const auto attributes=Assets::ControllerPendingDetail::Attributes(item.path());
                    if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Controller entry changed during inventory");
                    if(!entry.guid.empty() && (attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){
                        const auto marker=Assets::ControllerPendingDetail::Attributes(item.path()/"pending.json");
                        entry.kind=marker==INVALID_FILE_ATTRIBUTES?ControllerPendingKind::Incomplete:
                            (marker&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))?ControllerPendingKind::Unknown:ControllerPendingKind::Intent;
                    }
                    candidate.push_back(std::move(entry));
                }
            }
            std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.family!=b.family?a.family<b.family:a.name<b.name;});
            output=std::move(candidate);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
};
}
