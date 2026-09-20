/*****************************************************************//**
 * \file   PhysicsWorld.cpp
 * \brief  物理の管理(PhysX)
 *
 * 作成者 keeeep
 * 作成日 2026/5/15
 * 更新履歴	5.15 PhysicsXの追加
 *			9.15 コンポーネントとの自動同期、トリガー / レイヤー / 材質の反映、
 *			     PhysX 本体をプロセスで共有、姿勢の書き戻しを position / rotation に
 * *********************************************************************/
#include "PhysicsWorld.hpp"

#include <algorithm>
#include <cstring>

#include "Components.hpp"
#include "Logger.hpp"
#include "MonoBehavior.hpp"
#include "Terrain.hpp"

#include <PxPhysicsAPI.h>

#define PX_RELEASE(x) if(x) { x->release(); x = nullptr; }

using namespace physx;

namespace
{
	// ------------------------------------------------------------------ //
	//   PhysX の本体(Foundation / Physics)はプロセスに 1 つしか作れない。
	//   以前はシーンごとに作っていて、物理を持つシーンが 2 つ同時にあると落ちた
	// ------------------------------------------------------------------ //
	struct PhysXSdk
	{
		PxDefaultAllocator allocator;
		PxDefaultErrorCallback errorCallback;
		PxFoundation* foundation = nullptr;
		PxPhysics* physics = nullptr;
		PxCooking* cooking = nullptr;	// HeightField(地形)を作るのに要る
		int refCount = 0;
	};

	PhysXSdk& Sdk()
	{
		static PhysXSdk sdk;
		return sdk;
	}

