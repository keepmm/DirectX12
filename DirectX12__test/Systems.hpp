#pragma once

#include "World.hpp"
#include "Components.hpp"

class SpinSystem
{
public:
	void Update(World& world, float deltatime)
	{
		world.Each<TransformComponent, SpinComponent>(
			[deltatime](Entity, TransformComponent& transform, SpinComponent& spin)
			{
				spin.angle += spin.speed * deltatime;
				const matrix rotation = DirectX::XMMatrixRotationY(spin.angle);
				DirectX::XMStoreFloat4x4(&transform.world, rotation);
			}
		);
	}
};
