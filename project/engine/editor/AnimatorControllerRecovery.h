#pragma once
#include "AnimatorControllerPublication.h"

namespace Engine::Editor {
enum class ControllerRecoveryState { Prepared, Parked, Published, Conflict };
struct ControllerRecoveryReport {
    ControllerRecoveryState state=ControllerRecoveryState::Conflict;
    std::filesystem::path source,directory;
    std::string intent;
    bool operator==(const ControllerRecoveryReport&) const = default;
};
// Source-only recovery: retain the journal and both revisions. This does not
// acknowledge a draft, update World owners or unblock another writer.
class AnimatorControllerRecovery final {
    friend class AnimatorControllerAcknowledgement;
    friend class AnimatorControllerCreationRecovery;
    friend class ControllerRecordInspection;
    using Writer=HeldAnimatorControllerWriter;
    using File=Writer::File;
    using Revision=Assets::AnimatorControllerRevision;
    using V=crude_json::value;
    struct Observed {std::unique_ptr<File> file;std::string bytes;Revision::Identity identity;};
    static V ParseIntent(const std::string& text,size_t fields=9) {
        if(text.empty() || !Scripting::ValidScriptText(text,2*1024*1024))throw std::runtime_error("Invalid Controller intent text");
        size_t depth=0,nodes=0;bool quoted=false,escape=false;
        for(char c:text){
            if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='\"')quoted=false;continue;}
            if(c=='\"'){quoted=true;if(++nodes>64)throw std::runtime_error("Controller intent nodes exceeded");}
            else if(c=='{' || c=='['){if(++depth>4 || ++nodes>64)throw std::runtime_error("Controller intent depth exceeded");}
            else if(c=='}' || c==']'){if(!depth)throw std::runtime_error("Controller intent unmatched scope");--depth;}
            else if(c==',' && ++nodes>64)throw std::runtime_error("Controller intent items exceeded");
        }
        if(quoted || depth)throw std::runtime_error("Incomplete Controller intent");
        auto value=V::parse(text);
        // Only accept the immutable canonical form emitted by publication.
        // Duplicate keys, alternate encodings and malformed numbers are refused.
        if(!value.is_object() || value.get<crude_json::object>().size()!=fields || value.dump()!=text)throw std::runtime_error("Noncanonical Controller intent");
        return value;
    }
    static Revision::Identity Identity(const V& value){
        if(!value.is_object() || value.get<crude_json::object>().size()!=3)throw std::runtime_error("Invalid Controller recovery identity");
        auto number=[&](const char* key){if(!value.contains(key) || !value[key].is_number())throw std::runtime_error("Missing identity number");const auto n=value[key].get<double>();if(!std::isfinite(n) || n<0 || n>4294967295.0 || std::floor(n)!=n)throw std::runtime_error("Invalid identity DWORD");return static_cast<DWORD>(n);};
        Revision::Identity id{number("volume"),number("high"),number("low")};if(!id.high && !id.low)throw std::runtime_error("Missing file index");return id;
    }
    static Observed Observe(const std::filesystem::path& path,const std::filesystem::path& root,size_t limit,DWORD access=GENERIC_READ){
        Observed result;const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND)return result;throw std::runtime_error("Cannot observe Controller recovery path");}
        result.file=std::make_unique<File>(path,access);Writer::ReadFile(*result.file,path,root,limit,result.bytes,result.identity);return result;
    }
    static bool Matches(const Observed& file,const Revision::Identity& id,const std::string& bytes){return file.file && file.identity==id && file.bytes==bytes;}
    using Action=std::function<void(const ControllerRecoveryReport&,const std::function<void()>&,const std::function<void()>&)>;
    static void Revalidate(const Observed& observed,const std::filesystem::path& path,const std::filesystem::path& root,size_t limit){
        if(!observed.file){const auto attributes=GetFileAttributesW(path.c_str());const auto code=GetLastError();if(attributes!=INVALID_FILE_ATTRIBUTES || (code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND))throw std::runtime_error("Recovery absent path changed");return;}
        std::string bytes;Revision::Identity id;Writer::ReadFile(*observed.file,path,root,limit,bytes,id);
        if(bytes!=observed.bytes || id!=observed.identity)throw std::runtime_error("Recovery held revision changed");
    }
    static bool Run(const Base::ProjectContext& project,const std::string& guid,ControllerRecoveryReport& output,std::string& error,const ControllerRecoveryReport* expected,const Action& action={}){
        try {
            if(!project.IsOpen() || guid.empty() || !AnimationSystem::AnimatorComponent::ValidControllerGuid(guid))throw std::runtime_error("Recovery requires open project/Controller GUID");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!AnimatorControllerPublication::Within(root,std::filesystem::canonical(project.GetRootPath())) || std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recovery root invalid or asset move pending");
            const auto directory=root/".kubo/controller-transactions"/guid;
            const auto attributes=GetFileAttributesW(directory.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !AnimatorControllerPublication::Within(std::filesystem::canonical(directory),root))throw std::runtime_error("Recovery directory missing/linked/escaped");
            auto lock=Observe(root/".kubo/controller-writers"/(guid+".lock"),root,0,GENERIC_READ|GENERIC_WRITE);if(!lock.file)throw std::runtime_error("Recovery lock missing");
            auto journal=Observe(directory/"pending.json",root,2*1024*1024);if(!journal.file)throw std::runtime_error("Incomplete transaction: preserve for review");
            auto intent=ParseIntent(journal.bytes);
            for(const auto* key:{"format","guid","sourceRelative","originalBytes","nextBytes","metadataBytes"})if(!intent.contains(key) || !intent[key].is_string())throw std::runtime_error("Invalid intent fields");
            if(intent["format"].get<crude_json::string>()!="kubo-controller-source-publication-1" || intent["guid"].get<crude_json::string>()!=guid)throw std::runtime_error("Controller recovery format/GUID mismatch");
            const auto relative=Base::PathFromUtf8(intent["sourceRelative"].get<crude_json::string>());
            if(relative.empty() || relative.is_absolute() || relative.has_root_name())throw std::runtime_error("Recovery source must be relative");
            for(const auto& part:relative)if(part==L"." || part==L"..")throw std::runtime_error("Recovery traversal refused");
            const auto sourcePath=root/relative;
            if(Assets::AssetRegistry::Classify(sourcePath)!=Assets::AssetType::AnimatorController || !AnimatorControllerPublication::Within(std::filesystem::weakly_canonical(sourcePath),root))throw std::runtime_error("Recovery source invalid");
            const auto original=intent["originalBytes"].get<crude_json::string>(),next=intent["nextBytes"].get<crude_json::string>(),meta=intent["metadataBytes"].get<crude_json::string>();
            AnimationSystem::Controller::Graph graph;
            if(!AnimationSystem::Controller::TextCodec::Decode(original,graph,error) || !AnimationSystem::Controller::TextCodec::Decode(next,graph,error))return false;
            if(meta.size()>16384 || !AnimationSystem::Controller::TextCodec::ValidateJson(meta,error))throw std::runtime_error("Invalid recovery metadata");
            const auto metadataValue=V::parse(meta);
            if(!metadataValue.is_object() || !metadataValue.contains("guid") || !metadataValue["guid"].is_string() || metadataValue["guid"].get<crude_json::string>()!=guid || !metadataValue.contains("version") || !metadataValue["version"].is_number() || metadataValue["version"].get<double>()!=1)throw std::runtime_error("Recovery metadata GUID/version mismatch");
            const auto oldId=Identity(intent["originalIdentity"]),nextId=Identity(intent["nextIdentity"]),metaId=Identity(intent["metadataIdentity"]);
            if(oldId==nextId || oldId==metaId || nextId==metaId)throw std::runtime_error("Recovery identities alias");
            auto metadata=Observe(Assets::AssetRegistry::MetadataPath(sourcePath),root,16384);if(!Matches(metadata,metaId,meta))throw std::runtime_error("Recovery metadata changed");
            const DWORD access=GENERIC_READ|((expected && !action)?DELETE:0);const auto limit=AnimationSystem::Controller::TextCodec::MaxBytes;
            auto source=Observe(sourcePath,root,limit,access),backup=Observe(directory/"original.source",root,limit,access),stage=Observe(directory/"next.source",root,limit,access);
            ControllerRecoveryReport report;report.source=sourcePath;report.directory=directory;report.intent=journal.bytes;
            if(Matches(source,oldId,original) && !backup.file && Matches(stage,nextId,next))report.state=ControllerRecoveryState::Prepared;
            else if(!source.file && Matches(backup,oldId,original) && Matches(stage,nextId,next))report.state=ControllerRecoveryState::Parked;
            else if(Matches(source,nextId,next) && Matches(backup,oldId,original) && !stage.file)report.state=ControllerRecoveryState::Published;
            if(expected){
                if(report!=*expected || report.state==ControllerRecoveryState::Conflict)throw std::runtime_error("Recovery conflicts/changed; inspect again");
                if(action)action(report,[&]{
                    if(std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory))!=root || std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Recovery root/move changed");
                    Revalidate(journal,directory/"pending.json",root,2*1024*1024);
                    Revalidate(metadata,Assets::AssetRegistry::MetadataPath(sourcePath),root,16384);
                    Revalidate(source,sourcePath,root,limit);Revalidate(backup,directory/"original.source",root,limit);Revalidate(stage,directory/"next.source",root,limit);
                },[&]{journal.file.reset();backup.file.reset();stage.file.reset();});
                else if(report.state==ControllerRecoveryState::Parked)AnimatorControllerPublication::Rename(backup.file->handle,sourcePath);
                else if(report.state==ControllerRecoveryState::Published){
                    AnimatorControllerPublication::Rename(source.file->handle,directory/"next.source");
                    try{AnimatorControllerPublication::Rename(backup.file->handle,sourcePath);}catch(...){try{AnimatorControllerPublication::Rename(source.file->handle,sourcePath);}catch(...){}throw;}
                }
            }
            output=std::move(report);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
public:
    static bool Inspect(const Base::ProjectContext& project,const std::string& guid,ControllerRecoveryReport& output,std::string& error){return Run(project,guid,output,error,nullptr);}
    static bool RestoreOriginal(const Base::ProjectContext& project,const std::string& guid,const ControllerRecoveryReport& expected,std::string& error){ControllerRecoveryReport unused;return Run(project,guid,unused,error,&expected);}
};
}
