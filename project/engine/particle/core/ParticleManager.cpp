#include "ParticleManager.h"
#include "DirectXCommon.h"
#include "GraphicsPipeline.h"
#include "SrvManager.h"
#include <ModelManager.h>
#include <TextureManager.h>
#include "CameraManager.h"
#include <MyMath.h>
#include <algorithm>
#include <numbers>
#include <imgui.h>

namespace {
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float kQuadHalfSize = 0.5f;
const Vector3 kDefaultParticleNormal = { 0.0f, 0.0f, 1.0f };
const Vector4 kInitialParticleColor = { 1.0f, 1.0f, 1.0f, 0.0f };
}

namespace Engine::Particle {

ParticleManager::~ParticleManager() = default;

//シングルトンインスタンスの取得
ParticleManager* ParticleManager::GetInstance()
{
	static ParticleManager instance;
	return &instance;
}

void ParticleManager::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{
	//引数で受け取ったポインタをメンバ変数に代入
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;

	// エミット時のばらつきに使う乱数エンジンは初期化時に 1 度だけ用意する
	std::random_device seedGenerator;
	std::mt19937 random(seedGenerator());
	randomEngine = random;

	// 全グループ共通で使うパーティクル用パイプラインを生成する
	graphicsPipeline_ = std::make_unique<Engine::Base::GraphicsPipeline>();
	graphicsPipeline_->Initialize(dxCommon_);
	graphicsPipeline_->CreateParticle();
}

void ParticleManager::Finalize()
{
	graphicsPipeline_.reset();
	particleGroups.clear();
	model_ = nullptr;
	dxCommon_ = nullptr;
	srvManager_ = nullptr;
}

void ParticleManager::Update()
{
	Update(kFixedParticleDeltaTime);
}

void ParticleManager::Update(float deltaTime)
{
	Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!activeCamera) {
		return;
	}
	const float appliedDeltaTime = ResolveDeltaTime(deltaTime);
	lastAppliedDeltaTime_ = appliedDeltaTime;

	Matrix4x4 viewMatrix = activeCamera->GetViewMatrix();
	Matrix4x4 projectionMatrix = activeCamera->GetProjectionMatrix();

	for (auto& particleGroupEntry : particleGroups) {
		UpdateParticleGroup(particleGroupEntry.second, appliedDeltaTime, viewMatrix, projectionMatrix);
	}
}

float ParticleManager::ResolveDeltaTime(float deltaTime) const
{
	if (useFixedDeltaTime_) {
		return kFixedParticleDeltaTime;
	}
	return std::clamp(deltaTime, 0.0f, kMaxParticleDeltaTime);
}

void ParticleManager::UpdateParticleGroup(ParticleGroup& particleGroup, float deltaTime, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
	auto& behavior = particleGroup.behavior;
	if (!behavior) {
		particleGroup.instanceCount = 0;
		return;
	}
	uint32_t counter = 0;
	size_t writeIndex = 0;
	for (size_t readIndex = 0; readIndex < particleGroup.particles.size(); ++readIndex) {
		Particle& particle = particleGroup.particles[readIndex];
		if (particle.lifetime <= particle.currentTime) {
			continue;
		}

		behavior->Update(particle, deltaTime, particleGroup.materialData);
		UpdateAliveParticle(particle, particleGroup, counter, viewMatrix, projectionMatrix);
		if (writeIndex != readIndex) {
			particleGroup.particles[writeIndex] = particle;
		}
		++writeIndex;
	}
	particleGroup.particles.resize(writeIndex);

	// このフレームに実際に生存していた数だけ描画数へ反映する
	particleGroup.instanceCount = counter;
}

void ParticleManager::UpdateAliveParticle(Particle& particle, ParticleGroup& particleGroup, uint32_t& counter, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
	float alpha = 1.0f - (particle.currentTime / particle.lifetime);
	Matrix4x4 rotateMatrix = MyMath::MakeRotateMatrix(particle.transform.rotate);
	Matrix4x4 worldMatrix = MyMath::MakeScaleMatrix(particle.transform.scale) * rotateMatrix * MyMath::MakeTranslateMatrix(particle.transform.translate);
	Matrix4x4 worldViewProjectionMatrix = worldMatrix * viewMatrix * projectionMatrix;

	// 生存中インスタンスだけを前から詰めて書き込み、残りは描画対象から外す
	if (counter < particleGroup.maxInstanceCount) {
		particleGroup.instanceData[counter].WVP = worldViewProjectionMatrix;
		particleGroup.instanceData[counter].World = worldMatrix;
		particleGroup.instanceData[counter].color = particle.color;
		particleGroup.instanceData[counter].color.w = alpha;
		++counter;
	}
}

