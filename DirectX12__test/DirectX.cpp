#include "DirectX.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include "d3dx12.h"
#include "Shader.hpp"
#include <DirectXTex.h>
#include "Logger.hpp"
#include <dxgidebug.h>
#include "ShaderTypes.hpp"

using ushort = unsigned short;

constexpr float ClearColor[] = {0.0f, 0.5f, 0.7f, 1.0f};

DirectXApp* DirectXApp::s_Instance = nullptr;

DirectXApp::DirectXApp(HWND hWnd, int Window_Width, int Window_Height) :
	m_Window_hWnd(hWnd),
	m_Window_Width(Window_Width),
	m_Window_Height(Window_Height),
	m_Fence_Event(nullptr),
	m_RTV_Handle{}
{
	s_Instance = this;

	UINT FlagsDXGI = 0;
	ID3D12Debug* debug = nullptr;
	HRESULT hr;
#if _DEBUG
	ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
	{
		dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}
	D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	if (debug) {
		debug->EnableDebugLayer();
		debug->Release();
	}
	FlagsDXGI |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	hr = CreateDXGIFactory2(FlagsDXGI, IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) {
		return;
	}

	// ----------------------------------------------//
	//					デバイスの作成				 //
	// ----------------------------------------------//

	ComPtr<IDXGIAdapter> adapter;
	hr = m_Factory->EnumAdapters(0, adapter.GetAddressOf());
	if (FAILED(hr)) {
		return;
	}

	hr = D3D12CreateDevice(
		adapter.Get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(m_Device.GetAddressOf()));
	if (FAILED(hr)) {
		return;
	}
#if _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(m_Device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
	{
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->Release();
	}
#endif

	hr = m_Device.As(&m_Device2);


	if (FAILED(hr) || m_Device2 == nullptr)
	{
		m_Device2 = nullptr;
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
	if (SUCCEEDED(m_Device->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS7,
		&options7,
		sizeof(options7)
	)))
	{
		m_MeshShaderSupported = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
	}

	// -----------------------------------------------//
	//	  コマンドアロケータとコマンドキューの作成	  //
	// -----------------------------------------------//
	for (int i = 0; i < RTV_NUM; ++i)
	{
		hr = m_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(m_CommandAllocator[i].GetAddressOf())
		);
		if (FAILED(hr))
		{
			return;
		}
	}


	// ---------------------------------//
	//		   コマンドキューの作成			//
	// ---------------------------------//
	D3D12_COMMAND_QUEUE_DESC desc_command_queue;
	ZeroMemory(&desc_command_queue, sizeof(desc_command_queue));
	desc_command_queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc_command_queue.Priority = 0;
	desc_command_queue.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	hr = m_Device->CreateCommandQueue(
		&desc_command_queue,
		IID_PPV_ARGS(m_CommandQueue.GetAddressOf())
	);
	if (FAILED(hr)) {
		return;
	}

	m_Fence_Event = CreateEvent(NULL, FALSE, FALSE, NULL);
	hr = m_Device->CreateFence(
		0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(m_Fence.GetAddressOf())
	);
	if (FAILED(hr)) {
		return;
	}
	// --------------------------------------//
	//			スワップチェーンの作成		 //
	// --------------------------------------//
	DXGI_SWAP_CHAIN_DESC desc_swap_chain;
	ZeroMemory(&desc_swap_chain, sizeof(desc_swap_chain));
	desc_swap_chain.BufferCount = RTV_NUM;
	desc_swap_chain.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc_swap_chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc_swap_chain.OutputWindow = m_Window_hWnd;
	desc_swap_chain.SampleDesc.Count = 1;
	desc_swap_chain.Windowed = TRUE;
	desc_swap_chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc_swap_chain.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	hr = m_Factory->CreateSwapChain(
		m_CommandQueue.Get(),
		&desc_swap_chain,
		(IDXGISwapChain**)m_SwapChain.GetAddressOf());
	if (FAILED(hr)) {
		return;
	}

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	for (int i = 0; i < RTV_NUM; ++i)
	{
		m_FenceValue[i] = 0;
	}

	hr = m_Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_CommandAllocator[m_FrameIndex].Get(),
		nullptr,
		IID_PPV_ARGS(m_CommandList.GetAddressOf())
	);
	if (FAILED(hr)) {
		return;
	}

	m_CommandList.As(&m_CommandList6);

	hr = m_CommandList->Close();
	if (FAILED(hr)) {
		return;
	}

	// ディスクリプタヒープの作成
	// RenderTargetView を3つ
	m_RtvAllocator.Init(
		m_Device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		64,
		false);

	UINT strideHandleBytes = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < desc_swap_chain.BufferCount; i++) {
		m_SwapChain->GetBuffer(
			i,
			IID_PPV_ARGS(m_RenderTargets[i].GetAddressOf())
		);

		UINT rtvIndex = 0;
		m_RtvAllocator.Allocate(rtvIndex);	// RTVスロットを確保

		m_RTV_Handle[i] = m_RtvAllocator.Cpu(rtvIndex);	// RTVのCPUハンドルを取得
		m_Device->CreateRenderTargetView(
			m_RenderTargets[i].Get(),
			nullptr,
			m_RTV_Handle[i]
		);

		m_RenderTargets[i]->SetName(L"BackBuffer");
	}

	// ------------------------------------//
	//	 深度バッファと DSV ヒープの作成   //
	// ------------------------------------//
	m_DsvAllocator.Init(m_Device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		16,
		false);

	CD3DX12_RESOURCE_DESC depthResDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_TYPELESS, 
		m_Window_Width, m_Window_Height,
		1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	// ClearValueは実フォーマットで指定
	CD3DX12_CLEAR_VALUE DepthClearValue(DXGI_FORMAT_D32_FLOAT, 1.0f, 0);

	CD3DX12_HEAP_PROPERTIES depthHeapProp(D3D12_HEAP_TYPE_DEFAULT);

	m_Device->CreateCommittedResource(
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&DepthClearValue,
		IID_PPV_ARGS(m_Depthbuffer.GetAddressOf()));

	// DSVスロットを確保して深度バッファ生成
	UINT dsvIndex = 0;
	m_DsvAllocator.Allocate(dsvIndex);
	m_DSV_Handle = m_DsvAllocator.Cpu(dsvIndex);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format				= DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension		= D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags				= D3D12_DSV_FLAG_NONE;
	m_Device->CreateDepthStencilView(
		m_Depthbuffer.Get(),
		&dsvDesc,
		m_DSV_Handle
	);

	// =======================================================
	//		グローバル SRV ヒープ（CBV/SRV/UAV・256スロット）
	// ======================================================
	m_SrvAllocator.Init(
		m_Device.Get(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		256,
		true
	);

	// 深度SRV
	UINT depthdsvIndex = 0;
	m_SrvAllocator.Allocate(depthdsvIndex);
	m_DepthSrvHandleCpu = m_SrvAllocator.Cpu(depthdsvIndex);
	m_DepthSrvHandleGpu = m_SrvAllocator.Gpu(depthdsvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
	depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.Texture2D.MipLevels = 1;
	m_Device->CreateShaderResourceView(
		m_Depthbuffer.Get(),
		&depthSrvDesc,
		m_DepthSrvHandleCpu
	);

	CreateRootSignature();
	CreateDeferredRootSignature();
	CreatePipelineStateObject();

	m_ShadowMap.Init();

	return;
}

bool DirectXApp::ReloadShader()
{
	if (!m_ShaderLibrary.ReoadChanged())
	{
		return false;
	}

	WaitForGPUIdle();
	m_PsoCache.Clear();
	CreatePipelineStateObject();
	return true;
}

bool DirectXApp::CreateShadeFromSource(const std::string& name, const std::string& hlslCode, std::string& psEntry, std::string& outError, bool alphaBlend)
{
	namespace fs = std::filesystem;
	fs::create_directories(L"Assets/Shaders");
	const std::wstring& path = L"Assets/Shaders/" + std::wstring(name.begin(), name.end()) + L".hlsl";

	// 1,保存
	std::ofstream ofs(path, std::ios::binary);
	ofs << hlslCode;

	//2 強制コンパイル
	const Shader* ps = m_ShaderLibrary.Reload(path, psEntry, "ps_5_0", outError);
	if (ps == nullptr) return false;

	// 3 PSO登録
	WaitForGPUIdle();
	ShaderPassDef def;
	def.psFile = path;
	def.psEntry = psEntry;
	def.alphaBlend = alphaBlend;

}

void DirectXApp::DeferredLightingPass(const RenderContext& ctx,
	D3D12_CPU_DESCRIPTOR_HANDLE targetRtv,
	const D3D12_VIEWPORT& vp, const D3D12_RECT& sc)
{
	auto* cmd = m_CommandList.Get();
	const UINT slot = m_FrameIndex % RTV_NUM;

	// G-Buffer + 深度を SRV へ
	m_GBuffer.TransitionToRead(cmd);
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	// 出力先（ビューポートRT, DSVなし）
	cmd->OMSetRenderTargets(1, &targetRtv, FALSE, nullptr);
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	const float clr[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmd->ClearRenderTargetView(targetRtv, clr, 0, nullptr);

	cmd->SetGraphicsRootSignature(m_DeferredRootSignature.Get());
	cmd->SetPipelineState(m_DeferredPso.Get());

	// b0 Frame
	FrameCB fdata{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		XMStoreFloat4x4(&fdata.viewProj, XMMatrixTranspose(v * p));
		const auto invV = XMMatrixInverse(nullptr, v);
		float4x4 iv; XMStoreFloat4x4(&iv, invV);
		fdata.cameraPos = { iv._41, iv._42, iv._43, 1.0f };
	}
	const auto b0 = m_CBAllocator.Allocate(slot, &fdata, sizeof(fdata));

	// b2 Light
	const auto b2 = m_CBAllocator.Allocate(slot, &ctx.lightCb, sizeof(LightCB));

	// b3 invViewProj
	DeferredCB dcb{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		const auto inv = XMMatrixInverse(nullptr, v * p);
		XMStoreFloat4x4(&dcb.invViewProj, XMMatrixTranspose(inv));
		dcb.envParam = { HasEnvironment() ? (float)(m_EnvMipLevels - 1) : 0.0f, 0.5f, 0.15f, 1.0f };
	}
	const auto b3 = m_CBAllocator.Allocate(slot, &dcb, sizeof(dcb));

	if (b0 == 0 || b2 == 0 || b3 == 0) return;

	cmd->SetGraphicsRootConstantBufferView(0, b0);
	cmd->SetGraphicsRootConstantBufferView(1, b2);
	cmd->SetGraphicsRootConstantBufferView(2, b3);
	cmd->SetGraphicsRootDescriptorTable(3, m_GBuffer.GetSrvTableStart());

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); 
	cmd->DrawInstanced(3, 1, 0, 0);

	bool hasVolumetric = false;
	for (int i = 0; i < (int)ctx.lightCb.lightCount.x; ++i)
	{
		const auto& l = ctx.lightCb.lights[i];
		if ((int)l.param.x >= 2 && l.param.w > 0.0f)
		{
			hasVolumetric = true;
			break;
		}
	}


	if(hasVolumetric)
	{
		// ---- ボリュームライト: ハーフ解像度で描いて加算アップサンプル ----
		const UINT hw = m_Window_Width / 2, hh = m_Window_Height / 2;

		m_VolumetricHalf.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
		auto volRtv = m_VolumetricHalf.GetRTV();
		const float zero[4] = { 0,0,0,0 };
		cmd->ClearRenderTargetView(volRtv, zero, 0, nullptr);
		cmd->OMSetRenderTargets(1, &volRtv, FALSE, nullptr);
		D3D12_VIEWPORT hvp{ 0,0,(float)hw,(float)hh,0,1 };
		D3D12_RECT     hsc{ 0,0,(LONG)hw,(LONG)hh };
		cmd->RSSetViewports(1, &hvp);
		cmd->RSSetScissorRects(1, &hsc);
		cmd->SetPipelineState(m_VolumetricPso.Get());   // 既存のレイマーチPSO(加算→0クリア上なので実質上書き)
		cmd->DrawInstanced(3, 1, 0, 0);

		// ハーフRT → HDR へ加算アップサンプル(バイリニアで滑らかに拡大)
		m_VolumetricHalf.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		FrameCB dummy{};
		auto cbDummy = m_CBAllocator.Allocate(slot, &dummy, sizeof(dummy));
		PostPass(m_VolumetricAddPso.Get(), m_VolumetricHalf.GetSRV(), cbDummy,
			targetRtv, (UINT)vp.Width, (UINT)vp.Height);
	}

	// 深度を書き戻し
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXApp::PostProcessCopy(RenderTexture& src,
	D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
	const D3D12_VIEWPORT& vp, const D3D12_RECT& sc)
{
	auto* cmd = m_CommandList.Get();

	// src(HDR) を SRV へ遷移
	src.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// 直前パスでマテリアル自前ヒープが束縛されている可能性があるのでグローバルへ戻す
	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(_countof(heaps), heaps);

	// 出力先（DSVなし）
	cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);

	cmd->SetGraphicsRootSignature(m_PostRootSignature.Get());
	cmd->SetPipelineState(m_CopyPso.Get());

	cmd->SetGraphicsRootDescriptorTable(0, src.GetSRV());

	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
}

void DirectXApp::PostProcessBloom(D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, const D3D12_VIEWPORT& vp, const D3D12_RECT& sc)
{
	auto* cmd = m_CommandList.Get();
	const UINT slot = m_FrameIndex % RTV_NUM;
	const UINT bw = m_Window_Width / 2, bh = m_Window_Height / 2;

	PostCB pcb{};
	pcb.threshold = 1.0f;   // 1.0超（＝HDR高輝度）を抽出
	pcb.intensity = 1.0f;

	m_HdrScene.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	auto cb1 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	PostPass(m_BrightPso.Get(), m_HdrScene.GetSRV(), cb1, m_BloomA.GetRTV(), bw, bh);

	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomB.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	pcb.texelSize = { 1.0f / bw, 0.0f };
	auto cb2 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	PostPass(m_BlurPso.Get(), m_BloomA.GetSRV(), cb2, m_BloomB.GetRTV(), bw, bh);

	m_BloomB.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
	pcb.texelSize = { 0.0f, 1.0f / bh };
	auto cb3 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	PostPass(m_BlurPso.Get(), m_BloomB.GetSRV(), cb3, m_BloomA.GetRTV(), bw, bh);

	m_HdrScene.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_BloomA.Transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	auto cb4 = m_CBAllocator.Allocate(slot, &pcb, sizeof(pcb));
	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->SetGraphicsRootSignature(m_PostRootSignature.Get());
	cmd->SetPipelineState(m_CompositePso.Get());
	cmd->SetGraphicsRootDescriptorTable(0, m_CompositeSrvStart);
	cmd->SetGraphicsRootConstantBufferView(1, cb4);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
}

ID3D12PipelineState* DirectXApp::RegisterShaderPass(const std::string& name, const ShaderPassDef& def)
{
	const Shader* vs = m_ShaderLibrary.Load(def.vsFile, def.vsEntry, def.vsProfile);
	const Shader* ps = m_ShaderLibrary.Load(def.psFile, def.psEntry, def.psProfile);
	if (vs == nullptr || ps == nullptr)
	{
		return nullptr;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakeBasePsoDesc();
	desc.VS = vs->GetByteCode();
	desc.PS = ps->GetByteCode();
	desc.RasterizerState.CullMode = def.cullMode;
	if (def.alphaBlend)
	{
		auto& rt = desc.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
	}

	auto pso = m_PsoCache.GetOrCreate(name, m_Device.Get(), desc);
	if (pso == nullptr)
	{
		return nullptr;
	}

	m_ShaderRegistry[name] = { def,pso };
	return pso.Get();
}

ID3D12PipelineState* DirectXApp::GetPipelineStateByName(std::string& name) const
{
	auto it = m_ShaderRegistry.find(name);
	if (it != m_ShaderRegistry.end())
	{
		return it->second.pso.Get();
	}
	return nullptr;
}

std::vector<std::string> DirectXApp::GetShaderNames() const
{
	std::vector<std::string> names;
	names.reserve(m_ShaderRegistry.size());
	for (auto& [name, _] : m_ShaderRegistry)
	{
		names.push_back(name);
	}
	return names;
}



void DirectXApp::CreateDeferredRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0
	rootParameters[1].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);	// b2
	rootParameters[2].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);	// b3
	rootParameters[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL); // t0~t6

	CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc[3] = {
	CD3DX12_STATIC_SAMPLER_DESC(
	0,	// shaderRegister : s0
	D3D12_FILTER_MIN_MAG_MIP_LINEAR,
	D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	D3D12_TEXTURE_ADDRESS_MODE_WRAP,
	D3D12_TEXTURE_ADDRESS_MODE_WRAP
	),

CD3DX12_STATIC_SAMPLER_DESC(
	1,	// shaderRegister : s1
	D3D12_FILTER_MIN_MAG_MIP_POINT,
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	),

	[] {
		CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
			2,
			D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		return shadowSampler;
	}()
	};

	CD3DX12_ROOT_SIGNATURE_DESC desc;
	desc.Init(
		_countof(rootParameters),rootParameters,
		_countof(staticSamplerDesc),staticSamplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	ComPtr<ID3DBlob> sig, err;
	if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err)))
	{
		return;
	}

	m_Device->CreateRootSignature(
		0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(m_DeferredRootSignature.GetAddressOf())
	);
}

