#include "EntityScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Logger.h"
#include "../../assets/SpriteReference.h"
#include <stdexcept>

namespace Engine::Scene {
void EntityScene::SetUIOutputExtent(Base::ViewExtent extent) {
    if(!Base::ValidGameOutputExtent(extent))throw std::invalid_argument("Invalid UI output extent");
    uiOutputExtent_=extent;
}
void EntityScene::SynchronizeSprites() {
    RenderSpriteFrame frame;
    const auto result=BuildRenderSpriteFrame(world_,uiOutputExtent_,frame);
    if(result!=RenderSpriteResult::Success){
        if(spriteResult_!=result)Base::Logger::Log("Entity Sprite frame refused; no partial UI drawn",Base::Logger::Level::Error);
        spriteResult_=result;return;
    }
    const auto report=[&](const std::string& message){
        if(reportedSpriteErrors_.size()<256 && reportedSpriteErrors_.insert(message).second)
            Base::Logger::Log("Entity Sprite: "+message,Base::Logger::Level::Error);
    };
    // Retire inactive/unassigned/deleted entries. Sprite data uploads are frame
    // owned, textures/shared indices are owned by their managers, not this map.
    for(auto it=sprites_.begin();it!=sprites_.end();){
        const bool live=std::any_of(frame.items.begin(),frame.items.end(),[&](const auto& item){return item.entity==it->first;});
        if(!live)it=sprites_.erase(it);else ++it;
    }
    for(const auto& item:frame.items){
        const auto* entity=world_.Find(item.entity);if(!entity)continue;
        std::string error;
        const auto path=Assets::ResolveSpriteReference(entity->sprite,assets_,projectPaths_.Root(),error);
        if(path.empty()){
            sprites_.erase(item.entity);report(error);continue;
        }
        const auto asset=Base::PathToUtf8(path);auto found=sprites_.find(item.entity);
        if(found!=sprites_.end() && found->second.asset==asset && found->second.guid==entity->sprite.assetGuid)continue;
        try {
            SpriteEntry candidate;candidate.asset=asset;candidate.guid=entity->sprite.assetGuid;candidate.authoredAsset=entity->sprite.assetPath;
            candidate.object=std::make_unique<Graphics2D::Sprite>();
            candidate.object->Initialize(Graphics2D::SpriteCommon::GetInstance(),asset);
            sprites_.insert_or_assign(item.entity,std::move(candidate));
        }catch(const std::exception& exception){
            // A failed new identity must not display the old image.
            sprites_.erase(item.entity);report(exception.what());
        }
    }
    spriteResult_=result;
}
void EntityScene::DrawSprites() {
    const auto result=BuildRenderSpriteFrame(world_,uiOutputExtent_,spriteFrame_);
    if(result!=RenderSpriteResult::Success){
        if(spriteResult_!=result)Base::Logger::Log("Entity Sprite draw frame refused; no stale/prefix UI drawn",Base::Logger::Level::Error);
        spriteResult_=result;return;
    }
    spriteResult_=result;
    if(spriteFrame_.items.empty())return;
    Graphics2D::SpriteCommon::GetInstance()->CommonDraw(true);
    for(const auto& item:spriteFrame_.items){
        const auto found=sprites_.find(item.entity);if(found==sprites_.end() || !found->second.object)continue;
        const auto* entity=world_.Find(item.entity);
        if(!entity || entity->sprite.assetGuid!=found->second.guid ||
            (entity->sprite.assetGuid.empty() && entity->sprite.assetPath!=found->second.authoredAsset))continue;
        auto& sprite=*found->second.object;
        sprite.SetAnchorPoint(item.pivot);sprite.SetColor(item.color);
        sprite.SetIsFlipX(item.flipX);sprite.SetIsFlipY(item.flipY);
        sprite.Update(item.world,spriteFrame_.projection);sprite.Draw();
    }
}
}
