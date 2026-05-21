/*****************************************************************//**
 * \file   DirectX.cpp
 * \brief  DirectX12の初期化と描画開始・終了処理
 * 
 * 作成者 keep
 * 作成日 2026/2/24
 * 更新履歴 2026/2/24 keep 作成
 *		   2026/4.24 keep ダブルbufferシステムから
 *						  トリプルバッファシステムに変更
 *		　　2026/4/27 keep ルートシグネチャの作成処理を追加
 *						   Shaderの作成をクラスに変更
 * *********************************************************************/
#include "DirectX.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include "d3dx12.h"
#include "Shader.hpp"

using ushort = unsigned short;

constexpr float ClearColor[] = {0.0f, 0.5f, 0.7f, 1.0f};

DirectXApp::DirectXApp(HWND hWnd, int Window_Width, int Window_Height) :
	m_Window_hWnd(hWnd),
	m_Window_Width(Window_Width),
	m_Window_Height(Window_Height),
	m_Fence_Event(nullptr),
	m_RTV_Handle{}
{
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

	ComPtr<IDXGIAdapter> adapter;
	hr = m_Factory->EnumAdapters(0, adapter.GetAddressOf());
	if (FAILED(hr)) {
		return;
	}

	// デバイスの生成
	// DirectX12では最低でも
	// D3D_FEATURE_LEVEL_11_0
	// の機能レベルをサポートしている必要がある
	hr = D3D12CreateDevice(
		adapter.Get(),
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(m_Device.GetAddressOf()));
	if (FAILED(hr)) {
		return;
	}

	// DirectX12の機能レベル12_0以降の機能を使用するために、ID3D12Device2インターフェースを取得
	hr = m_Device.As(&m_Device2);

	// 12_0以降の機能がサポートされていない場合は、ID3D12Device2の取得に失敗するため、m_Device2はnullptrになる
	// 12_0以降の機能がサポートされているかどうかを確認するために、m_Device2がnullptrでないかをチェック
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

	// ================================
	//		コマンドアロケータを生成
	// ================================
	// 4.24 更新
	// コマンドアロケータは、コマンドリストが使用するメモリを管理するオブジェクト
	// 3つのバッファを使用するため、3つのコマンドアロケータを生成する
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

	// =============================
	//		コマンドキューを生成
	// =============================
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

	// =====================================
	//		コマンドキュー用のフェンスを準備
	// =====================================
	m_Fence_Event = CreateEvent(NULL, FALSE, FALSE, NULL);
	hr = m_Device->CreateFence(
		0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(m_Fence.GetAddressOf())
	);
	if (FAILED(hr)) {
		return;
	}
	// ================================
	//		スワップチェインを生成
	// ================================
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

	// ==============================
	//		コマンドリストの作成
	// ==============================
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

	// ==============================================
	//		ディスクリプタヒープ(Rendertarget用)の作成
	// ==============================================
	D3D12_DESCRIPTOR_HEAP_DESC desc_heap;
	ZeroMemory(&desc_heap, sizeof(desc_heap));
	desc_heap.NumDescriptors = RTV_NUM;
	desc_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	desc_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = m_Device->CreateDescriptorHeap(
		&desc_heap,
		IID_PPV_ARGS(m_RTV_Heap.GetAddressOf())
	);
	if (FAILED(hr)) {
		return;
	}

	// レンダーターゲット(プライマリ用)の作成
	UINT strideHandleBytes = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < desc_swap_chain.BufferCount; i++) {
		m_SwapChain->GetBuffer(
			i,
			IID_PPV_ARGS(m_RenderTargets[i].GetAddressOf())
		);

		m_RTV_Handle[i] = m_RTV_Heap->GetCPUDescriptorHandleForHeapStart();
		m_RTV_Handle[i].ptr += i * strideHandleBytes;
		m_Device->CreateRenderTargetView(
			m_RenderTargets[i].Get(),
			nullptr,
			m_RTV_Handle[i]
		);
	}

	// =======================================================
	//		ディスクリプタヒープ(Depth Stencil View用)の作成
	// =======================================================
	D3D12_DESCRIPTOR_HEAP_DESC dsvheapDesc = {};
	dsvheapDesc.NumDescriptors = 1;
	dsvheapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	m_Device->CreateDescriptorHeap(
		&dsvheapDesc,
		IID_PPV_ARGS(m_DSV_Heap.GetAddressOf())
	);

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

	m_Device->CreateDepthStencilView(m_Depthbuffer.Get(),
		nullptr,
		m_DSV_Heap->GetCPUDescriptorHandleForHeapStart());

	// ============================
	//		3D描画用アセットの作成
	// ============================
	CreateRootSignature();
	CreatePipelineStateObject();

	return;
}

bool DirectXApp::ReloadShader()
{
	if (!m_ShaderLibrary.ReoadChanged())
	{
		return false;
	}

	m_PsoCache.Clear();
	CreatePipelineStateObject();
	return true;
}

void DirectXApp::CreateRootSignature()
{
#pragma region RoptSignature(テクスチャ適応前)

	// ==================================
	//		1. ルートパラメータを定義
	// ==================================
	
	CD3DX12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	
	// ここでは一つのルートパラメータを作成
	// 頂点シェーダで使う定数バッファ(b0)をバインドする設定
	CD3DX12_ROOT_PARAMETER rootParameters[2] = {};
	// b0
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

	// t0
	rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	// ルートパラメータ... GPUシェーダーに渡すリソース(定数バッファ・テクスチャ・サンプラーなど)のこと
	// D3D12_SHADER_VISIBILITY_VERTEX... 頂点シェーダーのみで使用

	// サンプラー定義
	CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc(
		0,	// shaderRegister : s0
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
		);

	// ==================================
	//		2. ルートシグネチャの説明を作成
	// ==================================
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		_countof(rootParameters),
		rootParameters,
		1,
		&staticSamplerDesc,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);