void DirectXApp::CreatePostRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srv;
	srv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);	// t0
	CD3DX12_ROOT_PARAMETER rp[2];
	rp[0].InitAsDescriptorTable(1, &srv, D3D12_SHADER_VISIBILITY_PIXEL);
	rp[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);	// b0

	CD3DX12_STATIC_SAMPLER_DESC samp(0,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	CD3DX12_ROOT_SIGNATURE_DESC desc;
	desc.Init(_countof(rp), rp, 1, &samp, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> sig, err;
	    D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    m_Device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(m_PostRootSignature.GetAddressOf()));
}

void DirectXApp::CreateRootSignature()
{

	CD3DX12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0);
	// t0 albedo
	// t1 toon ramp
	// t2 normal
	// t3 metal
	// t4 roughness
	// t5 環境

	// 配列の数がそのまま定数バッファやSRVの数になる
	CD3DX12_ROOT_PARAMETER rootParameters[7] = {};
	// b0 ~ b3 にCBVを割り当てる
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[3].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_ALL);

	// t0 にSRVを割り当てる
	rootParameters[4].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[5].InitAsConstantBufferView(4,0,D3D12_SHADER_VISIBILITY_VERTEX);
	rootParameters[6].InitAsShaderResourceView(7,0,D3D12_SHADER_VISIBILITY_VERTEX);


	CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc[3] = {
		CD3DX12_STATIC_SAMPLER_DESC(
		0,	// shaderRegister : s0
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
		),

	CD3DX12_STATIC_SAMPLER_DESC(
		1,	// shaderRegister : s1
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
		),

		[] {
			CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
				2, 
				D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
				D3D12_TEXTURE_ADDRESS_MODE_BORDER, 
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,
				D3D12_TEXTURE_ADDRESS_MODE_BORDER);
			shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
			return shadowSampler;
		}()
	};

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		_countof(rootParameters),
		rootParameters,
		_countof(staticSamplerDesc),
		staticSamplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> SerializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;


	auto result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&SerializedRootSig,
		&errorBlob);
	if (FAILED(result))return;

	m_Device->CreateRootSignature(
		0,
		SerializedRootSig->GetBufferPointer(),
		SerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(m_rootSignature.GetAddressOf())
	);
}

