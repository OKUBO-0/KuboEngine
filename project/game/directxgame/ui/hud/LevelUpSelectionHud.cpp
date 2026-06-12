#include "game/directxgame/ui/hud/LevelUpSelectionHud.h"
#include "CameraManager.h"
#include "Input.h"
#include "MyMath.h"
#include "ParticleManager.h"
#include "game/directxgame/core/DirectXGameDataPaths.h"
#include "game/directxgame/core/GameTextureCache.h"
#include "game/directxgame/core/ScreenUtil.h"
#include "game/directxgame/core/UILayoutIO.h"
#include "game/directxgame/effects/DirectXGameParticleBehaviors.h"
#include "game/directxgame/effects/GameParticleEffects.h"
#include "game/directxgame/player/Player.h"
#include "game/directxgame/player/PlayerManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>

namespace {

constexpr float kSlideSpeed = 4200.0f;
constexpr float kScreenWidth = 1280.0f;
constexpr Vector2 kDefaultChoiceSize{ 1280.0f, 720.0f };
constexpr float kDefaultChoiceStepY = 140.0f;
constexpr Vector2 kDefaultHitboxOffset{ 465.0f, 214.0f };
constexpr Vector2 kDefaultHitboxSize{ 435.0f, 68.0f };

std::string WeaponLevelTexturePath(
	const char* weaponDirectory,
	int32_t nextLevel)
{
	return std::string("ui/game/") +
		weaponDirectory +
		"/lv" +
		std::to_string(nextLevel) +
		".png";
}

void PreloadTextures()
{
	const std::array<const char*, 16> staticTextures{
		"ui/game/lvup_attack.png",
		"ui/game/lvup_attack_icon.png",
		"ui/game/lvup_maxhp.png",
		"ui/game/lvup_maxhp_icon.png",
		"ui/game/lvup_speed.png",
		"ui/game/lvup_speed_icon.png",
		"ui/game/lvup_heal.png",
		"ui/game/lvup_heal_icon.png",
		"ui/game/normal/icon.png",
		"ui/game/orbit/icon.png",
		"ui/game/orbit/add.png",
		"ui/game/drone/icon.png",
		"ui/game/drone/add.png",
		"ui/game/lightning/icon.png",
		"ui/game/lightning/add.png",
		"ui/game/levelup.png",
	};

	std::vector<std::string> texturePaths;
	texturePaths.reserve(staticTextures.size() + 28);
	for (const char* texture : staticTextures) {
		texturePaths.emplace_back(texture);
	}
	const std::array<const char*, 4> weaponDirectories{
		"normal",
		"orbit",
		"drone",
		"lightning",
	};
	for (const char* weaponDirectory : weaponDirectories) {
		for (int32_t level = 2; level <= 8; ++level) {
			texturePaths.push_back(
				WeaponLevelTexturePath(weaponDirectory, level));
		}
	}
	DirectXGame::GameTextureCache::LoadBatch(texturePaths);
}

bool IsPointInRect(
	const Vector2& point,
	const Vector2& position,
	const Vector2& size)
{
	return point.x >= position.x &&
		point.x <= position.x + size.x &&
		point.y >= position.y &&
		point.y <= position.y + size.y;
}

struct ConfettiSpawnArea {
	Vector3 center{};
	Vector3 horizontalAxis{ 1.0f, 0.0f, 0.0f };
	Vector3 verticalAxis{ 0.0f, 1.0f, 0.0f };
	Vector3 depthAxis{ 0.0f, 0.0f, 1.0f };
	float horizontalRange = 5.5f;
};

ConfettiSpawnArea CalculateConfettiSpawnArea(
	const DirectXGame::Player& player)
{
	const Vector3 playerPosition = player.GetWorldPosition();
	ConfettiSpawnArea spawnArea{};
	spawnArea.center = playerPosition;

	const Engine::CameraSystem::Camera* activeCamera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		return spawnArea;
	}

	const Matrix4x4& cameraWorld = activeCamera->GetWorldMatrix();
	const Matrix4x4& projection = activeCamera->GetProjectionMatrix();
	const Vector3 cameraPosition = activeCamera->GetTransform().translate;
	Vector3 cameraForward = MyMath::Normalize(Vector3{
		cameraWorld.m[2][0],
		cameraWorld.m[2][1],
		cameraWorld.m[2][2],
	});
	const Vector3 cameraRight = MyMath::Normalize(Vector3{
		cameraWorld.m[0][0],
		cameraWorld.m[0][1],
		cameraWorld.m[0][2],
	});
	const Vector3 cameraUp = MyMath::Normalize(Vector3{
		cameraWorld.m[1][0],
		cameraWorld.m[1][1],
		cameraWorld.m[1][2],
	});

	float playerDepth =
		MyMath::Dot(playerPosition - cameraPosition, cameraForward);
	if (playerDepth <= 0.1f) {
		cameraForward = cameraForward * -1.0f;
		playerDepth =
			MyMath::Dot(playerPosition - cameraPosition, cameraForward);
	}
	if (playerDepth <= 0.1f) {
		playerDepth = 20.0f;
	}

