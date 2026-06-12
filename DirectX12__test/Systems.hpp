#pragma once

#include "World.hpp"
#include "Components.hpp"
#include "RenderContext.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Input.hpp"
#include "MonoBehavior.hpp"

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

		if (renderContext.cbAllocator != nullptr)
		{
			const UINT frameSlot = renderContext.frameIndex % RTV_NUM;
			const D3D12_GPU_VIRTUAL_ADDRESS b2 = renderContext.cbAllocator->Allocate(
			frameSlot,&renderContext.lightCb,sizeof(LightCB));

			if (b2 != 0)
			{
				renderContext.CommandList->SetGraphicsRootConstantBufferView(2, b2);
			}
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
					renderContext.frameIndex,
					renderContext.cbAllocator);

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
		// ライトがない場合のためにデフォルト
		m_Data = {};

		UINT count = 0;

		world.Each<LightComponent>([&](Entity entity, LightComponent& light)
			{
				// 非アクティブ、または上限に達したらスキップ
				if (!light.isActive || count >= MAX_LIGHTS)
				{
					return;
				}

				LightData& dst = m_Data.lights[count];

				// 色 × 強度
				dst.color = light.color;
				dst.color.x *= light.intensity;
				dst.color.y *= light.intensity;
				dst.color.z *= light.intensity;

				// 位置と方向（Transformがあれば回転から導出）
				if (world.HasComponent<TransformComponent>(entity))
				{
					const auto& tr = world.GetComponent<TransformComponent>(entity);
					dst.posRange = float4(
						tr.position.x, tr.position.y, tr.position.z,
						light.range);

					const auto rot = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&tr.rotation));
					const auto fwd = DirectX::XMVector3Rotate(
						DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot);
					DirectX::XMStoreFloat4(&dst.dir, DirectX::XMVector3Normalize(fwd));

					// ギズモ表示用に書き戻す
					DirectX::XMStoreFloat3(&light.direction, DirectX::XMVector3Normalize(fwd));
				}
				else
				{
					const auto dirVec = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
						light.direction.x, light.direction.y, light.direction.z, 0.0f));
					DirectX::XMStoreFloat4(&dst.dir, dirVec);
					dst.posRange.w = light.range;
				}

				// タイプとスポット角
				dst.param.x = static_cast<float>(light.type);
				dst.param.y = cosf(DirectX::XMConvertToRadians(light.spotAngle * 0.5f));

				// 環境光は最初のライトのものを採用
				if (count == 0)
				{
					m_Data.ambientColor = light.ambientColor;
				}

				++count;
			});

		m_Data.lightCount.x = static_cast<float>(count);
	}

	inline const LightCB& GetLightData() const
	{
		return m_Data;
	}

private:
	LightCB m_Data;
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
				if (!fl.Enabled) return;

				//--------------- //
				// 右クリックで回転  //
				//--------------- //
				if (INPUT->MouseInput.Right().IsPressed())
				{
					fl.yaw += (float)INPUT->MouseInput.DeltaX() * fl.rotateSpeed;
					fl.pitch += (float)INPUT->MouseInput.DeltaY() * fl.rotateSpeed;
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
					move = DirectX::XMVectorScale(move, fl.moveSpeed * deltatime);
					vector pos = XMLoadFloat3(&tr.position);
					pos = DirectX::XMVectorAdd(pos, DirectX::XMVector3Rotate(move, q));
					DirectX::XMStoreFloat3(&tr.position, pos);
				}
				tr.RebuildWorld();
			});
	}
};

class CameraSystem
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

class NameSytem
{
public:
	static std::string GetName(World& world, Entity entity)
	{
		world.Each<NameComponent>([&](Entity e, NameComponent& name)
			{
				if (e == entity)
				{
					return name.name;
				}
			});
		return std::to_string(entity);
	}

	static void SetName(World& world, Entity entity, const std::string& name)
	{
		// -------------------------//
		// 同じ名前が存在してる場合	//
		// 名前 + _番号にする		//
		// -------------------------//
		std::string newName = GenerateName(world, entity, name, 1);
		if (world.HasComponent<NameComponent>(entity))
			world.GetComponent<NameComponent>(entity).name = newName;   // 書き戻す
		else
			world.AddComponent<NameComponent>(entity, NameComponent{ newName });
	}

	/// @brief 名前を捜索する
	static std::string GenerateName(World& world, Entity entity,const std::string& name,int num)
	{
		std::string result = name;
		bool conflict = true;
		while (conflict)
		{
			conflict = false;
			world.Each<NameComponent>([&](Entity e, NameComponent& nameComp)
				{
					// 自分自身は除外
					if(e != entity && nameComp.name == result)
					{
						conflict = true;
					}
				});
			if (conflict)
			{
				result = name + "_" + std::to_string(num++);
			}
		}

		return result;
	}
};
