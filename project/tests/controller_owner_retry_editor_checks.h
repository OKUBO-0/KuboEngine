class ControllerOwnerRetryEditorChecks final : public Engine::Scene::Game {
    using Access=Engine::Scene::GameApplicationChecksAccess;
    AnimatorHostFixture fixture_;std::filesystem::path source_;std::string bad_,world_;int frames_=0;bool done_=false,retired_=false;
    static std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Require(in.good(),"Retry fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
    static void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Require(!out.fail(),"Retry fixture write failed");}
public:
    void Initialize() override {
        fixture_.Prepare("ControllerOwnerRetryEditor");source_=fixture_.root/"Assets/scene-controller.animfsm";
        auto graph=Engine::Scene::SceneValue::parse(Read(source_));graph["layers"][0]["states"][0]["clip"]="missing-initial-clip";
        bad_=graph.dump();Write(source_,bad_);
        SetProjectRoot(fixture_.root);SetProjectFile(fixture_.root/"entry.kubo");SetProjectAssetRoot(fixture_.root/"Assets");SetEditorMode(true);
        std::string error;Require(ConfigureStartupScene(fixture_.root/"entry.scene",error),error.c_str());Game::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());
        world_=Access::World(*this);const auto guid=Access::Entities(*this).Find(fixture_.controllerEntity)->animator.controllerGuid;
        Require(OpenControllerAssetEditor(guid,error) && SelectEditorEntity(fixture_.controllerEntity,false,false,error),error.c_str());
        auto& document=*Access::ControllerPanel(*this).Session().Document();auto corrected=document.Definition();
        corrected.layers[0].states[0].clip="default";corrected.layers[0].states[0].name="Recovered initial owner";
        Require(document.Apply(corrected,error) && !ReloadSelectedController(error),"Unresolved retry draft bypassed guard");
    }
    void Update() override {
        Game::Update();++frames_;std::string error;
        auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Require(scene,"Retry Scene missing");
        if(frames_==1){
            const auto old=scene->GetModelRenderStatus(fixture_.controllerEntity);
            Require(!old.loaded && !scene->GetAnimatorRenderStatus(fixture_.controllerEntity).ready,"Bad initial Controller unexpectedly created owner");
            Require(!scene->ReloadController(fixture_.controllerEntity,error) && !scene->GetModelRenderStatus(fixture_.controllerEntity).loaded &&
                scene->GetModelRenderStatus(fixture_.controllerEntity).failedRevision==old.failedRevision &&
                Access::World(*this)==world_ && Read(source_)==bad_,"Failed explicit initial retry changed failure/World/Source");
        }
        if(frames_==2){
            Require(!scene->GetModelRenderStatus(fixture_.controllerEntity).loaded && Access::World(*this)==world_,"Uncorrected initial owner unexpectedly retried successfully");
            Require(SaveControllerAsset(error),error.c_str());auto& panel=Access::ControllerPanel(*this);const auto saved=Read(source_);
            Require(!scene->GetModelRenderStatus(fixture_.controllerEntity).loaded,"Source Save itself created runtime owner");
            Require(ReloadSelectedController(error) && scene->GetModelRenderStatus(fixture_.controllerEntity).loaded &&
                scene->GetAnimatorRenderStatus(fixture_.controllerEntity).ready && !scene->GetAnimatorRenderStatus(fixture_.controllerEntity).playing &&
                scene->GetControllerState(fixture_.controllerEntity)=="Recovered initial owner" && scene->GetAnimatorRenderStatus(fixture_.controllerEntity).time==0 &&
                Access::World(*this)==world_ && Read(source_)==saved && panel.Session().Document()->UndoCount()==1 && !IsSceneDirty(),
                "Saved initial owner retry/reset/authored Pause/World/Source/history isolation failed");
        }
        if(frames_==3){Require(scene->GetAnimatorRenderStatus(fixture_.controllerEntity).ready &&
            scene->GetControllerState(fixture_.controllerEntity)=="Recovered initial owner" && Access::World(*this)==world_,"Recovered owner did not survive next frame");done_=true;
            std::cout<<"PASS: actual Editor failed initial Controller owner/private failed retry/source correction+Save/explicit recreation/reset/authored Pause/source+World+history retention/next frame\n";}
    }
    bool IsEndRequest() const override{return done_ || frames_>7 || Game::IsEndRequest();}
    void Finalize() override{Game::Finalize();retired_=true;}
    void Verify(){Require(done_ && retired_,"Initial retry native scenario incomplete");fixture_.VerifyGpu();}
};
