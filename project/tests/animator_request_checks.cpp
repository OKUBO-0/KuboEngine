#include "engine/script/ScriptAnimatorRequests.h"
#include <cstdlib>
#include <new>
#include <iostream>
namespace {bool fail=false;void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}}
void* operator new(size_t n){if(fail){fail=false;throw std::bad_alloc();}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{using namespace Engine::Scripting;using Engine::AnimationSystem::AnimatorCommand;Engine::AnimationSystem::AnimatorComponent settings;settings.clip=std::string(256,'a');ScriptAnimatorBatch batch;ScriptAnimatorRequests requests;ScriptAudioRequests audio;
 Require(batch.Append(1,AnimatorCommand::Play,settings) && batch.Values()[0].Matches(settings),"Snapshot preparation failed");auto changed=settings;changed.speed=2;Require(!batch.Values()[0].Matches(changed),"Changed settings matched request");
 std::vector<ScriptAudioRequest> sounds(1);sounds[0].path="owned.wav";audio.SetEnabled(true);
 Require(!requests.PublishTogether(batch,&audio,sounds) && audio.Take().empty(),"Disabled Animator published Audio");requests.SetEnabled(true);
 fail=true;bool refused=false;try{requests.PublishTogether(batch,&audio,sounds);}catch(const std::bad_alloc&){refused=true;}fail=false;Require(refused && requests.Take().count==0 && audio.Take().empty(),"Audio allocation failure partially published paired requests");
 audio.SetEnabled(false);Require(!requests.PublishTogether(batch,&audio,sounds) && requests.Take().count==0,"Disabled Audio published Animator");audio.SetEnabled(true);
 Require(requests.PublishTogether(batch,&audio,sounds) && requests.Take().count==1 && audio.Take().size()==1,"Paired publication failed");
 ScriptAnimatorBatch full;for(int i=0;i<64;++i)Require(full.Append(1,static_cast<AnimatorCommand>(i%4),settings),"Batch cap early");Require(!full.Append(1,AnimatorCommand::Play,settings),"Batch cap bypassed");
 for(int i=0;i<4;++i)Require(requests.PublishTogether(full,nullptr,{}),"Pending cap early");Require(!requests.PublishTogether(batch,&audio,sounds) && audio.Take().empty(),"Full Animator queue published Audio");const auto pending=requests.Take();Require(pending.count==256 && pending.values[255].command==AnimatorCommand::Restart,"FIFO/cap failed");
 Require(!batch.Append(0,AnimatorCommand::Play,settings) && !batch.Append(1,static_cast<AnimatorCommand>(99),settings),"Invalid request admitted");
 Require(requests.PublishTogether(batch,nullptr,{}),"Before disable publication failed");requests.SetEnabled(false);Require(requests.Take().count==0,"Disable retained requests");
 auto malformed=pending.values[0];malformed.clipBytes=65535;Require(!malformed.Matches(settings),"Malformed public pending length accepted");
 auto controller=settings;controller.controllerGuid=std::string(32,'a');ScriptAnimatorBatch controllerBatch;
 Require(controllerBatch.Append(1,AnimatorCommand::Play,controller) && controllerBatch.Values()[0].Matches(controller),"Controller request lost GUID");
 Require(!batch.Values()[0].Matches(controller) && !controllerBatch.Values()[0].Matches(settings),"Legacy/Controller request identities collided");
 controller.controllerGuid=std::string(16,'b');Require(!controllerBatch.Values()[0].Matches(controller),"Stale Controller request matched new GUID");
 auto badController=controllerBatch.Values()[0];badController.controllerGuidBytes=255;Require(!badController.Matches(settings),"Oversized Controller request read accepted");
 controller.controllerGuid=std::string(32,'a');ScriptAnimatorBatch writes;
 Require(writes.AppendController(1,ScriptAnimatorRequestKind::Float,"speed",2,false,controller) && writes.AppendController(1,ScriptAnimatorRequestKind::Bool,"grounded",0,true,controller) && writes.AppendController(1,ScriptAnimatorRequestKind::Trigger,"jump",0,false,controller),"Controller typed snapshots refused");
 Require(writes.Values().size()==3 && writes.Values()[0].ValidControllerPayload() && writes.Values()[1].flag && writes.Values()[2].kind==ScriptAnimatorRequestKind::Trigger,"Controller typed FIFO/payload lost");
 Require(!writes.AppendController(1,ScriptAnimatorRequestKind::Float,"speed",(std::numeric_limits<double>::infinity)(),false,controller) && !writes.AppendController(1,ScriptAnimatorRequestKind::Trigger,std::string(129,'a'),0,false,controller) && !writes.AppendController(1,ScriptAnimatorRequestKind::Trigger,"jump",0,false,settings) && writes.Values().size()==3,"Invalid Controller write appended");
 auto malformedWrite=writes.Values()[0];malformedWrite.parameterBytes=65535;Require(!malformedWrite.ValidControllerPayload(),"Malformed Controller payload length accepted");
 std::cout<<"PASS: bounded fixed Animator snapshots/FIFO/caps and paired Audio admission, no partial publication on disabled/full/allocation failure, stale settings/invalid refusal and disable drain\n";return 0;
}catch(const std::exception& error){fail=false;std::cerr<<error.what()<<'\n';return 1;}}
