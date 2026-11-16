#include "EntityScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Logger.h"
#include "TextureManager.h"
#include "UITextRaster.h"
#include "../../assets/SpriteReference.h"
#include "../../input/InputFrame.h"
#include <stdexcept>
#include <atomic>

namespace Engine::Scene {
void EntityScene::SetUIOutputExtent(Base::ViewExtent extent) {
    if(!Base::ValidGameOutputExtent(extent))throw std::invalid_argument("Invalid UI output extent");
    uiOutputExtent_=extent;
}
void EntityScene::CancelUIButtons() noexcept {
    hoveredButton_=pressedButton_=0;
    pressedButtonScene_.clear();
}
std::optional<Scripting::ScriptSceneTransition> EntityScene::UpdateUIButtons(const InputSystem::InputFrame& input,
    std::optional<Vector2> point,Base::ViewExtent gameOutput) {
    if(!input.available || !point || spriteResult_!=RenderSpriteResult::Success || gameOutput!=spriteFrame_.extent){
        CancelUIButtons();
        return std::nullopt;
    }
    hoveredButton_=0;
    const SpriteComponent* selected=nullptr;
    // Match the most recent rendered frame and consider enabled buttons in reverse paint order.
    for(auto item=spriteFrame_.items.rbegin();item!=spriteFrame_.items.rend();++item){
        if(!item->ContainsOutputPoint(*point))continue;
        const auto* entity=world_.Find(item->entity);
        if(!entity || !entity->active || !entity->hasSprite || !entity->sprite.enabled ||
            entity->sprite.buttonSceneGuid.empty())continue;
        const auto image=sprites_.find(item->entity);
        const auto text=texts_.find(item->entity);
        if((image==sprites_.end() || !image->second.Matches(entity->sprite)) &&
            (text==texts_.end() || !entity->sprite.HasVisibleText() || !text->second.Matches(entity->sprite)))continue;
        hoveredButton_=item->entity;
        selected=&entity->sprite;
        break;
    }
    const auto& mouse=input.mouse[0];
    if(mouse.pressed){
        pressedButton_=hoveredButton_;
        pressedButtonScene_=selected?selected->buttonSceneGuid:std::string{};
    }
    if(mouse.down)return std::nullopt;
    const bool clicked=mouse.released && selected && hoveredButton_==pressedButton_ &&
        selected->buttonSceneGuid==pressedButtonScene_;
    pressedButton_=0;
    pressedButtonScene_.clear();
    if(clicked)return Scripting::ScriptSceneTransition{"guid:"+selected->buttonSceneGuid,0};
    return std::nullopt;
}
void EntityScene::ReportSpriteError(const std::string& message) {
    if(reportedSpriteErrors_.size()<256 && reportedSpriteErrors_.insert(message).second)
        Base::Logger::Log("Entity Sprite: "+message,Base::Logger::Level::Error);
}
void EntityScene::SynchronizeSprites() {
    RenderSpriteFrame frame;
    const auto result=BuildRenderSpriteFrame(world_,uiOutputExtent_,frame);
    if(result!=RenderSpriteResult::Success){
        if(spriteResult_!=result)Base::Logger::Log("Entity Sprite frame refused; no partial UI drawn",Base::Logger::Level::Error);
        spriteResult_=result;return;
    }
    // Retire inactive/unassigned/deleted entries. Sprite data uploads are frame
    // owned, textures/shared indices are owned by their managers, not this map.
    for(auto it=sprites_.begin();it!=sprites_.end();){
        const bool live=std::any_of(frame.items.begin(),frame.items.end(),[&](const auto& item){return item.entity==it->first;});
        if(!live)it=sprites_.erase(it);else ++it;
    }
    for(const auto& item:frame.items){
        const auto* entity=world_.Find(item.entity);if(!entity)continue;
        if(!entity->sprite.HasImage()){sprites_.erase(item.entity);continue;}
        std::string error;
        const auto path=Assets::ResolveSpriteReference(entity->sprite,assets_,projectPaths_.Root(),error);
        if(path.empty()){
            sprites_.erase(item.entity);ReportSpriteError(error);continue;
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
            sprites_.erase(item.entity);ReportSpriteError(exception.what());
        }
    }
    SynchronizeUIText(frame);
    spriteResult_=result;
}
void EntityScene::SynchronizeUIText(const RenderSpriteFrame& frame) {
    auto* textures=Base::TextureManager::GetInstance();
    if(retiredTextTextures_>=8){
        textures->CollectUnusedTextures();
        retiredTextTextures_=0;
    }
    for(auto it=texts_.begin();it!=texts_.end();){
        const auto* entity=world_.Find(it->first);
        const bool visible=entity && entity->sprite.HasVisibleText() &&
            std::any_of(frame.items.begin(),frame.items.end(),[&](const auto& item){return item.entity==it->first;});
        if(!visible){it=texts_.erase(it);++retiredTextTextures_;}else ++it;
    }
    constexpr size_t maxTextPixels=8*1024*1024;
    size_t usedPixels=0;
    static std::atomic_uint64_t textureId{0};
    for(const auto& item:frame.items){
        const auto* entity=world_.Find(item.entity);
        if(!entity || !entity->sprite.HasVisibleText())continue;
        const auto& sprite=entity->sprite;
        const auto scale=(std::min)(1.0f,UITextRasterizer::MaxDimension/(std::max)(sprite.size.x,sprite.size.y));
        const auto width=static_cast<uint32_t>((std::max)(1.0f,std::ceil(sprite.size.x*scale)));
        const auto height=static_cast<uint32_t>((std::max)(1.0f,std::ceil(sprite.size.y*scale)));
        const size_t area=size_t(width)*height;
        auto found=texts_.find(item.entity);
        if(area>maxTextPixels-usedPixels){
            if(found!=texts_.end()){texts_.erase(found);++retiredTextTextures_;}
            ReportSpriteError("UI text texture budget exceeded");
            continue;
        }
        usedPixels+=area;
        if(found!=texts_.end() && found->second.Matches(sprite))continue;
        if(found!=texts_.end()){texts_.erase(found);++retiredTextTextures_;}
        try {
            if(!textRasterizer_)textRasterizer_=std::make_unique<UITextRasterizer>();
            auto alignment=DWRITE_TEXT_ALIGNMENT_CENTER;
            if(sprite.textAlignment==UITextAlignment::Left)alignment=DWRITE_TEXT_ALIGNMENT_LEADING;
            if(sprite.textAlignment==UITextAlignment::Right)alignment=DWRITE_TEXT_ALIGNMENT_TRAILING;
            const auto pixels=textRasterizer_->Rasterize(sprite.text,width,height,sprite.textSize*scale,alignment);
            const auto key="__ui_text/"+std::to_string(textureId.fetch_add(1,std::memory_order_relaxed));
            TextEntry entry{sprite.text,sprite.size,sprite.textSize,sprite.textAlignment,std::make_unique<Graphics2D::Sprite>()};
            entry.object->Initialize(Graphics2D::SpriteCommon::GetInstance(),
                textures->AcquireTextureFromRGBA(key,width,height,pixels.data(),size_t(width)*4,false));
            texts_.emplace(item.entity,std::move(entry));
        }catch(const std::exception& error){
            ++retiredTextTextures_;
            ReportSpriteError(error.what());
        }
    }
}
void EntityScene::DrawSprites(RenderSpriteFrame& frame) {
    const auto result=BuildRenderSpriteFrame(world_,uiOutputExtent_,frame);
    if(result!=RenderSpriteResult::Success){
        frame.items.clear();
        if(spriteResult_!=result)Base::Logger::Log("Entity Sprite draw frame refused; no stale/prefix UI drawn",Base::Logger::Level::Error);
        spriteResult_=result;return;
    }
    spriteResult_=result;
    if(frame.items.empty())return;
    Graphics2D::SpriteCommon::GetInstance()->CommonDraw(true);
    for(const auto& item:frame.items){
        const auto* entity=world_.Find(item.entity);
        if(!entity)continue;
        const auto& settings=entity->sprite;
        const float shade=item.entity!=hoveredButton_ || settings.buttonSceneGuid.empty()?1.0f:
            item.entity==pressedButton_?0.74f:0.88f;
        if(const auto image=sprites_.find(item.entity);image!=sprites_.end() && image->second.Matches(settings)){
            auto& sprite=*image->second.object;
            sprite.SetAnchorPoint(item.pivot);
            sprite.SetColor({item.color.x*shade,item.color.y*shade,item.color.z*shade,item.color.w});
            sprite.SetIsFlipX(item.flipX);sprite.SetIsFlipY(item.flipY);
            sprite.Update(item.world,frame.projection);sprite.Draw();
        }
        if(const auto text=texts_.find(item.entity);text!=texts_.end() && settings.HasVisibleText() && text->second.Matches(settings)){
            auto& sprite=*text->second.object;
            // Preserve the mirrored image rectangle and keep the glyphs readable.
            sprite.SetAnchorPoint({item.flipX?1-item.pivot.x:item.pivot.x,item.flipY?1-item.pivot.y:item.pivot.y});
            const auto& tint=settings.textColor;
            sprite.SetColor({tint.x*shade,tint.y*shade,tint.z*shade,tint.w*item.color.w});
            sprite.Update(item.world,frame.projection);sprite.Draw();
        }
    }
}
}