void DirectXApp::CreatePipelineStateObject()
{
	D3D12_INPUT_ELEMENT_DESC InputLayout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"Normal",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}, 
		{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,60,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,76,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};

	const Shader* vs = m_ShaderLibrary.Load(L"VertexShader.hlsl", "BasicVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PixelShader.hlsl", "BasicPS", "ps_5_0");
	const Shader* toon = m_ShaderLibrary.Load(L"ToonShader.hlsl", "ToonPS", "ps_5_0");
	const Shader* wirePs = m_ShaderLibrary.Load(L"PixelShader.hlsl", "WireFramePS", "ps_5_0");
	const Shader* lineVS = m_ShaderLibrary.Load(L"PS_LineShader.hlsl", "LineVS", "vs_5_0");
	const Shader* linePS = m_ShaderLibrary.Load(L"PS_LineShader.hlsl", "LinePS", "ps_5_0");
	const Shader* iconPS = m_ShaderLibrary.Load(L"PixelShader.hlsl", "unlitPS", "ps_5_0");
	const Shader* skyVS = m_ShaderLibrary.Load(L"SkyBoxShader.hlsl", "SkyboxVS", "vs_5_0");
	const Shader* skyPS = m_ShaderLibrary.Load(L"SkyBoxShader.hlsl", "SkyboxPS", "ps_5_0");
	const Shader* shadowVS = m_ShaderLibrary.Load(L"ShadowShader.hlsl", "ShadowVS", "vs_5_0");
	const Shader* BeamVS = m_ShaderLibrary.Load(L"LaserBeamShader.hlsl", "LaserVS", "vs_5_0");
	const Shader* BeamPS = m_ShaderLibrary.Load(L"LaserBeamShader.hlsl", "LaserPS", "ps_5_0");

	if (vs == nullptr || ps == nullptr || toon == nullptr || 
		wirePs == nullptr || lineVS == nullptr || linePS == nullptr || 
		skyPS == nullptr || skyVS == nullptr || shadowVS == nullptr || BeamVS == nullptr || BeamPS == nullptr)
	{
		assert(false);
		return;
	}

	D3D12_INPUT_ELEMENT_DESC linelayout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { InputLayout, _countof(InputLayout) };
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = vs->GetByteCode();
	psoDesc.PS = ps->GetByteCode();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;

	const size_t vsIndex = static_cast<size_t>(E_VERTEX_SHADER::BASIC);
	const size_t psIndex = static_cast<size_t>(E_PIXEL_SHADER::BASIC);
	const size_t psToonIndex = static_cast<size_t>(E_PIXEL_SHADER::TOON);


	RegisterBuiltinShaders();

	auto wireDesc = psoDesc;
	wireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	wireDesc.PS = wirePs->GetByteCode();


	m_pipelineStateWireFrame = m_PsoCache.GetOrCreate("BasicVS_WireFramePS",
		m_Device.Get(),
		wireDesc
	);
	if(m_pipelineStateWireFrame == nullptr) {
		assert(false);
	}

	auto lineDesc = psoDesc;
	lineDesc.VS = lineVS->GetByteCode();
	lineDesc.PS = linePS->GetByteCode();
	lineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	lineDesc.InputLayout = { linelayout,_countof(linelayout) };
	lineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	lineDesc.DepthStencilState.DepthEnable = FALSE;
	m_LinePso = m_PsoCache.GetOrCreate("LineVS_LinePS",
		m_Device.Get(),
		lineDesc
	);
	if (m_LinePso == nullptr) {
		assert(false);
	}

	auto BeamDesc = lineDesc;
	BeamDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	BeamDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	auto& brt = BeamDesc.BlendState.RenderTarget[0];
	// アルファブレンド有効
	brt.BlendEnable				= TRUE;
	brt.SrcBlend				= D3D12_BLEND_SRC_ALPHA;
	brt.DestBlend				= D3D12_BLEND_ONE;
	brt.BlendOp					= D3D12_BLEND_OP_ADD;
	brt.SrcBlendAlpha			= D3D12_BLEND_ONE;
	brt.DestBlendAlpha			= D3D12_BLEND_ZERO;
	brt.BlendOpAlpha			= D3D12_BLEND_OP_ADD;
	brt.RenderTargetWriteMask	= D3D12_COLOR_WRITE_ENABLE_ALL;

	BeamDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	BeamDesc.DepthStencilState.DepthEnable = FALSE;

	BeamDesc.VS = BeamVS->GetByteCode();
	BeamDesc.PS = BeamPS->GetByteCode();

	// --- R8用（forward経路 / BeamRenderer.Init に渡すPSO）---
	m_BeamPso = m_PsoCache.GetOrCreate("BeamVS_BeamPS",
		m_Device.Get(),
		BeamDesc
	);
	if (m_BeamPso == nullptr) {
		assert(false);
	}

	// --- HDR用（deferredのBloom前描画）---
	auto BeamHdrDesc = BeamDesc;
	BeamHdrDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_BeamHdrPso = m_PsoCache.GetOrCreate("BeamVS_BeamPS_HDR",
		m_Device.Get(),
		BeamHdrDesc
	);
	if (m_BeamHdrPso == nullptr) {
		assert(false);
	}
	const Shader* FireworkPS = m_ShaderLibrary.Load(L"LaserBeamShader.hlsl", "FireworkPS", "ps_5_0");
	auto FireworkDesc = BeamDesc;
	FireworkDesc.PS = FireworkPS->GetByteCode();
	auto& frt = FireworkDesc.BlendState.RenderTarget[0];
	frt.SrcBlend = D3D12_BLEND_ONE;   // PS側でαを乗算済み(pre-multiplied)
	frt.DestBlend = D3D12_BLEND_ONE;
	m_FireworkPso = m_PsoCache.GetOrCreate("BeamVS_FireworkPS",
		m_Device.Get(), FireworkDesc);
	if (m_FireworkPso == nullptr) { assert(false); }


	auto iconDesc = psoDesc;                 // BASICベース
	iconDesc.PS = iconPS->GetByteCode();

	// アルファブレンド有効
	auto& rt = iconDesc.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	iconDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	m_IconPso = m_PsoCache.GetOrCreate("IconPSO", m_Device.Get(), iconDesc);
	if (m_IconPso == nullptr) { assert(false); }

	// ---------------------------------- //
	//			UI用のPSOを作成			  //
	// ---------------------------------- //
	auto uiDesc = iconDesc;
	auto& uirt = uiDesc.BlendState.RenderTarget[0];
	uirt.BlendEnable = TRUE;
	uirt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	uirt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	uirt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	uirt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	uiDesc.DepthStencilState.DepthEnable = FALSE;
	uiDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	uiDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	m_UIPso = m_PsoCache.GetOrCreate("UIPSO", m_Device.Get(), uiDesc);
	if (m_UIPso == nullptr) { assert(false); }

	// ---------------------------------- //
	//			SkyBox用のPSOを作成		  //
	// ---------------------------------- //
	auto skyDesc = psoDesc;
	skyDesc.VS = skyVS->GetByteCode();
	skyDesc.PS = skyPS->GetByteCode();
	skyDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	m_SkyPso = m_PsoCache.GetOrCreate("SkyBoxPSO", m_Device.Get(), skyDesc);
	if (m_SkyPso == nullptr) { assert(false); }

	// ---------------------------------- //
	// 		Shadow用のPSOを作成			  //
	// ---------------------------------- //
	auto sd = MakeBasePsoDesc();
	sd.VS = shadowVS->GetByteCode();
	sd.PS = D3D12_SHADER_BYTECODE{ nullptr, 0 };   // PSなし
	sd.NumRenderTargets = 0;
	sd.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	sd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	sd.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	sd.RasterizerState.DepthBias = 10000;               // ピーターパン/アクネ対策
	sd.RasterizerState.SlopeScaledDepthBias = 1.5f;
	m_ShadowPso = m_PsoCache.GetOrCreate("ShadowPSO", m_Device.Get(), sd);

	CreatePostRootSignature();

	CreateGbufferPSO();
	CreateDeferredPSO();
	CreatePostPSO();
	CreateCopyPSO();
	m_CBAllocator.Init();

	m_GBuffer.Init(
		m_Window_Width, m_Window_Height,
		m_Depthbuffer.Get(),          // 深度リソース
		m_EnvTexture.Get(),           // null可
		m_ShadowMap.GetResource(),
		m_EnvMipLevels);   // シャドウリソース

	m_HdrScene.Init(m_Window_Width, m_Window_Height, DXGI_FORMAT_R16G16B16A16_FLOAT);
	UINT bw = m_Window_Width, bh = m_Window_Height;
	m_BloomA.Init(bw, bh, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_BloomB.Init(bw, bh, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_VolumetricHalf.Init(m_Window_Width / 2, m_Window_Height / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_HdrScene.GetResource()->SetName(L"HDRScene");
	m_BloomA.GetResource()->SetName(L"BloomA");
	m_BloomB.GetResource()->SetName(L"BloomB");

	CreateBloomPSOs();
	CreateVolumetricPSO();
	BuildCompositeSrvTable();
}

void DirectXApp::RegisterBuiltinShaders()
{
	struct Entry { const char* name; ShaderPassDef def; };
	const Entry builtins[] = {
		{ "Basic", { L"VertexShader.hlsl","BasicVS","vs_5_0", L"PixelShader.hlsl","BasicPS","ps_5_0", false } },
		{ "Toon",  { L"VertexShader.hlsl","BasicVS","vs_5_0", L"ToonShader.hlsl", "ToonPS", "ps_5_0", false } },
		{ "Unlit", { L"VertexShader.hlsl","BasicVS","vs_5_0", L"PixelShader.hlsl","unlitPS","ps_5_0", true  } },
		{ "PBR",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"PbrShader.hlsl",  "PbrPS",  "ps_5_0", false } },
		{ "Rim",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"RimShader.hlsl",  "RimPS",  "ps_5_0", false } },
		{ "Fresnel",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"FresnelShader.hlsl",  "FresnelPS",  "ps_5_0", false } },
		{ "Dissolve",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"DissolveShader.hlsl",  "DissolvePS",  "ps_5_0", false } },
		{ "BlinnPhong",   { L"VertexShader.hlsl","BasicVS","vs_5_0", L"BlinnPhongShader.hlsl",  "PhongPS",  "ps_5_0", false } },

		{ "SkinnedPBR", { L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"PBRShader.hlsl","PbrPS","ps_5_0", false } },
		{ "SkinnedToon",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"ToonShader.hlsl","ToonPS","ps_5_0", false } },
		{ "SkinnedUnlit",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"PixelShader.hlsl","unlitPS","ps_5_0", true } },
		{ "SkinnedRim",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"RimShader.hlsl","RimPS","ps_5_0", false } },
		{ "SkinnedFresnel",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"FresnelShader.hlsl","FresnelPS","ps_5_0", false } },
		{ "SkinnedDissolve",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"DissolveShader.hlsl","DissolvePS","ps_5_0", false } },
		{ "SkinnedBlinnPhong",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"BlinnPhongShader.hlsl","PhongPS","ps_5_0", false } },


		{ "Genshin_Toon",{ L"SkinnedShader.hlsl","SkinnedVS","vs_5_0", L"Genshin_ToonShader.hlsl","Genshin_ToonPS","ps_5_0", false } },
		{ "Genshin_Outline", { L"GenshinOutline.hlsl","Genshin_OutlineVS","vs_5_0",
					   L"GenshinOutline.hlsl","Genshin_OutlinePS","ps_5_0",
					   false, D3D12_CULL_MODE_FRONT } },
	};
	for (auto& e : builtins) RegisterShaderPass(e.name, e.def);
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC DirectXApp::MakeBasePsoDesc() const
{
	static const D3D12_INPUT_ELEMENT_DESC layout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"Normal",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{ "BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,60,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0 },
		{"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,76,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.InputLayout = { layout, _countof(layout) };
	desc.pRootSignature = m_rootSignature.Get();
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.SampleMask = UINT_MAX;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	desc.SampleDesc.Count = 1;
	return desc;
}

void DirectXApp::CreateGbufferPSO()
{
	auto desc = MakeBasePsoDesc();

	const Shader* vs = m_ShaderLibrary.Load(L"SkinnedShader.hlsl", "SkinnedVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"GBufferShader.hlsl", "GBufferPS", "ps_5_0");
	desc.VS = vs->GetByteCode();
	desc.PS = ps->GetByteCode();

	if (!vs || !ps) { OutputDebugStringA("GBuffer shader load failed\n"); assert(false); return; }

	desc.NumRenderTargets = 3;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Albedo
	desc.RTVFormats[1] = DXGI_FORMAT_R10G10B10A2_UNORM;  // Normal
	desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;     // ORM
	desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

	// 深度は「書き込みあり・テストあり」（不透明を前から手前判定）
	desc.DepthStencilState.DepthEnable = TRUE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_GBufferPso.GetAddressOf()));
}

void DirectXApp::CreateDeferredPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_DeferredRootSignature.Get();

	const Shader* vs = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "DeferredPS", "ps_5_0");

	if (!vs || !ps) { OutputDebugStringA("Deferred shader load failed\n"); assert(false); return; }

	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();

	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_DeferredPso.GetAddressOf()));
}

