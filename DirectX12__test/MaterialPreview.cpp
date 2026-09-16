#include "MaterialPreview.hpp"

#include "d3dx12.h"
#include "DirectX.hpp"
#include "Material.hpp"
#include "Logger.hpp"
#include "ShaderTypes.hpp"
#include "ConstantBufferAllocator.hpp"

MaterialPreview& MaterialPreview::Get()
{
    static MaterialPreview instance;
    return instance;
}

void MaterialPreview::Init(UINT size)
{
    if (m_Initialized) return;
    m_Size = size;

    // 32x16 だとサムネイルには十分すぎるくらい滑らか
    m_Sphere.CreateSphere(32, 16);

    m_Initialized = EnsureDepth();
}

bool MaterialPreview::EnsureDepth()
{
    if (m_Depth != nullptr) return true;

    auto device = APP->GetDevice();
    if (device == nullptr) return false;

    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS, m_Size, m_Size,
        1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    // ClearValue は実フォーマットで指定
    CD3DX12_CLEAR_VALUE clear(DXGI_FORMAT_D32_FLOAT, 1.0f, 0);
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

    if (FAILED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
        IID_PPV_ARGS(m_Depth.GetAddressOf()))))
    {
        LOG->LogError("MaterialPreview: 深度バッファの作成に失敗");
        return false;
    }

    UINT dsvIndex = 0;
    APP->GetDsvAllocator().Allocate(dsvIndex);
    m_DsvHandle = APP->GetDsvAllocator().Cpu(dsvIndex);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(m_Depth.Get(), &dsvDesc, m_DsvHandle);
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE MaterialPreview::Request(const std::shared_ptr<Material>& mat)
{
    if (!mat || !m_Initialized) return {};

    Slot* slot = FindOrCreate(mat);
    if (slot == nullptr) return {};

    slot->idleFrames = 0;   // 見られているので生かす
    slot->dirty = true;     // パラメータをいじっている最中は毎フレーム描き直す

    return slot->rt.IsValid() ? slot->rt.GetSRV() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

MaterialPreview::Slot* MaterialPreview::FindOrCreate(const std::shared_ptr<Material>& mat)
{
    auto it = m_Slots.find(mat.get());
    if (it != m_Slots.end())
    {
        Slot& slot = *it->second;

        // 破棄されたマテリアルと同じアドレスに、別のマテリアルが作られた。
        // 枠(RT)はそのまま使い回し、中身を描き直す
        if (slot.material.expired())
        {
            slot.material = mat;
            slot.dirty = true;
            slot.deadFrames = 0;
        }
        return &slot;
    }

    auto slot = std::make_unique<Slot>();
    if (FAILED(slot->rt.Init(m_Size, m_Size))) return nullptr;
    slot->material = mat;

    Slot* raw = slot.get();
    m_Slots.emplace(mat.get(), std::move(slot));
    return raw;
}

void MaterialPreview::RenderRequested(ID3D12GraphicsCommandList* cmd, UINT frameIndex)
{
    if (cmd == nullptr || !m_Initialized) return;

    // マテリアルが破棄されてから RT を消すまで待つフレーム数。
    // 破棄された同じフレームで ImGui::Image が既にこの SRV を積んでいるので、
    // すぐ Release すると ImGui の描画が解放済みのテクスチャを踏んで落ちる
    constexpr int kReleaseDelay = RTV_NUM + 1;

    for (auto it = m_Slots.begin(); it != m_Slots.end();)
    {
        Slot& slot = *it->second;
        auto mat = slot.material.lock();

        const bool dead = !mat && (++slot.deadFrames > kReleaseDelay);
        const bool idle = mat && (++slot.idleFrames > 60);   // しばらく誰も見ていない

        if (dead || idle)
        {
            slot.rt.Release();
            it = m_Slots.erase(it);
            continue;
        }

        if (mat && slot.dirty)
        {
            RenderOne(cmd, slot, *mat, frameIndex);
            slot.dirty = false;
        }
        ++it;
    }
}

void MaterialPreview::BindPreviewLight(ID3D12GraphicsCommandList* cmd, UINT frameIndex)
{
    LightCB light{};
    light.ambientColor = { 0.18f, 0.19f, 0.22f, 1.0f };
    light.lightCount = { 1.0f, 0.35f, 0.0f, 0.0f };   // y: IBL の強さ(RenderSettings と同じ既定値)
    light.lights[0].dir = { -0.4f, -0.7f, 0.6f, 0.0f };
    light.lights[0].color = { 1.0f, 0.98f, 0.94f, 1.0f };
    light.lights[0].param = { 0.0f, 0.0f, 0.0f, 0.0f };   // x=0: Directional

    auto& cb = APP->GetConstantBufferAllocator();
    const auto b2 = cb.Allocate(frameIndex % RTV_NUM, &light, sizeof(LightCB));
    if (b2 != 0) cmd->SetGraphicsRootConstantBufferView(2, b2);
}

void MaterialPreview::RenderOne(ID3D12GraphicsCommandList* cmd, Slot& slot,
    Material& mat, UINT frameIndex)
{
    // -- rt を描画先へ -- 
    slot.rt.SetResourceBarrier(
        cmd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    auto rtv = slot.rt.GetRTV();
	cmd->OMSetRenderTargets(1, &rtv, FALSE, &m_DsvHandle);

    // 背景
    slot.rt.Clear(cmd, { 0.16f, 0.17f, 0.19f, 1.0f });
    cmd->ClearDepthStencilView(m_DsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT  vp = slot.rt.GetViewport();
	D3D12_RECT      sc = slot.rt.GetScissorRect();
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootSignature(APP->GetRootSignature().Get());

    // カメラ
	using namespace DirectX;
    const float r = 2.6f;
    const XMVECTOR eye = XMVectorSet(
		r * std::cos(m_Pitch) * std::sin(m_Yaw),
		r * std::sin(m_Pitch),
		r * std::cos(m_Pitch) * std::cos(m_Yaw),1.0f
    );

    float4x4 view{}, proj{};
	XMStoreFloat4x4(&view, XMMatrixLookAtLH(eye, XMVectorZero(), XMVectorSet(0, 1, 0, 0)));
	XMStoreFloat4x4(&proj, XMMatrixPerspectiveFovLH(XMConvertToRadians(35.0f), 1.0f, 0.1f, 100.0f));

    BindPreviewLight(cmd, frameIndex);
    auto& cb = APP->GetConstantBufferAllocator();

    // ---- 球を描く ---- //
    float4x4 world{};
    XMStoreFloat4x4(&world, XMMatrixIdentity());

    mat.Apply(cmd, world, view, proj, /*wireframe*/ false,
        frameIndex, &cb, mat.shaderName);
    m_Sphere.Draw(cmd);

    // ---- 読み取り用へ戻す ---- //
    slot.rt.SetResourceBarrier(cmd,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void MaterialPreview::Release()
{
    for (auto& [key, slot] : m_Slots) slot->rt.Release();
    m_Slots.clear();
    m_Depth.Reset();
    m_Initialized = false;
}