	PxPhysics* AcquireSdk()
	{
		PhysXSdk& s = Sdk();
		if (s.refCount == 0)
		{
			s.foundation = PxCreateFoundation(PX_PHYSICS_VERSION, s.allocator, s.errorCallback);
			if (!s.foundation) return nullptr;
			s.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *s.foundation, PxTolerancesScale(), true, nullptr);
			if (!s.physics) { PX_RELEASE(s.foundation); return nullptr; }

			// 地形の当たり判定に使う。無くても他の機能は動く
			s.cooking = PxCreateCooking(PX_PHYSICS_VERSION, *s.foundation,
				PxCookingParams(PxTolerancesScale()));
			if (!s.cooking)
				LOG->LogWarning("PhysicsWorld: PxCreateCooking に失敗(地形の当たり判定が作れません)");
		}
		++s.refCount;
		return s.physics;
	}

	void ReleaseSdk()
	{
		PhysXSdk& s = Sdk();
		if (s.refCount <= 0) return;
		if (--s.refCount == 0)
		{
			PX_RELEASE(s.cooking);
			PX_RELEASE(s.physics);
			PX_RELEASE(s.foundation);
		}
	}

	/// @brief word0 = 自分のレイヤーのビット / word1 = 衝突を許すレイヤーのマスク
	PxFilterData MakeFilter(const ColliderComponent& c)
	{
		return PxFilterData(1u << (c.layer & 31), c.collisionMask, 0, 0);
	}

	PxFilterFlags CollisionFilterShader(
		PxFilterObjectAttributes attributes0, PxFilterData filterData0,
		PxFilterObjectAttributes attributes1, PxFilterData filterData1,
		PxPairFlags& pairFlags, const void*, PxU32)
	{
		// どちらか片方でも相手のレイヤーを弾いていたら、ぶつからない(通り抜ける)
		if ((filterData0.word0 & filterData1.word1) == 0 ||
			(filterData1.word0 & filterData0.word1) == 0)
		{
			return PxFilterFlag::eSUPPRESS;
		}

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

	/// @brief 設定のハッシュ。変わったらアクターを作り直す
	size_t HashSettings(const RigidBodyComponent* rb, const ColliderComponent& c)
	{
		size_t h = 1469598103934665603ull;
		auto mix = [&h](const void* p, size_t n)
			{
				const auto* b = static_cast<const unsigned char*>(p);
				for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
			};
		const int shape = static_cast<int>(c.shapeType);
		mix(&shape, sizeof(shape));
		mix(&c.size, sizeof(c.size));
		mix(&c.radius, sizeof(c.radius));
		mix(&c.friction, sizeof(c.friction));
		mix(&c.restitution, sizeof(c.restitution));
		mix(&c.density, sizeof(c.density));
		mix(&c.isTrigger, sizeof(c.isTrigger));
		mix(&c.layer, sizeof(c.layer));
		mix(&c.collisionMask, sizeof(c.collisionMask));

		const bool hasRb = (rb != nullptr);
		mix(&hasRb, sizeof(hasRb));
		if (rb)
		{
			mix(&rb->mass, sizeof(rb->mass));
			mix(&rb->isKinematic, sizeof(rb->isKinematic));
			mix(&rb->isStatic, sizeof(rb->isStatic));
			mix(&rb->useGravity, sizeof(rb->useGravity));
		}
		return h;
	}

	/// @brief Transform のワールド姿勢(親を含む)を PhysX の姿勢にする
	PxTransform WorldPoseOf(World& world, Entity e)
	{
		if (!world.HasComponent<TransformComponent>(e)) return PxTransform(PxIdentity);

		using namespace DirectX;
		const auto& tr = world.GetComponent<TransformComponent>(e);

		// 親がいなければ position / rotation そのもの(world 行列はまだ古いことがある)
		if (tr.parent == INVALID_ENTITY || !world.HasComponent<TransformComponent>(tr.parent))
		{
			const XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&tr.rotation));
			XMFLOAT4 qn; XMStoreFloat4(&qn, q);
			return PxTransform(PxVec3(tr.position.x, tr.position.y, tr.position.z),
				PxQuat(qn.x, qn.y, qn.z, qn.w));
		}

		XMVECTOR s, r, t;
		XMMatrixDecompose(&s, &r, &t, XMLoadFloat4x4(&tr.world));
		XMFLOAT3 p; XMStoreFloat3(&p, t);
		XMFLOAT4 q; XMStoreFloat4(&q, XMQuaternionNormalize(r));
		return PxTransform(PxVec3(p.x, p.y, p.z), PxQuat(q.x, q.y, q.z, q.w));
	}

	size_t HashController(const CharacterControllerComponent& cc)
	{
		size_t h = 1469598103934665603ull;
		auto mix = [&h](const void* p, size_t n)
			{
				const auto* b = static_cast<const unsigned char*>(p);
				for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
			};
		mix(&cc.height, sizeof(cc.height));
		mix(&cc.radius, sizeof(cc.radius));
		mix(&cc.stepOffset, sizeof(cc.stepOffset));
		mix(&cc.slopeLimit, sizeof(cc.slopeLimit));
		mix(&cc.layer, sizeof(cc.layer));
		mix(&cc.collisionMask, sizeof(cc.collisionMask));
		return h;
	}

	// ------------------------------------------------------------------ //
	//   キャラクターの移動でどの形状にぶつかるか
	//   クエリの word0 = 自分のマスク(相手 word0 との AND は PhysX が判定)
	//            word3 = 自分のレイヤーのビット(相手のマスクとの AND はここで判定)
	// ------------------------------------------------------------------ //
	class ControllerQueryFilter : public PxQueryFilterCallback
	{
	public:
		PxQueryHitType::Enum preFilter(const PxFilterData& query, const PxShape* shape,
			const PxRigidActor*, PxHitFlags&) override
		{
			// トリガーは通り抜ける(通知はシミュレーション側の onTrigger で来る)
			if (shape->getFlags() & PxShapeFlag::eTRIGGER_SHAPE) return PxQueryHitType::eNONE;
			// 相手が自分のレイヤーを弾いている
			if ((shape->getQueryFilterData().word1 & query.word3) == 0) return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	ControllerQueryFilter& ControllerFilter()
	{
		static ControllerQueryFilter filter;
		return filter;
	}

	/// @brief レイ / 重なり判定でトリガーを無視する
	/// @note レイヤーの判定は PxQueryFilterData の word0 で PhysX 側が行う
	class QueryIgnoreTrigger : public PxQueryFilterCallback
	{
	public:
		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* shape,
			const PxRigidActor*, PxHitFlags&) override
		{
			if (shape->getFlags() & PxShapeFlag::eTRIGGER_SHAPE) return PxQueryHitType::eNONE;
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	QueryIgnoreTrigger& QueryFilter()
	{
		static QueryIgnoreTrigger filter;
		return filter;
	}

	/// @brief アクターから Entity を引く(userData に入れてある)
	Entity EntityOfActor(const PxRigidActor* actor)
	{
		if (!actor) return INVALID_ENTITY;
		return static_cast<Entity>(reinterpret_cast<uintptr_t>(actor->userData));
	}
}

// ------------------------------------------------------------------ //
//                           衝突の通知                                //
// ------------------------------------------------------------------ //

class PhysicsWorld::EventCallback : public PxSimulationEventCallback
{
public:
	std::vector<CollisionEvent> events;

	static Entity EntityOf(const PxActor* actor)
	{
		return static_cast<Entity>(reinterpret_cast<uintptr_t>(actor->userData));
	}

	void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override
	{
		// 削除済みのアクターが混ざることがある
		if (header.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1))
			return;

		const Entity a = EntityOf(header.actors[0]);
		const Entity b = EntityOf(header.actors[1]);
		for (PxU32 i = 0; i < count; ++i)
		{
			if (pairs[i].events & PxPairFlag::eNOTIFY_TOUCH_FOUND) events.push_back({ a, b, false, true });
			if (pairs[i].events & PxPairFlag::eNOTIFY_TOUCH_LOST)  events.push_back({ a, b, false, false });
		}
	}

	void onTrigger(PxTriggerPair* pairs, PxU32 count) override
	{
		for (PxU32 i = 0; i < count; ++i)
		{
			const PxTriggerPair& p = pairs[i];
			if (p.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				continue;
			const bool enter = (p.status & PxPairFlag::eNOTIFY_TOUCH_FOUND);
			events.push_back({ EntityOf(p.triggerActor), EntityOf(p.otherActor), true, enter });
		}
	}

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}
};

// ------------------------------------------------------------------ //
//                           初期化 / 破棄                             //
// ------------------------------------------------------------------ //

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld()
{
	RemoveAll();
	for (auto& [key, m] : m_Materials) PX_RELEASE(m);
	m_Materials.clear();

	PX_RELEASE(m_ControllerManager);	// シーンより先に解放する(中のコントローラーもまとめて消える)
	PX_RELEASE(m_Scene);
	PX_RELEASE(m_Dispatcher);
	if (m_EventCallback) ReleaseSdk();	// Init が成功していたときだけ参照を持っている
}

PxPhysics* PhysicsWorld::GetPhysics() const
{
	return Sdk().physics;
}

void PhysicsWorld::Init()
{
	if (m_Scene) return;   // 初期化済み

	PxPhysics* physics = AcquireSdk();
	if (!physics)
	{
		LOG->LogError("PhysicsWorld: PhysX の初期化に失敗しました");
		return;
	}
	m_EventCallback = std::make_unique<EventCallback>();

	PxSceneDesc sceneDesc(physics->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0.0f, -9.8f, 0.0f);
	sceneDesc.staticStructure = PxPruningStructureType::eDYNAMIC_AABB_TREE;
	sceneDesc.dynamicStructure = PxPruningStructureType::eDYNAMIC_AABB_TREE;
	sceneDesc.filterShader = CollisionFilterShader;
	sceneDesc.simulationEventCallback = m_EventCallback.get();
	// Kinematic 同士 / Kinematic と Static の接触も通知する(動く足場の上に乗ったか等)
	sceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
	sceneDesc.staticKineFilteringMode = PxPairFilteringMode::eKEEP;

	// 跳ね返りの足切り。既定(重力の0.2倍 ≒ 1.96m/s)だと、
	// よほど速く落ちない限り跳ね返りが丸ごと捨てられる
	sceneDesc.bounceThresholdVelocity = 0.5f;

	m_Dispatcher = PxDefaultCpuDispatcherCreate(4);
	sceneDesc.cpuDispatcher = m_Dispatcher;

	m_Scene = physics->createScene(sceneDesc);
	if (!m_Scene)
	{
		LOG->LogError("PhysicsWorld: createScene に失敗しました");
		return;
	}
	m_ControllerManager = PxCreateControllerManager(*m_Scene);
}

PxMaterial* PhysicsWorld::MaterialFor(float friction, float restitution)
{
	const auto key = std::make_pair(friction, restitution);
	auto it = m_Materials.find(key);
	if (it != m_Materials.end()) return it->second;

	PxMaterial* m = GetPhysics()->createMaterial(friction, friction, restitution);

	// 跳ね返りは「よく跳ねるほう」を採用する。
	// 平均だと、地面の restitution が 0 のときにボールが跳ねなくなる
	if (m) m->setRestitutionCombineMode(PxCombineMode::eMAX);

	m_Materials[key] = m;
	return m;
}

// ------------------------------------------------------------------ //
//                            アクター                                 //
// ------------------------------------------------------------------ //

PxRigidActor* PhysicsWorld::CreateActor(
	Entity entity, const RigidBodyComponent* rb, const ColliderComponent& c, BodyKind& outKind)
{
	PxPhysics* physics = GetPhysics();
	PxMaterial* material = MaterialFor(c.friction, c.restitution);
	if (!physics || !material) return nullptr;

	PxShape* shape = nullptr;
	switch (c.shapeType)
	{
	case ColliderComponent::ShapeType::Box:
		shape = physics->createShape(
			PxBoxGeometry((std::max)(0.001f, c.size.x * 0.5f), (std::max)(0.001f, c.size.y * 0.5f),
				(std::max)(0.001f, c.size.z * 0.5f)), *material, true);
		break;
	case ColliderComponent::ShapeType::Capsule:
		// PhysX のカプセルは X 軸方向なので、Z 軸まわりに 90 度回して縦(Y)にする。
		// size.y は円柱部分の長さ(両端の半球は含まない)
		shape = physics->createShape(
			PxCapsuleGeometry((std::max)(0.001f, c.radius), (std::max)(0.001f, c.size.y * 0.5f)), *material, true);
		if (shape) shape->setLocalPose(PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
		break;
	case ColliderComponent::ShapeType::Sphere:
	case ColliderComponent::ShapeType::Mesh:	// メッシュコライダーは未対応。球で代用
	default:
		shape = physics->createShape(PxSphereGeometry((std::max)(0.001f, c.radius)), *material, true);
		break;
	}
	if (!shape) return nullptr;

	// 接触を拾い始める距離 / めり込みを許す距離。
	// 小さすぎると細かい段差で弾かれ、大きすぎると浮いて見える
	shape->setContactOffset(0.02f);
	shape->setRestOffset(0.0f);

	shape->setSimulationFilterData(MakeFilter(c));
	shape->setQueryFilterData(MakeFilter(c));

	// トリガーは「重なりを通知するだけで、押し返さない」形状
	if (c.isTrigger)
	{
		shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
	}

	PxRigidActor* actor = nullptr;

	// RigidBody が無い / Static 指定 → 動かない壁や床
	if (rb == nullptr || rb->isStatic)
	{
		actor = physics->createRigidStatic(PxTransform(PxIdentity));
		outKind = BodyKind::Static;
		if (actor) actor->attachShape(*shape);
	}
	else
	{
		PxRigidDynamic* dynamic = physics->createRigidDynamic(PxTransform(PxIdentity));
		if (dynamic)
		{
			dynamic->attachShape(*shape);
			if (rb->isKinematic)
			{
				// スクリプトで Transform を動かすもの(動く足場など)
				dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
				outKind = BodyKind::Kinematic;
			}
			else
			{
				PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, (std::max)(0.001f, rb->mass));

				// 斜面を転がる箱は接触点が次々入れ替わる。
				// 既定(位置4回・速度1回)だと解ききれず、滑ったり引っかかったりする
				dynamic->setSolverIterationCounts(8, 2);

				// 止まった判定が早すぎると、転がっている途中で固まる
				dynamic->setSleepThreshold(0.01f);
				dynamic->setAngularDamping(0.05f);
				if (!rb->useGravity) dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
				outKind = BodyKind::Dynamic;
			}
			actor = dynamic;
		}
	}

	shape->release();   // アクターが参照を持つので、ここで手放してよい
	if (!actor) return nullptr;

	// 衝突通知で「どの Entity か」を引けるようにする(以前は入れておらず、相手が常に 0 だった)
	actor->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(entity));
	return actor;
}

