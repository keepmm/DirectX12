#include "MmdPhysics.hpp"
#include "Logger.hpp"
#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;
using namespace DirectX;

// ---- 変換ヘルパ(反転なし: 既存PhysicsWorldに合わせる) ----
static PxTransform ToPx(const XMMATRIX& m)
{
    XMVECTOR s, q, t;
    XMMatrixDecompose(&s, &q, &t, m);
    XMFLOAT4 qf; XMStoreFloat4(&qf, q);
    XMFLOAT3 tf; XMStoreFloat3(&tf, t);
    PxQuat pq(qf.x, qf.y, qf.z, qf.w);
    return PxTransform(PxVec3(tf.x, tf.y, tf.z), pq.getNormalized());
}
static XMMATRIX FromPx(const PxTransform& t)
{
    XMVECTOR q = XMVectorSet(t.q.x, t.q.y, t.q.z, t.q.w);
    XMMATRIX r = XMMatrixRotationQuaternion(q);
    r.r[3] = XMVectorSet(t.p.x, t.p.y, t.p.z, 1.0f);
    return r;
}

// 非衝突グループを扱うフィルタシェーダ
static PxFilterFlags MmdFilter(
    PxFilterObjectAttributes a0, PxFilterData fd0,
    PxFilterObjectAttributes a1, PxFilterData fd1,
    PxPairFlags& pairFlags, const void*, PxU32)
{
    // fd.word0 = 1<<group, fd.word1 = 非衝突マスク
    if ((fd0.word1 & fd1.word0) || (fd1.word1 & fd0.word0))
        return PxFilterFlag::eSUPPRESS;
    pairFlags = PxPairFlag::eCONTACT_DEFAULT;
    return PxFilterFlag::eDEFAULT;
}

