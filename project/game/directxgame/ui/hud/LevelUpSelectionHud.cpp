#include "LevelUpSelectionHud.h"
#include "CameraManager.h"
#include "Input.h"
#include "MyMath.h"
#include "ParticleManager.h"
#include "DataPaths.h"
#include "GameTextureCache.h"
#include "ScreenUtil.h"
#include "UILayoutIO.h"
#include "ParticleBehaviors.h"
#include "GameParticleEffects.h"
#include "Player.h"
#include "PlayerManager.h"
#include "DirectXCommon.h"
#include "Matrix4x4.h"
#include "MyMath.h"
#include "RenderingData.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr float kSlideSpeed = 4200.0f;
constexpr float kScreenWidth = 1280.0f;
constexpr Vector2 kDefaultChoiceSize{ 1280.0f, 720.0f };
constexpr float kDefaultChoiceStepY = 140.0f;
constexpr Vector2 kDefaultHitboxOffset{ 436.0f, 194.0f };
constexpr Vector2 kDefaultHitboxSize{ 406.0f, 65.0f };
constexpr Vector2 kOverlayPosition{ 427.0f, 88.0f };
constexpr Vector2 kOverlaySize{ 426.0f, 532.0f };
constexpr float kOverlayBorder = 4.0f;
constexpr float kOverlayCornerRadius = 7.0f;
constexpr float kChoiceBorder = 3.0f;
constexpr float kChoiceCornerRadius = 4.0f;
constexpr float kIconFrameSize = 54.0f;
constexpr float kIconInset = 6.0f;
constexpr float kSubIconSize = 20.0f;
constexpr float kChoiceTextOffsetX = 72.0f;
constexpr float kChoiceTextRightPadding = 12.0f;
constexpr float kChoiceTitleBaseScale = 0.34f;
constexpr float kChoiceDetailBaseScale = 0.24f;
constexpr float kLevelUpTextY = 132.0f;
constexpr float kLevelUpTextScale = 0.50f;
constexpr Vector4 kPanelBackgroundColor{ 0.0f, 0.0f, 0.0f, 0.78f };
constexpr Vector4 kFrameColor{ 1.0f, 0.96f, 0.0f, 1.0f };
constexpr Vector4 kSelectedFrameColor{ 1.0f, 1.0f, 0.36f, 1.0f };
constexpr int32_t kRoundedPanelCornerSegments = 8;
constexpr char kWhiteTexturePath[] = "Resources/DirectXGame/white1x1.png";
constexpr char kSelectSePath[] = "se/ui_select.wav";
constexpr char kDecideSePath[] = "se/ui_decide.wav";
constexpr char kAudioUiSelect[] = "ui.select";
constexpr char kAudioUiDecide[] = "ui.decide";