void PhysicsWorld::ReleaseActor(ActorRecord& record)
{
	if (!record.actor) return;
	if (m_Scene) m_Scene->removeActor(*record.actor);
	record.actor->release();
	record.actor = nullptr;
}

void PhysicsWorld::RemoveRigidbody(Entity entity)
{
	auto it = m_Actors.find(entity);
	if (it == m_Actors.end()) return;
	ReleaseActor(it->second);
	m_Actors.erase(it);
}

void PhysicsWorld::ReleaseController(ControllerRecord& record)
{
	PX_RELEASE(record.controller);	// アクターもシーンから外れる
}

void PhysicsWorld::RemoveAll()
{
	for (auto& [e, record] : m_Terrains) ReleaseTerrain(record);
	m_Terrains.clear();

	for (auto& [e, record] : m_Controllers) ReleaseController(record);
	m_Controllers.clear();

	for (auto& [e, record] : m_Actors) ReleaseActor(record);
	m_Actors.clear();
	if (m_EventCallback) m_EventCallback->events.clear();
}

// ------------------------------------------------------------------ //
//                              同期                                   //
// ------------------------------------------------------------------ //

void PhysicsWorld::SyncFromWorld(World& world)
{
	if (!m_Scene) return;

	// ---- 1) 消えた Entity / Collider を外した Entity のアクターを外す ---- //
	for (auto it = m_Actors.begin(); it != m_Actors.end(); )
	{
		if (!world.IsEntityAlive(it->first) || !world.HasComponent<ColliderComponent>(it->first)
			|| world.HasComponent<CharacterControllerComponent>(it->first)
			|| world.HasComponent<TerrainComponent>(it->first))
		{
			ReleaseActor(it->second);
			it = m_Actors.erase(it);
		}
		else ++it;
	}

	// ---- 2) Collider を持つ Entity: 作る / 作り直す / Transform に追従させる ---- //
	world.Each<ColliderComponent>([&](Entity e, ColliderComponent& col)
		{
			// キャラクターは SyncControllers、地形は SyncTerrains の管轄
			if (world.HasComponent<CharacterControllerComponent>(e)) return;
			if (world.HasComponent<TerrainComponent>(e)) return;

			const RigidBodyComponent* rb = world.HasComponent<RigidBodyComponent>(e)
				? &world.GetComponent<RigidBodyComponent>(e) : nullptr;
			const size_t settings = HashSettings(rb, col);
			const PxTransform pose = WorldPoseOf(world, e);

			auto it = m_Actors.find(e);
			if (it != m_Actors.end() && it->second.settings != settings)
			{
				// インスペクタで形や質量を変えた
				ReleaseActor(it->second);
				m_Actors.erase(it);
				it = m_Actors.end();
			}

			if (it == m_Actors.end())
			{
				BodyKind kind = BodyKind::Static;
				PxRigidActor* actor = CreateActor(e, rb, col, kind);
				if (!actor) return;
				actor->setGlobalPose(pose);
				m_Scene->addActor(*actor);
				m_Actors[e] = ActorRecord{ actor, settings, kind };
				return;
			}

			ActorRecord& rec = it->second;
			switch (rec.kind)
			{
			case BodyKind::Kinematic:
				// スクリプトが動かした Transform へ、物理として滑らかに移動させる
				static_cast<PxRigidDynamic*>(rec.actor)->setKinematicTarget(pose);
				break;
			case BodyKind::Static:
			{
				// 動かないはずだが、エディタ / スクリプトで動かされたら追従する
				const PxTransform cur = rec.actor->getGlobalPose();
				if (!(cur.p - pose.p).isZero() || !(cur.q == pose.q)) rec.actor->setGlobalPose(pose);
				break;
			}
			case BodyKind::Dynamic:
				// 物理が動かすので Transform からは読まない(ワープは SetActorPose)
				break;
			}
		});

	SyncControllers(world);
	SyncTerrains(world);
}

