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
constexpr char kBossLaserTorusModelPath[] =
	"boss_laser_torus/boss_laser_torus.obj";
constexpr size_t kBeamParticleCount = 52;
constexpr size_t kCircleParticleCount = 64;
constexpr size_t kShockwaveSegmentCount = 96;
constexpr float kSummonCrystalOffset = 28.0f;
constexpr float kBossSummonCrystalScale = 420.0f;
constexpr float kBossSummonCrystalY = 0.12f;
constexpr int32_t kSummonCrystalLingerFrames = 180;
constexpr float kBeamTelegraphY = 4.15f;
constexpr float kBeamBodyY = 4.35f;
constexpr float kBeamExplosionY = 4.45f;

float ClampFinite(float value, float fallback, float minimum, float maximum)
{
	if (!std::isfinite(value)) {
		return fallback;
	}
	return std::clamp(value, minimum, maximum);
}

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
	visibleCount_ = (std::min)(objects_.size(), beamCount * kBeamParticleCount);
	EnsureInitialized(visibleCount_);
	const float range = (std::max)(1.0f, telegraph.range);
	const float baseAngle = std::atan2(telegraph.direction.x, telegraph.direction.z);
	size_t objectIndex = 0;
	for (size_t beamIndex = 0; beamIndex < beamCount; ++beamIndex) {
		const float angleOffset = beamCount == 3u
			? (static_cast<float>(beamIndex) - 1.0f) * 0.52f
			: 0.0f;
		const float angle = baseAngle + angleOffset;
		const Vector3 direction{ std::sin(angle), 0.0f, std::cos(angle) };
		for (size_t particle = 0; particle < kBeamParticleCount; ++particle) {
			if (objectIndex >= objects_.size()) {
				break;
			}
			const float t = (static_cast<float>(particle) + 0.5f) /
				static_cast<float>(kBeamParticleCount);
			const float pulse = 0.62f + 0.38f *
				std::sin((t * 10.0f + telegraph.progress * 6.0f) *
					std::numbers::pi_v<float>);
			const Vector3 center = telegraph.position + direction * (range * t);
			Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
			const float size = 0.22f + telegraph.progress * 0.12f + pulse * 0.08f;
			object->SetScale({ size, size, size });
			object->SetRotate({ 0.0f, angle, 0.0f });
			object->SetTranslate({ center.x, kBeamTelegraphY + pulse * 0.08f, center.z });
			object->SetColor({ 1.0f, 1.0f, 1.0f, 0.55f + telegraph.progress * 0.42f });
			object->Update();
		}
	}
}

void BossRushTelegraph::Draw() const
{
	if (!visible_) {
		return;
	}
	const size_t count = (std::min)(visibleCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
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
	const size_t count = (std::min)(visibleCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
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
		object->SetCastsShadow(true);
	}
}

void BossAreaTelegraph::LockCrystalPositions(const Vector3& targetPosition)
{
	if (crystalPositionsLocked_) {
		return;
	}
	for (size_t index = 0; index < crystalPositions_.size(); ++index) {
		const float angle = 2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) / static_cast<float>(crystalPositions_.size()) +
			0.785f;
		crystalPositions_[index] = {
			targetPosition.x + std::sin(angle) * kSummonCrystalOffset,
			kBossSummonCrystalY,
			targetPosition.z + std::cos(angle) * kSummonCrystalOffset,
		};
	}
	crystalPositionsLocked_ = true;
}

