#include "DirectX.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include "d3dx12.h"
#include "Shader.hpp"
#include "DirectXTex/DirectXTex.h"

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
		16,
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
		DXGI_FORMAT_D32_FLOAT, m_Window_Width, m_Window_Height,
		1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
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

	CreateRootSignature();
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
	CD3DX12_ROOT_PARAMETER rootParameters[5] = {};
	// b0 ~ b3 にCBVを割り当てる
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[3].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_ALL);

	// t0 にSRVを割り当てる
	rootParameters[4].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);


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
		{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
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

	if (vs == nullptr || ps == nullptr || toon == nullptr || 
		wirePs == nullptr || lineVS == nullptr || linePS == nullptr || 
		skyPS == nullptr || skyVS == nullptr || shadowVS == nullptr)
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

	//CreateMeshShaderPipelineState();
	m_CBAllocator.Init();
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
		{"TANGENT",0,DXGI_FORMAT_R32G32B32_FLOAT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
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
	m_DeferredReleases[m_FrameIndex].clear();	// 前フレームの解放予約をクリア
	if (m_Fence->GetCompletedValue() < fenceToWait)
	{
		m_Fence->SetEventOnCompletion(fenceToWait, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}

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
	if (resolved.is_relative())
	{
		wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
		resolved = std::filesystem::path(exe).parent_path() / resolved;
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

	return true;
}

void DirectXApp::WaitForGPUIdle()
{
	if (!m_CommandQueue || !m_Fence || !m_Fence_Event) return;

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