void PhysicsWorld::SyncControllers(World& world)
{
	if (!m_ControllerManager) return;

	for (auto it = m_Controllers.begin(); it != m_Controllers.end(); )
	{
		if (!world.IsEntityAlive(it->first) || !world.HasComponent<CharacterControllerComponent>(it->first))
		{
			ReleaseController(it->second);
			it = m_Controllers.erase(it);
		}
		else ++it;
	}

	world.Each<CharacterControllerComponent>([&](Entity e, CharacterControllerComponent& cc)
		{
			if (!world.HasComponent<TransformComponent>(e)) return;
			// Transformの位置 + Centerがカプセルの中心
			const float halfHeight = (std::max(0.001f,cc.height) * 0.5f + (std::max)(0.001f, cc.radius));
			const PxVec3 foot = WorldPoseOf(world, e).p
				+ PxVec3(cc.center.x,cc.center.y - halfHeight,cc.center.z);
			const size_t settings = HashController(cc);

			auto it = m_Controllers.find(e);
			if (it != m_Controllers.end() && it->second.settings != settings)
			{
				ReleaseController(it->second);
				m_Controllers.erase(it);
				it = m_Controllers.end();
			}

			if (it == m_Controllers.end())
			{
				PxCapsuleControllerDesc desc;
				desc.height = (std::max)(0.001f, cc.height);
				desc.radius = (std::max)(0.001f, cc.radius);
				desc.stepOffset = (std::min)(cc.stepOffset, desc.height + desc.radius * 2.0f);
				desc.slopeLimit = std::cos(cc.slopeLimit * PxPi / 180.0f);
				desc.nonWalkableMode = PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
				desc.climbingMode = PxCapsuleClimbingMode::eCONSTRAINED;
				desc.contactOffset = 0.02f;
				desc.upDirection = PxVec3(0.0f, 1.0f, 0.0f);
				desc.material = MaterialFor(0.5f, 0.0f);
				desc.position = PxExtendedVec3(foot.x, foot.y + desc.radius + desc.height * 0.5f, foot.z);
				if (!desc.isValid())
				{
					LOG->LogWarning("CharacterController: 設定が不正です(Height / Radius / StepOffset)");
					return;
				}

				PxController* controller = m_ControllerManager->createController(desc);
				if (!controller) return;
				controller->setFootPosition(PxExtendedVec3(foot.x, foot.y, foot.z));

				// 衝突 / トリガー通知と、他のコライダーのレイヤー判定に使う
				PxRigidDynamic* actor = controller->getActor();
				actor->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e));
				const PxFilterData filter(1u << (cc.layer & 31), cc.collisionMask, 0, 0);
				PxShape* shape = nullptr;
				actor->getShapes(&shape, 1);
				if (shape)
				{
					shape->setSimulationFilterData(filter);
					shape->setQueryFilterData(filter);
				}

				cc.velocity = { 0.0f, 0.0f, 0.0f };
				m_Controllers[e] = ControllerRecord{ controller, settings, float3{ foot.x, foot.y, foot.z } };
				return;
			}

			// スクリプト / エディタが Transform を直接動かしていたらワープ
			ControllerRecord& rec = it->second;
			const PxVec3 diff = foot - PxVec3(rec.lastFoot.x, rec.lastFoot.y, rec.lastFoot.z);
			if (diff.magnitudeSquared() > 1e-8f)
			{
				rec.controller->setFootPosition(PxExtendedVec3(foot.x, foot.y, foot.z));
				rec.lastFoot = { foot.x, foot.y, foot.z };
			}
		});
}