void BossAreaTelegraph::Update(const BossAttackTelegraph& telegraph)
{
	visible_ =
		telegraph.type == BossAttackType::LeapShockwave ||
		telegraph.type == BossAttackType::SummonLeapShockwave ||
		telegraph.type == BossAttackType::BulletHell ||
		telegraph.type == BossAttackType::DomeBurst;
	if (!visible_) {
		visibleCount_ = 0;
		if (crystalLingerFrames_ > 0) {
			--crystalLingerFrames_;
			crystalVisibleCount_ = 4u;
			visible_ = true;
		} else {
			crystalVisibleCount_ = 0;
			crystalPositionsLocked_ = false;
		}
		return;
	}

	const bool leapShockwave =
		telegraph.type == BossAttackType::LeapShockwave ||
		telegraph.type == BossAttackType::SummonLeapShockwave;
	const size_t centerCount =
		telegraph.type == BossAttackType::SummonLeapShockwave ? 5u : 1u;
	if (telegraph.type == BossAttackType::SummonLeapShockwave) {
		LockCrystalPositions(telegraph.targetPosition);
	}
	const size_t ringLayers =
		telegraph.type == BossAttackType::DomeBurst ? 2u :
		(leapShockwave ? 1u : 1u);
	visibleCount_ = (std::min)(
		objects_.size(),
		centerCount * kCircleParticleCount * ringLayers);
	EnsureInitialized(visibleCount_);
	const float baseRadius = (std::max)(1.0f, telegraph.range);
	const Vector4 color = leapShockwave
		? Vector4{ 1.0f, 1.0f, 1.0f, 0.48f + telegraph.progress * 0.42f }
		: (telegraph.type == BossAttackType::BulletHell
			? Vector4{ 0.0f, 0.92f, 1.0f, 0.36f + telegraph.progress * 0.56f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 0.25f + telegraph.progress * 0.55f });
	size_t objectIndex = 0;
	for (size_t centerIndex = 0; centerIndex < centerCount; ++centerIndex) {
		Vector3 center = telegraph.type == BossAttackType::BulletHell ||
			telegraph.type == BossAttackType::DomeBurst
			? telegraph.position
			: telegraph.targetPosition;
		if (telegraph.type == BossAttackType::SummonLeapShockwave && centerIndex > 0) {
			center = crystalPositions_[centerIndex - 1];
		}
		for (size_t layer = 0; layer < ringLayers; ++layer) {
			const bool domeBurst = telegraph.type == BossAttackType::DomeBurst;
			const float domeProgress = domeBurst
				? telegraph.progress * telegraph.progress
				: telegraph.progress;
			const float layerRate = domeBurst
				? (layer == 0u ? 1.0f : std::clamp(domeProgress, 0.0f, 1.0f))
				: (leapShockwave
				? 1.0f
				: (ringLayers == 1u
					? 1.0f
					: (0.55f + 0.45f * std::fmod(
						telegraph.progress + static_cast<float>(layer) * 0.5f,
						1.0f))));
			const float domeGrowth = domeBurst ? 1.0f : (0.45f + telegraph.progress * 0.95f);
			const float ringGrowth = telegraph.type == BossAttackType::BulletHell
				? (0.70f + telegraph.progress * 0.25f)
				: 1.0f;
			const float centerRadiusScale =
				telegraph.type == BossAttackType::SummonLeapShockwave && centerIndex > 0
				? 0.74f
				: 1.0f;
			const float layerRadius = baseRadius * centerRadiusScale *
				(domeBurst ? domeGrowth * layerRate : layerRate * ringGrowth);
			const float layerHeight = 0.0f;
			for (size_t segment = 0; segment < kCircleParticleCount && objectIndex < objects_.size(); ++segment) {
				const float angle = 2.0f * std::numbers::pi_v<float> *
					static_cast<float>(segment) / static_cast<float>(kCircleParticleCount);
				const float sparkle = 0.55f + 0.45f *
					std::sin((static_cast<float>(segment) * 0.65f + telegraph.progress * 6.0f) *
						std::numbers::pi_v<float>);
				const Vector3 segmentPosition{
					center.x + std::sin(angle) * layerRadius,
					(leapShockwave ? 0.2f : 0.16f) + layerHeight,
					center.z + std::cos(angle) * layerRadius,
				};
				Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
				const float particleSize = domeBurst
					? (layer == 0u
						? 0.34f + sparkle * 0.08f
						: 0.54f + domeProgress * 0.38f + sparkle * 0.12f)
					: (leapShockwave
						? 0.34f + sparkle * 0.08f
						: 0.26f + sparkle * 0.12f);
				object->SetScale({
					particleSize,
					particleSize,
					particleSize,
				});
				object->SetRotate({ 0.0f, angle, 0.0f });
				object->SetTranslate(segmentPosition);
				if (domeBurst) {
					const float alpha = layer == 0u
						? 0.42f
						: (0.28f + domeProgress * 0.58f);
					object->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
				} else {
					object->SetColor(color);
				}
				object->Update();
			}
		}
	}

	crystalVisibleCount_ = 0;
	if (telegraph.type != BossAttackType::SummonLeapShockwave) {
		crystalPositionsLocked_ = false;
	}
}

