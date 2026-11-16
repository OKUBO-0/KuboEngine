#include "Framework.h"
#include "framework_run_failure_candidate.h"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
enum class Stage {None,Initialize,Update,EndQuery,Draw};
struct OperationFault {const void* token;Stage stage;};
struct CleanupFault {const void* token;unsigned attempt;};
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Counts {unsigned initialized=0,finalized=0,hostDestroyed=0,memberDestroyed=0,refusals=0;};
struct Member {Counts& counts;~Member(){++counts.memberDestroyed;}};
class Host final:public Engine::Base::Framework {
public:
    Host(Counts& counts,Stage stage,unsigned failures):counts_(counts),member_{counts},stage_(stage),failures_(failures){}
    ~Host() override{++counts_.hostDestroyed;}
    void Initialize() override{++counts_.initialized;RefuseReentry();Fail(Stage::Initialize);}
    void Update() override{++updates_;Fail(Stage::Update);}
    bool IsEndRequest() const override{Fail(Stage::EndQuery);return updates_>1;}
    void Draw() override{Fail(Stage::Draw);}
    void Finalize() override{++counts_.finalized;RefuseReentry();if(failures_){--failures_;throw CleanupFault{this,counts_.finalized};}}
    void RefuseReentry(){
        const auto beforeInit=counts_.initialized,beforeFinal=counts_.finalized;bool refused=false;
        try{Run();}catch(const std::logic_error&){refused=true;}
        Require(refused && beforeInit==counts_.initialized && beforeFinal==counts_.finalized,"Reentered/repeated Run changed lifecycle ownership");++counts_.refusals;
    }
    const Counts& GetCounts() const{return counts_;}
    Stage GetStage() const{return stage_;}
    bool Active() const{return runActiveCandidate_;}
private:
    void Fail(Stage stage) const{if(stage_==stage)throw OperationFault{this,stage};}
    Counts& counts_;Member member_;Stage stage_;unsigned failures_,updates_=0;
};
bool OperationMatches(const std::exception_ptr& fault,const Host* host){
    if(!fault)return host->GetStage()==Stage::None;
    try{std::rethrow_exception(fault);}catch(const OperationFault& error){return error.token==host && error.stage==host->GetStage();}catch(...){return false;}
}
bool CleanupMatches(const std::exception_ptr& fault,const Host* host){if(!fault)return false;try{std::rethrow_exception(fault);}catch(const CleanupFault& error){return error.token==host && error.attempt==1;}catch(...){return false;}}
}
void FixtureRunFatal(Engine::Base::Framework* base,const std::exception_ptr& operation,const std::exception_ptr& cleanup,unsigned attempts) noexcept{
    const auto* host=static_cast<const Host*>(base);const auto& counts=host->GetCounts();
    if(attempts!=3 || counts.finalized!=3 || counts.hostDestroyed || counts.memberDestroyed || !host->Active() || !OperationMatches(operation,host) || !CleanupMatches(cleanup,host))std::_Exit(74);
    std::cout<<"RUN_COORDINATOR_FATAL stage="<<static_cast<unsigned>(host->GetStage())<<" attempts=3 original_identity=1 complete_host_alive=1 unwind=0 native_exit=73\n"<<std::flush;std::_Exit(73);
}
int main(int argc,char** argv){try{
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    if(argc==3 && std::string_view(argv[1])=="fatal"){
        const auto stage=static_cast<unsigned>(argv[2][0]-'0');if(stage>4 || argv[2][1])return 2;
        Counts counts;auto host=std::make_unique<Host>(counts,static_cast<Stage>(stage),3);host->Run();return 76;
    }
    if(argc!=2 || std::string_view(argv[1])!="coordinated")return 2;
    unsigned cases=0;
    for(const auto stage:{Stage::None,Stage::Initialize,Stage::Update,Stage::EndQuery,Stage::Draw})for(unsigned failures:{0u,1u,2u}){
        Counts counts;auto host=std::make_unique<Host>(counts,stage,failures);std::exception_ptr operation,cleanup;bool paired=false;
        try{host->Run();}catch(const FrameworkRunFailure& error){operation=error.Operation();cleanup=error.Cleanup();paired=true;}
        catch(const OperationFault&){operation=std::current_exception();}catch(const CleanupFault&){cleanup=std::current_exception();}
        Require(OperationMatches(operation,host.get()) && bool(operation)==(stage!=Stage::None),"Coordinator lost original operation type/identity");
        Require((failures ? CleanupMatches(cleanup,host.get()) : !cleanup) && paired==(failures && stage!=Stage::None),"Coordinator replaced original first cleanup or pair");
        Require(counts.initialized==1 && counts.finalized==failures+1 && !counts.hostDestroyed && !counts.memberDestroyed && !host->Active(),"Coordinator did not complete cleanup while full host remained alive");
        host->RefuseReentry();Require(counts.refusals==counts.finalized+2,"Lifecycle admission count mismatch");
        host.reset();Require(counts.hostDestroyed==1 && counts.memberDestroyed==1,"Complete host destroyed before cleanup or not exact once");++cases;
    }
    std::cout<<"FRAMEWORK_RUN_COORDINATOR cases="<<cases<<" first_identity=1 cleanup_completed_before_throw=1 native_exit=0 no_window_gpu=1\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