void PhysicsWorld::ReleaseTerrain(TerrainRecord& record)
{
	if (record.actor)
	{
		if (m_Scene) m_Scene->removeActor(*record.actor);
		record.actor->release();
		record.actor = nullptr;
	}
	PX_RELEASE(record.heightField);
}

void PhysicsWorld::SyncTerrains(World& world)
{
	if (!m_Scene) return;

	PxCooking* cooking = Sdk().cooking;
	if (!cooking) return;

	// 消えた地形を外す
	for (auto it = m_Terrains.begin(); it != m_Terrains.end(); )
	{
		if (!world.IsEntityAlive(it->first) || !world.HasComponent<TerrainComponent>(it->first))
		{
			ReleaseTerrain(it->second);
			it = m_Terrains.erase(it);
		}
		else ++it;
	}

	world.Each<TerrainComponent>([&](Entity e, TerrainComponent& t)
		{
			const int cols = t.gridX + 1;	// X 方向の点の数
			const int rows = t.gridZ + 1;	// Z 方向の点の数
			if (t.Heights.size() != static_cast<size_t>(cols) * rows) return;

			const size_t settings = Terrain::HashSettings(t);

			// Transform のスケールは PxTransform に入らないので、
			// HeightField の間隔と高さの倍率に掛け込む
			float3 scale{ 1.0f, 1.0f, 1.0f };
			if (world.HasComponent<TransformComponent>(e))
			{
				using namespace DirectX;
				XMVECTOR s, r, tr;
				if (XMMatrixDecompose(&s, &r, &tr,
					XMLoadFloat4x4(&world.GetComponent<TransformComponent>(e).world)))
				{
					XMStoreFloat3(&scale, s);
				}
			}

			auto it = m_Terrains.find(e);
			if (it != m_Terrains.end() &&
				(it->second.settings != settings ||
					it->second.heightsVersion != t.heightsVersion ||
					std::fabs(it->second.scale.x - scale.x) > 1e-4f ||
					std::fabs(it->second.scale.y - scale.y) > 1e-4f ||
					std::fabs(it->second.scale.z - scale.z) > 1e-4f))
			{
				// 形が変わった。作り直す
				ReleaseTerrain(it->second);
				m_Terrains.erase(it);
				it = m_Terrains.end();
			}

			const PxTransform pose = WorldPoseOf(world, e);

			if (it != m_Terrains.end())
			{
				// 位置だけ追従させる
				if (!(it->second.actor->getGlobalPose().p - pose.p).isZero())
					it->second.actor->setGlobalPose(pose);
				return;
			}

			// ---- 高さを PhysX の形式に詰める ---- //
			// HeightField は「行 = X、列 = Z」。高さは 16bit 整数で持ち、
			// heightScale を掛けてワールドの高さになる
			std::vector<PxHeightFieldSample> samples(static_cast<size_t>(cols) * rows);
			for (int z = 0; z < rows; ++z)
			{
				for (int x = 0; x < cols; ++x)
				{
					const float h = t.Heights[static_cast<size_t>(z) * cols + x];
					auto& smp = samples[static_cast<size_t>(x) * rows + z];	// 行=X, 列=Z
					smp.height = static_cast<PxI16>(std::clamp(h, 0.0f, 1.0f) * 32767.0f);
					smp.materialIndex0 = 0;
					smp.materialIndex1 = 0;
					smp.clearTessFlag();
				}
			}

			PxHeightFieldDesc desc;
			desc.nbRows = static_cast<PxU32>(cols);
			desc.nbColumns = static_cast<PxU32>(rows);
			desc.samples.data = samples.data();
			desc.samples.stride = sizeof(PxHeightFieldSample);

			PxHeightField* hf = cooking->createHeightField(
				desc, GetPhysics()->getPhysicsInsertionCallback());
			if (!hf)
			{
				LOG->LogWarning("PhysicsWorld: HeightField の生成に失敗しました");
				return;
			}

			// 摩擦などは Collider があればそれに従う。無ければ既定値
			float friction = 0.5f, restitution = 0.0f;
			unsigned int layerBit = 1u, mask = 0xFFFFFFFFu;
			if (world.HasComponent<ColliderComponent>(e))
			{
				const auto& c = world.GetComponent<ColliderComponent>(e);
				friction = c.friction;
				restitution = c.restitution;
				layerBit = 1u << (c.layer & 31);
				mask = c.collisionMask;
			}

			// 高さ 0 の地形でも 0 除算にしない
			const float heightScale =
				(std::max)(t.Height, 0.001f) / 32767.0f * (std::max)(scale.y, 0.0001f);
			const PxHeightFieldGeometry geom(hf, PxMeshGeometryFlags(),
				heightScale,
				t.Width / t.gridX * (std::max)(scale.x, 0.0001f),	// 行(X)の間隔
				t.Depth / t.gridZ * (std::max)(scale.z, 0.0001f));	// 列(Z)の間隔

			PxShape* shape = GetPhysics()->createShape(geom, *MaterialFor(friction, restitution), true);
			if (!shape)
			{
				hf->release();
				return;
			}

			// HeightField の原点は角。メッシュは中心が原点なので半分ずらす
			shape->setLocalPose(PxTransform(PxVec3(
				-t.Width * 0.5f * scale.x, 0.0f, -t.Depth * 0.5f * scale.z)));

			const PxFilterData filter(layerBit, mask, 0, 0);
			shape->setSimulationFilterData(filter);
			shape->setQueryFilterData(filter);

			PxRigidStatic* actor = GetPhysics()->createRigidStatic(pose);
			if (!actor)
			{
				shape->release();
				hf->release();
				return;
			}
			actor->attachShape(*shape);
			shape->release();

			actor->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e));
			m_Scene->addActor(*actor);

			m_Terrains[e] = TerrainRecord{ actor, hf, settings, t.heightsVersion, scale };
			LOG->LogInfo("Terrain: 当たり判定を作成 "
				+ std::to_string(cols) + "x" + std::to_string(rows));
		});
}

