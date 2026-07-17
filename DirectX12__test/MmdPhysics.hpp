#pragma once

#include "Defines.hpp"
#include "ModelData.hpp"
#include <DirectXMath.h>
#include <vector>
#include <cstdint>

namespace physx {
    class PxPhysics; class PxScene; class PxRigidActor;
    class PxMaterial; class PxJoint; class PxDefaultCpuDispatcher;
}

class MmdPhysics
{
public:
    // 剛体/ジョイントとスケルトンからPhysXアクターを構築
    void Init(physx::PxPhysics* physics, const PmxPhysics& phys, const Skeleton& skel);
    void Destroy();

    // 毎フレーム: global(各ボーンのワールド行列) を入出力
    //   入力: FK/IK/付与後の global, 出力: 物理反映後の global
    void Step(std::vector<DirectX::XMMATRIX>& global, float dt);

    // 追従設定 + simulate
	void StepBegin(std::vector<DirectX::XMMATRIX>& global,float dt);

    // 前回結果を回収して書き戻し
	void StepFetch(std::vector<DirectX::XMMATRIX>& global);

    bool IsValid() const { return m_Scene != nullptr; }
    ~MmdPhysics() { Destroy(); }

private:
    struct Body
    {
        physx::PxRigidActor* actor = nullptr;
        int boneIndex = -1;
		uint8_t physicsType = 0;
        matrix offset{};
        matrix invOffset{};
    };

    physx::PxScene* m_Scene                     = nullptr;
	physx::PxMaterial* m_Material               = nullptr;
    physx::PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
    std::vector<Body> m_Bodies;
    std::vector<physx::PxJoint*> m_Joints;
    bool m_FirstStep = true;
    float m_Accum = 0.0f;
    bool m_SimPending = false;
};

