#pragma once

#include "Defines.hpp"
#include "World.hpp"
#include "Components.hpp"
#include "DirectX.hpp"

class Mesh;
class Material;

class Player
{
public:
	Player(
		_In_ World& world,
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const DirectXApp::PipelineStateTable pipelinestates,
		_In_ const ComPtr<ID3D12PipelineState>& wirePso);
	~Player() = default;

	void Update(_In_ float deltaTime);
	void SetPosition(const _In_ float3& position);
	void SetRotation(const _In_ float3& rotation);
	void Move(const _In_ float3& direction);
	void SetVelocity(_In_ const float3& velocity);

	Entity GetEntity() const { return m_Entity; }
	float3 GetPosition() const;
	float3 GetRotation() const;

private:
	void InitComponents(const ComPtr<ID3D12Device>& device);
	void InitMesh(const ComPtr<ID3D12Device>& device);
	void InitMaterial();
	void InitPhysics();

	World& m_World;
	Entity m_Entity{};

	ComPtr<ID3D12Device> m_Device;
	DirectXApp::PipelineStateTable m_PipelineStates{};
	ComPtr<ID3D12PipelineState> m_WirePso;

	float3 m_Position{ 0.0f, 0.0f, 0.0f };
	float3 m_Rotation{ 0.0f, 0.0f, 0.0f };
	float3 m_Velocity{ 0.0f, 0.0f, 0.0f };
};