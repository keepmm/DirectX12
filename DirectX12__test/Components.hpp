#pragma once

#include "Defines.hpp"
#include "RenderContext.hpp"
#include "MonoBehavior.hpp"
#include <memory>
#include <vector>

class Mesh;
class Material;
class Camera;

namespace PhysX
{
	class PxRigidActor;
	class PxShape;
}

struct TransformComponent
{
	POSITION position{ 0.0f, 0.0f, 0.0f };
	QUATERNION rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	SCALE scale{ 1.0f,1.0f,1.0f };

	float4x4 world{};
	bool isDirty = true;

	void MarkDirty() { isDirty = true; }

	void RebuildWorld()
	{
		const matrix scaleMatrix = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z);

		const auto rot = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&rotation));
		DirectX::XMStoreFloat4(&rotation, rot);
		const matrix rotMatrix = DirectX::XMMatrixRotationQuaternion(rot);
		const matrix transMatrix = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
		const matrix worldMatrix = scaleMatrix * rotMatrix * transMatrix;
		DirectX::XMStoreFloat4x4(&world, worldMatrix);
		isDirty = false;
	}

	void EnsureWorld()
	{
		if (isDirty)
		{
			RebuildWorld();
		}
	}
};

struct SpinComponent
{
	float angle = 0.0f;
	float speed = 1.0f;
};

struct MeshComponent
{
	std::shared_ptr<Mesh> mesh;
};

struct MaterialComponent
{
	std::shared_ptr<Material> material;
	ID3D12PipelineState* overridePso = nullptr;
	bool usePixelShader = false;
	E_PIXEL_SHADER pixelshader = E_PIXEL_SHADER::BASIC;
};

struct RigidBodyComponent
{
	PhysX::PxRigidActor* actor = nullptr;
	float mass = 1.0f;
	bool isKinematic = false;
	bool isStatic = false;
	bool useGravity = true;
};

struct ColliderComponent
{
	enum class ShapeType
	{
		Box,
		Sphere,
		Capsule,
		Mesh
	} shapeType = ShapeType::Box;
	SCALE size{ 1.0f, 1.0f, 1.0f };
	float radius = 0.5f;
	float friction = 0.5f;
	float restitution = 0.5f;
	float density = 1.0f;

	PhysX::PxShape* shape = nullptr;
};

struct CameraComponent
{
	/// @brief •`‰æƒ^ƒCƒv
	enum class Projection
	{
		Perspective,
		Orthographic
	} projection = Projection::Perspective;

	enum class CameraType
	{
		Main,
		Secondary
	} cameraType = CameraType::Main;
	float orhoSize = 10.0f;
	float fovY = 60.0f;
	float nearZ = 0.1f;
	float farZ = 100.0f;
	bool isActive = true;

	float4x4 view{};
	float4x4 proj{};
};

struct FreeLookComponent
{
	float moveSpeed = 5.0f;
	float rotateSpeed = 0.0025f;
	float yaw = 0.0f;
	float pitch = 0.0f;
	bool Enabled = true;
};

struct LightComponent
{
	enum class LightType
	{
		Directional,
		Point,
		Spot
	} type = LightType::Directional;
	COLOR color{ 1.0f, 1.0f, 1.0f, 1.0f };
	COLOR ambientColor{ 0.2f, 0.2f, 0.2f, 1.0f };
	float intensity = 1.0f;
	float range = 10.0f;
	POSITION direction{ 0.0f, -1.0f, 0.0f };
	float spotAngle = 45.0f;
	bool isActive = true;
	bool castShadows = false;

	bool isShow = false;

	bool usePixelShader = false;
	E_PIXEL_SHADER pixelShader = E_PIXEL_SHADER::BASIC;
};


struct NameComponent
{
	std::string name;
};


struct PrefabComponent
{
	std::string name;
	std::string guid;
};


struct SpriteComponent
{
	std::wstring texturePath;
	float2 size{ 1.0f, 1.0f };
	std::shared_ptr<Material> material;
};


struct ScriptComponent
{
	std::vector<std::unique_ptr<MonoBehavior>> behaviors;

	template<typename T, typename... TArgs>
	T& AddBehavior(World& world, Entity entity, TArgs&&... args)
	{
		static_assert(std::is_base_of_v<MonoBehavior, T>, "T must derive from MonoBehavior");
		auto b = std::make_unique<T>(std::forward<TArgs>(args)...);
		auto* raw = b.get();
		raw->Attach(&world, entity);
		behaviors.emplace_back(std::move(b));
		return *raw;
	}
};

//using Transform = TransformComponent;
//using Spin = SpinComponent;
//using MeshRenderer = MeshComponent;
//using MaterialRenderer = MaterialComponent;
//using RigidBody = RigidBodyComponent;
//using Collider = ColliderComponent;
//using Camera = CameraComponent;
//using Light = LightComponent;
//using SpriteRenderer = SpriteComponent;
//using Script = ScriptComponent;
//using Name = NameComponent;
//using Prefab = PrefabComponent;