void PreloadTextures()
{
	const std::vector<const char*> staticTextures{
		"white1x1.png",
		"ui/font/noto_sans_jp_black.png",
		"ui/game/lvup/attack_icon.png",
		"ui/game/lvup/maxhp_icon.png",
		"ui/game/lvup/speed_icon.png",
		"ui/game/lvup/heal_icon.png",
		"ui/game/lvup/icon_common_unknown.png",
		"ui/game/lvup/icon_weapon_bow_arrow.png",
		"ui/game/lvup/icon_weapon_rock.png",
		"ui/game/lvup/icon_weapon_thunder_staff.png",
		"ui/game/lvup/icon_weapon_flame_staff.png",
		"ui/game/lvup/icon_weapon_sword.png",
		"ui/game/lvup/icon_weapon_bone.png",
		"ui/game/lvup/icon_weapon_handgun.png",
		"ui/game/lvup/icon_weapon_boomerang.png",
		"ui/game/lvup/scroll.png",
		"ui/game/lvup/icon_weapon_aura.png",
		"ui/game/lvup/icon_passive_scroll.png",
		"ui/game/lvup/icon_stat_damage.png",
		"ui/game/lvup/icon_stat_maxhp.png",
		"ui/game/lvup/icon_stat_movespeed.png",
		"ui/game/lvup/icon_stat_attackspeed.png",
		"ui/game/lvup/icon_stat_duration.png",
		"ui/game/lvup/icon_stat_area.png",
		"ui/game/lvup/icon_stat_projectile_speed.png",
		"ui/game/lvup/icon_stat_projectile_count.png",
		"ui/game/lvup/icon_stat_pickup_range.png",
		"ui/game/lvup/icon_stat_exp_gain.png",
		"ui/game/lvup/icon_stat_coin_gain.png",
		"ui/game/lvup/icon_stat_crit_chance.png",
		"ui/game/lvup/icon_stat_crit_damage.png",
		"ui/game/lvup/icon_stat_armor.png",
		"ui/game/lvup/icon_stat_evasion.png",
		"ui/game/lvup/icon_stat_hp_regen.png",
		"ui/game/lvup/icon_stat_lifesteal.png",
		"ui/game/lvup/icon_stat_knockback.png",
		"ui/game/lvup/icon_stat_shoe.png",
		"ui/game/lvup/icon_stat_fire.png",
	};

	std::vector<std::string> texturePaths;
	texturePaths.reserve(staticTextures.size());
	for (const char* texture : staticTextures) {
		texturePaths.emplace_back(texture);
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

class LevelUpSelectionHud::RoundedPanel {
public:
	void Initialize(const Vector4& color)
	{
		spriteCommon_ = Engine::Graphics2D::SpriteCommon::GetInstance();
		materialData_.uvTransform = materialData_.uvTransform.MakeIdentity4x4();
		transformationMatrixData_.World =
			transformationMatrixData_.World.MakeIdentity4x4();
		SetColor(color);
	}

	void SetColor(const Vector4& color)
	{
		color_ = color;
		materialData_.color = color_;
	}

	void SetLayout(const Vector2& position, const Vector2& size, float radius)
	{
		position_ = position;
		size_ = size;
		radius_ = radius;
		borderThickness_ = 0.0f;
		geometryDirty_ = true;
	}

	void SetBorderLayout(
		const Vector2& position,
		const Vector2& size,
		float radius,
		float thickness)
	{
		position_ = position;
		size_ = size;
		radius_ = radius;
		borderThickness_ = (std::max)(0.0f, thickness);
		geometryDirty_ = true;
	}

	void Draw()
	{
		if (!spriteCommon_ || size_.x <= 0.0f || size_.y <= 0.0f || color_.w <= 0.0f) {
			return;
		}

		UpdateGeometry();
		UpdateMatrices();
		const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
			spriteCommon_->GetDxCommon();
		const Engine::Base::DirectXCommon::FrameUploadAllocation materialAllocation =
			dxCommon->AllocateFrameUpload(sizeof(MaterialSprite), 256);
		std::memcpy(materialAllocation.cpuAddress, &materialData_, sizeof(materialData_));
		const Engine::Base::DirectXCommon::FrameUploadAllocation transformAllocation =
			dxCommon->AllocateFrameUpload(sizeof(TransformationMatrixsprite), 256);
		std::memcpy(
			transformAllocation.cpuAddress,
			&transformationMatrixData_,
			sizeof(transformationMatrixData_));
		const Engine::Base::DirectXCommon::FrameUploadAllocation vertexAllocation =
			dxCommon->AllocateFrameUpload(
				sizeof(VertexData) * vertexData_.size(),
				alignof(VertexData));
		std::memcpy(
			vertexAllocation.cpuAddress,
			vertexData_.data(),
			sizeof(VertexData) * vertexData_.size());
		const Engine::Base::DirectXCommon::FrameUploadAllocation indexAllocation =
			dxCommon->AllocateFrameUpload(
				sizeof(uint32_t) * indexCount_,
				alignof(uint32_t));
		std::memcpy(
			indexAllocation.cpuAddress,
			indexData_.data(),
			sizeof(uint32_t) * indexCount_);

		const D3D12_VERTEX_BUFFER_VIEW vertexBufferView{
			.BufferLocation = vertexAllocation.gpuAddress,
			.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertexData_.size()),
			.StrideInBytes = sizeof(VertexData),
		};
		const D3D12_INDEX_BUFFER_VIEW indexBufferView{
			.BufferLocation = indexAllocation.gpuAddress,
			.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * indexCount_),
			.Format = DXGI_FORMAT_R32_UINT,
		};

		ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		commandList->IASetIndexBuffer(&indexBufferView);
		commandList->SetGraphicsRootConstantBufferView(0, materialAllocation.gpuAddress);
		commandList->SetGraphicsRootDescriptorTable(
			1,
			Engine::Base::TextureManager::GetInstance()->GetSrvHandleGPU(kWhiteTexturePath));
		commandList->SetGraphicsRootConstantBufferView(2, transformAllocation.gpuAddress);
		commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
		spriteCommon_->RecordSpriteDraw(
			static_cast<uint32_t>(
				sizeof(MaterialSprite) +
				sizeof(TransformationMatrixsprite) +
				sizeof(VertexData) * vertexData_.size() +
				sizeof(uint32_t) * indexCount_));
	}

private:
	void UpdateGeometry()
	{
		if (!geometryDirty_) {
			return;
		}

		const float width = (std::max)(1.0f, size_.x);
		const float height = (std::max)(1.0f, size_.y);
		const float radius = std::clamp(radius_, 0.0f, (std::min)(width, height) * 0.5f);
		const float borderThickness = std::clamp(
			borderThickness_,
			0.0f,
			(std::min)(width, height) * 0.5f);
		constexpr float pi = 3.14159265358979323846f;

		auto buildRoundedPoints = [](float widthValue, float heightValue, float radiusValue) {
			std::vector<Vector2> points;
			points.reserve(4 * (kRoundedPanelCornerSegments + 1));
			constexpr float piValue = 3.14159265358979323846f;
			auto appendCorner = [&points, radiusValue](const Vector2& center, float startAngle, float endAngle) {
			for (int32_t i = 0; i <= kRoundedPanelCornerSegments; ++i) {
				const float t = static_cast<float>(i) / static_cast<float>(kRoundedPanelCornerSegments);
				const float angle = startAngle + (endAngle - startAngle) * t;
				points.push_back({
					center.x + std::cos(angle) * radiusValue,
					center.y + std::sin(angle) * radiusValue,
					});
			}
		};
			appendCorner({ widthValue - radiusValue, radiusValue }, -piValue * 0.5f, 0.0f);
			appendCorner({ widthValue - radiusValue, heightValue - radiusValue }, 0.0f, piValue * 0.5f);
			appendCorner({ radiusValue, heightValue - radiusValue }, piValue * 0.5f, piValue);
			appendCorner({ radiusValue, radiusValue }, piValue, piValue * 1.5f);
			return points;
		};

		std::vector<Vector2> points = buildRoundedPoints(width, height, radius);

		vertexData_.clear();
		indexData_.clear();
		if (borderThickness > 0.0f) {
			const float innerWidth = (std::max)(1.0f, width - borderThickness * 2.0f);
			const float innerHeight = (std::max)(1.0f, height - borderThickness * 2.0f);
			const float innerRadius = std::clamp(
				radius - borderThickness,
				0.0f,
				(std::min)(innerWidth, innerHeight) * 0.5f);
			std::vector<Vector2> innerPoints =
				buildRoundedPoints(innerWidth, innerHeight, innerRadius);
			for (Vector2& point : innerPoints) {
				point.x += borderThickness;
				point.y += borderThickness;
			}
			const size_t pointCount = (std::min)(points.size(), innerPoints.size());
			vertexData_.reserve(pointCount * 2);
			for (size_t index = 0; index < pointCount; ++index) {
				vertexData_.push_back(MakeVertex(points[index]));
				vertexData_.push_back(MakeVertex(innerPoints[index]));
			}
			for (uint32_t index = 0; index < static_cast<uint32_t>(pointCount); ++index) {
				const uint32_t next = (index + 1u) % static_cast<uint32_t>(pointCount);
				const uint32_t outer0 = index * 2u;
				const uint32_t inner0 = outer0 + 1u;
				const uint32_t outer1 = next * 2u;
				const uint32_t inner1 = outer1 + 1u;
				indexData_.push_back(outer0);
				indexData_.push_back(outer1);
				indexData_.push_back(inner0);
				indexData_.push_back(inner0);
				indexData_.push_back(outer1);
				indexData_.push_back(inner1);
			}
			indexCount_ = static_cast<UINT>(indexData_.size());
			geometryDirty_ = false;
			return;
		}

		vertexData_.reserve(points.size() + 1);
		vertexData_.push_back(MakeVertex({ width * 0.5f, height * 0.5f }));
		for (const Vector2& point : points) {
			vertexData_.push_back(MakeVertex(point));
		}
		for (uint32_t i = 1; i + 1 < vertexData_.size(); ++i) {
			indexData_.push_back(0);
			indexData_.push_back(i);
			indexData_.push_back(i + 1);
		}
		indexData_.push_back(0);
		indexData_.push_back(static_cast<uint32_t>(vertexData_.size() - 1));
		indexData_.push_back(1);
		indexCount_ = static_cast<UINT>(indexData_.size());
		geometryDirty_ = false;
	}

	VertexData MakeVertex(const Vector2& position) const
	{
		VertexData vertex{};
		vertex.position = { position.x, position.y, 0.0f, 1.0f };
		vertex.texcoord = { 0.0f, 0.0f };
		vertex.normal = { 0.0f, 0.0f, -1.0f };
		return vertex;
	}

	void UpdateMatrices()
	{
		const EulerTransform transform{
			{ 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ position_.x, position_.y, 0.0f },
		};
		const Matrix4x4 world =
			MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		const Matrix4x4 projection = MyMath::MakeOrthographicMatrix(
			0.0f,
			0.0f,
			float(Engine::Base::WinApp::kClientWidth),
			float(Engine::Base::WinApp::kClientHeight),
			0.0f,
			100.0f);
		transformationMatrixData_.World = world;
		transformationMatrixData_.WVP =
			world * Matrix4x4{}.MakeIdentity4x4() * projection;
	}

	Engine::Graphics2D::SpriteCommon* spriteCommon_ = nullptr;
	std::vector<VertexData> vertexData_;
	std::vector<uint32_t> indexData_;
	MaterialSprite materialData_{};
	TransformationMatrixsprite transformationMatrixData_{};
	Vector2 position_{};
	Vector2 size_{};
	Vector4 color_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float radius_ = 0.0f;
	float borderThickness_ = 0.0f;
	UINT indexCount_ = 0;
	bool geometryDirty_ = true;
};