void DirectXApp::CreatePostPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_PostRootSignature.Get();
	const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PostProcess.hlsl", "CopyPS", "ps_5_0");
	if (!vs || !ps) { OutputDebugStringA("PostProcess shader load failed\n"); assert(false); return; }
	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();
	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_PostPso.GetAddressOf()));
}

void DirectXApp::CreateCopyPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_PostRootSignature.Get();

	const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PostProcess.hlsl", "CopyPS", "ps_5_0");

	if (!vs || !ps) { OutputDebugStringA("PostProcess shader load failed\n"); assert(false); return; }

	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();

	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_CopyPso.GetAddressOf()));

	//ボリューム加算
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC ad = desc;
		ad.pRootSignature = m_PostRootSignature.Get();
		ad.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		auto& rt = ad.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_ONE;
		rt.DestBlend = D3D12_BLEND_ONE;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ONE;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		HRESULT hr = m_Device->CreateGraphicsPipelineState(&ad, IID_PPV_ARGS(m_VolumetricAddPso.GetAddressOf()));
		assert(SUCCEEDED(hr) && m_VolumetricAddPso);
	}
}

void DirectXApp::CreateVolumetricPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_DeferredRootSignature.Get();

	const Shader* vs = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "FullScreenVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"DeferredLighting.hlsl", "VolumetricPS", "ps_5_0");

	if (!vs || !ps) { OutputDebugStringA("Volumetrics shader load failed\n"); assert(false); return; }

	desc.PS = ps->GetByteCode();
	desc.VS = vs->GetByteCode();

	desc.InputLayout = { nullptr, 0 };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DepthStencilState.StencilEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	desc.SampleMask = UINT_MAX;
	desc.SampleDesc.Count = 1;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

	D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D12_BLEND_ONE;   // 加算合成
	rt.DestBlend = D3D12_BLEND_ONE;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_ONE;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_VolumetricPso.GetAddressOf()));
}

