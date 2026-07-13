#include "HpGauge.h"
#include "GameplayRules.h"
#include "DataPaths.h"
#include "UILayoutIO.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "Matrix4x4.h"
#include "MyMath.h"
#include "RenderingData.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d12.h>
#include <vector>
#ifdef _DEBUG
#include <imgui.h>
#endif

namespace {

constexpr float kFrameBorderSize = 3.0f;
constexpr char kWhiteTexturePath[] = "Resources/DirectXGame/white1x1.png";
const Vector4 kFrameColor{ 0.86f, 0.90f, 0.94f, 0.96f };
const Vector4 kBackgroundColor{ 0.10f, 0.08f, 0.08f, 0.92f };
const Vector4 kFillColor{ 0.92f, 0.04f, 0.03f, 0.96f };
constexpr int32_t kCapSegments = 10;

int32_t StepDisplayValue(int32_t displayedValue, int32_t targetValue)
{
	if (displayedValue < targetValue) {
		displayedValue += std::max<int32_t>(1, (targetValue - displayedValue) / 10);
	} else if (displayedValue > targetValue) {
		displayedValue -= std::max<int32_t>(1, (displayedValue - targetValue) / 10);
	}

	return displayedValue;
}

float CalculateGaugeRate(int32_t displayedValue, int32_t maxValue)
{
	return DirectXGame::GameplayRules::CalculateGaugeRate(
		displayedValue,
		maxValue);
}

}

