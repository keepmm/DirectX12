// GBuffer.cpp
#include "GBuffer.hpp"
#include "DirectX.hpp"

void GBuffer::Init(UINT width, UINT height,
    ID3D12Resource* depthSrvCpu,
    ID3D12Resource* envSrvCpu,
    ID3D12Resource* shadowSrvCpu,
    UINT envMips)
{
    m_Width = width; m_Height = height;
	m_EnvMips = envMips;

    m_RT[0].Init(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    m_RT[1].Init(width, height, DXGI_FORMAT_R10G10B10A2_UNORM);
    m_RT[2].Init(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    BuildSrvTable(depthSrvCpu, envSrvCpu, shadowSrvCpu);
}

void GBuffer::BuildSrvTable(ID3D12Resource* depthSrvCpu, ID3D12Resource* envSrvCpu, ID3D12Resource* shadowSrvCpu)
{
    auto& srv = APP->GetSrvAllocator();
    auto dev  = APP->GetDevice();

    UINT base = srv.AllocateRange(7);
    m_SrvTableStartGpu = srv.Gpu(base);

    // 連続スロットへ直接SRVを作るヘルパ
    const auto MakeSrv = [&](UINT slot, ID3D12Resource* res, DXGI_FORMAT fmt, UINT mips)
        {
            if (!res) return;
            D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
            d.Format = fmt;
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels = mips;
            dev->CreateShaderResourceView(res, &d, srv.Cpu(base + slot));
        };

    MakeSrv(0, m_RT[0].GetResource().Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 1);    // t0 Albedo
    MakeSrv(2, m_RT[1].GetResource().Get(), DXGI_FORMAT_R10G10B10A2_UNORM, 1); // t2 Normal
    MakeSrv(3, m_RT[2].GetResource().Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 1);    // t3 ORM
    MakeSrv(4, depthSrvCpu, DXGI_FORMAT_R32_FLOAT, 1);                              // t4 Depth
    MakeSrv(5, envSrvCpu, DXGI_FORMAT_R32G32B32A32_FLOAT, m_EnvMips);        // t5 Env(null可)
    MakeSrv(6, shadowSrvCpu, DXGI_FORMAT_R32_FLOAT, 1);                             // t6 Shadow(null可)
}
