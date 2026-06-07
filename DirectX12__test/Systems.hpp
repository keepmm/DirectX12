#pragma once

#include "World.hpp"
#include "Components.hpp"
#include "RenderContext.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Input.hpp"

class SpinSystem
{
public:
	void Update(World& world, float deltatime)
	{
		world.Each<TransformComponent, SpinComponent>(
			[deltatime](Entity, TransformComponent& transform, SpinComponent& spin)
			{
				spin.angle += spin.speed * deltatime;
				const auto q = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, spin.angle, 0.0f);
				DirectX::XMStoreFloat4(&transform.rotation, DirectX::XMQuaternionNormalize(q));
				transform.MarkDirty();
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
		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		if (renderContext.useMeshShader)
		{
			if (!renderContext.meshShaderSupported ||
				renderContext.CommandList6 == nullptr ||
				renderContext.meshShaderPso == nullptr)
			{
				return;
			}

			renderContext.CommandList6->SetPipelineState(renderContext.meshShaderPso);
			renderContext.CommandList6->DispatchMesh(1, 1, 1);
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

				transform.EnsureWorld();

				const E_PIXEL_SHADER psType = material.usePixelShader 
					? material.pixelshader
					: renderContext.pixelShader;

				material.material->Apply(
					renderContext.CommandList,
					transform.world,
					renderContext.view,
					renderContext.projection,
					renderContext.wireframe,
					renderContext.vertexShader,
					psType,
					material.overridePso,
					renderContext.frameIndex);

				mesh.mesh->Draw(renderContext.CommandList);
			}
		);
	}
};

class LightSystem
{
public:
	void Apply(World& world)
	{
		LightComponent* activeLight = nullptr;

		world.Each<LightComponent>([&](Entity, LightComponent& light)
			{
				if (light.isActive && activeLight == nullptr)
				{
					activeLight = &light;
				}
			});

		if (activeLight == nullptr)
		{
			return;
		}

		if (activeLight->usePixelShader)
		{
			RenderSettings::Get().pixelShader = activeLight->pixelShader;
		}

		float3 direction = activeLight->direction;
		const DirectX::XMVECTOR dirVec = DirectX::XMVector3Normalize(
			DirectX::XMVectorSet(direction.x, direction.y, direction.z, 0.0f)
		);
		DirectX::XMStoreFloat3(&direction, dirVec);

		float4 color = activeLight->color;
		color.x *= activeLight->intensity;
		color.y *= activeLight->intensity;
		color.z *= activeLight->intensity;

		world.Each<MaterialComponent>([&](Entity, MaterialComponent& material)
			{
				if (material.material == nullptr)
				{
					return;
				}
				material.material->SetLightDir(direction);
				material.material->SetLightColor(color);
				material.material->SetAmbientColor(activeLight->ambientColor);
			}
		);
	}
};

class ScriptSystem
{
public:
	void Start(World& world)
	{
		world.Each<ScriptComponent>([](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnStart();
			});
	}

	void Update(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnUpdate(deltatime);
			});
	}

	void FixedUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnFixedUpdate(deltatime);
			});
	}

	void LateUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnLateUpdate(deltatime);
			});
	}

	void Draw(World& world, const RenderContext& context)
	{
		world.Each<ScriptComponent>([&context](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnDraw(context);
			});
	}
};

class SpriteRenderSystem
{
public:
	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext,
		_In_ Mesh& mesh,
		_In_ Material& spriteMaterial)
	{
		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		world.Each<TransformComponent, SpriteComponent>([&](Entity, TransformComponent& transform, SpriteComponent& sprite)
			{
				transform.EnsureWorld();

				Material* mat = nullptr;
				if (sprite.material)
				{
					mat = sprite.material.get();
				}
				else
				{
					mat = &spriteMaterial;
				}

				mat->Apply(
					renderContext.CommandList,
					transform.world,
					renderContext.view,
					renderContext.projection,
					renderContext.wireframe,
					renderContext.vertexShader,
					renderContext.pixelShader,
					nullptr,
					renderContext.frameIndex);

				mesh.Draw(renderContext.CommandList);
			});
	}
};

class FreeLookSystem
{
public:
	void Update(World& world, float deltatime)
	{
		world.Each<TransformComponent, FreeLookComponent>(
			[&](Entity entity, TransformComponent& tr, FreeLookComponent& fl)
			{
				// FreeLookが有効でない場合処理しない
				if (fl.Enabled) return;

				//--------------- //
				// 右クリックで回転  //
				//--------------- //
				if (INPUT->MouseInput.Right().IsPressed())
				{
					fl.yaw -= (float)INPUT->MouseInput.DeltaX() * fl.rotateSpeed;
					fl.pitch -= (float)INPUT->MouseInput.DeltaY() * fl.rotateSpeed;
					fl.pitch = std::clamp(fl.pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
				}

				// 回転の更新
				vector q = DirectX::XMQuaternionRotationRollPitchYaw(fl.pitch, fl.yaw, 0.0f);
				DirectX::XMStoreFloat4(&tr.rotation, DirectX::XMQuaternionNormalize(q));

				float speed = 1.0f;

				// 移動量
				vector move = DirectX::XMVectorZero();
				if (INPUT->Key.Shift().IsPressed()) speed *= 5.0f;
				if (INPUT->Key.W().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0.0f, 0.0f, speed, 0.0f));
				if (INPUT->Key.S().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0.0f, 0.0f, -speed, 0.0f));
				if (INPUT->Key.A().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(-speed, 0.0f, 0.0f, 0.0f));
				if (INPUT->Key.D().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(speed, 0.0f, 0.0f, 0.0f));

				if (!DirectX::XMVector3Equal(move, DirectX::XMVectorZero()))
				{
					move = DirectX::XMVector3Normalize(move);
					move * fl.moveSpeed * deltatime;
					vector pos = XMLoadFloat3(&tr.position);
					DirectX::XMVectorAdd(pos, DirectX::XMVector3Rotate(move, q));
					DirectX::XMStoreFloat3(&tr.position, pos);
				}
				tr.RebuildWorld();
			});
	}
};

class CamerSystem
{
public:
	void Update(World& world, float aspect)
	{
		world.Each<TransformComponent,CameraComponent>([aspect]
		(Entity, TransformComponent& tr, CameraComponent& camera) {
			if (!camera.isActive)
			{
				return;
			}

			vector pos = XMLoadFloat3(&tr.position);
			vector q = XMLoadFloat4(&tr.rotation);
			vector forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
			vector up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), q);
			DirectX::XMStoreFloat4x4(&camera.view, DirectX::XMMatrixLookToLH(pos, forward, up));

			matrix p = (camera.projection == CameraComponent::Projection::Perspective)
				? DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(camera.fovY), aspect, camera.nearZ, camera.farZ)
				: DirectX::XMMatrixOrthographicLH(camera.orhoSize * aspect, camera.orhoSize, camera.nearZ, camera.farZ);
			DirectX::XMStoreFloat4x4(&camera.proj, p);
			});
	}
};
