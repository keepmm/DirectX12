/*****************************************************************//**
 * \file   PhysicsWorld.hpp
 * \brief  物理の管理(PhysX)
 *
 * 作成者 keeeep
 * 作成日 2026/5/15
 * 更新履歴	5.15 PhysicsXの追加
 *			9.15 コンポーネントとの自動同期、トリガー / レイヤー / 材質の反映、
 *			     PhysX 本体をプロセスで共有、姿勢の書き戻しを position / rotation に
 * *********************************************************************/
#pragma once

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "World.hpp"
#include "Defines.hpp"

namespace physx
{
	class PxPhysics;
	class PxScene;
	class PxDefaultCpuDispatcher;
	class PxRigidActor;
	class PxMaterial;
	class PxHeightField;
	class PxCooking;
	class PxRigidStatic;
	class PxControllerManager;
	class PxController;
}

struct RigidBodyComponent;
struct ColliderComponent;
struct TerrainComponent;
struct CharacterControllerComponent;

class PhysicsWorld
{
public:
	PhysicsWorld();	// unique_ptr<EventCallback>(不完全型)のため .cpp で定義する
	~PhysicsWorld();

	PhysicsWorld(const PhysicsWorld&) = delete;
	PhysicsWorld& operator=(const PhysicsWorld&) = delete;

	/// @brief 初期化(2 回目以降は何もしない)
	void Init();
	bool IsReady() const noexcept { return m_Scene != nullptr; }

	/// @brief コンポーネントとアクターを揃える(シミュレーションの直前に呼ぶ)
	/// @note Collider を持つ Entity にアクターを作り、設定が変われば作り直し、
	///       消えた Entity のアクターは外す。Kinematic / Static は Transform に追従させる
	void SyncFromWorld(_In_ World& world);

	void Update(_In_ float deltaTime);

	/// @brief 動く剛体の姿勢を TransformComponent に書き戻す(シミュレーションの直後に呼ぶ)
	void SyncTransforms(_In_ World& world);

	/// @brief 衝突 / トリガーの通知をスクリプトへ配る
	void DispatchEvents(_In_ World& world);

	/// @brief キャラクターを動かして足元の位置を Transform に書き戻す(Update の直後、SyncTransforms の前に呼ぶ)
	void MoveCharacters(_In_ World& world, _In_ float deltaTime);

	/// @brief 全アクターを外す(Play 開始時に、エディタでの配置から作り直すため)
	void RemoveAll();

	/// @brief 1 つ外す(次の SyncFromWorld で、まだ Collider があれば作り直される)
	void RemoveRigidbody(_In_ Entity entity);

	/// @brief 瞬間移動(ワープ)。普段の移動は Transform を動かす(Kinematic)か、力を加える
	void SetActorPose(_In_ Entity entity, _In_ const float3& position, _In_ const float4& rotation);

	void SetGravity(_In_ const float3& gravity);

	/// @brief レイが当たったもの
	struct RayHit
	{
		Entity entity = INVALID_ENTITY;
		float3 point{};		// 当たった位置
		float3 normal{};	// 当たった面の向き
		float distance = 0.0f;
	};

	/// @brief 光線を飛ばして最初に当たったものを返す
	/// @param layerMask 当たり判定を取るレイヤーのビットマスク
	/// @note トリガーは無視する
	bool Raycast(_In_ const float3& origin, _In_ const float3& direction,
		_In_ float maxDistance, _In_ unsigned int layerMask, _Out_ RayHit& outHit) const;

	/// @brief 球の中に重なっているものを集める
	/// @return 見つかった数(maxCount で打ち切る)
	int OverlapSphere(_In_ const float3& center, _In_ float radius,
		_In_ unsigned int layerMask, _Out_writes_(maxCount) Entity* outEntities, _In_ int maxCount) const;

	/// @brief 力を加える(Dynamic な剛体のみ)
	/// @param impulse true なら瞬間的な衝撃(ノックバックなど)、false なら継続する力
	bool AddForce(_In_ Entity entity, _In_ const float3& force, _In_ bool impulse);

	bool SetVelocity(_In_ Entity entity, _In_ const float3& velocity);
	bool GetVelocity(_In_ Entity entity, _Out_ float3& outVelocity) const;

	physx::PxPhysics* GetPhysics() const;
	physx::PxScene* GetScene() const { return m_Scene; }

private:
	enum class BodyKind { Static, Kinematic, Dynamic };

	struct ActorRecord
	{
		physx::PxRigidActor* actor = nullptr;
		size_t settings = 0;		// 作ったときの設定のハッシュ(変わったら作り直す)
		BodyKind kind = BodyKind::Static;
	};

	struct CollisionEvent
	{
		Entity a;
		Entity b;
		bool isTrigger;
		bool isEnter;	// false = Exit
	};

	class EventCallback;

	physx::PxRigidActor* CreateActor(
		_In_ Entity entity,
		_In_opt_ const RigidBodyComponent* rb,
		_In_ const ColliderComponent& collider,
		_Out_ BodyKind& outKind);

	physx::PxMaterial* MaterialFor(float friction, float restitution);

	void ReleaseActor(_Inout_ ActorRecord& record);

	struct ControllerRecord
	{
		physx::PxController* controller = nullptr;
		size_t settings = 0;
		float3 lastFoot{};	// 最後に書き戻した足元。Transform がこれと違えばワープさせる
	};

	void SyncControllers(_In_ World& world);
	void ReleaseController(_Inout_ ControllerRecord& record);

	struct TerrainRecord
	{
		physx::PxRigidStatic* actor = nullptr;
		physx::PxHeightField* heightField = nullptr;
		size_t settings = 0;				// 大きさ / 分割数(変わったら作り直す)
		unsigned int heightsVersion = 0;	// 彫った回数(変わったら作り直す)
		float3 scale{ 1.0f, 1.0f, 1.0f };	// 作ったときの Transform のスケール
	};

	/// @brief 地形の当たり判定(HeightField)を作る / 更新する
	void SyncTerrains(_In_ World& world);
	void ReleaseTerrain(_Inout_ TerrainRecord& record);

	std::unique_ptr<EventCallback> m_EventCallback;
	physx::PxScene* m_Scene = nullptr;
	physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;

	std::unordered_map<Entity, ActorRecord> m_Actors;

	physx::PxControllerManager* m_ControllerManager = nullptr;
	std::unordered_map<Entity, ControllerRecord> m_Controllers;
	std::unordered_map<Entity, TerrainRecord> m_Terrains;

	// 摩擦 / 反発の組ごとに材質を使い回す
	std::map<std::pair<float, float>, physx::PxMaterial*> m_Materials;
};