void DirectXApp::CreateShadowSrv()
{
	auto* sm = m_ShadowMap.GetResource();
	if (!sm)return;

	m_SrvAllocator.Allocate(m_ShadowSrvIndex);
	m_ShadowSrvCpu = m_SrvAllocator.Cpu(m_ShadowSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = 1;
	m_Device->CreateShaderResourceView(sm, &srv, m_ShadowSrvCpu);
}

void DirectXApp::CreateEnvSrv()
{
	if (!m_EnvTexture) return;

	m_SrvAllocator.Allocate(m_EnvSrvIndex);
	m_EnvSrvCpu = m_SrvAllocator.Cpu(m_EnvSrvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = m_EnvMipLevels;
	m_Device->CreateShaderResourceView(m_EnvTexture.Get(), &srv, m_EnvSrvCpu);
}

void DirectXApp::CreateBloomPSOs()
{
	auto make = [&](const char* psEntry, DXGI_FORMAT rtv, ComPtr<ID3D12PipelineState>& out)
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
			desc.pRootSignature = m_PostRootSignature.Get();
			const Shader* vs = m_ShaderLibrary.Load(L"PostProcess.hlsl", "FullScreenVS", "vs_5_0");
			const Shader* ps = m_ShaderLibrary.Load(L"PostProcess.hlsl", psEntry, "ps_5_0");
			if (!vs || !ps) { OutputDebugStringA("Bloom shader load failed\n"); assert(false); return; }
			desc.VS = vs->GetByteCode();
			desc.PS = ps->GetByteCode();
			desc.InputLayout = { nullptr, 0 };
			desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			desc.DepthStencilState.DepthEnable = FALSE;
			desc.DepthStencilState.StencilEnable = FALSE;
			desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
			desc.SampleMask = UINT_MAX;
			desc.SampleDesc.Count = 1;
			desc.NumRenderTargets = 1;
			desc.RTVFormats[0] = rtv;
			m_Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(out.GetAddressOf()));
		};
	make("BrightPS", DXGI_FORMAT_R16G16B16A16_FLOAT, m_BrightPso);
	make("BlurPS", DXGI_FORMAT_R16G16B16A16_FLOAT, m_BlurPso);
	make("CompositePS", DXGI_FORMAT_R8G8B8A8_UNORM, m_CompositePso);
}

