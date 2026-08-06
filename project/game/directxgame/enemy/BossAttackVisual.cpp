#include "BossAttackVisual.h"

#include "GameModelCache.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "ResourcePaths.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>

namespace {

constexpr char kEnvironmentTexturePath[] =
	"Resources/textures/skybox/test.dds";
constexpr size_t kBeamParticleCount = 48;
constexpr size_t kCircleParticleCount = 64;
constexpr size_t kShockwaveSegmentCount = 96;
constexpr float kSummonCrystalOffset = 12.0f;

Vector3 RotateDirectionXZ(const Vector3& direction, float radians)
{
	const float s = std::sin(radians);
	const float c = std::cos(radians);
	return {
		direction.x * c + direction.z * s,
		0.0f,
		-direction.x * s + direction.z * c,
	};
}

const char* ResolveTelegraphModelPath(const char* modelPath)
{
	if (!modelPath || !*modelPath) {
		return "cube.obj";
	}
	const std::filesystem::path fullPath =
		std::filesystem::path(DirectXGame::ResourcePaths::GetModelResourceRoot()) /
		modelPath;
	return std::filesystem::exists(fullPath) ? modelPath : "cube.obj";
}

void ConfigureObject(
	Engine::Graphics3D::Object3D& object,
	const char* modelPath = "cube.obj")
{
	object.Initialize(Engine::Graphics3D::Object3DCommon::GetInstance());
	DirectXGame::GameModelCache::ApplyToObject(
		object,
		DirectXGame::GameModelCache::Load(ResolveTelegraphModelPath(modelPath)));
	object.SetSkyboxFilePath(kEnvironmentTexturePath);
	object.SetLighting(false);
	object.SetCastsShadow(false);
	object.SetFrustumCullingEnabled(false);
	object.SetEnvironmentReflectionStrength(0.0f);
	object.SetEnvironmentRoughness(1.0f);
}

}

namespace DirectXGame {

BossRushTelegraph::BossRushTelegraph() = default;
BossRushTelegraph::~BossRushTelegraph() = default;

void BossRushTelegraph::EnsureInitialized(size_t count)
{
	for (size_t index = 0; index < count && index < objects_.size(); ++index) {
		if (objects_[index]) {
			continue;
		}
		objects_[index] = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*objects_[index]);
	}
}

void BossRushTelegraph::Update(const BossAttackTelegraph& telegraph)
{
	visible_ =
		telegraph.type == BossAttackType::Beam ||
		telegraph.type == BossAttackType::TripleBeam;
	if (!visible_) {
		visibleCount_ = 0;
		return;
	}
	const size_t beamCount = telegraph.type == BossAttackType::TripleBeam ? 3u : 1u;
	visibleCount_ = beamCount * kBeamParticleCount;
	EnsureInitialized(visibleCount_);
	const float range = (std::max)(1.0f, telegraph.range);
	const float baseAngle = std::atan2(telegraph.direction.x, telegraph.direction.z);
	size_t objectIndex = 0;
	for (size_t beamIndex = 0; beamIndex < beamCount; ++beamIndex) {
		const float angleOffset = beamCount == 3u
			? (static_cast<float>(beamIndex) - 1.0f) * 0.38f
			: 0.0f;
		const float angle = baseAngle + angleOffset;
		const Vector3 direction{ std::sin(angle), 0.0f, std::cos(angle) };
		for (size_t particle = 0; particle < kBeamParticleCount; ++particle) {
			const float t = (static_cast<float>(particle) + 0.5f) /
				static_cast<float>(kBeamParticleCount);
			const float pulse = 0.65f + 0.35f *
				std::sin((t * 8.0f + telegraph.progress * 5.0f) *
					std::numbers::pi_v<float>);
			const Vector3 center = telegraph.position + direction * (range * t);
			Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
			const float size = 0.16f + telegraph.progress * 0.10f + pulse * 0.055f;
			object->SetScale({ size, size, size });
			object->SetRotate({ 0.0f, angle, 0.0f });
			object->SetTranslate({ center.x, 0.22f + pulse * 0.05f, center.z });
			object->SetColor({
				1.0f,
				1.0f,
				1.0f,
				0.25f + telegraph.progress * 0.55f,
			});
			object->Update();
		}
	}
}

