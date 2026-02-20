#include "DirectX.hpp"
#include "Vertex.hpp"
#include <algorithm>
#include "d3dx12.h"

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

	// ================================
	//		コマンドアロケータを生成
	// ================================
	hr = m_Device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_CommandAllocator.GetAddressOf())
	);
	if (FAILED(hr)) {
		return;
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

	// ==============================
	//		コマンドリストの作成
	// ==============================
	hr = m_Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_CommandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(m_CommandList.GetAddressOf())
	);
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

void DirectXApp::CreateRootSignature()
{
	// ==================================
	//		1. ルートパラメータを定義
	// ==================================
	// ここでは一つのルートパラメータを作成
	// 頂点シェーダで使う定数バッファ(b0)をバインドする設定
	CD3DX12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	// ルートパラメータ... GPUシェーダーに渡すリソース(定数バッファ・テクスチャ・サンプラーなど)のこと
	// D3D12_SHADER_VISIBILITY_VERTEX... 頂点シェーダーのみで使用

	// ==================================
	//		2. ルートシグネチャの説明を作成
	// ==================================
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init(
		_countof(rootParameters),
		rootParameters,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

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
		{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
	};

	// VertexShader(頂点シェーダー)の読み込みとコンパイル
	ComPtr<ID3DBlob> vsBlob, psBlob,WireBlob;
	D3DCompileFromFile(L"VertexShader.hlsl",
		nullptr,
		nullptr,
		"BasicVS",
		"vs_5_0",
		0,
		0,
		&vsBlob,
		nullptr);

	// PixelShaderの読み込みとコンパイル
	D3DCompileFromFile(L"PixelShader.hlsl",
		nullptr,
		nullptr,
		"BasicPS",
		"ps_5_0",
		0,
		0,
		&psBlob,
		nullptr);

	D3DCompileFromFile(L"PixelShader.hlsl",
		nullptr,
		nullptr,
		"WireFramePS",
		"ps_5_0",
		0,
		0,
		&WireBlob,
		nullptr);


	/// パイプラインステートの情報定義
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { InputLayout, _countof(InputLayout) };
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(psBlob.Get());
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;	// カリングなし
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.StencilEnable = TRUE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;

	auto hr = m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) {
		assert(false);
	}

	// ワイヤフレームPSO
	auto wireDesc = psoDesc;
	wireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	wireDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;	// 深度バッファへの書き込みを無効化
	wireDesc.PS = CD3DX12_SHADER_BYTECODE(WireBlob.Get());

	hr = m_Device->CreateGraphicsPipelineState(&wireDesc, IID_PPV_ARGS(m_pipelineStateWireFrame.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) {
		assert(false);
	}
}

DirectXApp::~DirectXApp() {}


HRESULT DirectXApp::BeginRender()
{
	// 現在のバックバッファインデックスを取得
	int TargetIndex = m_SwapChain->GetCurrentBackBufferIndex();

	auto dsvhandle = m_DSV_Heap->GetCPUDescriptorHandleForHeapStart();

	// リソースバリアの設定
	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[TargetIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	m_CommandList->SetPipelineState(m_pipelineState.Get());
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
	m_CommandList->OMSetRenderTargets(1, &m_RTV_Handle[TargetIndex], FALSE, &dsvhandle);

	// レンダーターゲットのクリア処理
	m_CommandList->RSSetViewports(1, &vp);
	m_CommandList->ClearRenderTargetView(
		m_RTV_Handle[TargetIndex],
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
	int TargetIndex = m_SwapChain->GetCurrentBackBufferIndex();

	// Presentする前の準備
	SetResourceBarrier(
		m_CommandList.Get(),
		m_RenderTargets[TargetIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	m_CommandList->Close();

	// 積んだコマンドの実行
	ID3D12CommandList* pCommandList = m_CommandList.Get();
	m_CommandQueue->ExecuteCommandLists(1, &pCommandList);
	m_SwapChain->Present(1, 0);

	WaitForCommandQueue(m_CommandQueue.Get());

	m_CommandAllocator->Reset();
	m_CommandList->Reset(m_CommandAllocator.Get(), nullptr);
	return S_OK;
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

void DirectXApp::WaitForCommandQueue(ID3D12CommandQueue* pCommandQueue)
{
	static UINT64 frames = 0;
	frames++;

	pCommandQueue->Signal(m_Fence.Get(), frames);

	if (m_Fence->GetCompletedValue() < frames) {
		m_Fence->SetEventOnCompletion(frames, m_Fence_Event);
		WaitForSingleObject(m_Fence_Event, INFINITE);
	}
}
