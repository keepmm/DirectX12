#pragma once

#include "World.hpp"
#include "Components.hpp"
#include "RenderContext.hpp"
#include "Mesh.hpp"
#include "Material.hpp"

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

class RenderSystem
{
public:

	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext)
	{
		// CommandList‚ªnullptr‚Ìê‡‚Í•`‰æ‚Å‚«‚È‚¢‚Ì‚ÅAˆ—‚µ‚È‚¢
		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		world.Each<TransformComponent, MeshComponent, MaterialComponent>(
			[&renderContext](
				Entity,
				TransformComponent& transform,
				MeshComponent& mesh,
				MaterialComponent& material
				)
			{
				if (mesh.mesh == nullptr || material.material == nullptr)
				{
					return;
				}

				material.material->Apply(
					renderContext.CommandList,
					transform.world,
					renderContext.view,
					renderContext.projection,
					renderContext.wireframe,
					renderContext.vertexShader,
					renderContext.pixelShader,
					material.overridePso,
					renderContext.frameIndex);

				mesh.mesh->Draw(renderContext.CommandList);
			}
		);
	}
};