void BossRushTelegraph::Draw() const
{
	if (!visible_) {
		return;
	}
	for (size_t index = 0; index < visibleCount_; ++index) {
		if (objects_[index]) {
			Engine::Graphics3D::Object3D::SubmitForDraw(objects_[index].get());
		}
	}
}

void BossRushTelegraph::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!visible_) {
		return;
	}
	for (size_t index = 0; index < visibleCount_; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
		}
	}
}

BossAreaTelegraph::BossAreaTelegraph() = default;
BossAreaTelegraph::~BossAreaTelegraph() = default;

void BossAreaTelegraph::EnsureInitialized(size_t count)
{
	for (size_t index = 0; index < count && index < objects_.size(); ++index) {
		if (objects_[index]) {
			continue;
		}
		objects_[index] = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*objects_[index]);
	}
}

void BossAreaTelegraph::EnsureCrystals()
{
	for (std::unique_ptr<Engine::Graphics3D::Object3D>& object : crystalObjects_) {
		if (object) {
			continue;
		}
		object = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*object, "cube_world/crystal.glb");
	}
}

void BossAreaTelegraph::Update(const BossAttackTelegraph& telegraph)
{
	visible_ =
		telegraph.type == BossAttackType::LeapShockwave ||
		telegraph.type == BossAttackType::SummonLeapShockwave ||
		telegraph.type == BossAttackType::BulletHell ||
		telegraph.type == BossAttackType::ConvergingShockwave ||
		telegraph.type == BossAttackType::DomeBurst;
	if (!visible_) {
		visibleCount_ = 0;
		crystalVisibleCount_ = 0;
		return;
	}

	const size_t centerCount =
		telegraph.type == BossAttackType::SummonLeapShockwave ? 5u : 1u;
	const size_t ringLayers =
		telegraph.type == BossAttackType::DomeBurst ? 7u :
		(telegraph.type == BossAttackType::LeapShockwave ||
			telegraph.type == BossAttackType::SummonLeapShockwave ? 2u : 1u);
	visibleCount_ = (std::min)(
		objects_.size(),
		centerCount * kCircleParticleCount * ringLayers);
	EnsureInitialized(visibleCount_);
	const float baseRadius = (std::max)(1.0f, telegraph.range);
	const Vector4 color = { 1.0f, 1.0f, 1.0f, 0.25f + telegraph.progress * 0.55f };
	size_t objectIndex = 0;
	for (size_t centerIndex = 0; centerIndex < centerCount; ++centerIndex) {
		Vector3 center = telegraph.type == BossAttackType::BulletHell ||
			telegraph.type == BossAttackType::DomeBurst
			? telegraph.position
			: telegraph.targetPosition;
		if (telegraph.type == BossAttackType::SummonLeapShockwave && centerIndex > 0) {
			const float angle = 2.0f * std::numbers::pi_v<float> *
				static_cast<float>(centerIndex - 1) / 4.0f + 0.785f;
			center.x += std::sin(angle) * kSummonCrystalOffset;
			center.z += std::cos(angle) * kSummonCrystalOffset;
		}
		for (size_t layer = 0; layer < ringLayers; ++layer) {
			const bool domeBurst = telegraph.type == BossAttackType::DomeBurst;
			const float layerRate = domeBurst
				? (static_cast<float>(layer) + 1.0f) / static_cast<float>(ringLayers)
				: (ringLayers == 1u
					? 1.0f
					: (0.55f + 0.45f * std::fmod(
						telegraph.progress + static_cast<float>(layer) * 0.5f,
						1.0f)));
			const float domeGrowth = 0.45f + telegraph.progress * 0.95f;
			const float ringGrowth = telegraph.type == BossAttackType::ConvergingShockwave
				? (1.0f - telegraph.progress * 0.35f)
				: (telegraph.type == BossAttackType::BulletHell
					? (0.70f + telegraph.progress * 0.25f)
					: 1.0f);
			const float layerRadius = baseRadius *
				(domeBurst ? domeGrowth * layerRate : layerRate * ringGrowth);
			const float layerHeight = domeBurst
				? std::sin(layerRate * std::numbers::pi_v<float> * 0.5f) *
					baseRadius * domeGrowth * 0.62f
				: 0.0f;
			for (size_t segment = 0; segment < kCircleParticleCount && objectIndex < objects_.size(); ++segment) {
				const float angle = 2.0f * std::numbers::pi_v<float> *
					static_cast<float>(segment) / static_cast<float>(kCircleParticleCount);
				const float sparkle = 0.55f + 0.45f *
					std::sin((static_cast<float>(segment) * 0.65f + telegraph.progress * 6.0f) *
						std::numbers::pi_v<float>);
				const Vector3 segmentPosition{
					center.x + std::sin(angle) * layerRadius,
					0.12f + layerHeight,
					center.z + std::cos(angle) * layerRadius,
				};
				Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
				const float particleSize = domeBurst
					? 0.42f + layerRate * 0.24f + sparkle * 0.12f
					: 0.26f + sparkle * 0.12f;
				object->SetScale({
					particleSize,
					particleSize,
					particleSize,
				});
				object->SetRotate({ 0.0f, angle, 0.0f });
				object->SetTranslate(segmentPosition);
				object->SetColor(color);
				object->Update();
			}
		}
	}

	crystalVisibleCount_ = telegraph.type == BossAttackType::SummonLeapShockwave ? 4u : 0u;
	if (crystalVisibleCount_ > 0) {
		EnsureCrystals();
		for (size_t index = 0; index < crystalVisibleCount_; ++index) {
			const float angle = 2.0f * std::numbers::pi_v<float> *
				static_cast<float>(index) / static_cast<float>(crystalVisibleCount_) + 0.785f;
			const float pulse = 0.5f + 0.5f *
				std::sin((telegraph.progress * 4.0f + static_cast<float>(index) * 0.2f) *
					std::numbers::pi_v<float>);
			Engine::Graphics3D::Object3D* object = crystalObjects_[index].get();
			object->SetScale({ 3.3f + pulse * 0.35f, 3.3f + pulse * 0.35f, 3.3f + pulse * 0.35f });
			object->SetRotate({ 0.0f, telegraph.progress * 4.8f + angle, 0.0f });
			object->SetTranslate({
				telegraph.targetPosition.x + std::sin(angle) * kSummonCrystalOffset,
				0.2f + pulse * 0.35f,
				telegraph.targetPosition.z + std::cos(angle) * kSummonCrystalOffset,
			});
			object->SetColor({ 0.65f, 0.95f, 1.0f, 1.0f });
			object->Update();
		}
	}
}

