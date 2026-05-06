#pragma once

#include <cstdint>
#include <memory>

namespace DirectXGame {

class Enemy;

class IEnemyBehavior {
public:
	virtual ~IEnemyBehavior() = default;
	virtual void Update(Enemy& enemy, float deltaTime) = 0;
};

std::unique_ptr<IEnemyBehavior> CreateEnemyBehaviorByType(int32_t type);

} // namespace DirectXGame