void PhysicsWorld::MoveCharacters(World& world, float deltaTime)
{
	if (!m_Scene || deltaTime <= 0.0f) return;
	const PxVec3 gravity = m_Scene->getGravity();

	for (auto& [e, rec] : m_Controllers)
	{
		if (!rec.controller || !world.IsEntityAlive(e)) continue;
		if (!world.HasComponent<CharacterControllerComponent>(e) || !world.HasComponent<TransformComponent>(e)) continue;
		auto& cc = world.GetComponent<CharacterControllerComponent>(e);

		// 接地中は軽く押し付ける(下り坂で浮いて isGrounded がちらつくのを防ぐ)
		if (cc.isGrounded && cc.velocity.y < 0.0f) cc.velocity.y = -2.0f;
		cc.velocity.x += gravity.x * cc.gravityScale * deltaTime;
		cc.velocity.y += gravity.y * cc.gravityScale * deltaTime;
		cc.velocity.z += gravity.z * cc.gravityScale * deltaTime;

		const PxVec3 disp(
			cc.pendingMove.x + cc.velocity.x * deltaTime,
			cc.pendingMove.y + cc.velocity.y * deltaTime,
			cc.pendingMove.z + cc.velocity.z * deltaTime);
		cc.pendingMove = { 0.0f, 0.0f, 0.0f };

		const PxFilterData query(cc.collisionMask, 0, 0, 1u << (cc.layer & 31));
		PxControllerFilters filters(&query, &ControllerFilter());
		filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

		const PxControllerCollisionFlags hit = rec.controller->move(disp, 0.0001f, deltaTime, filters);
		cc.isGrounded = hit.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);
		cc.hitCeiling = hit.isSet(PxControllerCollisionFlag::eCOLLISION_UP);
		if (cc.isGrounded && cc.velocity.y < 0.0f) cc.velocity.y = 0.0f;
		if (cc.hitCeiling && cc.velocity.y > 0.0f) cc.velocity.y = 0.0f;	// 天井に頭をぶつけたら落ちる

		// 足元の位置を Transform へ(回転はスクリプトに任せる)
		const PxExtendedVec3 foot = rec.controller->getFootPosition();
		const float halfHeight = (std::max)(0.001f, cc.height) * 0.5f + (std::max)(0.001f, cc.radius);
		auto& tr = world.GetComponent<TransformComponent>(e);
		tr.position = {
			static_cast<float>(foot.x) - cc.center.x,
			static_cast<float>(foot.y) - cc.center.y + halfHeight,
			static_cast<float>(foot.z) - cc.center.z,
		};
		tr.RebuildWorld();
		rec.lastFoot = { static_cast<float>(foot.x), static_cast<float>(foot.y), static_cast<float>(foot.z) };
	}
}

