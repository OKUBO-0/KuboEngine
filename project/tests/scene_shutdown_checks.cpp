#include "engine/scene/core/BaseScene.h"
#include "engine/scene/core/SceneManager.h"
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Counts {int initialized=0,finalized=0,destroyed=0;};
class FaultScene final:public Engine::Scene::BaseScene {
public:
 FaultScene(Counts& counts,bool initFault,bool finalizeFault,bool allocationFault=false):counts_(counts),initFault_(initFault),finalizeFault_(finalizeFault),allocationFault_(allocationFault){}
 ~FaultScene() override{++counts_.destroyed;}
 void Initialize() override{++counts_.initialized;if(initFault_)throw std::runtime_error("expected scene initialize fault");}
 void Finalize() override{++counts_.finalized;const bool fault=finalizeFault_;finalizeFault_=false;if(fault){if(allocationFault_)throw std::bad_alloc();throw std::runtime_error("expected scene finalize fault");}}
 void Update() override{}
 void Draw() override{}
private:
 Counts& counts_;bool initFault_,finalizeFault_,allocationFault_;
};
class ReenterScene final:public Engine::Scene::BaseScene {
public:
 ReenterScene(Counts& counts,Counts& candidate,bool& published,bool& nested,bool& refused,bool& factoryRefused,bool& changeRefused)
  :counts_(counts),candidate_(candidate),published_(published),nested_(nested),refused_(refused),factoryRefused_(factoryRefused),changeRefused_(changeRefused){}
 ~ReenterScene() override{++counts_.destroyed;}
 void Initialize() override{++counts_.initialized;}
 void Finalize() override{
  ++counts_.finalized;auto* manager=Engine::Scene::SceneManager::GetInstance();
  published_=manager->GetCurrentScene()==this;
  // Avoid the old implementation recursively destroying its executing callback.
  // Reentry is exercised only after the manager has unregistered this Scene.
  if(published_)return;
  manager->Finalize();nested_=true;
  manager->Update(42.0f);manager->Draw();
  try{manager->SetNextScene(std::make_unique<FaultScene>(candidate_,false,false));}
  catch(const std::logic_error&){refused_=true;}
  try{manager->SetSceneFactory(nullptr);}catch(const std::logic_error&){factoryRefused_=true;}
  try{manager->ChangeScene("shutdown fixture");}catch(const std::logic_error&){changeRefused_=true;}
 }
 void Update() override{}
 void Draw() override{}
private:
 Counts& counts_;Counts& candidate_;bool& published_;bool& nested_;bool& refused_;bool& factoryRefused_;bool& changeRefused_;
};
// Own fixture cleanup precedes borrowed stack counters even on an assertion.
struct ResetManager {
 ~ResetManager(){try{Engine::Scene::SceneManager::GetInstance()->Finalize();}catch(...){try{Engine::Scene::SceneManager::GetInstance()->Finalize();}catch(...){}}}
};
}
int main(int argc,char** argv){try {
 using namespace Engine::Scene;auto* manager=SceneManager::GetInstance();manager->Finalize();
 bool allRetired=true,allOnce=true;
 const bool reentryOnly=argc>1 && std::string(argv[1])=="--reentry-only";
 const bool allocationOnly=argc>1 && std::string(argv[1])=="--allocation-only";
 if(!reentryOnly && !allocationOnly)for(bool failedInitialize:{false,true}){
  Counts active,pending;ResetManager cleanup;
  manager->SetNextScene(std::make_unique<FaultScene>(active,failedInitialize,true));
  std::string initError;
  try{manager->Update(0.125f);}catch(const std::runtime_error& error){initError=error.what();}
  Require(active.initialized==1 && (failedInitialize?initError=="expected scene initialize fault":initError.empty()),"Scene initialization fixture did not execute requested case");
  manager->SetNextScene(std::make_unique<FaultScene>(pending,false,false));
  std::string shutdownError;
  try{manager->Finalize();}catch(const std::runtime_error& error){shutdownError=error.what();}
  const bool retired=manager->GetCurrentScene()==nullptr && active.finalized==1 && active.destroyed==1
   && pending.initialized==0 && pending.finalized==0 && pending.destroyed==1
   && manager->GetFrameDeltaTime()==1.0f/60.0f;
  std::cout<<"SCENE_SHUTDOWN init_failed="<<failedInitialize<<" current_retained="<<(manager->GetCurrentScene()!=nullptr)
   <<" active_destroyed="<<active.destroyed<<" pending_destroyed="<<pending.destroyed<<" retired="<<retired<<'\n';
  // Repair this fixture on the old implementation before reporting its failure.
  manager->Finalize();
  Require(shutdownError=="expected scene finalize fault","Cleanup replaced or lost original Finalize diagnostic");
  allRetired=allRetired && retired;
  allOnce=allOnce && active.finalized==1 && active.destroyed==1 && pending.destroyed==1;
 }
 Require(allRetired,"Finalize exception retained current/pending Scene or stale clock before owner destruction");
 Require(allOnce,"Repeated Finalize retried retired Scene callbacks");
 if(!allocationOnly){
  Counts active,candidate;ResetManager cleanup;bool published=false,nested=false,refused=false,factoryRefused=false,changeRefused=false;
  manager->SetNextScene(std::make_unique<ReenterScene>(active,candidate,published,nested,refused,factoryRefused,changeRefused));manager->Update();
  manager->Finalize();
  const bool retired=manager->GetCurrentScene()==nullptr && active.finalized==1 && active.destroyed==1
   && candidate.initialized==0 && candidate.finalized==0 && candidate.destroyed==1 && manager->GetFrameDeltaTime()==1.0f/60.0f;
  std::cout<<"SCENE_SHUTDOWN_REENTRY published="<<published<<" nested="<<nested<<" admission_refused="<<refused
   <<" retired="<<retired<<'\n';
  manager->Finalize();
  Require(!published && nested && refused && factoryRefused && changeRefused && retired,"Shutdown callback still published its Scene or admitted a new owner during reentry");
  // The teardown guard must reset for the next host, including after exceptions.
  manager->SetSceneFactory(nullptr);manager->SetNextScene(std::make_unique<FaultScene>(candidate,false,false));manager->Update();manager->Finalize();
  Require(candidate.initialized==1 && candidate.finalized==1 && candidate.destroyed==2,"Shutdown guard leaked into a later host");
 }
 if(!reentryOnly){
  Counts active,pending;ResetManager cleanup;
  manager->SetNextScene(std::make_unique<FaultScene>(active,false,true,true));manager->Update();
  manager->SetNextScene(std::make_unique<FaultScene>(pending,false,false));bool originalType=false;
  try{manager->Finalize();}catch(const std::bad_alloc&){originalType=true;}
  const bool retired=originalType && manager->GetCurrentScene()==nullptr && active.destroyed==1 && pending.destroyed==1;
  std::cout<<"SCENE_SHUTDOWN_BAD_ALLOC original_type="<<originalType<<" retired="<<retired<<'\n';
  manager->Finalize();
  Require(retired && active.finalized==1,"Callback bad_alloc changed type or retained/retried Scene ownership");
 }
 std::cout<<"PASS: Scene finalizer exception retires current/pending owners, preserves diagnostic and resets clock, including failed Initialize and repeated Finalize (CPU fixture, not GPU/Framework shutdown)\n";
 return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
