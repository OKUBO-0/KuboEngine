#pragma once

#include "FloatingNumberEvent.h"
#include "GameTextureCache.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine::Graphics2D {
class Sprite;
}

namespace DirectXGame {

class FloatingNumberPresentation final {
public:
	void Initialize();
	void Reset();
	void AddDamage(const Vector3& position, int32_t value);
	void AddExp(const Vector3& position, int32_t value);
	void AddCoin(const Vector3& position, int32_t value);
	void AddEvents(const std::vector<FloatingNumberEvent>& events);
	void Update(float deltaTime);
	void Draw();

private:
	struct NumberInstance {
		Vector3 worldPosition{};
		int32_t value = 0;
		Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float age = 0.0f;
		float lifetime = 0.95f;
		std::vector<std::unique_ptr<Engine::Graphics2D::Sprite>> digits;
	};

	void Add(const Vector3& position, int32_t value, const Vector4& color);
	void PrepareDigits(NumberInstance& instance);

	TextureHandle digitTexture_ = 0;
	std::vector<NumberInstance> numbers_;
};

} // namespace DirectXGame
