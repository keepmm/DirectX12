/*****************************************************************//**
 * \file   PhysicsWorld.hpp
 * \brief  ï®óùÇÃä«óù
 * 
 * çÏê¨é“ keeeep
 * çÏê¨ì˙ 2026/5/15
 * çXêVóöó	5.15 PhysicsXÇÃí«â¡
 * *********************************************************************/
#pragma once

#include "World.hpp"
#include "Defines.hpp"

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxScene;
	class PxDefaultCpuDispatcher;
	class PxCudaContextManager;
	class PxRigidActor;
	class PxActor;
	class PxMaterial;
	class PxDefaultAllocator;
	class PxDefaultErrorCallback;
}

class PhysicsWorld
{
public:
	PhysicsWorld();
	~PhysicsWorld();

	void Init();
	void Update(_In_ float deltaTime);
	void SetGravity(const _In_ float3& gravity);

	void AddRigidbody(_In_ Entity entity,
		_In_ const struct RigidBodyComponent& rigidBody,
		_In_ const struct ColliderComponent& collider);

	void RemoveRigidbody(_In_ Entity entity);

	void SyncTransforms(_In_ class World& world);

	void SetActorPose(
		_In_ Entity entity,
		_In_ const float3& position,
		_In_ const float4& rotation);

	physx::PxPhysics* GetPhysics() const { return m_Physics; }
	physx::PxScene* GetScene() const { return m_Scene; }

private:
	std::unique_ptr<physx::PxDefaultAllocator> m_Allocator;
	std::unique_ptr<physx::PxDefaultErrorCallback> m_ErrorCallback;
	physx::PxFoundation* m_Foundation = nullptr;
	physx::PxPhysics* m_Physics = nullptr;
	physx::PxScene* m_Scene = nullptr;
	physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
	physx::PxCudaContextManager* m_CudaContextManager = nullptr;
	physx::PxMaterial* m_DefaultMaterial = nullptr;

	std::unordered_map<Entity, physx::PxRigidActor*> m_ActorMap;
	physx::PxRigidActor* CreatePhysicsActor(const struct RigidBodyComponent& rb, const struct ColliderComponent& collider);
};