	const float halfHeight =
		projection.m[1][1] != 0.0f
		? playerDepth / projection.m[1][1]
		: std::tan(0.45f * 0.5f) * playerDepth;
	const float halfWidth =
		projection.m[0][0] != 0.0f
		? playerDepth / projection.m[0][0]
		: halfHeight * (16.0f / 9.0f);
	constexpr float kBottomScreenOffsetRatio = 0.84f;
	spawnArea.center =
		cameraPosition +
		cameraForward * playerDepth +
		cameraUp * (-halfHeight * kBottomScreenOffsetRatio);
	spawnArea.horizontalAxis = cameraRight;
	spawnArea.verticalAxis = cameraUp;
	spawnArea.depthAxis = cameraForward;
	spawnArea.horizontalRange = halfWidth * 0.98f;
	return spawnArea;
}

}

namespace DirectXGame {

void LevelUpSelectionHud::Initialize()
{
	PreloadTextures();
	overlay_.Initialize("ui/game/levelup.png", { 0.0f, 0.0f });
	overlay_.SetSize({ kScreenWidth, 720.0f });

	const UILayoutIO::LayoutMap layout =
		UILayoutIO::LoadOrDefault(DataPaths::kLevelupLayout, {});
	choiceSize_ =
		UILayoutIO::GetVector2(layout, "choiceSize", kDefaultChoiceSize);
	choiceStepY_ =
		UILayoutIO::GetFloat(layout, "choiceSpacingY", kDefaultChoiceStepY);
	choiceHitboxOffset_ = UILayoutIO::GetVector2(
		layout,
		"choiceHitboxOffset",
		kDefaultHitboxOffset);
	choiceHitboxSize_ = UILayoutIO::GetVector2(
		layout,
		"choiceHitboxSize",
		kDefaultHitboxSize);

	for (UILabel& choiceSprite : choiceSprites_) {
		choiceSprite.Initialize(
			"ui/game/lvup_attack.png",
			{ 0.0f, 0.0f });
		choiceSprite.SetSize(choiceSize_);
	}
	for (UILabel& choiceIcon : choiceIcons_) {
		choiceIcon.Initialize(
			"ui/game/lvup_attack_icon.png",
			{ 0.0f, 0.0f });
		choiceIcon.SetSize(choiceSize_);
		choiceIcon.SetVisible(false);
	}
	ApplyLayout();
}

void LevelUpSelectionHud::Start(PlayerManager& playerManager)
{
	BuildChoices(playerManager);
	selection_ = 0;
	slideOffsetX_ = kScreenWidth;
	animationState_ = AnimationState::Entering;
	selectionPending_ = false;
	ApplyLayout();
}

bool LevelUpSelectionHud::Update(
	PlayerManager& playerManager,
	float deltaTime,
	float animationTime,
	int32_t moveDelta,
	bool confirmTriggered,
	GameInputBindings::NavigationInputDevice inputDevice)
{
	if (animationState_ == AnimationState::Entering) {
		slideOffsetX_ =
			(std::max)(0.0f, slideOffsetX_ - kSlideSpeed * deltaTime);
		if (slideOffsetX_ <= 0.0f) {
			slideOffsetX_ = 0.0f;
			animationState_ = AnimationState::Idle;
		}
		ApplyLayout();
		return false;
	}
	if (animationState_ == AnimationState::Exiting) {
		slideOffsetX_ -= kSlideSpeed * deltaTime;
		ApplyLayout();
		if (slideOffsetX_ <= -kScreenWidth) {
			if (selectionPending_ && !choices_.empty()) {
				const size_t index = static_cast<size_t>(std::clamp(
					selection_,
					0,
					static_cast<int32_t>(choices_.size()) - 1));
				LevelUpChoiceService::Apply(
					playerManager,
					choices_[index].upgrade);
			}
			selectionPending_ = false;
			animationState_ = AnimationState::Hidden;
			return true;
		}
		return false;
	}
	if (animationState_ != AnimationState::Idle) {
		return false;
	}

	if (moveDelta != 0) {
		MoveSelection(moveDelta);
	}
	const int32_t hoveredChoiceIndex = GetHoveredChoiceIndex();
	if (inputDevice == GameInputBindings::NavigationInputDevice::Mouse &&
		hoveredChoiceIndex >= 0) {
		selection_ = hoveredChoiceIndex;
	}

	const float selectedPulse =
		0.5f + 0.5f * std::sin(animationTime * 7.2f);
	for (size_t index = 0; index < choiceSprites_.size(); ++index) {
		const bool selected = index == static_cast<size_t>(selection_);
		const Vector4 choiceColor =
			selected
			? Vector4{
				1.08f + selectedPulse * 0.10f,
				1.08f + selectedPulse * 0.10f,
				0.74f + selectedPulse * 0.16f,
				1.0f,
			}
			: Vector4{ 0.86f, 0.86f, 0.86f, 1.0f };
		choiceSprites_[index].SetColor(choiceColor);
		choiceIcons_[index].SetColor(choiceColor);
		choiceSprites_[index].SetAlpha(selected ? 1.0f : 0.78f);
		choiceIcons_[index].SetAlpha(selected ? 1.0f : 0.78f);
	}

	const bool mouseConfirm =
		hoveredChoiceIndex >= 0 &&
		GameInputBindings::IsMouseConfirmTriggered(
			Engine::InputSystem::Input::GetInstance());
	const bool confirmed =
		inputDevice == GameInputBindings::NavigationInputDevice::Mouse
		? mouseConfirm
		: confirmTriggered;
	if (confirmed) {
		selectionPending_ = true;
		animationState_ = AnimationState::Exiting;
	}
	return false;
}

void LevelUpSelectionHud::Draw()
{
	overlay_.Draw();
	for (UILabel& choiceSprite : choiceSprites_) {
		choiceSprite.Draw();
	}
	for (UILabel& choiceIcon : choiceIcons_) {
		choiceIcon.Draw();
	}
}

void LevelUpSelectionHud::SpawnConfetti(
	const Player& player,
	const GameParticleEffects& particleEffects) const
{
	const GameParticleEffects::Tuning& tuning =
		particleEffects.GetTuning();
	const uint32_t confettiCount = static_cast<uint32_t>(
		(std::max)(120, tuning.levelUpConfettiCount));
	if (confettiCount == 0) {
		return;
	}

	const ConfettiSpawnArea spawnArea =
		CalculateConfettiSpawnArea(player);
	ConfettiParticleBehavior::Settings settings{};
	settings.lifetimeMin = 1.8f;
	settings.lifetimeMax = 2.8f;
	settings.velocityScale =
		(std::max)(1.55f, tuning.confettiVelocityScale);
	settings.scaleMultiplier =
		(std::max)(1.9f, tuning.confettiScaleMultiplier);
	settings.gravity = 0.012f;
	settings.horizontalAxis = spawnArea.horizontalAxis;
	settings.verticalAxis = spawnArea.verticalAxis;
	settings.depthAxis = spawnArea.depthAxis;
	settings.horizontalOffsetRange = spawnArea.horizontalRange;
	settings.depthOffsetRange = 1.2f;
	settings.yOffset = -0.65f;

	Engine::Particle::ParticleManager* particleManager =
		Engine::Particle::ParticleManager::GetInstance();
	particleManager->SetBehavior(
		"DirectXGame.Confetti",
		std::make_unique<ConfettiParticleBehavior>(settings));
	particleManager->Emit(
		"DirectXGame.Confetti",
		spawnArea.center,
		confettiCount);
}

void LevelUpSelectionHud::BuildChoices(
	const PlayerManager& playerManager)
{
	choices_ = LevelUpChoiceService::Build(
		playerManager,
		choiceSprites_.size());
	for (size_t index = 0; index < choiceSprites_.size(); ++index) {
		const bool hasChoice = index < choices_.size();
		choiceSprites_[index].SetTexture(
			hasChoice
			? choices_[index].texturePath
			: "ui/game/lvup_attack.png");
		choiceSprites_[index].SetSize(choiceSize_);
		choiceIcons_[index].SetTexture(
			hasChoice
			? choices_[index].iconPath
			: "ui/game/lvup_attack_icon.png");
		choiceIcons_[index].SetSize(choiceSize_);
		choiceIcons_[index].SetVisible(hasChoice);
	}
	ApplyLayout();
}

void LevelUpSelectionHud::ApplyLayout()
{
	overlay_.SetPosition({ slideOffsetX_, 0.0f });
	for (size_t index = 0; index < choiceSprites_.size(); ++index) {
		const Vector2 position{
			slideOffsetX_,
			choiceStepY_ * static_cast<float>(index),
		};
		choiceSprites_[index].SetPosition(position);
		choiceSprites_[index].SetSize(choiceSize_);
		choiceIcons_[index].SetPosition(position);
		choiceIcons_[index].SetSize(choiceSize_);
	}
}

void LevelUpSelectionHud::MoveSelection(int32_t delta)
{
	const int32_t maxSelection =
		static_cast<int32_t>(choices_.empty() ? 0 : choices_.size() - 1);
	selection_ += delta;
	if (selection_ < 0) {
		selection_ = maxSelection;
	} else if (selection_ > maxSelection) {
		selection_ = 0;
	}
}

int32_t LevelUpSelectionHud::GetHoveredChoiceIndex() const
{
	Engine::InputSystem::Input* input =
		Engine::InputSystem::Input::GetInstance();
	if (!input || choices_.empty()) {
		return -1;
	}
	if (!ScreenUtil::IsInsideDebugSceneViewport(input->GetMousePos())) {
		return -1;
	}

	const Vector2 mousePosition =
		ScreenUtil::ToGamePosition(input->GetMousePos());
	for (int32_t index = 0;
		index < static_cast<int32_t>(choices_.size());
		++index) {
		const Vector2 position{
			slideOffsetX_ + choiceHitboxOffset_.x,
			choiceHitboxOffset_.y +
				choiceStepY_ * static_cast<float>(index),
		};
		if (IsPointInRect(
			mousePosition,
			position,
			choiceHitboxSize_)) {
			return index;
		}
	}
	return -1;
}

}
