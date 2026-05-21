/*****************************************************************//**
 * \file   Components.hpp
 * \brief  コンポーネント管理
 * 
 * 作成者 keeeep
 * 作成日 2026/4/15
 * 更新履歴	4.15 コンポーネントの追加
 *			5.15 PhysicsXの追加
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include <memory>

class Mesh;
class Material;

namespace PhysX
{
	class PxRigidActor;
	class PxShape;
}

struct TransformComponent
{
	float4x4 world{};
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
};

struct RigidBodyComponent
{
	PhysX::PxRigidActor* actor = nullptr;
	float mass = 1.0f;
	bool isKinematic = false;
	bool isStatic = false;
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
	float3 size{ 1.0f, 1.0f, 1.0f }; // Boxの場合のサイズ
	float radius = 0.5f;
	float friction = 0.5f;
	float restitution = 0.5f;
	float density = 1.0f;

	PhysX::PxShape* shape = nullptr;
};
