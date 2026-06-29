#pragma once

#include "EnemyDefinition.h"
#include <cstdint>
#include <memory>

namespace DirectXGame {

class Enemy;

class IEnemyBehavior {
public:
	virtual ~IEnemyBehavior() = default;
	virtual void Update(Enemy& enemy, float deltaTime) = 0;
};

std::unique_ptr<IEnemyBehavior> CreateEnemyBehavior(EnemyBehaviorType type);

} // namespace DirectXGame