void DirectXApp::BuildCompositeSrvTable()
{
	UINT base = m_SrvAllocator.AllocateRange(2);
	m_CompositeSrvStart = m_SrvAllocator.Gpu(base);

	D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
	d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	d.Texture2D.MipLevels = 1;
	d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_Device->CreateShaderResourceView(m_HdrScene.GetResource().Get(), &d, m_SrvAllocator.Cpu(base + 0));
	m_Device->CreateShaderResourceView(m_BloomA.GetResource().Get(), &d, m_SrvAllocator.Cpu(base + 1));
}

void DirectXApp::PostPass(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srvTable, D3D12_GPU_VIRTUAL_ADDRESS cb, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv, UINT w, UINT h)
{
	auto* cmd = m_CommandList.Get();
	ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
	cmd->SetDescriptorHeaps(1, heaps);
	cmd->OMSetRenderTargets(1, &dstRtv, FALSE, nullptr);
	D3D12_VIEWPORT vp{ 0,0,(float)w,(float)h,0,1 };
	D3D12_RECT sc{ 0,0,(LONG)w,(LONG)h };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	cmd->SetGraphicsRootSignature(m_PostRootSignature.Get());
	cmd->SetPipelineState(pso);
	cmd->SetGraphicsRootDescriptorTable(0, srvTable);
	cmd->SetGraphicsRootConstantBufferView(1, cb);
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
}

