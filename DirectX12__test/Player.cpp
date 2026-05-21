#include "Player.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "ModelLoader.hpp"
#include <DirectXMath.h>

Player::Player(
	World& world,
	const ComPtr<ID3D12Device>& device,
	const DirectXApp::PipelineStateTable pipelinestates,
	const ComPtr<ID3D12PipelineState>& wirePso)
	: m_World(world), m_Device(device), m_PipelineStates(pipelinestates), m_WirePso(wirePso)
{
	m_Entity = m_World.CreateEntity();
	InitComponents(device);
}

void Player::SetPosition(const float3& position)
{
	m_Position = position;
}

void Player::SetRotation(const float3& rotation)
{
	m_Rotation = rotation;
}

void Player::Move(const float3& direction)
{
	m_Position.x += direction.x;
	m_Position.y += direction.y;
	m_Position.z += direction.z;
}

void Player::SetVelocity(const float3& velocity)
{
	m_Velocity = velocity;
}

float3 Player::GetPosition() const
{
	return m_Position;
}

float3 Player::GetRotation() const
{
	return m_Rotation;
}

void Player::InitComponents(const ComPtr<ID3D12Device>& device)
{
	TransformComponent transform{};
	DirectX::XMStoreFloat4x4(&transform.world, DirectX::XMMatrixIdentity());
	m_World.AddComponent<TransformComponent>(m_Entity, transform);

	// メッシュとマテリアルの初期化
	InitMesh(device);
	InitMaterial();

	// 物理の初期化
	InitPhysics();
}

void Player::InitMesh(const ComPtr<ID3D12Device>& device)
{
	auto loadResult = ModelLoader::LoadFromFile(device, "Assets/Player.fbx", 0.01f);

	auto mesh = loadResult.mesh;
	if (mesh == nullptr)
	{
		mesh = MakeShared<Mesh>();
		mesh->CreateCube(device);
	}

	m_World.AddComponent<MeshComponent>(m_Entity, MeshComponent{ mesh });
}

void Player::InitMaterial()
{
	auto material = MakeShared<Material>();
	material->Init(m_Device, m_PipelineStates, m_WirePso);

	material->SetTextureFromFile(L"Assets/Mutant_diffuse.png");

	m_World.AddComponent<MaterialComponent>(m_Entity, MaterialComponent{ material });
}

void Player::InitPhysics()
{
	// RigidBody コンポーネントの追加
	RigidBodyComponent rigidBody{};
	rigidBody.mass = 1.0f;
	rigidBody.isKinematic = false;
	rigidBody.isStatic = false;
	m_World.AddComponent<RigidBodyComponent>(m_Entity, rigidBody);

	// コライダーコンポーネントの追加（カプセル形状）
	ColliderComponent collider{};
	collider.shapeType = ColliderComponent::ShapeType::Capsule;
	collider.radius = 0.5f;
	collider.size = float3(1.0f, 2.0f, 1.0f);
	collider.friction = 0.5f;
	collider.restitution = 0.3f;
	collider.density = 1.0f;
	m_World.AddComponent<ColliderComponent>(m_Entity, collider);
}

void Player::Update(float deltaTime)
{
	if (!m_World.HasComponent<TransformComponent>(m_Entity))
	{
		return;
	}

	auto& transform = m_World.GetComponent<TransformComponent>(m_Entity);

	// 回転行列の構築
	DirectX::XMMATRIX rotX = DirectX::XMMatrixRotationX(m_Rotation.x);
	DirectX::XMMATRIX rotY = DirectX::XMMatrixRotationY(m_Rotation.y);
	DirectX::XMMATRIX rotZ = DirectX::XMMatrixRotationZ(m_Rotation.z);
	DirectX::XMMATRIX rotMatrix = rotX * rotY * rotZ;

	// 平行移動行列の構築
	DirectX::XMMATRIX transMatrix = DirectX::XMMatrixTranslation(
		m_Position.x, m_Position.y, m_Position.z);

	// ワールド行列の計算
	DirectX::XMMATRIX worldMatrix = rotMatrix * transMatrix;
	DirectX::XMStoreFloat4x4(&transform.world, worldMatrix);

	// 速度を位置に適用
	m_Position.x += m_Velocity.x * deltaTime;
	m_Position.y += m_Velocity.y * deltaTime;
	m_Position.z += m_Velocity.z * deltaTime;
}