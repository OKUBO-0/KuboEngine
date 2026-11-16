#pragma once
#include "AnimatorControllerSession.h"
#include "AnimatorControllerLayerDetails.h"
#include "AnimatorControllerDetails.h"
#include "AnimatorControllerCanvasState.h"
#include "ControllerCanvasLayoutFile.h"
#include "ControllerLayoutAutosave.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <cstring>

namespace Engine::Editor {
namespace ControllerNodes=ax::NodeEditor;
class AnimatorControllerPanel final {
#if defined(KUBO_APPLICATION_CHECKS)
    friend class ControllerCanvasChecksAccess;
#endif
    AnimatorControllerSession session_;
    ControllerNodes::EditorContext* context_=nullptr;
    std::vector<uint64_t> canvasNodes_;
    bool open_=false,closeRequested_=false;size_t layer_=0,state_=0;uint64_t frames_=0;
    std::string status_,saveAsPath_="NewController.animfsm";ControllerLayerDetailsView layerDetails_;bool positioned_=false;
    const AnimatorControllerDocument* canvasDocument_=nullptr;uint64_t canvasRevision_=0;
    AnimatorControllerCanvasState canvasState_;
    Base::ProjectContext layoutProject_;
    ControllerCanvasLayoutSnapshot layoutSnapshot_;
    ControllerCanvasLayoutSnapshot preferencesSnapshot_;
    bool preferencesReady_=false;
    std::string preferencesStatus_;
    struct PreparedProjectPreferences {
        Base::ProjectContext project;ControllerCanvasLayoutSnapshot snapshot;
        bool enabled=true,ready=false;std::string status;
    };
    bool PrepareProjectPreferences(const Base::ProjectContext& project,PreparedProjectPreferences& prepared,std::string& error) const;
    void PublishProjectPreferences(PreparedProjectPreferences& prepared) noexcept;
    std::string layoutGuid_,layoutStatus_;
    bool layoutReady_=false;
    ControllerLayoutAutosave layoutAuto_;
    bool layoutBaselinePending_=true;
    uint64_t layoutObservedRevision_=0;
    AnimatorControllerCanvasState geometryBaseline_;uint64_t geometryOwner_=0,geometryRevision_=0;
    void ResetLayoutTracking() noexcept {layoutReady_=false;layoutGuid_.clear();layoutStatus_.clear();layoutAuto_.Reset();layoutBaselinePending_=true;layoutObservedRevision_=0;geometryBaseline_.Reset();geometryOwner_=geometryRevision_=0;}
    bool PrepareLayout(bool apply,std::string& error);
    void TickLayoutAutosave(bool changed,bool editable);
    bool CaptureCanvasPositions();
    bool CaptureCanvasPositions(AnimatorControllerCanvasState& target);
    bool NativeGeometryMatches(const AnimatorControllerCanvasState& target) const;
    enum class CanvasCommand { Commit, Undo, Redo };
    bool RunCanvasCommand(CanvasCommand command,std::string& error);
    bool SyncGeometry(std::string& error);
    ControllerDetails::ParameterView parameters_;ControllerDetails::TransitionView transition_;
    struct Current {
        ControllerNodes::EditorContext* previous;
        explicit Current(ControllerNodes::EditorContext* next):previous(ControllerNodes::GetCurrentEditor()){ControllerNodes::SetCurrentEditor(next);}
        ~Current(){ControllerNodes::SetCurrentEditor(previous);}
    };
    static void TextRow(const std::string& text){const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const ImVec4 clip(a.x,a.y,b.x,b.y);ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),a,ImGui::GetColorU32(ImGuiCol_Text),text.data(),text.data()+text.size(),0,&clip);}
public:
    ~AnimatorControllerPanel(){RetireGui();}
    AnimatorControllerSession& Session() noexcept{return session_;}
    const AnimatorControllerSession& Session() const noexcept{return session_;}
    bool Dirty() const noexcept{return session_.Document() && session_.Document()->IsDirty();}
    bool NeedsResolution() const noexcept{return Dirty() || session_.HasPendingAcknowledgement();}
    uint64_t CanvasFrames() const noexcept{return frames_;}
    bool HasContext() const noexcept{return context_!=nullptr;}
    const std::string& SaveAsPath() const noexcept{return saveAsPath_;}
    bool ConfigureLayoutProject(const Base::ProjectContext& project,std::string& error);
    bool ReloadEditorPreferences(std::string& error);
    bool SetAutoSaveLayout(bool enabled,std::string& error);
    bool AutoSaveLayoutEnabled() const noexcept{return layoutAuto_.Enabled();}
    bool EditorPreferencesReady() const noexcept{return preferencesReady_;}
    bool SaveLayout(std::string& error);
    bool RestoreSavedLayout(std::string& error);
    bool FlushLayout(std::string& error);
    bool CommitCanvasLayout(std::string& error);
    bool UndoEdit(std::string& error);
    bool RedoEdit(std::string& error);
    void DrawSaveAsPath(){ControllerDetails::OwnerScope owner(&session_,session_.GuiOwnerEpoch());char path[4097]{};std::memcpy(path,saveAsPath_.data(),saveAsPath_.size());if(ImGui::InputText("New asset path (.animfsm)",path,sizeof(path)))saveAsPath_=path;ImGui::TextWrapped("Relative to the Assets root. Existing folders only; existing files are never overwritten.");}
    bool New(std::string& error){if(!NeedsResolution() && !FlushLayout(error))return false;if(!session_.New(error))return false;RetireGui();canvasState_.Reset();ResetLayoutTracking();layer_=state_=0;transition_={};parameters_={};layerDetails_={};open_=true;closeRequested_=false;status_.clear();return true;}
    void ShowPreparedDraft() noexcept {RetireGui();canvasState_.Reset();ResetLayoutTracking();layer_=state_=0;transition_={};parameters_={};layerDetails_={};open_=true;closeRequested_=false;status_.clear();}
    bool Open(const Base::ProjectContext& project,const Assets::AssetRegistry& assets,const std::string& guid,std::string& error) {
        PreparedProjectPreferences prepared;if(!PrepareProjectPreferences(project,prepared,error))return false;
        if(!NeedsResolution() && !FlushLayout(error))return false;
        if(!session_.Open(project,assets,guid,error))return false;
        PublishProjectPreferences(prepared);ResetLayoutTracking();
        RetireGui();canvasState_.Reset();layer_=state_=0;transition_={};parameters_={};layerDetails_={};open_=true;closeRequested_=false;status_.clear();RestoreSavedLayout(layoutStatus_);return true;
    }
    bool Close(bool discard,std::string& error){if(!discard && !NeedsResolution() && !FlushLayout(error)){layoutStatus_=error;return false;}if(!session_.Close(error,discard))return false;RetireGui();canvasState_.Reset();ResetLayoutTracking();open_=closeRequested_=false;return true;}
    void RetireGui() noexcept {if(context_){auto* owned=context_;context_=nullptr;if(ControllerNodes::GetCurrentEditor()==owned)ControllerNodes::SetCurrentEditor(nullptr);ControllerNodes::DestroyEditor(owned);}canvasNodes_.clear();positioned_=false;canvasDocument_=nullptr;canvasRevision_=0;}
void Draw(bool editable,const std::function<bool(std::string&)>& save={},const std::function<bool(const std::string&,std::string&)>& saveAs={},const std::function<bool(std::string&)>& reload={});
};
}