DirectXApp::~DirectXApp()
{
	WaitForGPUIdle();
	s_Instance = nullptr;

	if (m_Fence_Event != nullptr)
	{
		CloseHandle(m_Fence_Event);
		m_Fence_Event = nullptr;
	}
}


HRESULT DirectXApp::BeginRender()
{
	const UINT targetIndex = m_FrameIndex;

	const UINT64 fenceToWait = m_FenceValue[targetIndex];
	if (m_Fence->GetCompletedValue() < fenceToWait)
	{
		m_Fence->SetEventOnCompletion(fenceToWait, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}
	m_DeferredReleases[m_FrameIndex].clear();	// 前フレームの解放予約をクリア

	HRESULT hr = m_CommandAllocator[targetIndex]->Reset();
	if (FAILED(hr)) {
		return hr;
	}

	hr = m_CommandList->Reset(m_CommandAllocator[targetIndex].Get(), nullptr);
	if (FAILED(hr)) {
		return hr;
	}

	auto dsvhandle = m_DSV_Handle;

	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[targetIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	// グローバル SRV ヒープをフレーム開始時に 1 回だけバインド
	if (m_SrvAllocator.heap)
	{
		ID3D12DescriptorHeap* heaps[] = { m_SrvAllocator.heap.Get() };
		m_CommandList->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	m_CBAllocator.Reset(m_FrameIndex);	// 定数バッファアロケータをリセット

	m_CommandList->SetGraphicsRootSignature(m_rootSignature.Get());

	D3D12_RECT scissorRect = { 0,0,(LONG)m_Window_Width,(LONG)m_Window_Height };
	m_CommandList->RSSetScissorRects(1, &scissorRect);

	D3D12_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width	= (FLOAT)m_Window_Width;
	vp.Height	= (FLOAT)m_Window_Height;
	vp.MinDepth = 0;
	vp.MaxDepth = 1.0f;


	m_CommandList->OMSetRenderTargets(1, &m_RTV_Handle[targetIndex], FALSE, &dsvhandle);

	m_CommandList->RSSetViewports(1, &vp);
	m_CommandList->ClearRenderTargetView(
		m_RTV_Handle[targetIndex],
		ClearColor,
		0,
		nullptr
	);

	m_CommandList->ClearDepthStencilView(
		dsvhandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0, 0, nullptr
	);

	m_CommandList->RSSetViewports(1, &vp);

	return S_OK;
}

void DirectXApp::BeginGeometryPass()
{
	auto* cmd = m_CommandList.Get();

	// 深度を書き込み状態へ
	m_GBuffer.TransitionToWrite(cmd);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
	{
		m_GBuffer.GetRTV(0),
		m_GBuffer.GetRTV(1),
		m_GBuffer.GetRTV(2)
	};

	for (int i = 0; i < 3; ++i)
	{
		cmd->ClearRenderTargetView(rtvs[i], ClearColor, 0, nullptr);
	}
	cmd->ClearDepthStencilView(m_DSV_Handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmd->OMSetRenderTargets(3, rtvs, FALSE, &m_DSV_Handle);

	cmd->SetGraphicsRootSignature(m_rootSignature.Get());
}

void DirectXApp::DeferredLightingPass(const RenderContext& ctx)
{
	auto* cmd = m_CommandList.Get();
	const UINT slot = m_FrameIndex % RTV_NUM;

	// --- G-Buffer + 深度を SRV へ ---
	m_GBuffer.TransitionToRead(cmd);
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// --- 出力先: バックバッファ（DSV無し）---
	cmd->OMSetRenderTargets(1, &m_RTV_Handle[m_FrameIndex], FALSE, nullptr);
	cmd->ClearRenderTargetView(m_RTV_Handle[m_FrameIndex], ClearColor, 0, nullptr);

	cmd->SetGraphicsRootSignature(m_DeferredRootSignature.Get());
	cmd->SetPipelineState(m_DeferredPso.Get());

	// ===== b0 Frame（viewProj/cameraPos）=====
	FrameCB fdata{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		XMStoreFloat4x4(&fdata.viewProj, XMMatrixTranspose(v * p));
		const auto invV = XMMatrixInverse(nullptr, v);
		float4x4 iv; XMStoreFloat4x4(&iv, invV);
		fdata.cameraPos = { iv._41, iv._42, iv._43, 1.0f };
	}
	const auto b0 = m_CBAllocator.Allocate(slot, &fdata, sizeof(fdata));

	// ===== b2 Light =====
	const auto b2 = m_CBAllocator.Allocate(slot, &ctx.lightCb, sizeof(LightCB));

	// ===== b3 invViewProj（深度→ワールド復元用）=====
	DeferredCB dcb{};
	{
		const auto v = XMLoadFloat4x4(&ctx.view);
		const auto p = XMLoadFloat4x4(&ctx.projection);
		const auto inv = XMMatrixInverse(nullptr, v * p);
		// シェーダで mul(clip, invViewProj) と行ベクトル運用なので転置して格納
		XMStoreFloat4x4(&dcb.invViewProj, XMMatrixTranspose(inv));
	}
	const auto b3 = m_CBAllocator.Allocate(slot, &dcb, sizeof(dcb));

	if (b0 == 0 || b2 == 0 || b3 == 0) return; // リング枯渇ガード

	// ルートパラメータ: 0=b0, 1=b2, 2=b3, 3=SRVテーブル
	cmd->SetGraphicsRootConstantBufferView(0, b0);
	cmd->SetGraphicsRootConstantBufferView(1, b2);
	cmd->SetGraphicsRootConstantBufferView(2, b3);
	cmd->SetGraphicsRootDescriptorTable(3, m_GBuffer.GetSrvTableStart());

	cmd->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->DrawInstanced(3, 1, 0, 0);
	cmd->SetPipelineState(m_VolumetricPso.Get()); // 加算ブレンドPSO（下記で作成）
	cmd->DrawInstanced(3, 1, 0, 0);

	// 深度を書き戻し（スカイ/半透明で使うなら）
	SetResourceBarrier(cmd, m_Depthbuffer.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

HRESULT DirectXApp::EndRender()
{
	const UINT targetIndex = m_FrameIndex;

	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[targetIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	HRESULT hr = m_CommandList->Close();
	if (FAILED(hr)) {
		return hr;
	}

	ID3D12CommandList* pCommandList = m_CommandList.Get();
	m_CommandQueue->ExecuteCommandLists(1, &pCommandList);

	const UINT64 singnalFenceValue = ++m_NextFenceValue;
	hr = m_CommandQueue->Signal(m_Fence.Get(), singnalFenceValue);
	if (FAILED(hr))
	{
		return hr;
	} 
	
	m_FenceValue[targetIndex] = singnalFenceValue; 

	hr = m_SwapChain->Present(1, 0);
	if (FAILED(hr))
	{
		return hr;
	}

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

	return S_OK;
}

void DirectXApp::Present()
{
	const UINT targetIndex = m_FrameIndex;

	auto dsvhandle = m_DsvAllocator.heap->GetCPUDescriptorHandleForHeapStart();
	m_CommandList->OMSetRenderTargets(1, &m_RTV_Handle[targetIndex], FALSE, &dsvhandle);

	D3D12_RECT scissorRect = { 0, 0, (LONG)m_Window_Width, (LONG)m_Window_Height };
	m_CommandList->RSSetScissorRects(1, &scissorRect);

	D3D12_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = static_cast<FLOAT>(m_Window_Width);
	vp.Height = static_cast<FLOAT>(m_Window_Height);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;

	m_CommandList->RSSetViewports(1, &vp);
}

bool DirectXApp::LoadEnvironment(const std::wstring& hdrpath)
{
	std::filesystem::path resolved = hdrpath;
	if (resolved.is_relative() && !std::filesystem::exists(resolved))
	{
		wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
		resolved = std::filesystem::path(exe).parent_path() / hdrpath;
	}

	DirectX::TexMetadata meta{}; DirectX::ScratchImage img{};
	if (FAILED(DirectX::LoadFromHDRFile(resolved.c_str(), &meta, img)))
		return false;

	// ミップ生成（ラフネス反射のボケ用）
	DirectX::ScratchImage mipped{};
	if (FAILED(DirectX::GenerateMipMaps(*img.GetImage(0, 0, 0),
		DirectX::TEX_FILTER_LINEAR, 0, mipped)))
		mipped = std::move(img);   // 失敗時はミップ無しで続行
	const DirectX::TexMetadata& mm = mipped.GetMetadata();
	m_EnvMipLevels = static_cast<UINT>(mm.mipLevels);
	{
		const DirectX::Image* top = mipped.GetImage(m_EnvMipLevels - 1, 0, 0);
		if (top && top->pixels)
		{
			// HDRは R32G32B32A32_FLOAT 想定
			const float* p = reinterpret_cast<const float*>(top->pixels);
			m_EnvAmbient = { p[0], p[1], p[2] };
		}
	}

	CreateEnvSrv();

	return true;
}

void DirectXApp::WaitForGPUIdle()
{
	if (!m_CommandQueue || !m_Fence || !m_Fence_Event) return;

	ComPtr<ID3D12DeviceRemovedExtendedData> dredData;
	auto device = m_Device.Get();
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dredData))))
	{
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
		D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
		dredData->GetAutoBreadcrumbsOutput(&breadcrumbs);
		dredData->GetPageFaultAllocationOutput(&pageFault);
		LOG->LogError("DRED PageFault VA = " + std::to_string(pageFault.PageFaultVA));
	}

	const UINT64 fenceToWait = ++m_NextFenceValue;
	if(FAILED(m_CommandQueue->Signal(m_Fence.Get(), fenceToWait)))
	{
		return;
	}

	if(m_Fence->GetCompletedValue() < fenceToWait)
	{
		m_Fence->SetEventOnCompletion(fenceToWait, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}
}

void DirectXApp::CreateMeshShaderPipelineState()
{
	m_MeshPso.Reset();

	if(!m_MeshShaderSupported) {
		return;
	}

	const Shader* mesh = m_ShaderLibrary.Load(L"MS_MeshShader.hlsl", "MeshMain", "ms_6_5");
	const Shader* meshPS = m_ShaderLibrary.Load(L"MS_MeshShader.hlsl", "MeshPS", "ps_6_0");
	if(mesh == nullptr || meshPS == nullptr) {
		assert(false);
		return;
	}

	struct MeshPipelineStateStream
	{
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeRootSingature = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		ID3D12RootSignature* rootSignature = nullptr;

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeMS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
		D3D12_SHADER_BYTECODE ms = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typePS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
		D3D12_SHADER_BYTECODE ps = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeBlend = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
		D3D12_BLEND_DESC blendDesc = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeRasterizer = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
		D3D12_RASTERIZER_DESC rasterizerDesc = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeDepthStencil = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
		D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeRTVFormat = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
		D3D12_RT_FORMAT_ARRAY rtvFormats = {};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeSampleDesc = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
		DXGI_SAMPLE_DESC sampleDesc = {1, 0};

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typeSampleMask = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
		UINT sampleMask = UINT_MAX;

		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE typePrimitiveTopology = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
		D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	};

	MeshPipelineStateStream stream{};
	stream.rootSignature = m_rootSignature.Get();
	stream.ms = mesh->GetByteCode();
	stream.ps = meshPS->GetByteCode();
	stream.blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	stream.rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	stream.depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	stream.rtvFormats.NumRenderTargets = 1;
	stream.rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
	streamDesc.SizeInBytes = sizeof(stream);
	streamDesc.pPipelineStateSubobjectStream = &stream;


	const HRESULT hr = m_Device2 ? m_Device2->CreatePipelineState(
		&streamDesc,
		IID_PPV_ARGS(m_MeshPso.ReleaseAndGetAddressOf())) : E_FAIL;
	if (FAILED(hr))
	{
		m_MeshPso.Reset();
	}
}

void DirectXApp::SetResourceBarrier(ID3D12GraphicsCommandList* CommandList, ID3D12Resource* Resource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
	if(!CommandList || !Resource) {
		return;
	}

	D3D12_RESOURCE_BARRIER descBarrier;
	ZeroMemory(&descBarrier, sizeof(descBarrier));
	descBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	descBarrier.Transition.pResource = Resource;
	descBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	descBarrier.Transition.StateBefore = Before;
	descBarrier.Transition.StateAfter = After;
	CommandList->ResourceBarrier(1, &descBarrier);
}