void BossAreaTelegraph::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!visible_) {
		return;
	}
	const size_t count = (std::min)(visibleCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
		}
	}
	const size_t crystalCount = (std::min)(crystalVisibleCount_, crystalObjects_.size());
	for (size_t index = 0; index < crystalCount; ++index) {
		if (crystalObjects_[index]) {
			objects.push_back(crystalObjects_[index].get());
		}
	}
}

void BossAreaTelegraph::AppendShadowObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!visible_) {
		return;
	}
	const size_t crystalCount = (std::min)(crystalVisibleCount_, crystalObjects_.size());
	for (size_t index = 0; index < crystalCount; ++index) {
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
		ConfigureObject(*objects_[index], kBossLaserTorusModelPath);
	}
}

void BossBeamBurst::Initialize(
	const Vector3& position,
	const Vector3& direction,
	int32_t beamCount,
	float length,
	float width,
	float angleOffset)
{
	position_ = position;
	direction_ = direction;
	const float directionLength = std::sqrt(direction_.x * direction_.x + direction_.z * direction_.z);
	if (directionLength > 0.001f) {
		direction_.x /= directionLength;
		direction_.z /= directionLength;
	} else {
		direction_ = { 0.0f, 0.0f, 1.0f };
	}
	visibleCount_ =
		(std::min)(
			objects_.size(),
			static_cast<size_t>(std::clamp(beamCount, 1, 3)));
	length_ = (std::max)(12.0f, length);
	width_ = std::clamp(width, 0.2f, 16.0f);
	angleOffset_ = std::clamp(angleOffset, 0.0f, 1.6f);
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
	constexpr float kDuration = 0.36f;
	if (elapsedTime_ >= kDuration) {
		active_ = false;
		return false;
	}
	const float progress = std::clamp(elapsedTime_ / kDuration, 0.0f, 1.0f);
	const float alpha = (1.0f - progress * 0.64f) * 0.96f;
	const float beamLength = length_;
	const float baseAngle = std::atan2(direction_.x, direction_.z);
	const size_t beamCount = (std::max)(size_t{ 1 }, visibleCount_);
	for (size_t index = 0; index < beamCount; ++index) {
		const float angleOffset = beamCount == 3u
			? (static_cast<float>(index) - 1.0f) * angleOffset_
			: 0.0f;
		const float angle = baseAngle + angleOffset;
		const Vector3 direction = RotateDirectionXZ(direction_, angleOffset);
		const Vector3 center = position_ + direction * (beamLength * 0.5f);
		Engine::Graphics3D::Object3D* object = objects_[index].get();
		const float pulse =
			0.84f + 0.16f * std::sin((elapsedTime_ * 18.0f + static_cast<float>(index)) *
				std::numbers::pi_v<float>);
		const float lengthScale = ClampFinite(beamLength / 1.23f, 48.0f, 4.0f, 260.0f);
		const float widthScale = ClampFinite(
			(width_ * (0.82f + progress * 0.12f) * pulse) / 1.23f,
			2.0f,
			0.35f,
			24.0f);
		object->SetScale({
			lengthScale,
			widthScale,
			ClampFinite(width_ * 0.42f, 1.2f, 0.35f, 12.0f),
		});
		object->SetRotate({ 0.0f, angle - std::numbers::pi_v<float> * 0.5f, 0.0f });
		object->SetTranslate({
			center.x,
			kBeamBodyY + pulse * 0.08f,
			center.z,
		});
		object->SetColor({ 0.0f, 0.95f, 1.0f, alpha });
		object->Update();
	}
	return true;
}

void BossBeamBurst::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!active_) {
		return;
	}
	const size_t count = (std::min)(visibleCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
		}
	}
}

BossBeamExplosionChain::BossBeamExplosionChain() = default;
BossBeamExplosionChain::~BossBeamExplosionChain() = default;