LevelUpSelectionHud::LevelUpSelectionHud() = default;

LevelUpSelectionHud::~LevelUpSelectionHud() = default;

void LevelUpSelectionHud::Initialize()
{
	PreloadTextures();
	overlayFrame_ = std::make_unique<RoundedPanel>();
	overlayFrame_->Initialize(kFrameColor);
	overlayPanel_ = std::make_unique<RoundedPanel>();
	overlayPanel_->Initialize(kPanelBackgroundColor);
	levelUpText_.Initialize(
		"ui/font/noto_sans_jp_black.png",
		"ui/font/noto_sans_jp_black.json");
	levelUpText_.SetText("LEVEL UP");
	levelUpText_.SetScale(kLevelUpTextScale);
	levelUpText_.SetAdvanceMultiplier(0.92f);
	levelUpText_.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

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

	for (size_t index = 0; index < kChoiceCount; ++index) {
		choiceBackgrounds_[index] = std::make_unique<RoundedPanel>();
		choiceBackgrounds_[index]->Initialize(kPanelBackgroundColor);
		choiceFrames_[index] = std::make_unique<RoundedPanel>();
		choiceFrames_[index]->Initialize(kFrameColor);
		choiceIconFrames_[index] = std::make_unique<RoundedPanel>();
		choiceIconFrames_[index]->Initialize(kFrameColor);
	}
	for (UILabel& choiceIcon : choiceIcons_) {
		choiceIcon.Initialize(
			"ui/game/lvup/attack_icon.png",
			{ 0.0f, 0.0f });
		choiceIcon.SetSize(choiceSize_);
		choiceIcon.SetVisible(false);
	}
	for (UILabel& choiceSubIcon : choiceSubIcons_) {
		choiceSubIcon.Initialize(
			"ui/game/lvup/icon_stat_damage.png",
			{ 0.0f, 0.0f });
		choiceSubIcon.SetSize({ kSubIconSize, kSubIconSize });
		choiceSubIcon.SetVisible(false);
	}
	for (BitmapText& text : choiceTitleTexts_) {
		text.Initialize(
			"ui/font/noto_sans_jp_black.png",
			"ui/font/noto_sans_jp_black.json");
		text.SetScale(kChoiceTitleBaseScale);
		text.SetAdvanceMultiplier(1.0f);
		text.SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	}
	for (BitmapText& text : choiceDetailTexts_) {
		text.Initialize(
			"ui/font/noto_sans_jp_black.png",
			"ui/font/noto_sans_jp_black.json");
		text.SetScale(kChoiceDetailBaseScale);
		text.SetAdvanceMultiplier(1.0f);
		text.SetColor({ 1.0f, 1.0f, 1.0f, 0.95f });
	}
	selectSeHandle_ = GameAudioCache::LoadWave(kSelectSePath);
	decideSeHandle_ = GameAudioCache::LoadWave(kDecideSePath);
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
					choices_[index]);
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
		if (selectSeHandle_) {
			GameAudioCache::PlayTuned(
				selectSeHandle_,
				kAudioUiSelect,
				0.55f,
				0.035f);
		}
	}
	const int32_t hoveredChoiceIndex = GetHoveredChoiceIndex();
	if (inputDevice == GameInputBindings::NavigationInputDevice::Mouse &&
		hoveredChoiceIndex >= 0 &&
		hoveredChoiceIndex != selection_) {
		selection_ = hoveredChoiceIndex;
		if (selectSeHandle_) {
			GameAudioCache::PlayTuned(
				selectSeHandle_,
				kAudioUiSelect,
				0.55f,
				0.035f);
		}
	}

	const float selectedPulse =
		0.5f + 0.5f * std::sin(animationTime * 7.2f);
	for (size_t index = 0; index < choiceFrames_.size(); ++index) {
		const bool selected = index == static_cast<size_t>(selection_);
		const Vector4 frameColor =
			selected
			? Vector4{
				1.0f,
				1.0f,
				0.20f + selectedPulse * 0.16f,
				1.0f,
			}
			: kFrameColor;
		if (choiceFrames_[index]) {
			choiceFrames_[index]->SetColor(frameColor);
		}
		if (choiceIconFrames_[index]) {
			choiceIconFrames_[index]->SetColor(frameColor);
		}
		choiceIcons_[index].SetColor({ 1.0f, 1.0f, 1.0f, selected ? 1.0f : 0.88f });
		choiceSubIcons_[index].SetColor({ 1.0f, 1.0f, 1.0f, selected ? 1.0f : 0.92f });
		const Vector4 titleColor = selected
			? Vector4{ 1.0f, 1.0f, 0.86f, 1.0f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 0.92f };
		choiceTitleTexts_[index].SetColor(titleColor);
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
		if (decideSeHandle_) {
			GameAudioCache::PlayTuned(decideSeHandle_, kAudioUiDecide, 0.72f);
		}
	}
	return false;
}

