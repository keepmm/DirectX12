#include "PhysicsWorld.hpp"
#include "Components.hpp"
#include "MonoBehavior.hpp"

#include <PxPhysicsAPI.h>

#define PX_RELEASE(x) if(x) { x->release(); x = nullptr; }

using namespace physx;

namespace {
	PxDefaultAllocator gAllocator;
	PxDefaultErrorCallback gErrorCallback;

	PxFilterFlags CollisionFilterShader(
		PxFilterObjectAttributes attributes0, PxFilterData filterData0,
		PxFilterObjectAttributes attributes1, PxFilterData filterData1,
		PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
	{
		if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
		{
			pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
			return PxFilterFlag::eDEFAULT;
		}
		pairFlags = PxPairFlag::eCONTACT_DEFAULT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST;
		return PxFilterFlag::eDEFAULT;
	}
}

class PhysicsWorld::EventCallback : public PxSimulationEventCallback
{
public:
	std::vector<CollisionEvent> events;

	void onContact(
		const PxContactPairHeader& pairHeader,
		const PxContactPair* pairs,
		PxU32 nbPairs) override
	{
		Entity a = static_cast<Entity>(reinterpret_cast<uintptr_t>(pairHeader.actors[0]->userData));
		Entity b = static_cast<Entity>(reinterpret_cast<uintptr_t>(pairHeader.actors[1]->userData));

		for (PxU32 i = 0; i < nbPairs; ++i)
		{
			const PxContactPair& pair = pairs[i];
			if (pair.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)
				events.push_back({ a,b,false,true });
			if (pair.events & PxPairFlag::eNOTIFY_TOUCH_LOST)
				events.push_back({ a,b,false,false });
		}
	}

	void onTrigger(PxTriggerPair* pairs, PxU32 count)override
	{
		for (PxU32 i = 0; i < count; ++i)
		{
			const PxTriggerPair& pair = pairs[i];
			if (pair.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				continue;

			Entity trigger = static_cast<Entity>(reinterpret_cast<uintptr_t>(pair.triggerActor->userData));
			Entity other = static_cast<Entity>(reinterpret_cast<uintptr_t>(pair.otherActor->userData));
			bool isEnter = pair.status & PxPairFlag::eNOTIFY_TOUCH_FOUND;
			events.push_back({ trigger,other,true,isEnter });
		}
	}

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}
};


PhysicsWorld::PhysicsWorld()
{
		
}

PhysicsWorld::~PhysicsWorld()
{
	PX_RELEASE(m_Scene);
	PX_RELEASE(m_Dispatcher);
	PX_RELEASE(m_Physics);
	PX_RELEASE(m_Foundation);
}

void PhysicsWorld::Init()
{
	m_Allocator = std::make_unique<PxDefaultAllocator>();
	m_ErrorCallback = std::make_unique<PxDefaultErrorCallback>();

	m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, *m_Allocator, *m_ErrorCallback);
	if (!m_Foundation)
	{
		throw std::runtime_error("PxCreateFoundation failed!");
	}

	m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, PxTolerancesScale(), true, nullptr);
	if (!m_Physics)
	{
		throw std::runtime_error("PxCreatePhysics failed!");
	}

	PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.8f, 0.0f);
	sceneDesc.staticStructure = PxPruningStructureType::eDYNAMIC_AABB_TREE;
	sceneDesc.dynamicStructure = PxPruningStructureType::eDYNAMIC_AABB_TREE;

	m_EventCallback = std::make_unique<EventCallback>();

	sceneDesc.filterShader = CollisionFilterShader;
	sceneDesc.simulationEventCallback = m_EventCallback.get();

	m_Dispatcher = PxDefaultCpuDispatcherCreate(4);
	sceneDesc.cpuDispatcher = m_Dispatcher;

	m_Scene = m_Physics->createScene(sceneDesc);
	if (!m_Scene)
	{
		throw std::runtime_error("createScene failed!");
	}

	m_DefaultMaterial = m_Physics->createMaterial(0.5f, 0.5f, 0.6f);
}

void PhysicsWorld::Update(float deltatime)
{
	if (m_Scene)
	{
		m_Scene->simulate(deltatime);
		m_Scene->fetchResults(true);
	}
}

void PhysicsWorld::AddRigidbody(Entity entity, const RigidBodyComponent& rigidBody, const ColliderComponent& collider)
{
	if (m_Physics == nullptr || m_Scene == nullptr)
	{
		return;
	}

	if(m_ActorMap.find(entity) != m_ActorMap.end())
	{
		return;
	}

	PxRigidActor* actor = CreatePhysicsActor(rigidBody, collider);
	if (actor)
	{
		m_Scene->addActor(*actor);
		m_ActorMap[entity] = actor;
	}
}

void PhysicsWorld::RemoveRigidbody(Entity entity)
{
	auto it = m_ActorMap.find(entity);
	if (it != m_ActorMap.end() && m_Scene)
	{
		m_Scene->removeActor(*it->second);
		it->second->release();
		m_ActorMap.erase(it);
	}
}

