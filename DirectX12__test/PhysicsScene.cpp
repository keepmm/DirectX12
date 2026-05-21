#include "PhysicsScene.hpp"
#include "Camera.hpp"
#include "Components.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Time.hpp"
#include "imguiinit.hpp"

PhysicsScene::PhysicsScene(const ComPtr<ID3D12Device>& device, const DirectXApp::PipelineStateTable pipelinestates, const ComPtr<ID3D12PipelineState>& wirePso)
{
	m_Camera = MakeUnique<Camera>();

	m_SharedMesh = MakeShared<Mesh>();
	m_SharedMesh->CreateCube(device);

	m_SharedMaterial = MakeShared<Material>();
	m_SharedMaterial->Init(device, pipelinestates, wirePso);
	
	m_GroundEntity = m_World.CreateEntity();
	{
		TransformComponent transform{};
		const DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(20.0f, 1.0f, 20.0f);
		const DirectX::XMMATRIX translate = DirectX::XMMatrixTranslation(0.0f, -0.5f, 0.0f);
		DirectX::XMStoreFloat4x4(&transform.world, scale * translate);

		m_World.AddComponent<TransformComponent>(m_GroundEntity, transform);
		m_World.AddComponent<MeshComponent>(m_GroundEntity, MeshComponent{ m_SharedMesh });
		m_World.AddComponent<MaterialComponent>(m_GroundEntity, MaterialComponent{ m_SharedMaterial });

		RigidBodyComponent rb{};
		rb.isStatic = true;

		ColliderComponent collider{};
		collider.shapeType = ColliderComponent::ShapeType::Box;
		collider.size = float3(20.0f, 1.0f, 20.0f);

		m_World.AddComponent<RigidBodyComponent>(m_GroundEntity, rb);
		m_World.AddComponent<ColliderComponent>(m_GroundEntity, collider);

		m_PhysicsWorld.AddRigidbody(m_GroundEntity, rb, collider);
		m_PhysicsWorld.SetActorPose(m_GroundEntity, float3(0.0f,-0.5f,0.0f),float4(0.0f,0.0f,0.0f,1.0f));
	}


}


void PhysicsScene::Update()
{
	if (m_Camera)
	{
		m_Camera->Update();
	}

	const float deltatime = TIME->GetDeltaTime();
	m_PhysicsWorld.Update(deltatime);
	m_PhysicsWorld.SyncTransforms(m_World);

	const float3 lightDir = { -0.3f,-1.0f,-0.2f };
	const float4 lightcolor = { 1.0f,1.0f,1.0f,1.0f };
	const float4 ambientcolor = { 0.2f,0.2f,0.2f,1.0f };

	m_World.Each<MaterialComponent>(
		[&](Entity, MaterialComponent& material)
		{
			if (material.material == nullptr)
			{
				return;
			}

			material.material->SetAmbientColor(ambientcolor);
			material.material->SetLightColor(lightcolor);
			material.material->SetLightDir(lightDir);
		}
	);
}

void PhysicsScene::Draw(const RenderContext& render)
{
	RenderContext context = render;
	context.view = m_Camera->GetViewMatrix(false);
	context.projection = m_Camera->GetProjectionMatrix(false);
	m_RenderSystem.Draw(m_World, context);

	DrawUI();
}

void PhysicsScene::SpawnBox(const float3& pos, const float3& size, float mass)
{
	// à¯êîÇÉNÉâÉìÉvÇ∑ÇÈ
	const float clampedMass = std::max(0.1f, mass);

	const float3 clampedSize = {
		std::max(0.1f, size.x),
		std::max(0.1f, size.y),
		std::max(0.1f, size.z)
	};

	m_BoxEntity = m_World.CreateEntity();
	{
		TransformComponent transform{};
		const DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(clampedSize.x, clampedSize.y, clampedSize.z);
		const DirectX::XMMATRIX translate = DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
		DirectX::XMStoreFloat4x4(&transform.world, scale * translate);

		m_World.AddComponent<TransformComponent>(m_BoxEntity, transform);
		m_World.AddComponent<MeshComponent>(m_BoxEntity, MeshComponent{ m_SharedMesh });
		m_World.AddComponent<MaterialComponent>(m_BoxEntity, MaterialComponent{ m_SharedMaterial });

		RigidBodyComponent rb{};
		rb.mass = clampedMass;

		ColliderComponent collider{};
		collider.shapeType = ColliderComponent::ShapeType::Box;
		collider.size = clampedSize;
		m_World.AddComponent<RigidBodyComponent>(m_BoxEntity, rb);
		m_World.AddComponent<ColliderComponent>(m_BoxEntity, collider);

		m_PhysicsWorld.AddRigidbody(m_BoxEntity, rb, collider);
		m_PhysicsWorld.SetActorPose(m_BoxEntity, float3(0.0f, 5.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f));
	}

	++m_BoxCount;
}

void PhysicsScene::DrawUI()
{
	ImGui::Begin(u8("Physics Scene"));
	ImGui::Text("Box Count : %d", m_BoxCount);

	ImGui::SliderFloat3(u8("Spawn Position"), &m_SpawnPosition.x, -10.0f, 10.0f);
	ImGui::SliderFloat(u8("Spawn Size"), &m_SpawnSize, 0, 5.0f);
	ImGui::SliderFloat(u8("Spawn Mass"), &m_SpawnMass, 0, 10.0f);
	if (ImGui::Button("Spawn Box"))
	{
		SpawnBox(m_SpawnPosition, float3(m_SpawnSize, m_SpawnSize, m_SpawnSize), m_SpawnMass);
	}
	ImGui::End();
}