namespace DirectXGame {

class HpGaugeCapsulePanel {
public:
	void Initialize(const Vector4& color)
	{
		spriteCommon_ = Engine::Graphics2D::SpriteCommon::GetInstance();
		const size_t vertexCapacity = 2 + (kCapSegments + 1) * 2;
		const size_t indexCapacity = (vertexCapacity - 1) * 3;
		vertexData_.resize(vertexCapacity);
		indexData_.resize(indexCapacity);

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

	void SetLayout(const Vector2& position, const Vector2& size)
	{
		position_ = position;
		size_ = size;
		geometryDirty_ = true;
	}

	void Draw()
	{
		if (size_.x <= 0.0f || size_.y <= 0.0f) {
			return;
		}

		UpdateGeometry();
		UpdateMatrices();
		const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
			spriteCommon_->GetDxCommon();

		const Engine::Base::DirectXCommon::FrameUploadAllocation materialAllocation =
			dxCommon->AllocateFrameUpload(
				sizeof(MaterialSprite),
				256);
		std::memcpy(
			materialAllocation.cpuAddress,
			&materialData_,
			sizeof(materialData_));
		const Engine::Base::DirectXCommon::FrameUploadAllocation transformAllocation =
			dxCommon->AllocateFrameUpload(
				sizeof(TransformationMatrixsprite),
				256);
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
			.SizeInBytes =
				static_cast<UINT>(sizeof(VertexData) * vertexData_.size()),
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
		commandList->SetGraphicsRootDescriptorTable(1, Engine::Base::TextureManager::GetInstance()->GetSrvHandleGPU(kWhiteTexturePath));
		commandList->SetGraphicsRootConstantBufferView(2, transformAllocation.gpuAddress);
		commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
	}

private:
	void UpdateGeometry()
	{
		if (!geometryDirty_) {
			return;
		}

		const float width = (std::max)(1.0f, size_.x);
		const float height = (std::max)(1.0f, size_.y);
		const float radius = height * 0.5f;
		const float rightCenterX = (std::max)(radius, width - radius);
		const float centerY = radius;
		constexpr float pi = 3.14159265358979323846f;

		uint32_t vertex = 0;
		vertexData_[vertex++] = MakeVertex({ width * 0.5f, centerY });
		for (int32_t i = 0; i <= kCapSegments; ++i) {
			const float angle = -pi * 0.5f + pi * static_cast<float>(i) / static_cast<float>(kCapSegments);
			vertexData_[vertex++] = MakeVertex({ rightCenterX + std::cos(angle) * radius, centerY + std::sin(angle) * radius });
		}
		for (int32_t i = 0; i <= kCapSegments; ++i) {
			const float angle = pi * 0.5f + pi * static_cast<float>(i) / static_cast<float>(kCapSegments);
			vertexData_[vertex++] = MakeVertex({ radius + std::cos(angle) * radius, centerY + std::sin(angle) * radius });
		}

		uint32_t index = 0;
		for (uint32_t i = 1; i + 1 < vertex; ++i) {
			indexData_[index++] = 0;
			indexData_[index++] = i;
			indexData_[index++] = i + 1;
		}
		indexData_[index++] = 0;
		indexData_[index++] = vertex - 1;
		indexData_[index++] = 1;

		indexCount_ = index;
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
		EulerTransform transform{
			{ 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ position_.x, position_.y, 0.0f },
		};
		Matrix4x4 world = MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		Matrix4x4 projection = MyMath::MakeOrthographicMatrix(
			0.0f, 0.0f, float(Engine::Base::WinApp::kClientWidth), float(Engine::Base::WinApp::kClientHeight), 0.0f, 100.0f);
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
	UINT indexCount_ = 0;
	bool geometryDirty_ = true;
};

HpGauge::HpGauge() = default;

HpGauge::~HpGauge() = default;

void HpGauge::Initialize()
{
	const UILayoutIO::LayoutMap layout = UILayoutIO::LoadOrDefault(DataPaths::kHudLayout, {});
	layoutSettings_.position = UILayoutIO::GetVector2(layout, "hpPosition", layoutSettings_.position);
	layoutSettings_.size = UILayoutIO::GetVector2(layout, "hpSize", layoutSettings_.size);

	frame_ = std::make_unique<HpGaugeCapsulePanel>();
	background_ = std::make_unique<HpGaugeCapsulePanel>();
	fill_ = std::make_unique<HpGaugeCapsulePanel>();
	frame_->Initialize(kFrameColor);
	background_->Initialize(kBackgroundColor);
	fill_->Initialize(kFillColor);
	ApplyLayout();
}

void HpGauge::Update()
{
	displayedHP_ = StepDisplayValue(displayedHP_, targetHP_);
	displayedRate_ = CalculateGaugeRate(displayedHP_, maxHP_);
	RefreshFillLayout();
}

void HpGauge::Draw()
{
	frame_->Draw();
	background_->Draw();
	if (displayedRate_ > 0.0f) {
		fill_->Draw();
	}
}

void HpGauge::SetHP(int32_t current, int32_t max)
{
	maxHP_ = std::max<int32_t>(1, max);
	targetHP_ = GameplayRules::ClampGaugeValue(current, max);
}

bool HpGauge::IsDepleted() const
{
	return displayedHP_ <= 0;
}

void HpGauge::DebugDrawImGui()
{
#ifdef _DEBUG
	if (!ImGui::CollapsingHeader("HUD HP", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Enable HUD Debug##HP", &layoutSettings_.debugEnabled);
	if (!layoutSettings_.debugEnabled) {
		return;
	}

	float position[2]{ layoutSettings_.position.x, layoutSettings_.position.y };
	if (ImGui::DragFloat2("HP Position", position, 1.0f, -400.0f, 1280.0f)) {
		layoutSettings_.position = { position[0], position[1] };
		ApplyLayout();
	}

	float size[2]{ layoutSettings_.size.x, layoutSettings_.size.y };
	if (ImGui::DragFloat2("HP Size", size, 1.0f, 4.0f, 512.0f)) {
		layoutSettings_.size = { size[0], size[1] };
		ApplyLayout();
	}

	if (ImGui::Button("Save HP Layout")) {
		SaveLayout();
	}
#endif
}

void HpGauge::SaveLayout() const
{
	UILayoutIO::Save(DataPaths::kHudLayout,
		{
			{ "hpPosition", { layoutSettings_.position.x, layoutSettings_.position.y } },
			{ "hpSize", { layoutSettings_.size.x, layoutSettings_.size.y } },
		});
}

void HpGauge::ApplyLayout()
{
	frame_->SetLayout(layoutSettings_.position, layoutSettings_.size);

	const Vector2 gaugePosition{
		layoutSettings_.position.x + kFrameBorderSize,
		layoutSettings_.position.y + kFrameBorderSize,
	};
	const Vector2 gaugeSize{
		(std::max)(1.0f, layoutSettings_.size.x - kFrameBorderSize * 2.0f),
		(std::max)(1.0f, layoutSettings_.size.y - kFrameBorderSize * 2.0f),
	};
	background_->SetLayout(gaugePosition, gaugeSize);
	RefreshFillLayout();
}

void HpGauge::RefreshFillLayout()
{
	const Vector2 gaugePosition{
		layoutSettings_.position.x + kFrameBorderSize,
		layoutSettings_.position.y + kFrameBorderSize,
	};
	const Vector2 gaugeSize{
		(std::max)(1.0f, layoutSettings_.size.x - kFrameBorderSize * 2.0f),
		(std::max)(1.0f, layoutSettings_.size.y - kFrameBorderSize * 2.0f),
	};
	const float fillWidth = gaugeSize.x * displayedRate_;
	fill_->SetLayout(gaugePosition, { fillWidth, gaugeSize.y });
}

}