#pragma endregion

	// ==================================
	//		3. ルートシグネチャをシリアライズ
	// ==================================
	ComPtr<ID3DBlob> SerializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	// シリアライズ実行、失敗すると例外が発生する
	auto result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&SerializedRootSig,
		&errorBlob);
	if (FAILED(result))return;

	// ==================================
	//		4. ルートシグネチャの生成
	// ==================================
	m_Device->CreateRootSignature(
		0,
		SerializedRootSig->GetBufferPointer(),
		SerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(m_rootSignature.GetAddressOf())
	);
}

void DirectXApp::CreatePipelineStateObject()
{
	// 頂点レイアウト
	D3D12_INPUT_ELEMENT_DESC InputLayout[] =
	{
		{"Position",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"Normal",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}, 
	};

	//// ---- シェーダー読み込み ----
	//Shader vs;
	//Shader ps;
	//Shader toon;
	//Shader wirePs;
	//Shader lineVS;
	//Shader linePS;

	//if (!vs.LoadFromFile(L"VertexShader.hlsl", "BasicVS", "vs_5_0")) {
	//	assert(false);
	//	return;
	//}
	//if (!ps.LoadFromFile(L"PixelShader.hlsl", "BasicPS", "ps_5_0")) {
	//	assert(false);
	//	return;
	//}
	//if(!toon.LoadFromFile(L"ToonShader.hlsl", "ToonPS", "ps_5_0")) {
	//	assert(false);
	//	return;
	//}

	//if (!wirePs.LoadFromFile(L"PixelShader.hlsl", "WireFramePS", "ps_5_0")) {
	//	assert(false);
	//	return;
	//}

	//if (!linePS.LoadFromFile(L"PS_LineShader.hlsl", "LinePS", "ps_5_0"))
	//{
	//	assert(false);
	//	return;
	//}

	//if(!lineVS.LoadFromFile(L"PS_LineShader.hlsl", "LineVS", "vs_5_0"))
	//{
	//	assert(false);
	//	return;
	//}

	const Shader* vs = m_ShaderLibrary.Load(L"VertexShader.hlsl", "BasicVS", "vs_5_0");
	const Shader* ps = m_ShaderLibrary.Load(L"PixelShader.hlsl", "BasicPS", "ps_5_0");
	const Shader* toon = m_ShaderLibrary.Load(L"ToonShader.hlsl", "ToonPS", "ps_5_0");
	const Shader* wirePs = m_ShaderLibrary.Load(L"PixelShader.hlsl", "WireFramePS", "ps_5_0");
	const Shader* lineVS = m_ShaderLibrary.Load(L"PS_LineShader.hlsl", "LineVS", "vs_5_0");
	const Shader* linePS = m_ShaderLibrary.Load(L"PS_LineShader.hlsl", "LinePS", "ps_5_0");

	if (vs == nullptr || ps == nullptr || toon == nullptr || 
		wirePs == nullptr || lineVS == nullptr || linePS == nullptr)
	{
		assert(false);
		return;
	}

	// ライン用のレイアウト
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
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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


	m_PipelineStates[vsIndex][psIndex] = m_PsoCache.GetOrCreate("BasicVS_BasicPS",
		m_Device.Get(),
		psoDesc
	);
	if(m_PipelineStates[vsIndex][psIndex] == nullptr) {
		assert(false);
	}

	auto toonDesc = psoDesc;
	toonDesc.PS = toon->GetByteCode();
	m_PipelineStates[vsIndex][psToonIndex] = m_PsoCache.GetOrCreate("BasicVS_ToonPS",
		m_Device.Get(),toonDesc
	);
	if(m_PipelineStates[vsIndex][psToonIndex] == nullptr) {
		assert(false);
	}


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

	CreateMeshShaderPipelineState();
}

DirectXApp::~DirectXApp() 
{
	WaitForGPUIdle();

	if (m_Fence_Event != nullptr)
	{
		CloseHandle(m_Fence_Event);
		m_Fence_Event = nullptr;
	}
}


HRESULT DirectXApp::BeginRender()
{
	// 現在のバックバッファインデックスを取得
	const UINT targetIndex = m_FrameIndex;

	// バックバッファ用アロケータGPU完了待ち
	const UINT64 fenceToWait = m_FenceValue[targetIndex];
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

	auto dsvhandle = m_DSV_Heap->GetCPUDescriptorHandleForHeapStart();

	// リソースバリアの設定
	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[targetIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

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

	// レンダーターゲットのセット
	m_CommandList->OMSetRenderTargets(1, &m_RTV_Handle[targetIndex], FALSE, &dsvhandle);

	// レンダーターゲットのクリア処理
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

	// Presentする前の準備
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

	// 積んだコマンドの実行
	ID3D12CommandList* pCommandList = m_CommandList.Get();
	m_CommandQueue->ExecuteCommandLists(1, &pCommandList);

	// このフレームの完了フェンス値を記録
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

	// メッシュシェーダーが対応していない場合処理しない
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
	D3D12_RESOURCE_BARRIER descBarrier;
	ZeroMemory(&descBarrier, sizeof(descBarrier));
	descBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	descBarrier.Transition.pResource = Resource;
	descBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	descBarrier.Transition.StateBefore = Before;
	descBarrier.Transition.StateAfter = After;
	CommandList->ResourceBarrier(1, &descBarrier);
}