void ParticleManager::Draw()
{
	lastDrawCallCount_ = 0;
	lastDrawnInstanceCount_ = 0;

	// パーティクルグループが設定されていない場合は描画しない
	if (particleGroups.empty()) {
		return;
	}

	//ルートシグネチャを設定
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(graphicsPipeline_->GetRootSignatureParticle());
	//psoを設定
	dxCommon_->GetCommandList()->SetPipelineState(graphicsPipeline_->GetGraphicsPipelineStateParticle());
	// primitive topology を設定
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// グループごとに頂点バッファ、material、instance SRV、texture SRV を切り替えて描画する
	for (const auto& particleGroupEntry : particleGroups) {
		const auto& particleGroup = particleGroupEntry.second;
		if (particleGroup.instanceCount == 0) {
			continue;
		}

		dxCommon_->GetCommandList()->IASetVertexBuffers(0, 1, &particleGroup.vertexBufferView);
		dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(0, particleGroup.materialResource->GetGPUVirtualAddress());
		srvManager_->SetGraphicsRootDescriptorTable(2, particleGroup.materialdata.textureIndex);
		srvManager_->SetGraphicsRootDescriptorTable(1, particleGroup.srvIndex);
		dxCommon_->GetCommandList()->DrawInstanced(UINT(particleGroup.vertexCount), particleGroup.instanceCount, 0, 0);
		++lastDrawCallCount_;
		lastDrawnInstanceCount_ += particleGroup.instanceCount;
	}
}

void ParticleManager::CreateParticleGroup(
	const std::string& name,
	const std::string& textureFilePath,
	VerticesType verticesType,
	std::unique_ptr<IParticleBehavior> behavior,
	uint32_t maxInstanceCount)
{
	//登録済みなら早期リターン
	if (particleGroups.contains(name)) {
		return;
	}

	// 先に空グループを登録して以後の初期化を at(name) で揃える
	ParticleGroup particleGroup;
	particleGroup.debugName = name;
	particleGroup.maxInstanceCount = (std::max)(1u, maxInstanceCount);
	particleGroups.insert(std::make_pair(name, std::move(particleGroup)));

	ParticleGroup& targetGroup = particleGroups.at(name);
	InitializeParticleGroupMaterial(targetGroup);
	InitializeParticleGroupVertices(targetGroup, verticesType);
	InitializeParticleGroupTexture(targetGroup, textureFilePath);
	InitializeParticleGroupInstances(targetGroup);

	// Strategy として振る舞いを保持し、Emit/Update 時に差し替えて使う
	targetGroup.behavior = std::move(behavior);
}

void ParticleManager::InitializeParticleGroupMaterial(ParticleGroup& particleGroup)
{
	particleGroup.materialResource = dxCommon_->CreateBufferResource(sizeof(Material));
	particleGroup.materialData = nullptr;
	particleGroup.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&particleGroup.materialData));
	particleGroup.materialData->color = { Vector4(1.0f, 1.0f, 1.0f, 1.0f) };
	particleGroup.materialData->enableLighting = false;
	particleGroup.materialData->uvTransform = particleGroup.materialData->uvTransform.MakeIdentity4x4();
}

void ParticleManager::InitializeParticleGroupVertices(ParticleGroup& particleGroup, VerticesType verticesType)
{
	std::vector<VertexData> vertices = MakeCylinderVertices();
	switch (verticesType) {
	case VerticesType::Quad:
		vertices = MakeQuadVertices();
		break;
	case VerticesType::Ring:
		vertices = MakeRingVertices();
		break;
	case VerticesType::Cylinder:
		vertices = MakeCylinderVertices();
		break;
	}

	particleGroup.vertexCount = static_cast<uint32_t>(vertices.size());
	particleGroup.vertexResource = dxCommon_->CreateBufferResource(sizeof(VertexData) * vertices.size());
	particleGroup.vertexBufferView.BufferLocation = particleGroup.vertexResource->GetGPUVirtualAddress();
	particleGroup.vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	particleGroup.vertexBufferView.StrideInBytes = sizeof(VertexData);

	VertexData* vertexData = nullptr;
	particleGroup.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, vertices.data(), sizeof(VertexData) * vertices.size());
	particleGroup.vertexResource->Unmap(0, nullptr);
}