void LevelUpSelectionHud::Draw()
{
	if (overlayPanel_) {
		overlayPanel_->Draw();
	}
	if (overlayFrame_) {
		overlayFrame_->Draw();
	}
	levelUpText_.Draw();
	for (const std::unique_ptr<RoundedPanel>& choiceBackground : choiceBackgrounds_) {
		if (choiceBackground) {
			choiceBackground->Draw();
		}
	}
	for (const std::unique_ptr<RoundedPanel>& choiceFrame : choiceFrames_) {
		if (choiceFrame) {
			choiceFrame->Draw();
		}
	}
	for (const std::unique_ptr<RoundedPanel>& iconFrame : choiceIconFrames_) {
		if (iconFrame) {
			iconFrame->Draw();
		}
	}
	for (UILabel& choiceIcon : choiceIcons_) {
		choiceIcon.Draw();
	}
	for (UILabel& choiceSubIcon : choiceSubIcons_) {
		choiceSubIcon.Draw();
	}
	for (BitmapText& text : choiceTitleTexts_) {
		text.Draw();
	}
	for (BitmapText& text : choiceDetailTexts_) {
		text.Draw();
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
	const Engine::Particle::ParticleGroupHandle confettiHandle =
		particleEffects.GetHandles().confetti;
	particleManager->SetBehavior(
		confettiHandle,
		std::make_unique<ConfettiParticleBehavior>(settings));
	particleManager->Emit(
		confettiHandle,
		spawnArea.center,
		confettiCount);
}

void LevelUpSelectionHud::BuildChoices(
	const PlayerManager& playerManager)
{
	choices_ = LevelUpChoiceService::Build(
		playerManager,
		choiceFrames_.size());
	for (size_t index = 0; index < choiceFrames_.size(); ++index) {
		const bool hasChoice = index < choices_.size();
		choiceIcons_[index].SetTexture(
			hasChoice
			? choices_[index].iconPath
			: "ui/game/lvup/attack_icon.png");
		choiceIcons_[index].SetVisible(hasChoice);
		const bool hasSubIcon =
			hasChoice && !choices_[index].subIconPath.empty();
		choiceSubIcons_[index].SetTexture(
			hasSubIcon
			? choices_[index].subIconPath
			: "ui/game/lvup/icon_stat_damage.png");
		choiceSubIcons_[index].SetVisible(hasSubIcon);
		choiceTitleTexts_[index].SetText(
			hasChoice ? choices_[index].titleText : "");
		choiceDetailTexts_[index].SetText(
			hasChoice ? choices_[index].detailText : "");
	}
	ApplyLayout();
}

void LevelUpSelectionHud::ApplyLayout()
{
	const Vector2 overlayFramePosition{
		slideOffsetX_ + kOverlayPosition.x,
		kOverlayPosition.y,
	};
	if (overlayFrame_) {
		overlayFrame_->SetBorderLayout(
			overlayFramePosition,
			kOverlaySize,
			kOverlayCornerRadius,
			kOverlayBorder);
	}
	if (overlayPanel_) {
		overlayPanel_->SetLayout(
			overlayFramePosition,
			kOverlaySize,
			kOverlayCornerRadius);
	}
	levelUpText_.SetScaleToFit(kLevelUpTextScale, kOverlaySize.x - 80.0f);
	const float levelUpTextWidth = levelUpText_.MeasureWidth();
	levelUpText_.SetPosition({
		overlayFramePosition.x + (kOverlaySize.x - levelUpTextWidth) * 0.5f,
		kLevelUpTextY,
		});
	const float textMaxWidth = (std::max)(
		1.0f,
		choiceHitboxSize_.x - kChoiceTextOffsetX - kChoiceTextRightPadding);
	for (size_t index = 0; index < choiceFrames_.size(); ++index) {
		const Vector2 position{
			slideOffsetX_ + choiceHitboxOffset_.x,
			choiceHitboxOffset_.y + choiceStepY_ * static_cast<float>(index),
		};
		if (choiceFrames_[index]) {
			choiceFrames_[index]->SetBorderLayout(
				position,
				choiceHitboxSize_,
				kChoiceCornerRadius,
				kChoiceBorder);
		}
		if (choiceBackgrounds_[index]) {
			choiceBackgrounds_[index]->SetLayout(
				position,
				choiceHitboxSize_,
				kChoiceCornerRadius);
		}
		const Vector2 iconFramePosition{
			position.x + 5.0f,
			position.y + 5.0f,
		};
		if (choiceIconFrames_[index]) {
			choiceIconFrames_[index]->SetBorderLayout(
				iconFramePosition,
				{ kIconFrameSize, kIconFrameSize },
				3.0f,
				kChoiceBorder);
		}
		const bool hasSubIcon = choiceSubIcons_[index].IsVisible();
		const float mainIconSize = hasSubIcon
			? kIconFrameSize - 18.0f
			: kIconFrameSize - kIconInset * 2.0f;
		const float mainIconInset = hasSubIcon ? 4.0f : kIconInset;
		choiceIcons_[index].SetPosition({
			iconFramePosition.x + mainIconInset,
			iconFramePosition.y + mainIconInset,
			});
		choiceIcons_[index].SetSize({
			mainIconSize,
			mainIconSize,
			});
		choiceSubIcons_[index].SetPosition({
			iconFramePosition.x + kIconFrameSize - kSubIconSize - 4.0f,
			iconFramePosition.y + kIconFrameSize - kSubIconSize - 4.0f,
			});
		choiceSubIcons_[index].SetSize({
			kSubIconSize,
			kSubIconSize,
			});
		choiceTitleTexts_[index].SetScaleToFit(
			kChoiceTitleBaseScale,
			textMaxWidth);
		choiceDetailTexts_[index].SetScaleToFit(
			kChoiceDetailBaseScale,
			textMaxWidth);
		choiceTitleTexts_[index].SetPosition({
			position.x + kChoiceTextOffsetX,
			position.y + 7.0f,
			});
		choiceDetailTexts_[index].SetPosition({
			position.x + kChoiceTextOffsetX + 1.0f,
			position.y + 38.0f,
			});
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
	if (GameInputBindings::IsGameInputSuppressedByImGui()) {
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
