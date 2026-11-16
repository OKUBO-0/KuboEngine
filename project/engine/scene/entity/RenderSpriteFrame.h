#pragma once
#include "EntityTransformMatrix.h"
#include "../../base/render/ViewExtent.h"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <new>

namespace Engine::Scene {
enum class RenderSpriteResult { Success, InvalidExtent, InvalidWorld, InvalidComponent, Limit, AllocationFailure };
struct RenderSpriteItem {
    EntityId entity=0,canvas=0;
    int canvasOrder=0,spriteOrder=0;
    Matrix4x4 world{}; // unit quad -> output pixels, full composed affine XY
    Vector2 pivot{};
    Vector4 color{};
    bool flipX=false,flipY=false;
};
struct RenderSpriteFrame {
    static constexpr size_t MaximumSprites=1024,MaximumEntities=10000,MaximumWork=262144;
    Base::ViewExtent extent{};
    Matrix4x4 projection{};
    std::vector<RenderSpriteItem> items;
    size_t skippedInvalidFrames=0,withoutCanvas=0,unassigned=0,work=0;
    void Swap(RenderSpriteFrame& other) noexcept {
        using std::swap;swap(extent,other.extent);swap(projection,other.projection);items.swap(other.items);
        swap(skippedInvalidFrames,other.skippedInvalidFrames);swap(withoutCanvas,other.withoutCanvas);
        swap(unassigned,other.unassigned);swap(work,other.work);
    }
};
// Overlay: +X right/+Y down; all Entity ancestors contribute their full TRS.
// Nearest Canvas (including self) chooses reference scaling and sort group.
// scaleWithOutput stretches X/Y independently to the actual output extent.
// Any inactive Entity/disabled Canvas ancestor suppresses its entire subtree.
// Nested Canvas chooses its own scale (never multiplies ancestor canvas scales).
// No clipping, text/input, world-space UI, anchors or asset resolution here.
inline RenderSpriteResult BuildRenderSpriteFrame(const EntityWorld& world,Base::ViewExtent extent,RenderSpriteFrame& output) noexcept {
    if(!Base::ValidGameOutputExtent(extent))return RenderSpriteResult::InvalidExtent;
    try {
        const auto& entities=world.GetEntities();const auto count=entities.size();
        if(count>RenderSpriteFrame::MaximumEntities)return RenderSpriteResult::Limit;
        RenderSpriteFrame candidate;candidate.extent=extent;
        // Flatten Z for painter-ordered overlay; no depth-based sorting.
        candidate.projection={{{2.0f/extent.width,0,0,0},{0,-2.0f/extent.height,0,0},{0,0,0,0},{-1,1,0,1}}};
        std::unordered_map<EntityId,size_t> indices;indices.reserve(count);
        for(size_t i=0;i<count;++i){
            const auto& e=entities[i];
            if(!e.id || e.id>EntityWorld::MaximumEntityId || !indices.emplace(e.id,i).second)return RenderSpriteResult::InvalidWorld;
            if((e.hasCanvas && !e.canvas.IsValid()) || (e.hasSprite && !e.sprite.IsValid()))return RenderSpriteResult::InvalidComponent;
        }
        std::vector<size_t> chain;chain.reserve(count);
        std::vector<uint32_t> visited(count,0);uint32_t stamp=0;
        const auto finite=[](const Matrix4x4& matrix){for(const auto& row:matrix.m)for(float n:row)if(!std::isfinite(n))return false;return true;};
        for(size_t i=0;i<count;++i){
            const auto& e=entities[i];if(!e.hasSprite || !e.sprite.enabled)continue;
            chain.clear();++stamp;size_t cursor=i,canvas=count;bool hidden=false,invalid=false;
            for(;;){
                if(++candidate.work>RenderSpriteFrame::MaximumWork)return RenderSpriteResult::Limit;
                if(visited[cursor]==stamp){invalid=true;break;}visited[cursor]=stamp;
                const auto& ancestor=entities[cursor];
                if(!ancestor.active || (ancestor.hasCanvas && !ancestor.canvas.enabled)){hidden=true;break;}
                if(canvas==count && ancestor.hasCanvas)canvas=cursor;
                chain.push_back(cursor);
                if(!ancestor.parent)break;
                const auto parent=indices.find(ancestor.parent);if(parent==indices.end()){invalid=true;break;}cursor=parent->second;
            }
            if(hidden)continue;
            if(invalid){++candidate.skippedInvalidFrames;continue;}
            if(canvas==count){++candidate.withoutCanvas;continue;}
            if(e.sprite.assetPath.empty() && e.sprite.assetGuid.empty()){++candidate.unassigned;continue;}
            if(e.sprite.size.x==0 || e.sprite.size.y==0 || e.sprite.color.w==0)continue;
            Matrix4x4 matrix{};
            for(size_t index=0;index<chain.size();++index){
                if(++candidate.work>RenderSpriteFrame::MaximumWork)return RenderSpriteResult::Limit;
                const auto local=EntityLocalMatrix(entities[chain[index]].transform);
                if(!finite(local)){invalid=true;break;}
                matrix=index?matrix*local:local;
                if(!finite(matrix)){invalid=true;break;}
            }
            if(invalid){++candidate.skippedInvalidFrames;continue;}
            const auto& settings=entities[canvas].canvas;
            const float sx=settings.scaleMode==CanvasScaleMode::ConstantPixels?1.0f:float(extent.width)/settings.referenceSize.x;
            const float sy=settings.scaleMode==CanvasScaleMode::ConstantPixels?1.0f:float(extent.height)/settings.referenceSize.y;
            const Matrix4x4 size{{{e.sprite.size.x,0,0,0},{0,e.sprite.size.y,0,0},{0,0,1,0},{0,0,0,1}}};
            const Matrix4x4 scale{{{sx,0,0,0},{0,sy,0,0},{0,0,1,0},{0,0,0,1}}};
            const auto pixels=size*matrix*scale;
            bool bounded=true;for(const auto& row:pixels.m)for(float n:row)if(!std::isfinite(n) || std::abs(n)>1e20f)bounded=false;
            const double a=pixels.m[0][0],b=pixels.m[0][1],c=pixels.m[1][0],d=pixels.m[1][1];
            const auto bound=(std::max)({std::abs(a),std::abs(b),std::abs(c),std::abs(d)});
            // Reject degenerate/near edge-on projected quads without decomposing
            // (and thereby losing) shear, mirrors or nonuniform parent scale.
            if(!bounded || bound==0 || std::abs((a/bound)*(d/bound)-(b/bound)*(c/bound))<1e-7){++candidate.skippedInvalidFrames;continue;}
            if(candidate.items.size()==RenderSpriteFrame::MaximumSprites)return RenderSpriteResult::Limit;
            candidate.items.push_back({e.id,entities[canvas].id,settings.sortOrder,e.sprite.sortOrder,pixels,e.sprite.pivot,e.sprite.color,e.sprite.flipX,e.sprite.flipY});
        }
        std::sort(candidate.items.begin(),candidate.items.end(),[](const auto& a,const auto& b){
            if(a.canvasOrder!=b.canvasOrder)return a.canvasOrder<b.canvasOrder;
            if(a.canvas!=b.canvas)return a.canvas<b.canvas;
            if(a.spriteOrder!=b.spriteOrder)return a.spriteOrder<b.spriteOrder;
            return a.entity<b.entity;
        });
        output.Swap(candidate);return RenderSpriteResult::Success;
    }catch(const std::bad_alloc&){return RenderSpriteResult::AllocationFailure;}
    catch(...){return RenderSpriteResult::InvalidWorld;}
}
}