void BossBeamExplosionChain::EnsureInitialized(size_t count)
{
	for (size_t index = 0; index < count && index < objects_.size(); ++index) {
		if (objects_[index]) {
			continue;
		}
		objects_[index] = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*objects_[index], kBossLaserTorusModelPath);
	}
}

void BossBeamExplosionChain::Initialize(
	const Vector3& origin,
	const Vector3& direction,
	int32_t beamCount,
	float length,
	float spacing,
	float interval,
	float radius,
	float startDelay,
	float angleOffset)
{
	origin_ = origin;
	direction_ = direction;
	const float directionLength =
		std::sqrt(direction_.x * direction_.x + direction_.z * direction_.z);
	if (directionLength > 0.001f) {
		direction_.x /= directionLength;
		direction_.z /= directionLength;
	} else {
		direction_ = { 0.0f, 0.0f, 1.0f };
	}
	beamCount_ = static_cast<size_t>(std::clamp(beamCount, 1, 3));
	length_ = (std::max)(8.0f, length);
	spacing_ = (std::max)(2.0f, spacing);
	interval_ = (std::max)(0.02f, interval);
	radius_ = (std::max)(0.5f, radius);
	startDelay_ = (std::max)(0.0f, startDelay);
	angleOffset_ = std::clamp(angleOffset, 0.0f, 1.6f);
	explosionCountPerBeam_ = (std::min)(
		size_t{ 24 },
		(std::max)(size_t{ 1 }, static_cast<size_t>(length_ / spacing_)));
	visibleCount_ = (std::min)(
		objects_.size(),
		beamCount_ * explosionCountPerBeam_);
	EnsureInitialized(visibleCount_);
	elapsedTime_ = 0.0f;
	active_ = true;
	Update(0.0f);
}

bool BossBeamExplosionChain::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	constexpr float kExplosionLife = 0.42f;
	const float totalDuration = startDelay_ + kExplosionLife;
	if (elapsedTime_ >= totalDuration) {
		active_ = false;
		return false;
	}
	const float baseAngle = std::atan2(direction_.x, direction_.z);
	size_t objectIndex = 0;
	for (size_t beamIndex = 0; beamIndex < beamCount_; ++beamIndex) {
		const float angleOffset = beamCount_ == 3u
			? (static_cast<float>(beamIndex) - 1.0f) * angleOffset_
			: 0.0f;
		const Vector3 direction = RotateDirectionXZ(direction_, angleOffset);
		for (size_t explosionIndex = 0; explosionIndex < explosionCountPerBeam_; ++explosionIndex) {
			if (objectIndex >= objects_.size()) {
				break;
			}
			const float localTime = elapsedTime_ - startDelay_;
			const bool visible = localTime >= 0.0f && localTime < kExplosionLife;
			const float explosionProgress =
				std::clamp(localTime / kExplosionLife, 0.0f, 1.0f);
			const float distance =
				(std::min)(length_, (static_cast<float>(explosionIndex) + 1.0f) * spacing_);
			const Vector3 center = origin_ + direction * distance;
			Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
			if (!visible) {
				object->SetScale({ 0.001f, 0.001f, 0.001f });
				object->SetTranslate({ center.x, -20.0f, center.z });
				object->SetColor({ 0.0f, 0.96f, 1.0f, 0.0f });
				object->Update();
				continue;
			}
			const float pulse = 0.55f + 0.45f *
				std::sin((elapsedTime_ * 11.0f + static_cast<float>(explosionIndex) * 0.35f) *
					std::numbers::pi_v<float>);
			const float alpha = (1.0f - explosionProgress * 0.72f) * 0.88f;
			const float ringScale = ClampFinite(
				radius_ * (1.12f + explosionProgress * 0.48f + pulse * 0.08f),
				3.2f,
				0.5f,
				48.0f);
			object->SetScale({ ringScale, ringScale, ringScale });
			object->SetRotate({ 0.0f, baseAngle + angleOffset, 0.0f });
			object->SetTranslate({
				center.x,
				kBeamExplosionY + pulse * 0.08f,
				center.z,
			});
			object->SetColor({ 0.0f, 0.96f, 1.0f, alpha });
			object->Update();
		}
	}
	return true;
}

