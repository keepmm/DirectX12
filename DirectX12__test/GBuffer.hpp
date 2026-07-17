#pragma once
#include "RenderTexture.hpp"

class GBuffer
{
public:
    static constexpr UINT RT_COUNT = 3;

    void Init(UINT width, UINT height,
        ID3D12Resource* depthSrvCpu,
        ID3D12Resource* envSrvCpu,
        ID3D12Resource* shadowSrvCpu,
        UINT envMips);

    void TransitionToWrite(ID3D12GraphicsCommandList* cmd)
    {
        for (auto& rt : m_RT) rt.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    void TransitionToRead(ID3D12GraphicsCommandList* cmd)
    {
        for (auto& rt : m_RT) rt.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(UINT i) const { return m_RT[i].GetRTV(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvTableStart() const { return m_SrvTableStartGpu; }

private:
    void BuildSrvTable(
        ID3D12Resource* depthSrvCpu,
        ID3D12Resource* envSrvCpu,
        ID3D12Resource* shadowSrvCpu); 

    RenderTexture m_RT[RT_COUNT];
    D3D12_GPU_DESCRIPTOR_HANDLE m_SrvTableStartGpu{};
    UINT m_Width = 0, m_Height = 0;
	UINT m_EnvMips = 1;
};