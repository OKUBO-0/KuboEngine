#include "FloatingNumberPresentation.h"

#include "Camera.h"
#include "CameraManager.h"
#include "MyMath.h"
#include "GameSpriteFactory.h"
#include "ScreenUtil.h"
#include "DigitSpriteUtil.h"
#include <algorithm>
#include <cmath>

namespace DirectXGame {
namespace {

constexpr char kNumberTexturePath[] = "ui/number/numbers.png";
constexpr float kDigitTextureWidth = 24.0f;
constexpr Vector2 kDigitSize{ 18.0f, 24.0f };
constexpr float kDigitStep = 15.0f;
constexpr size_t kMaxFloatingNumbers = 80;

bool ProjectWorldToScreen(
	const Vector3& worldPosition,
	const Matrix4x4& viewProjection,
	Vector2& outScreen)
{
	const Vector3 ndc = MyMath::Transform(worldPosition, viewProjection);
	if (ndc.z < 0.0f || ndc.z > 1.0f) {
		return false;
	}
	const Vector2 clientSize = ScreenUtil::GetClientSize();
	if (clientSize.x <= 0.0f || clientSize.y <= 0.0f) {
		return false;
	}
	outScreen = {
		(ndc.x + 1.0f) * 0.5f * clientSize.x,
		(1.0f - ndc.y) * 0.5f * clientSize.y,
	};
	return std::isfinite(outScreen.x) && std::isfinite(outScreen.y);
}

std::vector<int32_t> ExtractDigits(int32_t value)
{
	value = std::clamp(value, 0, 9999);
	if (value == 0) {
		return { 0 };
	}
	std::vector<int32_t> digits;
	while (value > 0) {
		digits.push_back(value % 10);
		value /= 10;
	}
	std::reverse(digits.begin(), digits.end());
	return digits;
}

} // namespace

void FloatingNumberPresentation::Initialize()
{
	digitTexture_ = GameTextureCache::Load(kNumberTexturePath);
	numbers_.reserve(kMaxFloatingNumbers);
}

void FloatingNumberPresentation::Reset()
{
	numbers_.clear();
}

void FloatingNumberPresentation::AddDamage(const Vector3& position, int32_t value)
{
	Add(position, value, { 1.0f, 0.28f, 0.18f, 1.0f });
}

void FloatingNumberPresentation::AddExp(const Vector3& position, int32_t value)
{
	Add(position, value, { 0.35f, 1.0f, 0.58f, 1.0f });
}

void FloatingNumberPresentation::AddCoin(const Vector3& position, int32_t value)
{
	Add(position, value, { 1.0f, 0.86f, 0.22f, 1.0f });
}

void FloatingNumberPresentation::AddEvents(const std::vector<FloatingNumberEvent>& events)
{
	for (const FloatingNumberEvent& event : events) {
		Add(event.position, event.value, event.color);
	}
}

void FloatingNumberPresentation::Update(float deltaTime)
{
	for (NumberInstance& number : numbers_) {
		number.age += deltaTime;
		number.worldPosition.y += 1.25f * deltaTime;
	}
	numbers_.erase(
		std::remove_if(
			numbers_.begin(),
			numbers_.end(),
			[](const NumberInstance& number) {
				return number.age >= number.lifetime;
			}),
		numbers_.end());
}

void FloatingNumberPresentation::Draw()
{
	Engine::CameraSystem::Camera* camera =
		Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
	if (!camera) {
		return;
	}

	const Matrix4x4& viewProjection = camera->GetViewProjectionMatrix();
	for (NumberInstance& number : numbers_) {
		Vector2 screen{};
		if (!ProjectWorldToScreen(number.worldPosition, viewProjection, screen)) {
			continue;
		}
		const float progress = std::clamp(number.age / number.lifetime, 0.0f, 1.0f);
		Vector4 color = number.color;
		color.w *= 1.0f - progress;
		const float width = kDigitStep * static_cast<float>(number.digits.size());
		const Vector2 base{
			screen.x - width * 0.5f,
			screen.y - 28.0f - progress * 24.0f,
		};
		for (size_t index = 0; index < number.digits.size(); ++index) {
			Engine::Graphics2D::Sprite* digit = number.digits[index].get();
			if (!digit) {
				continue;
			}
			digit->SetPosition({
				base.x + kDigitStep * static_cast<float>(index),
				base.y,
				});
			digit->SetSize(kDigitSize);
			digit->SetColor(color);
			digit->Update();
			digit->Draw();
		}
	}
}

void FloatingNumberPresentation::Add(
	const Vector3& position,
	int32_t value,
	const Vector4& color)
{
	if (value <= 0 || digitTexture_ == 0) {
		return;
	}
	if (numbers_.size() >= kMaxFloatingNumbers) {
		numbers_.erase(numbers_.begin());
	}
	NumberInstance instance{};
	instance.worldPosition = position;
	instance.worldPosition.y += 2.35f;
	instance.value = value;
	instance.color = color;
	PrepareDigits(instance);
	numbers_.push_back(std::move(instance));
}

void FloatingNumberPresentation::PrepareDigits(NumberInstance& instance)
{
	const std::vector<int32_t> digits = ExtractDigits(instance.value);
	instance.digits.reserve(digits.size());
	for (int32_t digit : digits) {
		std::unique_ptr<Engine::Graphics2D::Sprite> sprite =
			GameSpriteFactory::Create(digitTexture_, { 0.0f, 0.0f });
		sprite->SetSize(kDigitSize);
		sprite->SetTextureSize({ kDigitTextureWidth, 32.0f });
		DigitSpriteUtil::SetDigitSprite(
			*sprite,
			kDigitTextureWidth,
			{ kDigitTextureWidth, 32.0f },
			digit);
		instance.digits.push_back(std::move(sprite));
	}
}

} // namespace DirectXGame