void MmdPhysics::Init(PxPhysics* physics, const PmxPhysics& phys, const Skeleton& skel)
{
    if (!physics || phys.rigidBodies.empty()) return;

    // --- 専用シーン(ゲームプレイ物理と分離) ---
    PxSceneDesc desc(physics->getTolerancesScale());
    desc.gravity = PxVec3(0.0f, -9.8f, 0.0f);
    m_Dispatcher = PxDefaultCpuDispatcherCreate(8);
    desc.cpuDispatcher = m_Dispatcher;
    desc.filterShader = MmdFilter;

    desc.solverType = PxSolverType::eTGS;
    desc.flags |= PxSceneFlag::eENABLE_STABILIZATION;

    m_Scene = physics->createScene(desc);
    m_Material = physics->createMaterial(0.5f, 0.5f, 0.0f);

    // --- 各ボーンのバインドワールド(平行移動のみ) ---
    const size_t nodeCount = skel.nodes.size();
    std::vector<XMMATRIX> bindGlobal(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i)
    {
        XMMATRIX local = XMLoadFloat4x4(&skel.nodes[i].localTransform);
        int p = skel.nodes[i].parentIndex;
        bindGlobal[i] = (p >= 0) ? local * bindGlobal[p] : local;
    }

    // --- 剛体 ---
    m_Bodies.resize(phys.rigidBodies.size());
    for (size_t i = 0; i < phys.rigidBodies.size(); ++i)
    {
        const PmxRigidBody& rb = phys.rigidBodies[i];
        Body& b = m_Bodies[i];
        b.boneIndex = rb.boneIndex;
        b.physicsType = rb.physicsType;

        // 剛体のバインドワールド = R(euler) * T(pos)
        XMMATRIX rbWorld =
            XMMatrixRotationRollPitchYaw(rb.rotation.x, rb.rotation.y, rb.rotation.z) *
            XMMatrixTranslation(rb.position.x, rb.position.y, rb.position.z);

        XMMATRIX boneBind = (rb.boneIndex >= 0 && rb.boneIndex < (int)nodeCount)
            ? bindGlobal[rb.boneIndex] : XMMatrixIdentity();
        b.offset = rbWorld * XMMatrixInverse(nullptr, boneBind);
        b.invOffset = XMMatrixInverse(nullptr, b.offset);

        // 形状(MMDカプセルはY軸→PhysXはX軸なのでZ回りに90度)
        PxShape* shape = nullptr;
        const PxQuat capRot(PxHalfPi, PxVec3(0, 0, 1));
        switch (rb.shape)
        {
        case 0: // 球
            shape = physics->createShape(PxSphereGeometry(std::max(rb.size.x, 0.01f)), *m_Material);
            break;
        case 1: // 箱
            shape = physics->createShape(PxBoxGeometry(
                std::max(rb.size.x, 0.01f), std::max(rb.size.y, 0.01f), std::max(rb.size.z, 0.01f)), *m_Material);
            break;
        case 2: // カプセル
            shape = physics->createShape(PxCapsuleGeometry(
                std::max(rb.size.x, 0.01f), std::max(rb.size.y * 0.5f, 0.01f)), *m_Material);
            if (shape) shape->setLocalPose(PxTransform(capRot));
            break;
        }
        if (!shape) continue;

        // 衝突フィルタ
        PxFilterData fd;
        fd.word0 = (1u << rb.group);
        fd.word1 = rb.noCollisionMask;
        shape->setSimulationFilterData(fd);
        shape->setContactOffset(0.02f);
        shape->setRestOffset(0.0f);

        PxTransform pose = ToPx(rbWorld);
        PxRigidActor* actor = nullptr;
        if (rb.physicsType == 0)
        {
            // ボーン追従: キネマティック
            PxRigidDynamic* dyn = physics->createRigidDynamic(pose);
            dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            dyn->attachShape(*shape);
            actor = dyn;
        }
        else
        {
            // 物理: ダイナミック
            PxRigidDynamic* dyn = physics->createRigidDynamic(pose);
            dyn->attachShape(*shape);
            PxRigidBodyExt::updateMassAndInertia(*dyn, std::max(rb.mass, 0.01f));
            dyn->setSolverIterationCounts(4, 2);
            dyn->setLinearDamping(std::max(rb.linearDamping, 0.3f));
            dyn->setAngularDamping(std::max(rb.angularDamping, 0.4f));   // 角減衰を強めてパタパタ抑制
            dyn->setMaxLinearVelocity(40.0f);
            dyn->setMaxAngularVelocity(30.0f);
            dyn->setSleepThreshold(0.02f);          // 静止時に眠らせて計算スキップ
            dyn->setStabilizationThreshold(0.002f);
			dyn->setMaxDepenetrationVelocity(1.0f);
            actor = dyn;
        }
        shape->release();
        b.actor = actor;
        m_Scene->addActor(*actor);
    }

    // --- ジョイント(6DOF) ---
    for (const PmxJoint& jt : phys.joints)
    {
        if (jt.rigidBodyA < 0 || jt.rigidBodyB < 0) continue;
        PxRigidActor* a = m_Bodies[jt.rigidBodyA].actor;
        PxRigidActor* bd = m_Bodies[jt.rigidBodyB].actor;
        if (!a || !bd) continue;

        XMMATRIX jWorld =
            XMMatrixRotationRollPitchYaw(jt.rotation.x, jt.rotation.y, jt.rotation.z) *
            XMMatrixTranslation(jt.position.x, jt.position.y, jt.position.z);
        PxTransform jointPose = ToPx(jWorld);

        PxTransform localA = a->getGlobalPose().getInverse() * jointPose;
        PxTransform localB = bd->getGlobalPose().getInverse() * jointPose;

        PxD6Joint* joint = PxD6JointCreate(*physics, a, localA, bd, localB);
        if (!joint) continue;

        // 移動: 0範囲はLOCK、範囲ありはLIMIT
        auto linMotion = [&](PxD6Axis::Enum ax, float lo, float hi)
            {
                joint->setMotion(ax, (lo == 0.0f && hi == 0.0f) ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED);
            };
        linMotion(PxD6Axis::eX, jt.moveLimitLower.x, jt.moveLimitUpper.x);
        linMotion(PxD6Axis::eY, jt.moveLimitLower.y, jt.moveLimitUpper.y);
        linMotion(PxD6Axis::eZ, jt.moveLimitLower.z, jt.moveLimitUpper.z);

        // 回転: 0範囲はLOCK、範囲ありはLIMIT
        auto angMotion = [&](PxD6Axis::Enum ax, float lo, float hi)
            {
                joint->setMotion(ax, (lo == 0.0f && hi == 0.0f) ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED);
            };
        angMotion(PxD6Axis::eTWIST, jt.rotLimitLower.x, jt.rotLimitUpper.x);
        angMotion(PxD6Axis::eSWING1, jt.rotLimitLower.y, jt.rotLimitUpper.y);
        angMotion(PxD6Axis::eSWING2, jt.rotLimitLower.z, jt.rotLimitUpper.z);

        // ツイスト制限(X回転)を実際に設定
        if (!(jt.rotLimitLower.x == 0.0f && jt.rotLimitUpper.x == 0.0f))
            joint->setTwistLimit(PxJointAngularLimitPair(jt.rotLimitLower.x, jt.rotLimitUpper.x));

        // スイング制限(Y/Z回転, コーンで対称近似)
        {
            float y = std::max(std::fabs(jt.rotLimitLower.y), std::fabs(jt.rotLimitUpper.y));
            float z = std::max(std::fabs(jt.rotLimitLower.z), std::fabs(jt.rotLimitUpper.z));
            if (y > 1e-4f || z > 1e-4f)
            {
                y = std::clamp(y, 0.01f, PxPi * 0.99f);
                z = std::clamp(z, 0.01f, PxPi * 0.99f);
                joint->setSwingLimit(PxJointLimitCone(y, z));
            }
        }

        // 移動制限(範囲がある場合のみ。MMDは大抵0なので通常不要)
        {
            float mx = std::max(std::fabs(jt.moveLimitLower.x), std::fabs(jt.moveLimitUpper.x));
            float my = std::max(std::fabs(jt.moveLimitLower.y), std::fabs(jt.moveLimitUpper.y));
            float mz = std::max(std::fabs(jt.moveLimitLower.z), std::fabs(jt.moveLimitUpper.z));
            float m = std::max({ mx, my, mz });
            if (m > 1e-4f)
                joint->setLinearLimit(PxJointLinearLimit(physics->getTolerancesScale(), m));
        }



        // ジョイントで繋がった剛体同士は衝突させない
        joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);
        linMotion(PxD6Axis::eX, jt.moveLimitLower.x, jt.moveLimitUpper.x);
        linMotion(PxD6Axis::eY, jt.moveLimitLower.y, jt.moveLimitUpper.y);
        linMotion(PxD6Axis::eZ, jt.moveLimitLower.z, jt.moveLimitUpper.z);
        angMotion(PxD6Axis::eTWIST, jt.rotLimitLower.x, jt.rotLimitUpper.x);
        angMotion(PxD6Axis::eSWING1, jt.rotLimitLower.y, jt.rotLimitUpper.y);
        angMotion(PxD6Axis::eSWING2, jt.rotLimitLower.z, jt.rotLimitUpper.z);
        m_Joints.push_back(joint);
    }

    LOG->LogInfo("MmdPhysics: bodies=" + std::to_string(m_Bodies.size())
        + " joints=" + std::to_string(m_Joints.size()));
}