void PhysicsWorld::Update(float deltaTime)
{
	if (!m_Scene) return;
	m_Scene->simulate(deltaTime);
	m_Scene->fetchResults(true);
}

void PhysicsWorld::SyncTransforms(World& world)
{
	using namespace DirectX;

	for (auto& [entity, rec] : m_Actors)
	{
		if (rec.kind != BodyKind::Dynamic || !rec.actor) continue;
		if (!world.HasComponent<TransformComponent>(entity)) continue;

		const PxTransform pose = rec.actor->getGlobalPose();
		auto& tr = world.GetComponent<TransformComponent>(entity);

		XMVECTOR pos = XMVectorSet(pose.p.x, pose.p.y, pose.p.z, 1.0f);
		XMVECTOR rot = XMVectorSet(pose.q.x, pose.q.y, pose.q.z, pose.q.w);

		// 親がいれば、ワールド姿勢を親の空間へ戻す
		if (tr.parent != INVALID_ENTITY && world.HasComponent<TransformComponent>(tr.parent))
		{
			const auto& parent = world.GetComponent<TransformComponent>(tr.parent);
			XMVECTOR det;
			const XMMATRIX invParent = XMMatrixInverse(&det, XMLoadFloat4x4(&parent.world));
			const XMMATRIX local = XMMatrixRotationQuaternion(rot) * XMMatrixTranslationFromVector(pos) * invParent;
			XMVECTOR s;
			XMMatrixDecompose(&s, &rot, &pos, local);
		}

		// 以前は world 行列だけ書き換えていて、position が古いまま(スクリプトから見て動かない)、
		// 固定更新が走らないフレームは TransformSystem に元の位置へ戻されてちらついていた
		XMStoreFloat3(&tr.position, pos);
		XMStoreFloat4(&tr.rotation, rot);
		tr.SyncEulerFromQuaternion();
		tr.RebuildWorld();
	}
}

void PhysicsWorld::DispatchEvents(World& world)
{
	if (!m_EventCallback) return;

	// 通知の中でスクリプトが Entity を消すことがあるので、先に取り出してから回す
	std::vector<CollisionEvent> events;
	events.swap(m_EventCallback->events);

	for (const auto& e : events)
	{
		auto notify = [&](Entity self, Entity other)
			{
				if (!world.IsEntityAlive(self) || !world.HasComponent<ScriptComponent>(self)) return;
				auto& sc = world.GetComponent<ScriptComponent>(self);
				for (auto& b : sc.behaviors)
				{
					if (!b) continue;
					if (e.isTrigger) e.isEnter ? b->OnTriggerEnter(other) : b->OnTriggerExit(other);
					else             e.isEnter ? b->OnCollisionEnter(other) : b->OnCollisionExit(other);
				}
			};
		notify(e.a, e.b);
		notify(e.b, e.a);
	}
}