void BossAreaTelegraph::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!visible_) {
		return;
	}
	for (size_t index = 0; index < visibleCount_; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
		}
	}
	for (size_t index = 0; index < crystalVisibleCount_; ++index) {
		if (crystalObjects_[index]) {
			objects.push_back(crystalObjects_[index].get());
		}
	}
}

BossBeamBurst::BossBeamBurst() = default;
BossBeamBurst::~BossBeamBurst() = default;

void BossBeamBurst::EnsureInitialized(size_t count)
{
	for (size_t index = 0; index < count && index < objects_.size(); ++index) {
		if (objects_[index]) {
			continue;
		}
		objects_[index] = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*objects_[index]);
	}
}

void BossBeamBurst::Initialize(
	const Vector3& position,
	const Vector3& direction,
	int32_t beamCount,
	float length)
{
	position_ = position;
	direction_ = direction;
	const float directionLength = std::sqrt(direction_.x * direction_.x + direction_.z * direction_.z);
	if (directionLength > 0.001f) {
		direction_.x /= directionLength;
		direction_.z /= directionLength;
	}
	visibleCount_ =
		static_cast<size_t>(std::clamp(beamCount, 1, 3)) * kBeamParticleCount;
	length_ = (std::max)(12.0f, length);
	EnsureInitialized(visibleCount_);
	elapsedTime_ = 0.0f;
	active_ = true;
	Update(0.0f);
}

