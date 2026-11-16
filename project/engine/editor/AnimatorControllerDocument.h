#pragma once
#include "AnimatorControllerSerialization.h"
#include "AnimatorControllerAuthoringIds.h"
#include "AnimatorControllerCanvasState.h"
#include <cstdint>

namespace Engine::Editor {
// Main-thread private authoring document; no file I/O, runtime state or GUI
// ownership. Callers must route dirty decisions before replacing this document.
class AnimatorControllerDocument final {
    friend class AnimatorControllerEdits;
    friend class AnimatorControllerPanel;
    using Graph=AnimationSystem::Controller::Graph;
    using Ids=AnimatorControllerAuthoringIds;
    using Layout=AnimatorControllerCanvasState;
    struct Edit {std::string before,after;Ids beforeIds,afterIds;Layout beforeLayout,afterLayout;size_t Bytes() const noexcept{return before.size()+after.size()+beforeIds.Bytes()+afterIds.Bytes()+beforeLayout.Bytes()+afterLayout.Bytes();}};
    Graph graph_;std::string text_,saved_;std::vector<Edit> undo_,redo_;
    Ids ids_;uint64_t nextId_=1;
    Layout layout_;
    size_t maximumCommands_,maximumBytes_;uint64_t revision_=0;bool loaded_=false,untitled_=false;
    void SwapState(AnimatorControllerDocument& other) noexcept {
        graph_.Swap(other.graph_);text_.swap(other.text_);saved_.swap(other.saved_);undo_.swap(other.undo_);redo_.swap(other.redo_);
        ids_.Swap(other.ids_);layout_.Swap(other.layout_);std::swap(nextId_,other.nextId_);std::swap(maximumCommands_,other.maximumCommands_);std::swap(maximumBytes_,other.maximumBytes_);
        std::swap(revision_,other.revision_);std::swap(loaded_,other.loaded_);std::swap(untitled_,other.untitled_);
    }
    void CheckRevision() const {if(revision_==(std::numeric_limits<uint64_t>::max)())throw std::overflow_error("Controller document revision exhausted");}
    static size_t Bytes(const std::vector<Edit>& entries) noexcept {size_t result=0;for(const auto& edit:entries)result+=edit.Bytes();return result;}
    bool MoveHistory(bool redo,std::string& error) {
        try {
            if(!loaded_ || (redo?redo_:undo_).empty()){error="Controller history unavailable";return false;}
            CheckRevision();auto nextUndo=undo_,nextRedo=redo_;
            auto& from=redo?nextRedo:nextUndo;auto& to=redo?nextUndo:nextRedo;
            auto text=redo?from.back().after:from.back().before;Graph graph;
            auto ids=redo?from.back().afterIds:from.back().beforeIds;
            auto layout=redo?from.back().afterLayout:from.back().beforeLayout;
            if(!AnimationSystem::Controller::TextCodec::Decode(text,graph,error))return false;
            ids.Validate(graph,nextId_);
            to.push_back(std::move(from.back()));from.pop_back();
            graph_.Swap(graph);text_.swap(text);ids_.Swap(ids);layout_.Swap(layout);undo_.swap(nextUndo);redo_.swap(nextRedo);++revision_;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool ApplyIdentified(const Graph& candidate,Ids ids,uint64_t next,std::string& error) {
        try {
            if(!loaded_ || next<nextId_){error="Controller identity edit unavailable/stale";return false;}
            std::string text;if(!AnimatorControllerSerialization::Encode(candidate,text,error))return false;
            if(text==text_ && ids==ids_){error.clear();return true;}
            CheckRevision();Graph graph;if(!AnimationSystem::Controller::TextCodec::Decode(text,graph,error))return false;
            ids.Validate(graph,next);
            auto layout=layout_.ForGraph(ids);
            return Publish(std::move(graph),std::move(text),std::move(ids),next,std::move(layout),error);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Publish(Graph graph,std::string text,Ids ids,uint64_t next,Layout layout,std::string& error) {
        try {
            CheckRevision();
            Edit edit{text_,text,ids_,ids,layout_,layout};if(edit.Bytes()>maximumBytes_){error="Controller edit exceeds history byte budget";return false;}
            auto nextUndo=undo_;nextUndo.push_back(std::move(edit));
            while(nextUndo.size()>maximumCommands_ || Bytes(nextUndo)>maximumBytes_)nextUndo.erase(nextUndo.begin());
            graph_.Swap(graph);text_.swap(text);ids_.Swap(ids);layout_.Swap(layout);nextId_=next;undo_.swap(nextUndo);redo_.clear();++revision_;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
public:
    static constexpr size_t MaximumCommands=64,MaximumHistoryBytes=8*1024*1024;
    explicit AnimatorControllerDocument(size_t commands=MaximumCommands,size_t bytes=MaximumHistoryBytes):maximumCommands_(commands),maximumBytes_(bytes) {
        if(!commands || commands>MaximumCommands || !bytes || bytes>MaximumHistoryBytes)throw std::invalid_argument("Controller history limits invalid");
    }
    const Graph& Definition() const noexcept {return graph_;}
    const std::string& Text() const noexcept {return text_;}
    const Ids& AuthoringIds() const noexcept {return ids_;}
    const Layout& Geometry() const noexcept{return layout_;}
    bool ApplyLayout(const Layout& candidate,std::string& error) {
        try {
            if(!loaded_){error="Controller layout unavailable";return false;}
            if(candidate.SameGeometry(layout_,ids_)){error.clear();return true;}
            auto layout=candidate.ForGraph(ids_);
            return Publish(graph_,text_,ids_,nextId_,std::move(layout),error);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    // Initial preferences are a baseline, not an Undo command. Later reloads
    // must use ApplyLayout so existing graph/layout history stays coherent.
    bool InitializeLayout(const Layout& candidate,std::string& error) {
        try {
            if(!loaded_ || !undo_.empty() || !redo_.empty()){error="Initial layout requires empty history";return false;}
            auto layout=candidate.ForGraph(ids_);if(layout==layout_){error.clear();return true;}
            CheckRevision();layout_.Swap(layout);++revision_;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Loaded() const noexcept {return loaded_;}
    bool IsUntitled() const noexcept {return untitled_;}
    bool IsDirty() const noexcept {return loaded_ && (untitled_ || text_!=saved_);}
    size_t UndoCount() const noexcept {return undo_.size();}
    size_t RedoCount() const noexcept {return redo_.size();}
    size_t HistoryBytes() const noexcept {return Bytes(undo_)+Bytes(redo_);}
    uint64_t Revision() const noexcept {return revision_;}
    bool HasSameState(const AnimatorControllerDocument& other) const noexcept {
        const auto same=[](const std::vector<Edit>& a,const std::vector<Edit>& b){
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)if(a[i].before!=b[i].before || a[i].after!=b[i].after || a[i].beforeIds!=b[i].beforeIds || a[i].afterIds!=b[i].afterIds || a[i].beforeLayout!=b[i].beforeLayout || a[i].afterLayout!=b[i].afterLayout)return false;
            return true;
        };
        return text_==other.text_ && saved_==other.saved_ && loaded_==other.loaded_ && untitled_==other.untitled_ &&
            revision_==other.revision_ && ids_==other.ids_ && layout_==other.layout_ && nextId_==other.nextId_ && maximumCommands_==other.maximumCommands_ && maximumBytes_==other.maximumBytes_ &&
            same(undo_,other.undo_) && same(redo_,other.redo_);
    }
    bool Load(std::string_view source,std::string& error,bool untitled=false) {
        try {
            CheckRevision();Graph graph;std::string text;
            if(!AnimationSystem::Controller::TextCodec::Decode(source,graph,error) || !AnimatorControllerSerialization::Encode(graph,text,error))return false;
            auto saved=untitled?std::string{}:text;
            auto next=nextId_;auto ids=Ids::Fresh(graph,next);
            auto layout=Layout{}.ForGraph(ids);
            graph_.Swap(graph);text_.swap(text);ids_.Swap(ids);layout_.Swap(layout);nextId_=next;saved_.swap(saved);undo_.clear();redo_.clear();loaded_=true;untitled_=untitled;++revision_;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Apply(const Graph& candidate,std::string& error) {
        try {
            if(!loaded_){error="Open a Controller document first";return false;}
            std::string text;if(!AnimatorControllerSerialization::Encode(candidate,text,error))return false;
            if(text==text_){error.clear();return true;}
            // Unknown whole-graph replacements cannot infer semantic identity.
            // Typed editor commands below supply explicit identity operations.
            auto next=nextId_;auto ids=Ids::Fresh(candidate,next);
            return ApplyIdentified(candidate,std::move(ids),next,error);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool Undo(std::string& error){return MoveHistory(false,error);}
    bool Redo(std::string& error){return MoveHistory(true,error);}
    // Receipt owns its prepared bytes; the document must outlive it. This is
    // only a saved-baseline commit capability, NOT proof of filesystem success.
    class PreparedSave final {
        friend class AnimatorControllerDocument;
        const AnimatorControllerDocument* owner_=nullptr;uint64_t revision_=0;std::string text_;
    public:
        const std::string& Text() const noexcept {return text_;}
    };
    bool PrepareSave(PreparedSave& output,std::string& error) const {
        try {
            if(!loaded_){error="Controller document unavailable";return false;}
            PreparedSave candidate;candidate.owner_=this;candidate.revision_=revision_;candidate.text_=text_;
            output.text_.swap(candidate.text_);output.owner_=candidate.owner_;output.revision_=candidate.revision_;error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    // Call ONLY after successful source publication using receipt.Text(). No
    // allocations after file publication; edits/history travel independently.
    bool CommitSaved(PreparedSave& receipt) noexcept {
        if(receipt.owner_!=this || receipt.revision_!=revision_ || receipt.text_!=text_)return false;
        saved_.swap(receipt.text_);untitled_=false;receipt.owner_=nullptr;return true;
    }
};
}
