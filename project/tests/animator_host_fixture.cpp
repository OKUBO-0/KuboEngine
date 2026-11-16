#include "animator_host_fixture.h"

void AnimatorHostFixture::Observe(const Engine::Scene::EntityWorld& world,float delta,const std::string& error) {
        auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
        const auto* record=world.Find(owner);const auto* failed=world.Find(fault);
        Check(scene && record && failed,"Animator host Scene/World missing");
        const int next=static_cast<int>(record->transform.rotation.y);
        const auto status=scene->GetAnimatorRenderStatus(owner);
        Check(status.ready && scene->GetModelRenderStatus(owner).loaded,"Animator host renderer not ready");
        const auto controllerStatus=scene->GetAnimatorRenderStatus(controllerEntity);
        const auto particleCount=scene->GetParticleRenderStatus().particles;
        if(stage==0)Check(particleCount==0,"A paused animation emitted an event burst");
        if(particleCount && !sawLandingBurst){
            Check(particleCount==4,"The animation event emitted the wrong number of particles");
            sawLandingBurst=true;
            std::cout<<"PASS: Controller landing marker emitted four particles on the same Entity\n";
        }
        Check(controllerStatus.ready && scene->GetModelRenderStatus(controllerEntity).loaded,"Scene Controller GUID renderer not ready");
        const auto* layerRecord=world.Find(controllerEntity);
        Check(layerRecord!=nullptr,"Layer reader Entity missing");
        const auto layerState=scene->ReadControllerLayer(*layerRecord,layerRecord->animator,1);
        Check(layerState.ready && layerState.state==(next<=1?"Idle":"Cutscene") && layerState.weight==(next<=1?0.75f:0.5f) && layerState.time>=0 && layerState.normalized>=0 && layerState.normalized<=1,"Actual layer query/force/weight consumption/failed-peer isolation failed");
        if(stage==0) {
            Engine::Scripting::ScriptAnimatorBatch missingLayer;
            Check(missingLayer.AppendLayerWeight(controllerEntity,3,0,layerRecord->animator),"Missing layer request fixture failed");
            Check(missingLayer.AppendForceState(controllerEntity,3,"Idle",0,layerRecord->animator) && missingLayer.AppendForceState(controllerEntity,1,"missing-state",0,layerRecord->animator),"Missing force request fixture failed");
            auto pending=std::make_unique<Engine::Scripting::ScriptAnimatorRequests::Pending>();pending->count=3;for(size_t i=0;i<3;++i)pending->values[i]=missingLayer.Values()[i];
            const auto usage=scene->ApplyAnimatorRequests(*pending);
            Check(usage.applied==0 && usage.rejected==3 && scene->ReadControllerLayer(*layerRecord,layerRecord->animator,1).weight==layerState.weight,"Actual owner accepted missing layer/state or changed healthy weight");
            std::cout<<"PASS: actual Editor/Player Lua layer state/time/progress/normalized/weight, queued setter/no prediction/failed-peer isolation/missing-layer consumption refusal\n";
            const auto beforeForce=world.Capture().dump();
            Check(scene->GetControllerStateNames(controllerEntity,0)==std::vector<std::string>{"Idle","Run"} && scene->GetControllerStateNames(controllerEntity,1)==std::vector<std::string>{"Idle","Cutscene"} && scene->GetControllerStateNames(owner,0).empty(),"Scene force-state catalog/owner guards failed");
            Check(scene->SetControllerLayerWeight(controllerEntity,1,0.25f) && scene->SetControllerLayerWeight(controllerEntity,1,layerState.weight) && !scene->SetControllerLayerWeight(controllerEntity,1,2),"Scene runtime weight guards failed");
            Check(scene->ForceControllerState(controllerEntity,1,"Idle",0) && !scene->ForceControllerState(controllerEntity,1,"missing",0) && !scene->ForceControllerState(controllerEntity,3,"Idle",0) && !scene->ForceControllerState(owner,0,"Idle",0),"Scene force-state actual owner admission failed");
            const auto afterForce=scene->ReadControllerLayer(*layerRecord,layerRecord->animator,1);
            Check(afterForce.ready && afterForce.time==0 && afterForce.progress==0 && afterForce.normalized==0 && afterForce.weight==layerState.weight && scene->GetControllerState(controllerEntity)=="Idle" && scene->GetAnimatorRenderStatus(controllerEntity).time==0 && world.Capture().dump()==beforeForce,"Scene force/weight changed authored World or peer base clock");
            std::cout<<"PASS: actual Editor/Player EntityScene force-state/catalog/runtime weight/paused instant reentry/base clock+authored preservation/owner and missing-layer refusal\n";
            Check(scene->GetControllerState(controllerEntity)=="Idle" && !controllerStatus.playing && controllerStatus.time==0,"Scene Controller ignored playOnAwake");
            Check(!scene->SetControllerBool(controllerEntity,"go",true) && !scene->SetControllerTrigger(owner,"go"),"Scene Controller typed admission failed");
            Check(scene->ControlAnimator(controllerEntity,Engine::AnimationSystem::AnimatorCommand::Play),"Scene Controller Play failed");
        }else if(stage==1) {
            Check(layerState.state=="Cutscene","Queued Lua force-state did not reach actual owner");
            std::cout<<"PASS: actual Editor/Player Lua force-state queued Cutscene admission/no prediction/failed-peer isolation/missing-state+layer rejection/base clock retention\n";
            Check(scene->GetControllerState(controllerEntity)=="Run" && controllerStatus.playing && controllerStatus.time>0,"Scene Controller trigger did not reach actual transition");
            Check(std::abs(controllerStatus.time-delta)<(std::max)(1e-5f,std::abs(delta)*1e-4f),"Scene speed did not multiply Controller state speed");
            controllerTime=controllerStatus.time;
            Check(scene->ControlAnimator(controllerEntity,Engine::AnimationSystem::AnimatorCommand::Pause),"Scene Controller Pause failed");
            std::cout<<"PASS: actual EntityScene authored Controller GUID/nonexistent legacy clip/typed trigger/state transition/state speed multiplier/Play+Pause admission\n";
            std::cout<<"PASS: actual Editor/Player scoped Lua Controller float/bool/trigger -> three-condition transition, failed-peer writes discarded, earlier healthy batch retained\n";
        }else {
            Check(scene->GetControllerState(controllerEntity)=="Run" && !controllerStatus.playing && controllerStatus.time==controllerTime,"Scene Controller Pause advanced clock or reset state");
        }
        Check(failed->transform.position.x==0,"Failed peer Animator hook published scalar");
        Check(error.find("animator-peer-discard")!=std::string::npos,"Failed Animator peer not dispatched/diagnosed");
        auto* loadedModel=Engine::Graphics3D::ModelManager::GetInstance()->GetOrLoadModel("walk.gltf",Engine::Base::PathToUtf8(root/"Assets/models"));
        const auto* clip=loadedModel->FindAnimationClip("default");
        Check(clip && clip->duration>0,"Animator host real clip missing");
        if(!blendChecked) {
            auto* common=Engine::Graphics3D::Object3DCommon::GetInstance();
            const auto resourceRoot=Engine::Base::PathToUtf8(root/"Assets/models");
            auto first=Engine::Graphics3D::Object3D::Create(common,"walk.gltf",resourceRoot);
            auto second=Engine::Graphics3D::Object3D::Create(common,"walk.gltf",resourceRoot);
            const float step=clip->duration/16;
            auto preview=first->CreateAnimationPreview();
            Check(preview && preview->SampleAnimationPreview("default",step*9) &&
                preview->GetAnimationTime()==step*9 && first->GetAnimationTime()==0,
                "Independent clip pose preview changed live animation time");
            std::vector<QuaternionTransform> previewPose;
            Check(Engine::AnimationSystem::SampleLocalPose(loadedModel->GetSkeleton(),*clip,step*9,previewPose),
                "Clip preview expected pose could not be sampled");
            for(size_t i=0;i<previewPose.size();++i){
                QuaternionTransform actual;
                Check(preview->GetAnimationJointTransform(i,actual) &&
                    std::abs(actual.translate.x-previewPose[i].translate.x)<1e-5f &&
                    std::abs(actual.rotate.w-previewPose[i].rotate.w)<1e-5f,"Preview pose differs from exact clip sample");
            }
            VerifyLayeredObject(common,loadedModel,resourceRoot,step);
            first->SetAnimationLoop(false);first->Update(step);
            Check(!first->BlendToAnimationClip("missing-owned-clip",step) && first->GetAnimationTime()==step,"Blend missing admission changed clock");
            Check(first->BlendToAnimationClip("default",2*step) && first->IsAnimationBlending(),"Blend admission failed");
            first->SetAnimationSpeed(0);first->Update(step);
            Check(first->GetAnimationTime()==0 && first->IsAnimationBlending(),"Paused blend advanced");
            first->SetAnimationSpeed(1);first->Update(step);
            std::vector<QuaternionTransform> expected;
            Check(Engine::AnimationSystem::SampleBlendedLocalPose(loadedModel->GetSkeleton(),*clip,2*step,*clip,step,0.5f,expected),"Expected blend sample failed");
            for(size_t i=0;i<expected.size();++i){QuaternionTransform actual;
                Check(first->GetAnimationJointTransform(i,actual),"Actual blended joint missing");
                Check(std::abs(actual.translate.x-expected[i].translate.x)<1e-5f && std::abs(actual.rotate.w-expected[i].rotate.w)<1e-5f,"Object3D did not publish sampled blend pose");
            }
            Check(second->GetAnimationTime()==0 && !second->IsAnimationBlending(),"Blend leaked into shared model peer");
            QuaternionTransform retainedPose;Check(first->GetAnimationJointTransform(0,retainedPose),"Snapshot joint missing");
            Check(first->BlendToAnimationClip("default",step),"Interrupted blend refused");first->Update(0);
            QuaternionTransform continuedPose;Check(first->GetAnimationJointTransform(0,continuedPose) && std::abs(continuedPose.translate.x-retainedPose.translate.x)<1e-5f && std::abs(continuedPose.rotate.w-retainedPose.rotate.w)<1e-5f,"Interrupted blend snapped local pose");
            first->Update(step);Check(!first->IsAnimationBlending() && std::abs(first->GetAnimationTime()-step)<1e-5f,"Interrupted blend did not complete at new target clock");
            Check(first->BlendToAnimationClip("default",step),"Second blend failed");first->ResetAnimationTime();Check(!first->IsAnimationBlending(),"Reset retained blend");
            Check(first->BlendToAnimationClip("default",step),"Reload blend setup failed");first->SetModel(loadedModel);Check(!first->IsAnimationBlending(),"Model assignment retained blend");
            auto controller=crude_json::value::parse(R"({"format":"kubo-animator-controller-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"jump","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":1,"loop":true},{"name":"Run","clip":"default","speed":2,"loop":true}],"transitions":[{"from":"Idle","to":"Run","blendSeconds":0.25,"exitTime":null,"reenter":false,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"jump","op":"set","value":true}]},{"from":"Run","to":"Idle","blendSeconds":0.25,"exitTime":0.75,"reenter":false,"conditions":[]}]}]})");
            controller["layers"][0]["transitions"][0]["blendSeconds"]=double(2*step);
            Check(first->BindAnimationController(controller.dump()) && second->BindAnimationController(controller.dump()),"Actual controller binding failed");
            Check(first->SetControllerFloat("speed",1) && first->SetControllerTrigger("jump") && !first->SetControllerBool("jump",true),"Actual typed controller writes failed");
            first->SetAnimationSpeed(0);first->Update(step);Check(first->GetControllerState()=="Idle","Paused controller consumed transition");
            first->SetAnimationSpeed(1);first->Update(step);
            Check(first->GetControllerState()=="Run" && second->GetControllerState()=="Idle" && first->IsAnimationBlending() && std::abs(first->GetAnimationTime()-2*step)<1e-5f,"Controller transition did not reach independent actual blend/clock");
            const auto blend=first->GetControllerBlendProgress(0);
            Check(blend && std::abs(*blend-.5f)<1e-5f && !second->GetControllerBlendProgress(0),
                "Controller monitor disagrees with active blend clock");
            first->Update(clip->duration);first->Update(step);
            Check(first->GetControllerState()=="Idle","Actual unwrapped loop clock missed exit crossing");
            first->Update(step);Check(first->GetControllerState()=="Idle","Actual controller trigger was not consumed");
            auto missingController=controller;missingController["layers"][0]["states"][1]["clip"]="missing_controller_clip";
            Check(!first->BindAnimationController(missingController.dump()) && first->GetControllerState()=="Idle","Missing controller clip replaced live binding");
            const auto assetController=controller;
            auto extraLayer=controller["layers"][0];extraLayer["name"]="Invalid model mask";extraLayer["mask"]=crude_json::array{"missing-model-layer-joint"};controller["layers"].push_back(extraLayer);
            Check(!first->BindAnimationController(controller.dump()) && first->GetControllerState()=="Idle","Invalid layer mask silently bound");
            first->SetModel(loadedModel);Check(first->GetControllerState().empty(),"Model change retained stale controller graph");
            Engine::Base::ProjectContext controllerProject;std::string controllerError;
            Check(controllerProject.Open(root/"entry.kubo",controllerError),controllerError.c_str());
            const auto controllerFile=root/"Assets/locomotion.animfsm";
            const auto writeController=[&](const std::string& bytes){std::ofstream out(controllerFile,std::ios::binary);out<<bytes;out.close();Check(!out.fail(),"Controller asset write failed");};
            writeController(assetController.dump());Engine::Assets::AssetRegistry controllerRegistry;
            Check(controllerRegistry.Scan(root/"Assets",controllerError),controllerError.c_str());
            const auto controllerGuid=controllerRegistry.FindByPath(controllerFile)->guid;
            Check(first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError),controllerError.c_str());
            const auto firstRevision=*first->GetControllerAssetRevision();
            Check(firstRevision.guid==controllerGuid && firstRevision.contentHash.size()==64 && first->GetControllerState()=="Idle","GUID assignment missing revision/state");
            Check(first->SetControllerFloat("speed",1) && first->SetControllerTrigger("jump"),"GUID parameter writes failed");first->Update(step);
            Check(first->GetControllerState()=="Run" && first->IsAnimationBlending() && first->GetControllerAssetRevision()->contentHash==firstRevision.contentHash,"GUID state transition lost revision");
            const auto retainedTime=first->GetAnimationTime();
            for(const auto* pendingKind:{"controller-transactions","controller-creations"}){
                const auto pending=root/"Assets/.kubo"/pendingKind/controllerGuid;std::filesystem::create_directories(pending);
                Check(!first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError,&firstRevision) && first->GetControllerState()=="Run" && first->GetAnimationTime()==retainedTime && first->IsAnimationBlending() && first->GetControllerAssetRevision()->contentHash==firstRevision.contentHash,"Pending GUID reload changed valid live owner/state/clock/blend/revision");
                std::filesystem::rename(pending,root/(std::string("retained-")+pendingKind));
            }
            std::cout<<"PASS: actual Object3D pending publication/creation GUID reload refusal retains valid state/clock/blend/revision\n";
            writeController(missingController.dump());
            Check(!first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError) && first->GetControllerState()=="Run" && first->GetAnimationTime()==retainedTime && first->GetControllerAssetRevision()->contentHash==firstRevision.contentHash,"Incompatible GUID rebind changed live playback");
            auto editedController=assetController;editedController["layers"][0]["states"][0]["speed"]=0.5;
            writeController(editedController.dump());
            Check(!first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError,&firstRevision) && first->GetControllerState()=="Run" && first->GetAnimationTime()==retainedTime,"Stale expected GUID revision changed live playback");
            Check(first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError) && first->GetControllerState()=="Idle" && first->GetControllerAssetRevision()->contentHash!=firstRevision.contentHash,"Explicit GUID reload failed");
            first->ResetAnimationTime();Check(first->GetControllerAssetRevision()!=nullptr,"Clock reset discarded GUID revision");
            Check(first->BindAnimationController(assetController.dump()) && !first->GetControllerAssetRevision(),"Text binding retained stale GUID revision");
            Check(first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError),controllerError.c_str());
            first->SetAnimationClip("default");Check(!first->GetControllerAssetRevision() && first->GetControllerState().empty(),"External clip retained GUID binding");
            Check(first->BindAnimationControllerAsset(controllerProject,controllerRegistry,controllerGuid,controllerError),controllerError.c_str());
            first->SetModel(loadedModel);Check(!first->GetControllerAssetRevision() && first->GetControllerState().empty(),"Model change retained GUID revision");
            std::cout<<"PASS: actual Object3D Controller GUID assignment/revision retention/real transition/incompatible+stale refusal/explicit reload/text+clip+model detach\n";
            blendChecked=true;std::cout<<"PASS: actual Object3D Controller typed parameters/trigger/independent states/real blend+target clock/Pause/unwrapped exit/missing+layer refusal/model reset\n";
            std::cout<<"PASS: actual Object3D two-clock blend pose/strict missing/snapshot interruption/pause/peer/reset/model change checks\n";
        }
        const auto names=loadedModel->GetAnimationClipNames();
        Check(!names.empty() && names.front()=="default" && scene->GetAnimationClipNames(owner)==names && scene->GetAnimationClipNames(fixed)==names,"Actual Model/Object3D/EntityScene clip catalog disagrees");
        for(const auto& name:names)Check(loadedModel->HasAnimationClip(name),"Catalog advertised a missing real animation clip");
        Check(scene->GetAnimationClipNames(fault).empty() && scene->GetAnimationClipNames(0).empty(),"Missing Model/Entity advertised clips");
        const auto* fixedRecord=world.Find(fixed);Check(fixedRecord,"Fixed Animator record missing");const auto fixedStatus=scene->GetAnimatorRenderStatus(fixed);
        const float ticks=fixedRecord->transform.position.x;
        if(ticks!=fixedTicks){Check(stage?fixedRecord->transform.rotation.z==1 && std::abs(fixedRecord->transform.rotation.y-fixedTime)<1e-5f:fixedRecord->transform.rotation.z==0 && fixedRecord->transform.rotation.y==-1,"Fixed Lua query did not sample the prior actual owner");}
        const float expectedFixed=(std::min)(ticks>0?((ticks!=fixedTicks?0:fixedTime)+delta*0.5f):0,clip->duration);
        Check(fixedStatus.ready && fixedStatus.playing==(ticks>0) && std::abs(fixedStatus.time-expectedFixed)<(std::max)(1e-5f,std::abs(expectedFixed)*1e-4f),"Fixed Animator requests did not reach independent host clock");fixedTicks=ticks;fixedTime=fixedStatus.time;
        if(next==stage){Check(status.time==time,"Paused/zero-step host advanced Animator");return;}
        Check(next==stage+1 && delta>0,"Animator host hook count changed unexpectedly");
        Check(stage?record->transform.position.z==1 && record->transform.position.y==(lastPlaying?1:0) && std::abs(record->transform.position.x-time)<1e-5f:record->transform.position.z==0 && record->transform.position.x==-1,"Lua state did not sample actual pre-command owner");
        if(next==8)Check(ticks>0 && SawLandingBurst(),"Animator host never dispatched fixed update or landing event");
        float expected=time;bool playing=false;
        switch(next){
        case 1:case 4:case 8:expected=delta*0.5f;playing=true;break;
        case 2:case 5:break;
        case 3:expected=0;break;
        case 6:case 7:expected=time+delta*0.5f;playing=true;break;
        default:break;
        }
        expected=(std::min)(expected,clip->duration);
        Check(status.playing==playing && std::abs(status.time-expected)<(std::max)(1e-5f,std::abs(expected)*1e-4f),"Lua command did not reach host Object3D clock or failed peer leaked command");
        Check(record->animator.clip=="default" && record->animator.speed==0.5f && !record->animator.loop && !record->animator.playOnAwake,"Runtime command changed authored Animator settings");
        stage=next;time=status.time;lastPlaying=status.playing;
        std::cout<<"ANIMATOR_HOST_STAGE="<<stage<<" PLAYING="<<status.playing<<" TIME="<<time<<" FIXED_TICKS="<<fixedTicks<<'\n';
    }