void PhysicsWorld::SetActorPose(Entity entity, const float3& position, const float4& rotation)
{
	auto it = m_Actors.find(entity);
	if (it == m_Actors.end() || !it->second.actor) return;
	it->second.actor->setGlobalPose(PxTransform(
		PxVec3(position.x, position.y, position.z), PxQuat(rotation.x, rotation.y, rotation.z, rotation.w)));
}

void PhysicsWorld::SetGravity(const float3& gravity)
{
	if (m_Scene) m_Scene->setGravity(PxVec3(gravity.x, gravity.y, gravity.z));
}

// ------------------------------------------------------------------ //
//                     問い合わせ(スクリプト用)                        //
// ------------------------------------------------------------------ //

bool PhysicsWorld::Raycast(
	const float3& origin, const float3& direction,
	float maxDistance, unsigned int layerMask, RayHit& outHit) const
{
	outHit = RayHit{};
	if (!m_Scene) return false;

	PxVec3 dir(direction.x, direction.y, direction.z);
	if (dir.magnitudeSquared() < 1e-12f) return false;
	dir.normalize();

	// word0 に相手のレイヤービットが入っているので、マスクとの AND で絞る
	const PxQueryFilterData filter(
		PxFilterData(layerMask, 0, 0, 0),
		PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);

	PxRaycastBuffer buffer;
	const bool hit = m_Scene->raycast(
		PxVec3(origin.x, origin.y, origin.z), dir, (std::max)(0.0001f, maxDistance),
		buffer, PxHitFlag::eDEFAULT, filter, &QueryFilter());

	if (!hit || !buffer.hasBlock) return false;

	const PxRaycastHit& b = buffer.block;
	outHit.entity = EntityOfActor(b.actor);
	outHit.point = { b.position.x, b.position.y, b.position.z };
	outHit.normal = { b.normal.x, b.normal.y, b.normal.z };
	outHit.distance = b.distance;
	return true;
}

int PhysicsWorld::OverlapSphere(
	const float3& center, float radius, unsigned int layerMask,
	Entity* outEntities, int maxCount) const
{
	if (!m_Scene || !outEntities || maxCount <= 0) return 0;

	const PxQueryFilterData filter(
		PxFilterData(layerMask, 0, 0, 0),
		PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);

	// 一度に受け取れる数。足りなければ maxCount までで打ち切る
	constexpr PxU32 kBufferSize = 64;
	PxOverlapHit hits[kBufferSize];
	PxOverlapBuffer buffer(hits, kBufferSize);

	const bool found = m_Scene->overlap(
		PxSphereGeometry((std::max)(0.0001f, radius)),
		PxTransform(PxVec3(center.x, center.y, center.z)),
		buffer, filter, &QueryFilter());
	if (!found) return 0;

	int count = 0;
	for (PxU32 i = 0; i < buffer.getNbAnyHits() && count < maxCount; ++i)
	{
		const Entity e = EntityOfActor(buffer.getAnyHit(i).actor);
		if (e == INVALID_ENTITY) continue;

		// 同じ Entity の別の形状が複数当たることがあるので、重複を省く
		bool already = false;
		for (int k = 0; k < count; ++k) if (outEntities[k] == e) { already = true; break; }
		if (!already) outEntities[count++] = e;
	}
	return count;
}

bool PhysicsWorld::AddForce(Entity entity, const float3& force, bool impulse)
{
	auto it = m_Actors.find(entity);
	if (it == m_Actors.end() || it->second.kind != BodyKind::Dynamic) return false;

	auto* body = static_cast<PxRigidDynamic*>(it->second.actor);
	body->addForce(PxVec3(force.x, force.y, force.z),
		impulse ? PxForceMode::eIMPULSE : PxForceMode::eFORCE, true);
	return true;
}

bool PhysicsWorld::SetVelocity(Entity entity, const float3& velocity)
{
	auto it = m_Actors.find(entity);
	if (it == m_Actors.end() || it->second.kind != BodyKind::Dynamic) return false;

	auto* body = static_cast<PxRigidDynamic*>(it->second.actor);
	body->setLinearVelocity(PxVec3(velocity.x, velocity.y, velocity.z), true);
	return true;
}

bool PhysicsWorld::GetVelocity(Entity entity, float3& outVelocity) const
{
	auto it = m_Actors.find(entity);
	if (it == m_Actors.end() || it->second.kind != BodyKind::Dynamic) return false;

	const PxVec3 v = static_cast<const PxRigidDynamic*>(it->second.actor)->getLinearVelocity();
	outVelocity = { v.x, v.y, v.z };
	return true;
}