bool BossBeamBurst::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	constexpr float kDuration = 0.34f;
	if (elapsedTime_ >= kDuration) {
		active_ = false;
		return false;
	}
	const float progress = std::clamp(elapsedTime_ / kDuration, 0.0f, 1.0f);
	const float alpha = (1.0f - progress) * 0.95f;
	const float beamLength = length_;
	const float baseAngle = std::atan2(direction_.x, direction_.z);
	const size_t beamCount = (std::max)(size_t{ 1 }, visibleCount_ / kBeamParticleCount);
	size_t objectIndex = 0;
	for (size_t index = 0; index < beamCount; ++index) {
		const float angleOffset = beamCount == 3u
			? (static_cast<float>(index) - 1.0f) * 0.38f
			: 0.0f;
		const float angle = baseAngle + angleOffset;
		const Vector3 direction = RotateDirectionXZ(direction_, angleOffset);
		for (size_t particle = 0; particle < kBeamParticleCount && objectIndex < objects_.size(); ++particle) {
			const float t = (static_cast<float>(particle) + 0.5f) /
				static_cast<float>(kBeamParticleCount);
			const float sideWave = std::sin((t * 10.0f + progress * 5.0f) *
				std::numbers::pi_v<float>);
			const Vector3 center = position_ + direction * (beamLength * t);
			Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
			const float corePulse = 1.0f - std::abs(0.5f - t) * 0.35f;
			const float size = 0.42f + progress * 0.18f + std::abs(sideWave) * 0.10f + corePulse * 0.08f;
			object->SetScale({ size, size, size });
			object->SetRotate({ 0.0f, angle, 0.0f });
			object->SetTranslate({
				center.x,
				0.85f + std::abs(sideWave) * 0.32f,
				center.z,
			});
			object->SetColor({ 0.25f, 0.86f, 1.0f, alpha });
			object->Update();
		}
	}
	return true;
}

void BossBeamBurst::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!active_) {
		return;
	}
	for (size_t index = 0; index < visibleCount_; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
		}
	}
}

BossShockwaveRing::BossShockwaveRing() = default;
BossShockwaveRing::~BossShockwaveRing() = default;

void BossShockwaveRing::EnsureInitialized()
{
	for (std::unique_ptr<Engine::Graphics3D::Object3D>& object : objects_) {
		if (object) {
			continue;
		}
		object = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*object);
	}
}

void BossShockwaveRing::Initialize(
	const Vector3& center,
	float radius,
	float delay,
	const Vector4& color)
{
	center_ = center;
	radius_ = (std::max)(1.0f, radius);
	delay_ = (std::max)(0.0f, delay);
	color_ = color;
	elapsedTime_ = 0.0f;
	active_ = true;
	EnsureInitialized();
}

bool BossShockwaveRing::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	if (elapsedTime_ < delay_) {
		return true;
	}
	constexpr float kDuration = 2.4f;
	const float localTime = elapsedTime_ - delay_;
	if (localTime >= kDuration) {
		active_ = false;
		return false;
	}
	const float progress = std::clamp(localTime / kDuration, 0.0f, 1.0f);
	const float currentRadius = radius_ * progress;
	const float alpha = (1.0f - progress) * color_.w;
	for (size_t index = 0; index < objects_.size(); ++index) {
		const float angle = 2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) / static_cast<float>(objects_.size());
		Engine::Graphics3D::Object3D* object = objects_[index].get();
		const float sparkle = 0.55f + 0.45f *
			std::sin((static_cast<float>(index) * 0.45f + progress * 5.0f) *
				std::numbers::pi_v<float>);
		const float particleSize = 0.48f + progress * 0.55f + sparkle * 0.18f;
		object->SetScale({ particleSize, particleSize, particleSize });
		object->SetRotate({ 0.0f, angle, 0.0f });
		object->SetTranslate({
			center_.x + std::sin(angle) * currentRadius,
			0.18f + sparkle * 0.06f,
			center_.z + std::cos(angle) * currentRadius,
		});
		object->SetColor({ color_.x, color_.y, color_.z, alpha });
		object->Update();
	}
	return true;
}