void ParticleManager::InitializeParticleGroupTexture(ParticleGroup& particleGroup, const std::string& textureFilePath)
{
	particleGroup.materialdata.textureFilePath = textureFilePath;
	Engine::Base::TextureManager::GetInstance()->LoadTexture(textureFilePath);
	particleGroup.materialdata.textureIndex = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath);
}

void ParticleManager::InitializeParticleGroupInstances(ParticleGroup& particleGroup)
{
	particleGroup.instanceCount = 0;
	particleGroup.instanceResource = dxCommon_->CreateBufferResource(sizeof(ParticleForGPU) * particleGroup.maxInstanceCount);
	particleGroup.instanceResource->Map(0, nullptr, reinterpret_cast<void**>(&particleGroup.instanceData));

	ParticleForGPU particleForGPU;
	particleForGPU.WVP = particleForGPU.WVP.MakeIdentity4x4();
	particleForGPU.World = particleForGPU.World.MakeIdentity4x4();
	particleForGPU.color = kInitialParticleColor;
	for (uint32_t index = 0; index < particleGroup.maxInstanceCount; ++index) {
		particleGroup.instanceData[index] = particleForGPU;
	}

	particleGroup.srvIndex = srvManager_->Allocate();
	srvManager_->CreateSRVforStructuredBuffer(
		particleGroup.srvIndex,
		particleGroup.instanceResource.Get(),
		particleGroup.maxInstanceCount,
		sizeof(ParticleForGPU));
}

void ParticleManager::Emit(const std::string& name, const Vector3& position, uint32_t count)
{
	assert(particleGroups.contains(name));
	if (!particleGroups.at(name).behavior) {
		return;
	}

	// 振る舞いオブジェクトに生成を委譲して、必要数だけパーティクルを積む
	for (uint32_t i = 0; i < count; ++i) {
		particleGroups.at(name).particles.push_back(particleGroups.at(name).behavior->Create(randomEngine, position));
	}

	// 次の Update までの暫定描画数として今回追加分を記録する
	particleGroups.at(name).instanceCount = (std::min)(count, particleGroups.at(name).maxInstanceCount);
}

void ParticleManager::SetModel(const std::string& filepath)
{
	// モデルを検索してセットする
	model_ = Engine::Graphics3D::ModelManager::GetInstance()->FindModel(filepath);
}

std::vector<VertexData> ParticleManager::MakeRingVertices(uint32_t RingDivide, float outerRadius, float innerRadius)
{
	std::vector<VertexData> ringVertices;
	const float radianPerDivide = kTwoPi / static_cast<float>(RingDivide);

	// リングは内外 2 本の円を 2 三角形ずつでつないで帯状メッシュにする
	for (uint32_t index = 0; index < RingDivide; ++index) {
		float angle = index * radianPerDivide;
		float nextAngle = ((index + 1) % RingDivide) * radianPerDivide;

		float sin = std::sinf(angle);
		float cos = std::cosf(angle);
		float sinnext = std::sinf(nextAngle);
		float cosnext = std::cosf(nextAngle);

		float u = (static_cast<float>(index) / RingDivide);
		float unext = (static_cast<float>(index + 1) / RingDivide);

		VertexData v[] = {
			{ {-sin * outerRadius,  cos * outerRadius,  0.0f, 1.0f},     {u,     0.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sin * innerRadius,  cos * innerRadius,  0.0f, 1.0f},     {u,     1.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sinnext * outerRadius, cosnext * outerRadius, 0.0f, 1.0f}, {unext, 0.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sinnext * outerRadius, cosnext * outerRadius, 0.0f, 1.0f}, {unext, 0.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sin * innerRadius,  cos * innerRadius,  0.0f, 1.0f},     {u,     1.0f}, {0.0f, 0.0f, 1.0f} },
			{ {-sinnext * innerRadius, cosnext * innerRadius, 0.0f, 1.0f}, {unext, 1.0f}, {0.0f, 0.0f, 1.0f} }
		};

		for (const auto& vert : v) {
			ringVertices.push_back(vert);
		}
	}

	return ringVertices;
}

