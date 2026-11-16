#include "Framework.h"
#include "framework_run_failure_candidate.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
enum class Stage {None,Initialize,Update,EndQuery,Draw};
struct OperationFault {const void* token;};
struct CleanupFault {const void* token;};
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Counts {unsigned finalized=0,hostDestroyed=0,memberDestroyed=0;};
struct OwnedMember {Counts& counts;~OwnedMember(){++counts.memberDestroyed;}};
class Host final : public Engine::Base::Framework {
public:
    Host(Counts& counts,Stage fail,unsigned cleanupFailures)
        :counts_(counts),member_{counts},stage_(fail),cleanupFailures_(cleanupFailures){}
    ~Host() override {++counts_.hostDestroyed;}
    void Initialize() override {Fail(Stage::Initialize);}
    void Update() override {++updates_;Fail(Stage::Update);}
    bool IsEndRequest() const override {Fail(Stage::EndQuery);return updates_>1;}
    void Draw() override {Fail(Stage::Draw);}
    void Finalize() override {
        ++counts_.finalized;
        if(cleanupFailures_){--cleanupFailures_;throw CleanupFault{this};}
    }
private:
    void Fail(Stage stage) const {if(stage_==stage)throw OperationFault{this};}
    Counts& counts_;OwnedMember member_;Stage stage_;unsigned cleanupFailures_,updates_=0;
};
bool IsOperation(const std::exception_ptr& failure,const void* token){
    if(!failure)return false;
    try{std::rethrow_exception(failure);}catch(const OperationFault& error){return error.token==token;}catch(...){return false;}
}
bool IsCleanup(const std::exception_ptr& failure,const void* token){
    if(!failure)return false;
    try{std::rethrow_exception(failure);}catch(const CleanupFault& error){return error.token==token;}catch(...){return false;}
}
}
int main(int argc,char** argv){try{
    if(argc!=2)return 2;
    const bool candidate=std::string_view(argv[1])=="candidate";
    if(!candidate && std::string_view(argv[1])!="before")return 2;
    unsigned cases=0;
    for(const auto stage:{Stage::None,Stage::Initialize,Stage::Update,Stage::EndQuery,Stage::Draw}) {
        for(unsigned failures:{0u,1u,2u}) {
            Counts counts;auto host=std::make_unique<Host>(counts,stage,failures);
            const auto* token=host.get();
            std::exception_ptr operation,cleanup;bool paired=false;
            try{host->Run();}
            catch(const FrameworkRunFailure& failure){operation=failure.Operation();cleanup=failure.Cleanup();paired=true;}
            catch(const OperationFault&){operation=std::current_exception();}
            catch(const CleanupFault&){cleanup=std::current_exception();}
            const bool operationExpected=stage!=Stage::None;
            const bool cleanupAttempted=candidate || !operationExpected;
            Require(counts.finalized==(cleanupAttempted ? 1u : 0u),"Wrong actual Run finalizer count");
            Require(IsOperation(operation,token)==operationExpected,"Original operation identity/type lost");
            const bool cleanupExpected=cleanupAttempted && failures;
            Require(IsCleanup(cleanup,token)==cleanupExpected,"Original cleanup identity/type lost");
            Require(paired==(candidate && operationExpected && cleanupExpected),"Paired diagnostic protocol mismatch");
            Require(!counts.hostDestroyed && !counts.memberDestroyed,"Caller lost complete host before explicit retry");
            unsigned retryFailures=0;
            // The private caller retains ownership across the propagated failure.
            // This is not the shipped Application's local unique_ptr unwind policy.
            for(unsigned retry=0;retry!=3;++retry){try{host->Finalize();break;}catch(const CleanupFault& error){Require(error.token==token,"Retry replaced cleanup identity");++retryFailures;}}
            Require(retryFailures==failures-(cleanupExpected ? 1u : 0u),"Pending cleanup attempts not retained");
            host.reset();Require(counts.hostDestroyed==1 && counts.memberDestroyed==1,"Retained host did not retire exactly once");
            ++cases;
        }
    }
    Require(cases==15,"Run candidate case coverage mismatch");
    std::cout<<"FRAMEWORK_RUN_CANDIDATE variant="<<(candidate ? "candidate" : "before")<<" cases="<<cases
        <<" original_identity=1 caller_retention=1 native_exit=0 no_window_gpu=1\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
