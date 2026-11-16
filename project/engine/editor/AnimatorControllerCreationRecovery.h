#pragma once
#include "AnimatorControllerCreation.h"
#include "AnimatorControllerAcknowledgement.h"

namespace Engine::Editor {
enum class ControllerCreationState { Prepared, MetadataOnly, Published, Conflict };
struct ControllerCreationReport {
    ControllerCreationState state=ControllerCreationState::Conflict;
    std::filesystem::path source,directory;std::string intent;
    bool operator==(const ControllerCreationReport&) const = default;
};
// Inspect/re-stage/archive only; document and registry adoption are separate.
class AnimatorControllerCreationRecovery final {
    using Recovery=AnimatorControllerRecovery;
    using Publication=AnimatorControllerPublication;
    using Revision=Assets::AnimatorControllerRevision;
    enum class Operation { Inspect, Restage, Archive };
    static bool Run(const Base::ProjectContext& project,const std::string& guid,ControllerCreationReport& output,std::string& error,Operation operation,const ControllerCreationReport* expected,std::filesystem::path* archived,const std::function<void(const std::filesystem::path&)>& beforeArchive,const std::function<void()>& afterSourcePark={}){
        try{
            if(!project.IsOpen() || guid.size()!=32 || !AnimationSystem::AnimatorComponent::ValidControllerGuid(guid))throw std::runtime_error("Creation recovery requires project/new GUID");
            const auto root=std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory));
            if(!Publication::Within(root,std::filesystem::canonical(project.GetRootPath())) || std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Creation recovery root/move invalid");
            const auto directory=root/".kubo/controller-creations"/guid;
            const auto attributes=GetFileAttributesW(directory.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Publication::Within(std::filesystem::canonical(directory),root))throw std::runtime_error("Creation recovery directory missing/linked/escaped");
            auto lock=Recovery::Observe(root/".kubo/controller-create.lock",root,0,GENERIC_READ|GENERIC_WRITE);if(!lock.file)throw std::runtime_error("Creation recovery lock missing");
            std::unique_ptr<AnimatorControllerAcknowledgement::Directory> ownedDirectory;
            if(operation==Operation::Archive)ownedDirectory=std::make_unique<AnimatorControllerAcknowledgement::Directory>(directory,root);
            auto journal=Recovery::Observe(directory/"pending.json",root,2*1024*1024);if(!journal.file)throw std::runtime_error("Incomplete creation intent; preserve for review");
            const auto intent=Recovery::ParseIntent(journal.bytes,7);
            for(const auto* key:{"format","guid","sourceRelative","sourceBytes","metadataBytes"})if(!intent.contains(key) || !intent[key].is_string())throw std::runtime_error("Creation intent text fields invalid");
            if(intent["format"].get<crude_json::string>()!="kubo-controller-creation-1" || intent["guid"].get<crude_json::string>()!=guid)throw std::runtime_error("Creation intent format/GUID mismatch");
            const auto relativeText=intent["sourceRelative"].get<crude_json::string>();if(!Scripting::ValidScriptText(relativeText,4096))throw std::runtime_error("Creation recovery invalid path text");
            const auto relative=Assets::MoveDetail::Relative(relativeText);Assets::MoveDetail::CheckParents(root,relative);
            const auto path=root/relative;
            if(Assets::AssetRegistry::Classify(path)!=Assets::AssetType::AnimatorController || !Publication::Within(std::filesystem::weakly_canonical(path),root))throw std::runtime_error("Creation source path invalid");
            const auto bytes=intent["sourceBytes"].get<crude_json::string>(),meta=intent["metadataBytes"].get<crude_json::string>();AnimationSystem::Controller::Graph graph;
            if(!AnimationSystem::Controller::TextCodec::Decode(bytes,graph,error))return false;
            if(meta.size()>16384 || !AnimationSystem::Controller::TextCodec::ValidateJson(meta,error))throw std::runtime_error("Creation metadata text invalid");
            const auto metaValue=crude_json::value::parse(meta);
            if(!metaValue.is_object() || !metaValue.contains("guid") || !metaValue["guid"].is_string() || metaValue["guid"].get<crude_json::string>()!=guid || !metaValue.contains("version") || !metaValue["version"].is_number() || metaValue["version"].get<double>()!=1)throw std::runtime_error("Creation metadata identity invalid");
            if(!intent.contains("sourceIdentity") || !intent.contains("metadataIdentity"))throw std::runtime_error("Creation native identity missing");
            const auto sourceId=Recovery::Identity(intent["sourceIdentity"]),metaId=Recovery::Identity(intent["metadataIdentity"]);if(sourceId==metaId)throw std::runtime_error("Creation identities alias");
            const auto metadataPath=Assets::AssetRegistry::MetadataPath(path);const DWORD access=GENERIC_READ|(operation==Operation::Restage?DELETE:0);const auto limit=AnimationSystem::Controller::TextCodec::MaxBytes;
            auto source=Recovery::Observe(path,root,limit,access),metadata=Recovery::Observe(metadataPath,root,16384,access),stage=Recovery::Observe(directory/"source.stage",root,limit,access),metaStage=Recovery::Observe(directory/"metadata.stage",root,16384,access);
            ControllerCreationReport report;report.source=path;report.directory=directory;report.intent=journal.bytes;
            if(!source.file && !metadata.file && Recovery::Matches(stage,sourceId,bytes) && Recovery::Matches(metaStage,metaId,meta))report.state=ControllerCreationState::Prepared;
            else if(!source.file && Recovery::Matches(metadata,metaId,meta) && Recovery::Matches(stage,sourceId,bytes) && !metaStage.file)report.state=ControllerCreationState::MetadataOnly;
            else if(Recovery::Matches(source,sourceId,bytes) && Recovery::Matches(metadata,metaId,meta) && !stage.file && !metaStage.file)report.state=ControllerCreationState::Published;
            if(operation!=Operation::Inspect){
                if(!expected || report!=*expected || report.state==ControllerCreationState::Conflict)throw std::runtime_error("Creation recovery state conflicts/changed; inspect again");
                if(operation==Operation::Restage){
                    if(report.state==ControllerCreationState::MetadataOnly)Publication::Rename(metadata.file->handle,directory/"metadata.stage");
                    else if(report.state==ControllerCreationState::Published){
                        Publication::Rename(source.file->handle,directory/"source.stage");
                        try{if(afterSourcePark)afterSourcePark();Publication::Rename(metadata.file->handle,directory/"metadata.stage");}
                        catch(...){try{Publication::Rename(source.file->handle,path);}catch(...){}throw;}
                    }
                }else{
                    if(report.state!=ControllerCreationState::Prepared && report.state!=ControllerCreationState::Published)throw std::runtime_error("Re-stage MetadataOnly creation before acknowledgement");
                    // Published acknowledgement also proves unique registered
                    // GUID + real loader admission, while source/meta stay held.
                    const auto admitPublished=[&]{
                        if(report.state!=ControllerCreationState::Published)return;
                        Assets::AssetRegistry registry;if(!registry.Scan(root,error,Assets::AssetRegistry::MetadataPolicy::ReadOnly))throw std::runtime_error(error);
                        Revision expectedRevision;expectedRevision.source=path;expectedRevision.metadata=metadataPath;expectedRevision.guid=guid;expectedRevision.bytes=bytes;expectedRevision.metadataBytes=meta;expectedRevision.sourceIdentity=sourceId;expectedRevision.metadataIdentity=metaId;
                        Assets::HeldAnimatorControllerAsset lease;if(!lease.OpenForRecovery(project,registry,guid,expectedRevision,error))throw std::runtime_error(error);
                    };
                    admitPublished();
                    const auto parent=root/".kubo/controller-creation-archive";
                    if(!Publication::Within(std::filesystem::weakly_canonical(parent),root))throw std::runtime_error("Creation archive escaped");
                    std::filesystem::create_directories(parent);const auto parentAttributes=GetFileAttributesW(parent.c_str());
                    if(parentAttributes==INVALID_FILE_ATTRIBUTES || (parentAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Publication::Within(std::filesystem::canonical(parent),root))throw std::runtime_error("Creation archive linked/changed");
                    static std::atomic<unsigned long long> sequence{0};auto destination=parent/(guid+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++sequence));
                    if(beforeArchive)beforeArchive(destination);
                    if(std::filesystem::canonical(project.ResolvePath(project.GetSettings().assetDirectory))!=root || std::filesystem::exists(root/".kubo/asset-move.pending.json"))throw std::runtime_error("Creation root/move changed before archive");
                    Assets::MoveDetail::CheckParents(root,relative);
                    const auto finalParentAttributes=GetFileAttributesW(parent.c_str());
                    if(finalParentAttributes==INVALID_FILE_ATTRIBUTES || !(finalParentAttributes&FILE_ATTRIBUTE_DIRECTORY) || (finalParentAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Publication::Within(std::filesystem::canonical(parent),root))throw std::runtime_error("Creation archive parent changed before rename");
                    Recovery::Revalidate(journal,directory/"pending.json",root,2*1024*1024);Recovery::Revalidate(source,path,root,limit);Recovery::Revalidate(metadata,metadataPath,root,16384);Recovery::Revalidate(stage,directory/"source.stage",root,limit);Recovery::Revalidate(metaStage,directory/"metadata.stage",root,16384);
                    admitPublished();
                    // Windows requires child file handles released for directory
                    // rename. Keep lock/directory/source/meta; archive unknown
                    // children too. Not a hostile child-name race sandbox.
                    journal.file.reset();stage.file.reset();metaStage.file.reset();Publication::Rename(ownedDirectory->handle,destination);archived->swap(destination);
                }
            }
            output=std::move(report);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
public:
    static bool Inspect(const Base::ProjectContext& project,const std::string& guid,ControllerCreationReport& output,std::string& error){return Run(project,guid,output,error,Operation::Inspect,nullptr,nullptr,{});}
    static bool Restage(const Base::ProjectContext& project,const std::string& guid,const ControllerCreationReport& expected,std::string& error,const std::function<void()>& afterSourcePark={}){ControllerCreationReport unused;return Run(project,guid,unused,error,Operation::Restage,&expected,nullptr,{},afterSourcePark);}
    static bool ArchiveResolved(const Base::ProjectContext& project,const std::string& guid,const ControllerCreationReport& expected,std::filesystem::path& archived,std::string& error,const std::function<void(const std::filesystem::path&)>& beforeArchive={}){ControllerCreationReport unused;return Run(project,guid,unused,error,Operation::Archive,&expected,&archived,beforeArchive);}
};
}
