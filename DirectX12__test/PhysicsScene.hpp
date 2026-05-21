#pragma once

#include "Scene.hpp"
#include "Defines.hpp"
#include "World.hpp"
#include "Systems.hpp"
#include "PhysicsWorld.hpp"

class Camera;

class PhysicsScene :
    public Scene
{
public:
    PhysicsScene(
        _In_ const ComPtr<ID3D12Device>& device,
        _In_ const DirectXApp::PipelineStateTable pipelinestates,
        _In_ const ComPtr<ID3D12PipelineState>& wirePso);
    ~PhysicsScene() final = default;

    void Update() final;
    void Draw(_In_ const RenderContext& renderContext) final;

    std::unique_ptr<Camera> m_Camera;

    World m_World;
    RenderSystem m_RenderSystem;
    PhysicsWorld m_PhysicsWorld;

    std::shared_ptr<Mesh> m_SharedMesh;
    std::shared_ptr<Material> m_SharedMaterial;

    Entity m_GroundEntity = INVALID_ENTITY;
    Entity m_BoxEntity = INVALID_ENTITY;

    // ---- UI ---- //
    void DrawUI();

    void SpawnBox(
        _In_ const float3& pos,
        _In_ const float3& size,
        _In_ float mass);

	void ResetBox();

	float3 m_SpawnPosition{ 0.0f, 5.0f, 0.0f };
	float m_SpawnSize = 1.0f;
	float m_SpawnMass = 1.0f;
	int m_BoxCount = 0;
};

