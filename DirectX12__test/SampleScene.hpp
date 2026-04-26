#pragma once

#include "Scene.hpp"
#include "Defines.hpp"
#include "World.hpp"
#include "Systems.hpp"

class Camera;

class SampleScene :
	public Scene
{
public:
	SampleScene(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const ComPtr<ID3D12PipelineState>& solidPso,
		_In_ const ComPtr<ID3D12PipelineState>& wirePso);
	~SampleScene() final;

	void Update() final;
	void Draw(_In_ const RenderContext& renderContext) final;

private:
	std::unique_ptr<Camera> m_Camera;

	World m_World;
	Entity m_CubeEntity = INVALID_ENTITY;

	SpinSystem m_SpinSystem;
	RenderSystem m_RenderSystem;

	bool m_Wireframe = true;
};