void BossShockwaveRing::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!active_ || elapsedTime_ < delay_) {
		return;
	}
	for (const std::unique_ptr<Engine::Graphics3D::Object3D>& object : objects_) {
		if (object) {
			objects.push_back(object.get());
		}
	}
}

BossSummonCrystal::BossSummonCrystal() = default;
BossSummonCrystal::~BossSummonCrystal() = default;

void BossSummonCrystal::Initialize(const Vector3& position, float delay)
{
	position_ = position;
	delay_ = (std::max)(0.0f, delay);
	elapsedTime_ = 0.0f;
	active_ = true;
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	ConfigureObject(*object_, "cube_world/crystal.glb");
	object_->SetScale({ 3.6f, 3.6f, 3.6f });
	object_->SetTranslate({ position_.x, 0.25f, position_.z });
	object_->SetColor({ 0.65f, 0.95f, 1.0f, 1.0f });
	object_->Update();
}

bool BossSummonCrystal::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	constexpr float kDuration = 0.72f;
	if (elapsedTime_ >= delay_ + kDuration) {
		active_ = false;
		return false;
	}
	const float localTime = (std::max)(0.0f, elapsedTime_ - delay_);
	const float progress = std::clamp(localTime / kDuration, 0.0f, 1.0f);
	const float pulse = 0.5f + 0.5f * std::sin(localTime * 12.0f);
	const float burstScale = 1.0f + progress * progress * 1.2f;
	if (object_) {
		const float scale = (3.6f + pulse * 0.45f) * burstScale;
		object_->SetScale({ scale, scale, scale });
		object_->SetRotate({ 0.0f, localTime * 1.8f, 0.0f });
		object_->SetTranslate({ position_.x, 0.25f + pulse * 0.28f, position_.z });
		object_->SetColor({ 0.65f, 0.95f, 1.0f, 1.0f });
		object_->Update();
	}
	return true;
}

BossSlamCube::BossSlamCube() = default;
BossSlamCube::~BossSlamCube() = default;

void BossSlamCube::Initialize(const Vector3& position, float delay)
{
	position_ = position;
	delay_ = (std::max)(0.0f, delay);
	elapsedTime_ = 0.0f;
	active_ = true;
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	ConfigureObject(*object_);
	object_->SetScale({ 1.5f, 3.2f, 1.5f });
	object_->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
	object_->SetTranslate({ position_.x, -3.2f, position_.z });
	object_->Update();
}

bool BossSlamCube::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	if (elapsedTime_ < delay_) {
		return true;
	}
	constexpr float kRiseDuration = 0.18f;
	constexpr float kHoldDuration = 0.22f;
	constexpr float kFallDuration = 0.2f;
	const float localTime = elapsedTime_ - delay_;
	if (localTime >= kRiseDuration + kHoldDuration + kFallDuration) {
		active_ = false;
		return false;
	}
	float heightProgress = 1.0f;
	if (localTime < kRiseDuration) {
		heightProgress = localTime / kRiseDuration;
	} else if (localTime > kRiseDuration + kHoldDuration) {
		heightProgress = 1.0f -
			(localTime - kRiseDuration - kHoldDuration) / kFallDuration;
	}
	heightProgress = std::clamp(heightProgress, 0.0f, 1.0f);
	object_->SetTranslate({ position_.x, -3.2f + heightProgress * 4.9f, position_.z });
	object_->Update();
	return true;
}

void BossSlamCube::Draw() const
{
	if (active_ && object_ && elapsedTime_ >= delay_) {
		Engine::Graphics3D::Object3D::SubmitForDraw(object_.get());
	}
}

} // namespace DirectXGame