PxRigidActor* PhysicsWorld::CreatePhysicsActor(const RigidBodyComponent& rb, const ColliderComponent& collider)
{
	PxShape* shape = nullptr;

	switch (collider.shapeType)
	{
	case ColliderComponent::ShapeType::Box:
		shape = m_Physics->createShape(
			PxBoxGeometry(collider.size.x * 0.5f, collider.size.y * 0.5f, collider.size.z * 0.5f),
			*m_DefaultMaterial);
		break;
	case ColliderComponent::ShapeType::Sphere:
		shape = m_Physics->createShape(
			PxSphereGeometry(collider.radius),
			*m_DefaultMaterial
		);
		break;
	case ColliderComponent::ShapeType::Capsule:
		shape = m_Physics->createShape(
			PxCapsuleGeometry(collider.radius, collider.size.y * 0.5f),
			*m_DefaultMaterial
		);
		break;
	case ColliderComponent::ShapeType::Mesh:
		shape = m_Physics->createShape(
			PxSphereGeometry(collider.radius),
			*m_DefaultMaterial
		);
		break;
	}

	if (!shape)
	{
		return nullptr;
	}

	shape->setSimulationFilterData(PxFilterData(1, 1, 0, 0));
	shape->setQueryFilterData(PxFilterData(1,1,0,0));

	PxRigidActor* actor = nullptr;

	if (rb.isStatic)
	{
		actor = m_Physics->createRigidStatic(PxTransform(PxVec3(0, 0, 0)));
		if (actor)
		{
			actor->attachShape(*shape);
		}
	}
	else if (rb.isKinematic)
	{
		PxRigidDynamic* dynamic = m_Physics->createRigidDynamic(PxTransform(PxVec3(0, 0, 0)));
		if (dynamic)
		{
			dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
			dynamic->attachShape(*shape);
			actor = dynamic;
		}
	}
	else {
		PxRigidDynamic* dynamic = m_Physics->createRigidDynamic(PxTransform(PxVec3(0, 0, 0)));
		if (dynamic)
		{
			PxRigidBodyExt::updateMassAndInertia(*dynamic, 1000.0f);
			dynamic->setMass(rb.mass);

			if (!rb.useGravity)
			{
				dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
			}

			dynamic->attachShape(*shape);
			actor = dynamic;
		}
	}

	shape->release();
	return actor;
}

void PhysicsWorld::SyncTransforms(World& world)
{
	for (auto& [entity, actor] : m_ActorMap)
	{
		if(!actor || !world.HasComponent<TransformComponent>(entity))
		{
			continue;
		}

		PxTransform pxTransform = actor->getGlobalPose();
		PxVec3 pos = pxTransform.p;
		PxQuat rot = pxTransform.q;

		auto& transformComp = world.GetComponent<TransformComponent>(entity);
		DirectX::XMFLOAT3 position(pos.x, pos.y, pos.z);
		DirectX::XMFLOAT4 quaternion(rot.x, rot.y, rot.z, rot.w);

		DirectX::XMVECTOR quat = DirectX::XMLoadFloat4(&quaternion);
		DirectX::XMMATRIX rotMatrix = DirectX::XMMatrixRotationQuaternion(quat);
		DirectX::XMMATRIX transMatrix = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
		DirectX::XMMATRIX scaleMatrix = DirectX::XMMatrixIdentity();
		if (world.HasComponent<ColliderComponent>(entity))
		{
			const auto& collider = world.GetComponent<ColliderComponent>(entity);
			scaleMatrix = DirectX::XMMatrixScaling(collider.size.x, collider.size.y, collider.size.z);
		}

		const DirectX::XMMATRIX worldMatrix = scaleMatrix * rotMatrix * transMatrix;
		DirectX::XMStoreFloat4x4(&transformComp.world, worldMatrix);
	}
}

void PhysicsWorld::DispatchEvents(World& world)
{
	if (!m_ErrorCallback) return;
	for (auto& e : m_EventCallback->events)
	{
		auto notify = [&](Entity self, Entity other)
			{
				if (!world.HasComponent<ScriptComponent>(self)) return;
				auto& sc = world.GetComponent<ScriptComponent>(self);
				for (auto& b : sc.behaviors)
				{
					if (e.isTrigger)
						e.isEnter ? b->OnTriggerEnter(other) : b->OnTriggerExit(other);
					else
						e.isEnter ? b->OnCollisionEnter(other) : b->OnCollisionExit(other);
				}
			};
		notify(e.a, e.b);
		notify(e.b, e.a);
	}
	m_EventCallback->events.clear();
}

void PhysicsWorld::SetActorPose(Entity entity, const float3& position, const float4& rotation)
{
	auto it = m_ActorMap.find(entity);
	if (it == m_ActorMap.end() || it->second == nullptr)
	{
		return;
	}

	const PxQuat quat(rotation.x, rotation.y, rotation.z, rotation.w);
	it->second->setGlobalPose(PxTransform(PxVec3(position.x, position.y, position.z), quat));
}