void BossBeamExplosionChain::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!active_) {
		return;
	}
	const size_t count = (std::min)(visibleCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
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
	const Vector4& color,
	float duration,
	float particleSize,
	float particleY,
	bool fixedRadius,
	size_t segmentCount,
	bool inward)
{
	center_ = center;
	radius_ = (std::max)(1.0f, radius);
	delay_ = (std::max)(0.0f, delay);
	color_ = color;
	duration_ = (std::max)(0.05f, duration);
	particleSize_ = (std::max)(0.05f, particleSize);
	particleY_ = particleY;
	fixedRadius_ = fixedRadius;
	inward_ = inward;
	segmentCount_ = std::clamp(segmentCount, size_t{ 3 }, objects_.size());
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
	const float localTime = elapsedTime_ - delay_;
	if (localTime >= duration_) {
		active_ = false;
		return false;
	}
	const float progress = std::clamp(localTime / duration_, 0.0f, 1.0f);
	const float currentRadius = inward_
		? radius_ * (1.0f - progress)
		: (fixedRadius_ ? radius_ : radius_ * progress);
	const float alpha = (fixedRadius_
		? (0.9f - progress * 0.58f)
		: (inward_ ? (0.92f - progress * 0.5f) : (1.0f - progress * 0.82f))) *
		color_.w;
	for (size_t index = 0; index < segmentCount_; ++index) {
		const float angle = 2.0f * std::numbers::pi_v<float> *
			static_cast<float>(index) / static_cast<float>(segmentCount_);
		Engine::Graphics3D::Object3D* object = objects_[index].get();
		const float sparkle = 0.55f + 0.45f *
			std::sin((static_cast<float>(index) * 0.45f + progress * 5.0f) *
				std::numbers::pi_v<float>);
		const float particleSize = particleSize_ *
			(0.72f + sparkle * 0.26f + (fixedRadius_ ? 0.0f : progress * 0.18f));
		object->SetScale({ particleSize, particleSize, particleSize });
		object->SetRotate({ 0.0f, angle, 0.0f });
		object->SetTranslate({
			center_.x + std::sin(angle) * currentRadius,
			particleY_ + sparkle * 0.05f,
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
	const size_t count = (std::min)(segmentCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
		}
	}
}

BossDomeBurstVisual::BossDomeBurstVisual() = default;
BossDomeBurstVisual::~BossDomeBurstVisual() = default;

void BossDomeBurstVisual::EnsureInitialized()
{
	for (std::unique_ptr<Engine::Graphics3D::Object3D>& object : objects_) {
		if (object) {
			continue;
		}
		object = std::make_unique<Engine::Graphics3D::Object3D>();
		ConfigureObject(*object);
	}
}

void BossDomeBurstVisual::Initialize(
	const Vector3& center,
	float radius,
	float duration,
	const Vector4& color)
{
	center_ = center;
	radius_ = (std::max)(1.0f, radius);
	duration_ = (std::max)(0.15f, duration);
	color_ = color;
	elapsedTime_ = 0.0f;
	active_ = true;
	visibleCount_ = 0;
	EnsureInitialized();
	Update(0.0f);
}

bool BossDomeBurstVisual::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	if (elapsedTime_ >= duration_) {
		active_ = false;
		return false;
	}
	const float progress = std::clamp(elapsedTime_ / duration_, 0.0f, 1.0f);
	const float fade = 1.0f - progress;
	constexpr size_t kLayerCount = 8;
	constexpr size_t kSegmentsPerLayer = 64;
	visibleCount_ = (std::min)(objects_.size(), kLayerCount * kSegmentsPerLayer);
	size_t objectIndex = 0;
	for (size_t layer = 0; layer < kLayerCount; ++layer) {
		const float layerRate =
			static_cast<float>(layer) / static_cast<float>(kLayerCount - 1u);
		const float height = radius_ * 0.92f *
			std::sin(layerRate * std::numbers::pi_v<float> * 0.5f);
		const float layerRadius = radius_ *
			(1.0f - 0.14f * layerRate) *
			(0.92f + progress * 0.22f);
		for (size_t segment = 0; segment < kSegmentsPerLayer && objectIndex < objects_.size(); ++segment) {
			const float angle = 2.0f * std::numbers::pi_v<float> *
				static_cast<float>(segment) / static_cast<float>(kSegmentsPerLayer);
			const float pulse = 0.66f + 0.34f *
				std::sin((static_cast<float>(segment) * 0.42f + layerRate * 3.0f + progress * 7.0f) *
					std::numbers::pi_v<float>);
			Engine::Graphics3D::Object3D* object = objects_[objectIndex++].get();
			const float particleSize = (0.96f + layerRate * 0.46f + progress * 0.58f) * pulse;
			object->SetScale({ particleSize, particleSize, particleSize });
			object->SetRotate({ 0.0f, angle, 0.0f });
			object->SetTranslate({
				center_.x + std::sin(angle) * layerRadius,
				0.36f + height,
				center_.z + std::cos(angle) * layerRadius,
			});
			object->SetColor({
				color_.x,
				color_.y * (0.78f + pulse * 0.22f),
				color_.z * (0.78f + pulse * 0.22f),
				color_.w * fade * (0.78f + pulse * 0.32f),
			});
			object->Update();
		}
	}
	return true;
}

void BossDomeBurstVisual::AppendRenderObjects(std::vector<Engine::Graphics3D::Object3D*>& objects) const
{
	if (!active_) {
		return;
	}
	const size_t count = (std::min)(visibleCount_, objects_.size());
	for (size_t index = 0; index < count; ++index) {
		if (objects_[index]) {
			objects.push_back(objects_[index].get());
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
	burstScheduled_ = delay_ <= 3600.0f;
	active_ = true;
	object_ = std::make_unique<Engine::Graphics3D::Object3D>();
	ConfigureObject(*object_, "cube_world/crystal.glb");
	object_->SetCastsShadow(true);
	object_->SetScale({
		kBossSummonCrystalScale,
		kBossSummonCrystalScale,
		kBossSummonCrystalScale,
	});
	object_->SetTranslate({ position_.x, kBossSummonCrystalY, position_.z });
	object_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object_->SetColor({ 0.72f, 0.42f, 1.0f, 1.0f });
	object_->Update();
}

void BossSummonCrystal::ScheduleBurst(float delayFromNow)
{
	delay_ = elapsedTime_ + (std::max)(0.0f, delayFromNow);
	burstScheduled_ = true;
}

bool BossSummonCrystal::Update(float deltaTime)
{
	if (!active_) {
		return false;
	}
	elapsedTime_ += (std::max)(0.0f, deltaTime);
	constexpr float kDuration = 0.72f;
	if (burstScheduled_ && elapsedTime_ >= delay_ + kDuration) {
		active_ = false;
		return false;
	}
	const float localTime = burstScheduled_
		? (std::max)(0.0f, elapsedTime_ - delay_)
		: 0.0f;
	const float progress = std::clamp(localTime / kDuration, 0.0f, 1.0f);
	const float summonProgress = std::clamp(elapsedTime_ / 0.28f, 0.0f, 1.0f);
	const float easedSummon =
		summonProgress * summonProgress * (3.0f - 2.0f * summonProgress);
	const float pulse = 0.5f + 0.5f * std::sin(elapsedTime_ * 9.0f);
	const float burstScale = !burstScheduled_ || elapsedTime_ < delay_
		? easedSummon
		: 1.0f + progress * progress * 0.82f;
	const float alpha = !burstScheduled_ || elapsedTime_ < delay_
		? 1.0f
		: (1.0f - progress * 0.85f);
	if (object_) {
		const float scale = ClampFinite(
			(kBossSummonCrystalScale + pulse * 16.0f) *
				(std::max)(0.12f, burstScale),
			kBossSummonCrystalScale,
			120.0f,
			760.0f);
		object_->SetScale({ scale, scale, scale });
		object_->SetRotate({ 0.0f, 0.0f, 0.0f });
		object_->SetTranslate({
			position_.x,
			kBossSummonCrystalY - (1.25f * (1.0f - easedSummon)) + pulse * 0.08f,
			position_.z,
		});
		object_->SetColor({ 0.72f + pulse * 0.24f, 0.42f, 1.0f, alpha });
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