void MmdPhysics::Step(std::vector<XMMATRIX>& global, float dt)
{
    if (!m_Scene) return;

    // 全剛体を現在のボーン姿勢へ揃えるヘルパ
    auto resyncAll = [&]()
        {
            for (Body& b : m_Bodies)
            {
                if (!b.actor || b.boneIndex < 0) continue;
                PxTransform pose = ToPx(b.offset * global[b.boneIndex]);
                if (auto* dyn = b.actor->is<PxRigidDynamic>())
                {
                    dyn->setGlobalPose(pose);
                    if (!(dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
                    {
                        dyn->setLinearVelocity(PxVec3(0));
                        dyn->setAngularVelocity(PxVec3(0));
                    }
                }
            }
        };
    auto writeback = [&]()
        {
            for (Body& b : m_Bodies)
            {
                if (!b.actor || b.physicsType == 0 || b.boneIndex < 0) continue;
                global[b.boneIndex] = b.invOffset * FromPx(b.actor->getGlobalPose());
            }
        };

    // 初回: 全剛体を現在姿勢で初期化
    if (m_FirstStep) { resyncAll(); m_FirstStep = false; return; }

    // ヒッチ防御: dtが大きすぎる(ロード直後/一時停止明け等)は再同期して発散回避
    if (dt > 1.0f / 20.0f)
    {
        resyncAll();
        writeback();
        m_Accum = 0.0f;
        return;
    }

    // 固定60Hzでサブステップ(最大4回)
    const float step = 1.0f / 60.0f;
    m_Accum += dt;
    int guard = 0;
    while (m_Accum >= step && guard++ < 1)
    {
        for (Body& b : m_Bodies)
        {
            if (!b.actor || b.physicsType != 0 || b.boneIndex < 0) continue;
            if (auto* dyn = b.actor->is<PxRigidDynamic>())
                dyn->setKinematicTarget(ToPx(b.offset * global[b.boneIndex]));
        }
        m_Scene->simulate(step);
        m_Scene->fetchResults(true);
        m_Accum -= step;

		if (m_Accum > step) m_Accum = step; // 2ステップ以上溜まる場合は1ステップに抑える
    }

    writeback();
}

void MmdPhysics::StepBegin(std::vector<DirectX::XMMATRIX>& global, float dt)
{
    if (!m_Scene)return;
    // 全剛体を現在のボーン姿勢へ揃える(初期化・ヒッチ復帰用)
    auto resyncAll = [&]()
        {
            for (Body& b : m_Bodies)
            {
                if (!b.actor || b.boneIndex < 0) continue;
                PxTransform pose = ToPx(b.offset * global[b.boneIndex]);
                if (auto* dyn = b.actor->is<PxRigidDynamic>())
                {
                    dyn->setGlobalPose(pose);
                    if (!(dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
                    {
                        dyn->setLinearVelocity(PxVec3(0));
                        dyn->setAngularVelocity(PxVec3(0));
                    }
                }
            }
        };
    // 物理剛体のglobalを現在の剛体姿勢で更新(ヒッチ時にメッシュを合わせる)
    auto writeback = [&]()
        {
            for (Body& b : m_Bodies)
            {
                if (!b.actor || b.physicsType == 0 || b.boneIndex < 0) continue;
                global[b.boneIndex] = b.invOffset * FromPx(b.actor->getGlobalPose());
            }
        };

    const float step = 1.0f / 60.0f;

    // 初回: 全剛体を現在姿勢で初期化(simulateは投げない)
    if (m_FirstStep)
    {
        resyncAll();
        m_FirstStep = false;
        m_Accum = 0.0f;
        return;
    }

    // ヒッチ防御: dtが大きすぎる(ロード直後/一時停止明け)は再同期して発散回避
    if (dt > 1.0f / 20.0f)
    {
        // 未回収のsimulateがあれば捨てる
        if (m_SimPending) { m_Scene->fetchResults(true); m_SimPending = false; }
        resyncAll();
        writeback();
        m_Accum = 0.0f;
        return;
    }

    // 固定60Hz: 1ステップ分溜まったら追従ターゲット設定してsimulateを投げる
    m_Accum += dt;
    if (m_Accum >= step)
    {
        // 直前に投げたものが未回収なら先に回収(通常はStepFetchで回収済み)
        if (m_SimPending) { m_Scene->fetchResults(true); m_SimPending = false; }

        for (Body& b : m_Bodies)
        {
            if (!b.actor || b.physicsType != 0 || b.boneIndex < 0) continue;
            if (auto* dyn = b.actor->is<PxRigidDynamic>())
                dyn->setKinematicTarget(ToPx(b.offset * global[b.boneIndex]));
        }

        m_Scene->simulate(step); 
        m_SimPending = true;

        m_Accum -= step;
        if (m_Accum > step) m_Accum = step;   // 溜め込み過ぎ防止
    }
}

void MmdPhysics::StepFetch(std::vector<DirectX::XMMATRIX>& global)
{
    if(!m_Scene || !m_SimPending) return;

    m_Scene->fetchResults(true);   // 裏で完了済みなのでほぼ待たない
    m_SimPending = false;

    for (Body& b : m_Bodies)
    {
        if (!b.actor || b.physicsType == 0 || b.boneIndex < 0) continue;
        global[b.boneIndex] = b.invOffset * FromPx(b.actor->getGlobalPose());
    }
}

void MmdPhysics::Destroy()
{
    if (m_Scene && m_SimPending) { m_Scene->fetchResults(true); m_SimPending = false; }
    for (auto* j : m_Joints) if (j) j->release();
    m_Joints.clear();
    for (auto& b : m_Bodies) if (b.actor) b.actor->release();
    m_Bodies.clear();
    if (m_Scene) { m_Scene->release(); m_Scene = nullptr; }
    if (m_Dispatcher) { m_Dispatcher->release(); m_Dispatcher = nullptr; }
    if (m_Material) { m_Material->release(); m_Material = nullptr; }
}