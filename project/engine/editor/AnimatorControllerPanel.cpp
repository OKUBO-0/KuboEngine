#include "AnimatorControllerPanel.h"
#include "DocumentShortcuts.h"
#include "ControllerEditorPreferencesFile.h"

namespace Engine::Editor {
bool AnimatorControllerPanel::PrepareProjectPreferences(const Base::ProjectContext& project,PreparedProjectPreferences& prepared,std::string& error) const {
    try{
        prepared.project=project;
        if(layoutProject_.GetProjectFile()==project.GetProjectFile() && preferencesReady_){
            prepared.snapshot=preferencesSnapshot_;prepared.enabled=layoutAuto_.Enabled();prepared.ready=true;prepared.status=preferencesStatus_;
        }else{
            prepared.ready=ControllerEditorPreferencesFile::Load(project,prepared.enabled,prepared.snapshot,prepared.status);
            // Optional malformed/unreadable settings must not block source
            // editing. Fail closed for automatic layout publication instead.
            if(!prepared.ready)prepared.enabled=false;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void AnimatorControllerPanel::PublishProjectPreferences(PreparedProjectPreferences& prepared) noexcept {
    static_assert(std::is_nothrow_move_assignable_v<Base::ProjectContext>);
    if(layoutProject_.GetProjectFile()!=prepared.project.GetProjectFile()){ResetLayoutTracking();ControllerCanvasLayoutSnapshot empty;layoutSnapshot_.Swap(empty);}
    layoutProject_=std::move(prepared.project);preferencesSnapshot_.Swap(prepared.snapshot);
    preferencesStatus_.swap(prepared.status);preferencesReady_=prepared.ready;layoutAuto_.SetEnabled(prepared.enabled);
}
bool AnimatorControllerPanel::ConfigureLayoutProject(const Base::ProjectContext& project,std::string& error) {
    PreparedProjectPreferences prepared;if(!PrepareProjectPreferences(project,prepared,error))return false;
    PublishProjectPreferences(prepared);error.clear();return true;
}
bool AnimatorControllerPanel::ReloadEditorPreferences(std::string& error) {
    bool enabled=layoutAuto_.Enabled();ControllerCanvasLayoutSnapshot snapshot;
    if(!ControllerEditorPreferencesFile::Load(layoutProject_,enabled,snapshot,error)){preferencesReady_=false;layoutAuto_.SetEnabled(false);return false;}
    preferencesSnapshot_.Swap(snapshot);preferencesReady_=true;layoutAuto_.SetEnabled(enabled);preferencesStatus_.clear();error.clear();return true;
}
bool AnimatorControllerPanel::SetAutoSaveLayout(bool enabled,std::string& error) {
    if(!preferencesReady_ || !layoutProject_.IsOpen()){error="Reload Controller editor preferences before changing auto-save";return false;}
    if(!ControllerEditorPreferencesFile::SaveChecked(layoutProject_,enabled,preferencesSnapshot_,error))return false;
    layoutAuto_.SetEnabled(enabled);preferencesStatus_.clear();error.clear();return true;
}
bool AnimatorControllerPanel::CaptureCanvasPositions() {
    return CaptureCanvasPositions(canvasState_);
}
bool AnimatorControllerPanel::CaptureCanvasPositions(AnimatorControllerCanvasState& target) {
    if(!context_)return false;bool changed=false;Current current(context_);
    for(const auto id:canvasNodes_){const auto p=ControllerNodes::GetNodePosition(id);const auto old=target.Position(id,{std::numeric_limits<float>::quiet_NaN(),0});if(target.Store(id,{p.x,p.y}))changed|=old!=AnimatorControllerCanvasState::Point{p.x,p.y};}
    return changed;
}
bool AnimatorControllerPanel::NativeGeometryMatches(const AnimatorControllerCanvasState& target) const {
    if(!context_)return true;Current current(context_);
    for(const auto id:canvasNodes_){const auto p=ControllerNodes::GetNodePosition(id);if(AnimatorControllerCanvasState::Pixel(target.Position(id,{}))!=AnimatorControllerCanvasState::Point{p.x,p.y})return false;}
    return true;
}
bool AnimatorControllerPanel::SyncGeometry(std::string& error) {
    try {
        auto* document=session_.Document();if(!document){error.clear();return true;}
        if(geometryOwner_==session_.GuiOwnerEpoch() && geometryRevision_==document->Revision()){error.clear();return true;}
        auto baseline=document->Geometry();
        // A graph-only command carries surviving geometry unchanged; keep a
        // later native-context preview. A geometry Undo/Redo must replace it.
        const bool restore=geometryOwner_!=session_.GuiOwnerEpoch() || !geometryBaseline_.SameGeometry(baseline,document->AuthoringIds());
        if(restore && !canvasState_.SameGeometry(baseline,document->AuthoringIds())){auto canvas=canvasState_;canvas.ApplyGeometry(baseline);canvasState_.Swap(canvas);RetireGui();}
        geometryBaseline_.Swap(baseline);geometryOwner_=session_.GuiOwnerEpoch();geometryRevision_=document->Revision();error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool AnimatorControllerPanel::RunCanvasCommand(CanvasCommand command,std::string& error) {
    try {
        auto* document=session_.Document();
        if(!document){if(command==CanvasCommand::Commit){error.clear();return true;}error="Controller document unavailable";return false;}
        const auto owner=session_.GuiOwnerEpoch(),revision=document->Revision();
        if(command==CanvasCommand::Commit && geometryOwner_==owner && geometryRevision_==revision && canvasState_.SameGeometry(document->Geometry(),document->AuthoringIds()) && NativeGeometryMatches(canvasState_)){error.clear();return true;}
        // All fallible capture/synchronization happens on private candidates.
        auto canvas=canvasState_;CaptureCanvasPositions(canvas);
        const bool restore=geometryOwner_!=owner || !geometryBaseline_.SameGeometry(document->Geometry(),document->AuthoringIds());
        if(restore && !canvas.SameGeometry(document->Geometry(),document->AuthoringIds()))canvas.ApplyGeometry(document->Geometry());
        const bool pending=!canvas.SameGeometry(document->Geometry(),document->AuthoringIds());
        if(command==CanvasCommand::Redo && pending){error="Uncommitted placement creates a new branch; finish or undo it before Redo";return false;}
        auto baseline=document->Geometry();
        const bool changeDocument=pending || command!=CanvasCommand::Commit;
        std::optional<AnimatorControllerDocument> candidate;
        if(changeDocument){
            candidate.emplace(*document);
            if(pending && !candidate->ApplyLayout(canvas,error))return false;
            if(command==CanvasCommand::Undo && !candidate->Undo(error))return false;
            if(command==CanvasCommand::Redo && !candidate->Redo(error))return false;
            canvas.ApplyGeometry(candidate->Geometry());baseline=candidate->Geometry();
        }
        const bool retire=command!=CanvasCommand::Commit || (context_ && (canvasDocument_!=document || canvasRevision_!=revision)) || !NativeGeometryMatches(canvas);
        if(document!=session_.Document() || owner!=session_.GuiOwnerEpoch() || revision!=document->Revision()){error="Controller changed while preparing canvas command";return false;}
        // Nothing below allocates. RetireGui has no settings sink/callbacks.
        if(candidate)document->SwapState(*candidate);
        canvasState_.Swap(canvas);geometryBaseline_.Swap(baseline);geometryOwner_=owner;geometryRevision_=document->Revision();
        if(retire)RetireGui();else if(context_)canvasRevision_=document->Revision();
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool AnimatorControllerPanel::CommitCanvasLayout(std::string& error) {return RunCanvasCommand(CanvasCommand::Commit,error);}
bool AnimatorControllerPanel::UndoEdit(std::string& error) {return RunCanvasCommand(CanvasCommand::Undo,error);}
bool AnimatorControllerPanel::RedoEdit(std::string& error) {return RunCanvasCommand(CanvasCommand::Redo,error);}
bool AnimatorControllerPanel::PrepareLayout(bool apply,std::string& error) {
    try {
        if(!layoutProject_.IsOpen() || !session_.Source() || !session_.Document()){error="Open or save a registered Controller before using layout preferences";return false;}
        auto guid=session_.Source()->guid;ControllerCanvasLayoutSnapshot receipt;AnimatorControllerCanvasState candidate;bool matched=false;
        if(!ControllerCanvasLayoutFile::Load(layoutProject_,guid,*session_.Document(),candidate,receipt,matched,error))return false;
        std::string message=matched?"Saved layout loaded for this exact Controller graph.":receipt.exists?"Saved layout does not match this graph. Automatic overwrite paused; Save graph layout explicitly to replace it.":"No saved layout. Current positions retained.";
        const bool allow=matched || !receipt.exists;
        if(apply && matched){auto& document=*session_.Document();const bool initial=!document.UndoCount() && !document.RedoCount();if(!(initial?document.InitializeLayout(candidate,error):document.ApplyLayout(candidate,error)))return false;}
        layoutGuid_.swap(guid);layoutSnapshot_.Swap(receipt);layoutReady_=true;
        layoutAuto_.Reset(allow);layoutBaselinePending_=apply;layoutObservedRevision_=session_.Document()->Revision();
        if(!apply)layoutAuto_.Observe(true,ImGui::GetCurrentContext()?ImGui::GetTime():0,false,false);
        if(apply && matched){canvasState_.Swap(candidate);RetireGui();}
        error.swap(message);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool AnimatorControllerPanel::RestoreSavedLayout(std::string& error) {
    if(NeedsResolution()){error="Save or resolve the Controller draft before loading layout preferences";return false;}
    if(!session_.Source() || !session_.Document() || !layoutProject_.IsOpen()){error="Open a registered Controller before loading its layout";return false;}
    if(!CommitCanvasLayout(error))return false;
    return PrepareLayout(true,error);
}
bool AnimatorControllerPanel::SaveLayout(std::string& error) {
    try {
    if(NeedsResolution()){error="Save or resolve the Controller draft before saving layout preferences";return false;}
    if(!session_.Source() || !session_.Document() || !layoutProject_.IsOpen()){error="Save a registered Controller before saving its layout";return false;}
    if(!CommitCanvasLayout(error))return false;
    if((!layoutReady_ || layoutGuid_!=session_.Source()->guid) && !PrepareLayout(false,error))return false;
    std::string message="Saved graph layout in project UserSettings (source and Scene unchanged).";
    if(!ControllerCanvasLayoutFile::SaveChecked(layoutProject_,session_.Source()->guid,*session_.Document(),canvasState_,layoutSnapshot_,error)){layoutAuto_.Failed();return false;}
    layoutAuto_.Saved();layoutObservedRevision_=session_.Document()->Revision();error.swap(message);return true;
    }catch(const std::exception& e){layoutAuto_.Failed();error=e.what();return false;}
}
bool AnimatorControllerPanel::FlushLayout(std::string& error) {
    try {
    if(NeedsResolution() || !session_.Source() || !layoutAuto_.Enabled() || !layoutProject_.IsOpen()){error.clear();return true;}
    if(!layoutAuto_.Blocked() && (!layoutReady_ || layoutGuid_!=session_.Source()->guid)){
        if(!PrepareLayout(false,error)){layoutAuto_.Failed();return false;}
    }
    const bool changed=CaptureCanvasPositions() || layoutObservedRevision_!=session_.Document()->Revision();layoutObservedRevision_=session_.Document()->Revision();
    layoutAuto_.Observe(changed,ImGui::GetCurrentContext()?ImGui::GetTime():0,false,false);
    if(!layoutAuto_.Allowed() || !layoutAuto_.Pending()){error.clear();return true;}
    if(layoutAuto_.Blocked()){error="Automatic layout save is paused after an error. Reload preferences, save explicitly, or disable Auto-save graph layout before closing.";return false;}
    return SaveLayout(error);
    }catch(const std::exception& e){layoutAuto_.Failed();error=e.what();return false;}
}
void AnimatorControllerPanel::TickLayoutAutosave(bool changed,bool editable) {
    if(!session_.Source() || !session_.Document() || !layoutProject_.IsOpen())return;
    if((!layoutReady_ || layoutGuid_!=session_.Source()->guid) && !layoutAuto_.Blocked()){
        if(!PrepareLayout(false,layoutStatus_)){layoutAuto_.Failed();return;}
    }
    const auto revision=session_.Document()->Revision();
    if(layoutBaselinePending_){layoutBaselinePending_=false;layoutObservedRevision_=revision;return;}
    changed|=layoutObservedRevision_!=revision;layoutObservedRevision_=revision;
    if(layoutAuto_.Observe(changed,ImGui::GetTime(),editable && !NeedsResolution(),ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left))){
        if(!SaveLayout(layoutStatus_))layoutAuto_.Failed();
    }
}
void AnimatorControllerPanel::Draw(bool editable,const std::function<bool(std::string&)>& save,const std::function<bool(const std::string&,std::string&)>& saveAs,const std::function<bool(std::string&)>& reload) {
        if(!open_)return;
        ImGui::SetNextWindowSize({800,650},ImGuiCond_FirstUseEver);
        bool shown=true;const bool visible=ImGui::Begin("Controller Graph",&shown);
        if(!shown){if(NeedsResolution())closeRequested_=true;else{std::string error;Close(false,error);}}
        if(visible && open_ && session_.Document()) {
            const auto shortcut=DocumentShortcuts::Take("Controller Graph",editable && !session_.HasPendingAcknowledgement());
            if(shortcut==DocumentShortcuts::Command::Undo)UndoEdit(status_);
            else if(shortcut==DocumentShortcuts::Command::Redo)RedoEdit(status_);
            auto& document=*session_.Document();
            CaptureCanvasPositions();std::string geometryError;if(!SyncGeometry(geometryError))status_.swap(geometryError);
            ImGui::TextUnformatted(session_.HasPendingAcknowledgement()?"Source saved; transaction acknowledgement pending":Dirty()?"Draft modified (not saved)":"Draft matches saved source baseline");
            ImGui::TextWrapped("Save publishes this asset; an untitled draft uses the new asset path below. Save As creates a new GUID and keeps the original. Runtime owners are not automatically reassigned or reloaded.");
            if(session_.Source())ImGui::TextWrapped("Source: %s",Base::PathToUtf8(session_.Source()->source).c_str());
            ImGui::BeginDisabled(!editable);
            {
            ControllerDetails::OwnerScope controlsOwner(&session_,session_.GuiOwnerEpoch());
            DrawSaveAsPath();
            ImGui::BeginDisabled(!save);
            if(ImGui::Button(session_.HasPendingAcknowledgement()?"Retry Save / acknowledgement":"Save Controller")){if(save(status_))status_="Controller saved. Runtime owner reload is explicit.";}
            ImGui::EndDisabled();ImGui::SameLine();
            ImGui::BeginDisabled(!saveAs || session_.HasPendingAcknowledgement());if(ImGui::Button("Save As new Controller")){if(saveAs(saveAsPath_,status_))status_="Created and registered Controller. Scene owners remain unchanged.";}ImGui::EndDisabled();
            ImGui::BeginDisabled(!reload || NeedsResolution() || !session_.Source());
            if(ImGui::Button("Reload selected owner")){if(reload(status_))status_="Selected owner reloaded: states, parameters and clocks reset; Pause intent retained.";}
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!preferencesReady_ || !layoutProject_.IsOpen());
            bool autoLayout=layoutAuto_.Enabled();if(ImGui::Checkbox("Auto-save graph layout",&autoLayout))SetAutoSaveLayout(autoLayout,preferencesStatus_);
            ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(!layoutProject_.IsOpen());
            if(ImGui::Button("Reload Controller preferences"))ReloadEditorPreferences(preferencesStatus_);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Auto-save preference is stored per project. Invalid settings disable automatic writes until reloaded.");
            if(!preferencesStatus_.empty())ImGui::TextWrapped("%s",preferencesStatus_.c_str());
            ImGui::TextWrapped("Layout auto-save waits for a quiet, saved Controller. Errors pause retries; Reload or Save explicitly to resume.");
            ImGui::BeginDisabled(NeedsResolution() || !session_.Source() || !layoutProject_.IsOpen());
            if(ImGui::Button("Save graph layout"))SaveLayout(layoutStatus_);
            ImGui::SameLine();if(ImGui::Button("Reload graph layout"))RestoreSavedLayout(layoutStatus_);
            ImGui::EndDisabled();
            if(!layoutStatus_.empty())ImGui::TextWrapped("%s",layoutStatus_.c_str());
            if(ImGui::Button("Undo")){UndoEdit(status_);}
            ImGui::SameLine();if(ImGui::Button("Redo")){RedoEdit(status_);}
            ImGui::SameLine();if(ImGui::Button("New draft"))New(status_);
            }
            ImGui::EndDisabled();
            // Lifecycle controls may replace the document above. Authoring
            // widgets must use the CURRENT owner epoch, not the old frame owner.
            ControllerDetails::OwnerScope authorOwner(&session_,session_.GuiOwnerEpoch());
            // Copies outlive a deferred mutation; never keep node views into a
            // document after a command replaces its graph/history.
            const auto graph=session_.Document()->Definition();
            const auto ids=session_.Document()->AuthoringIds();
            const auto viewRevision=session_.Document()->Revision();
            bool layoutChanged=false;
            canvasState_.Resolve(ids,layer_,state_);
            if(ImGui::BeginCombo("Layer",graph.layers[layer_].name.c_str())) {
                for(size_t i=0;i<graph.layers.size();++i){ImGui::PushID(static_cast<int>(i));if(ImGui::Selectable("##layer",i==layer_)){layoutChanged|=CaptureCanvasPositions();layer_=i;state_=0;layoutChanged|=canvasState_.Select(ids,layer_,state_);RetireGui();}TextRow(graph.layers[i].name);ImGui::PopID();}
                ImGui::EndCombo();
            }
            const auto& layer=graph.layers[layer_];state_=(std::min)(state_,layer.states.size()-1);
            const auto& layerIds=ids.layers[layer_];
            using Canvas=AnimatorControllerCanvasState;using Kind=Canvas::Kind;
            static_assert(sizeof(uintptr_t)>=sizeof(Canvas::Id),"Controller canvas requires 64-bit node IDs");
            const auto anyNode=Canvas::Key(Kind::AnyNode,layerIds.id),anyOutput=Canvas::Key(Kind::AnyOutput,layerIds.id);
            const auto node=[&](size_t i){return Canvas::Key(Kind::StateNode,layerIds.states[i]);};
            const auto input=[&](size_t i){return Canvas::Key(Kind::Input,layerIds.states[i]);};
            const auto output=[&](size_t i){return Canvas::Key(Kind::Output,layerIds.states[i]);};
            // Context rebuilds retire stale interaction gestures, but presentation
            // data is keyed by document-owned identities rather than compact indices.
            if(context_ && (canvasDocument_!=session_.Document() || canvasRevision_!=session_.Document()->Revision())){layoutChanged|=CaptureCanvasPositions();RetireGui();}
            if(!context_){std::vector<uint64_t> nodes;nodes.reserve(layer.states.size()+1);nodes.push_back(anyNode);for(size_t i=0;i<layer.states.size();++i)nodes.push_back(node(i));ControllerNodes::Config config;config.SettingsFile=nullptr;context_=ControllerNodes::CreateEditor(&config);if(!context_)throw std::runtime_error("Controller node context unavailable");canvasNodes_.swap(nodes);canvasDocument_=session_.Document();canvasRevision_=canvasDocument_->Revision();}
            std::optional<AnimationSystem::Controller::Transition> create;
            std::optional<size_t> removeEdge;
            {
                Current current(context_);ControllerNodes::Begin("Controller states",ImVec2(0,240));
                const bool restore=!positioned_;
                if(restore){const auto p=canvasState_.Position(anyNode,{0,0});ControllerNodes::SetNodePosition(anyNode,{p.x,p.y});for(size_t i=0;i<layer.states.size();++i){const auto q=canvasState_.Position(node(i),{180.0f+float(i%4)*210,float(i/4)*130});ControllerNodes::SetNodePosition(node(i),{q.x,q.y});}positioned_=true;}
                ControllerNodes::BeginNode(anyNode);ImGui::TextUnformatted("Any State");ControllerNodes::BeginPin(anyOutput,ControllerNodes::PinKind::Output);ImGui::TextUnformatted("out");ControllerNodes::EndPin();ControllerNodes::EndNode();
                for(size_t i=0;i<layer.states.size();++i){
                    ControllerNodes::BeginNode(node(i));ImGui::TextUnformatted(layer.states[i].name.c_str());if(i==layer.entry)ImGui::TextUnformatted("Entry");
                    ControllerNodes::BeginPin(input(i),ControllerNodes::PinKind::Input);ImGui::TextUnformatted("in");ControllerNodes::EndPin();
                    ImGui::SameLine();ControllerNodes::BeginPin(output(i),ControllerNodes::PinKind::Output);ImGui::TextUnformatted("out");ControllerNodes::EndPin();ControllerNodes::EndNode();
                }
                for(size_t i=0;i<layer.transitions.size();++i){const auto& edge=layer.transitions[i];ControllerNodes::Link(Canvas::Key(Kind::Link,layerIds.transitions[i]),edge.from?output(*edge.from):anyOutput,input(edge.to));}
                if(editable) {
                  if(ControllerNodes::BeginCreate()) {
                    ControllerNodes::PinId a,b;if(ControllerNodes::QueryNewLink(&a,&b)) {
                        auto from=a.Get(),to=b.Get();if(Canvas::Find(layerIds.states,from,Kind::Input))std::swap(from,to);
                        const auto source=Canvas::Find(layerIds.states,from,Kind::Output),target=Canvas::Find(layerIds.states,to,Kind::Input);
                        if((from==anyOutput || source) && target && (!source || *source!=*target)) {
                            if(ControllerNodes::AcceptNewItem()){AnimationSystem::Controller::Transition edge;edge.from=source;edge.to=*target;edge.blendSeconds=.2f;create=edge;}
                        }else ControllerNodes::RejectNewItem();
                    }
                  }
                  // BeginCreate marks the interaction active even when it
                  // returns false. EndCreate must balance every invocation.
                  ControllerNodes::EndCreate();
                }
                if(editable && ControllerNodes::BeginDelete()) {
                    ControllerNodes::LinkId link;if(ControllerNodes::QueryDeletedLink(&link)){const auto index=Canvas::Find(layerIds.transitions,link.Get(),Kind::Link);if(index && ControllerNodes::AcceptDeletedItem())removeEdge=*index;else ControllerNodes::RejectDeletedItem();}
                    ControllerNodes::NodeId node;while(ControllerNodes::QueryDeletedNode(&node))ControllerNodes::RejectDeletedItem();ControllerNodes::EndDelete();
                }
                if(restore)ControllerNodes::SelectNode(node(state_));
                ControllerNodes::NodeId selected[1];if(ControllerNodes::GetSelectedNodes(selected,1)>0){if(const auto index=Canvas::Find(layerIds.states,selected[0].Get(),Kind::StateNode))state_=*index;}
                layoutChanged|=canvasState_.Select(ids,layer_,state_);
                ControllerNodes::End();++frames_;
                layoutChanged|=CaptureCanvasPositions();
            }
            // The live drag remains a preview until release; one complete
            // multi-node gesture becomes one command on the shared timeline.
            if(editable && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()){std::string error;if(!CommitCanvasLayout(error))status_.swap(error);}
            AnimatorControllerEdits edit(*session_.Document());
            // Node mutations apply after End, and block other edits this frame.
            if(create)edit.AddTransition(layer_,*create,status_);
            else if(removeEdge)edit.RemoveTransition(layer_,*removeEdge,status_);
            ImGui::BeginDisabled(!editable || create.has_value() || removeEdge.has_value());
            {
            ControllerDetails::IdentityScope stateIdentity(layerIds.states[state_]);ControllerDetails::IdentityScope stateEpoch(viewRevision);
            auto next=layer.states[state_];char name[129]{},clip[257]{};std::memcpy(name,next.name.data(),next.name.size());std::memcpy(clip,next.clip.data(),next.clip.size());
            bool changed=false;
            changed|=ImGui::InputText("State name (Enter)",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue);next.name=name;
            changed|=ImGui::InputText("Clip (Enter)",clip,sizeof(clip),ImGuiInputTextFlags_EnterReturnsTrue);next.clip=clip;
            changed|=ImGui::InputFloat("State speed (Enter)",&next.speed,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
            changed|=ImGui::Checkbox("State loop",&next.loop);
            if(changed)edit.ReplaceState(layer_,state_,next,status_);
            if(ImGui::Button("Set Entry"))edit.SetEntry(layer_,state_,status_);
            ImGui::SameLine();if(ImGui::Button("Delete unused state"))edit.RemoveState(layer_,state_,{},status_);
            if(ImGui::Button("Add state")) {
                size_t number=0;std::string generated;
                do {generated="State "+std::to_string(number++);}while(std::any_of(layer.states.begin(),layer.states.end(),[&](const auto& value){return value.name==generated;}));
                edit.AddState(layer_,{generated,"default",1,true},status_);
            }
            }
            ImGui::EndDisabled();
            DrawControllerLayerDetails(graph,ids,layer_,edit,editable && session_.Document()->Revision()==viewRevision,viewRevision,status_,layerDetails_);
            ControllerDetails::Parameters(graph,ids,edit,editable && session_.Document()->Revision()==viewRevision,viewRevision,parameters_,status_);
            ControllerDetails::Transitions(graph,ids,layer_,edit,editable && session_.Document()->Revision()==viewRevision,viewRevision,transition_,status_);
            TickLayoutAutosave(layoutChanged,editable);
            if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
        }
        ImGui::End();
        if(closeRequested_) {
            ImGui::OpenPopup("Unsaved Controller draft");
            if(ImGui::BeginPopupModal("Unsaved Controller draft",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped(session_.HasPendingAcknowledgement()?"A published source still needs acknowledgement. Retry Save before closing; Discard cannot remove retained recovery records.":"Save this registered asset, discard draft edits, or cancel. Scene edits are managed separately.");
                if(!session_.Source())DrawSaveAsPath();
                ImGui::BeginDisabled(!editable || !save);
                if(ImGui::Button("Save and close")){if(save(status_) && Close(false,status_))ImGui::CloseCurrentPopup();}
                ImGui::EndDisabled();ImGui::SameLine();
                if(ImGui::Button("Discard draft")){if(Close(true,status_))ImGui::CloseCurrentPopup();}
                ImGui::SameLine();if(ImGui::Button("Cancel")){closeRequested_=false;ImGui::CloseCurrentPopup();}
                if(!status_.empty())ImGui::TextWrapped("%s",status_.c_str());
                ImGui::EndPopup();
            }
        }
    }
}