std::vector<VertexData> ParticleManager::MakeCylinderVertices(uint32_t cylinderDivide, float topRadius, float bottomRadius, float height)
{
	const float radianPerDivide = kTwoPi / static_cast<float>(cylinderDivide);
	std::vector<VertexData> cylinderVertices;

	// 円周方向の各分割を 2 三角形に分けて側面だけを構成する
	for (uint32_t index = 0; index < cylinderDivide; ++index) {
		float sin = std::sinf(index * radianPerDivide);
		float cos = std::cosf(index * radianPerDivide);
		float sinnext = std::sinf((index + 1) * radianPerDivide);
		float cosnext = std::cosf((index + 1) * radianPerDivide);
		float u = float(index) / float(cylinderDivide);
		float unext = float(index + 1) / float(cylinderDivide);

		VertexData v[] = {
			{{-sin * topRadius, height, cos * topRadius, 1.0f},              {u, 0.0f},     {-sin, 0.0f, cos}},
			{{-sinnext * topRadius, height, cosnext * topRadius, 1.0f},      {unext, 0.0f}, {-sinnext, 0.0f, cosnext}},
			{{-sin * bottomRadius, 0.0f, cos * bottomRadius, 1.0f},          {u, 1.0f},     {-sin, 0.0f, cos}},
			{{-sinnext * topRadius, height, cosnext * topRadius, 1.0f},      {unext, 0.0f}, {-sinnext, 0.0f, cosnext}},
			{{-sinnext * bottomRadius, 0.0f, cosnext * bottomRadius, 1.0f},  {unext, 1.0f}, {-sinnext, 0.0f, cosnext}},
			{{-sin * bottomRadius, 0.0f, cos * bottomRadius, 1.0f},          {u, 1.0f},     {-sin, 0.0f, cos}}
		};

		for (const auto& vert : v) {
			cylinderVertices.push_back(vert);
		}
	}

	return cylinderVertices;
}

std::vector<VertexData> ParticleManager::MakeQuadVertices()
{
	std::vector<VertexData> vertices = {
		{{-kQuadHalfSize, -kQuadHalfSize, 0.0f, 1.0f}, {0.0f, 1.0f}, kDefaultParticleNormal},
		{{-kQuadHalfSize,  kQuadHalfSize, 0.0f, 1.0f}, {0.0f, 0.0f}, kDefaultParticleNormal},
		{{ kQuadHalfSize, -kQuadHalfSize, 0.0f, 1.0f}, {1.0f, 1.0f}, kDefaultParticleNormal},
		{{ kQuadHalfSize, -kQuadHalfSize, 0.0f, 1.0f}, {1.0f, 1.0f}, kDefaultParticleNormal},
		{{-kQuadHalfSize,  kQuadHalfSize, 0.0f, 1.0f}, {0.0f, 0.0f}, kDefaultParticleNormal},
		{{ kQuadHalfSize,  kQuadHalfSize, 0.0f, 1.0f}, {1.0f, 0.0f}, kDefaultParticleNormal},
	};
	return vertices;
}

void ParticleManager::SetBehavior(const std::string& groupName, std::unique_ptr<IParticleBehavior> behavior)
{
	assert(particleGroups.contains(groupName) && "ParticleGroup does not exist!");
	particleGroups.at(groupName).behavior = std::move(behavior);
}

size_t ParticleManager::GetTotalActiveParticleCount() const
{
	size_t total = 0;
	for (const auto& particleGroupEntry : particleGroups) {
		total += particleGroupEntry.second.particles.size();
	}
	return total;
}

std::optional<size_t> ParticleManager::GetActiveParticleCount(const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.particles.size();
}

std::optional<uint32_t> ParticleManager::GetParticleGroupMaxInstanceCount(const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.maxInstanceCount;
}

void ParticleManager::SetParticleGroupDebugName(const std::string& groupName, const std::string& debugName)
{
	auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return;
	}
	it->second.debugName = debugName;
}

std::optional<std::string> ParticleManager::GetParticleGroupDebugName(const std::string& groupName) const
{
	const auto it = particleGroups.find(groupName);
	if (it == particleGroups.end()) {
		return std::nullopt;
	}
	return it->second.debugName;
}

}
