/*****************************************************************//**
 * \file   DirectX.hpp
 * \brief  DirectX12のレンダラー
 *
 * 作成者 murao
 * 作成日 2026/2/11
 * 更新履歴 2/11 作成
 * *********************************************************************/
#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "Defines.hpp"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

class DirectXApp
{
public:
	static constexpr int RTV_NUM = 2;

public:
	// 初期化
	DirectXApp(HWND hWnd, int Window_Width, int Window_Height);


	~DirectXApp();

	// レンダリング
	HRESULT BeginRender();
	HRESULT EndRender();

	ComPtr<ID3D12Device> GetDevice() const { return m_Device; }
	ComPtr<ID3D12GraphicsCommandList> GetCommandList() const { return m_CommandList; }

	ComPtr<ID3D12PipelineState> GetPipelineState() const { return m_pipelineState; }
	ComPtr<ID3D12PipelineState> GetPipelineStateWireFrame() const { return m_pipelineStateWireFrame; }
private:
	HWND m_Window_hWnd;
	int  m_Window_Width;
	int  m_Window_Height;

	HANDLE m_Fence_Event;

	ComPtr<ID3D12Device> m_Device;
	ComPtr<ID3D12CommandQueue> m_CommandQueue;
	ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_CommandList;
	ComPtr<IDXGISwapChain3> m_SwapChain;
	ComPtr<ID3D12Fence> m_Fence;
	ComPtr<IDXGIFactory3> m_Factory;
	ComPtr<ID3D12DescriptorHeap> m_RTV_Heap;
	ComPtr<ID3D12DescriptorHeap> m_DSV_Heap;
	ComPtr<ID3D12Resource> m_RenderTargets[RTV_NUM];
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_Handle[RTV_NUM];

	/* 
	*		ルートシグネチャ
	* GPUに「シェーダーがどのリソースを使うか」を使える設定情報
	* 具体的には、定数バッファやテクスチャなどのバインド場所を決める
	* ルートシグネチャがないと、シェーダーにデータを渡せずに描画ができない
	*/
	ComPtr<ID3D12RootSignature> m_rootSignature;

	/*		パイプラインステートオブジェクト
	* パイプラインステートオブジェクト 通称PSO
	* シェーダー、ブレンド、ラスタライザ、深度ステンシルなどの設定をまとめたもの。
	* 描画時には必ずこの状態をセットしてから描画コマンドを実行する必要がある
	*/
	ComPtr<ID3D12PipelineState> m_pipelineState;

	/*
	*	ワイヤーフレーム用のパイプライン
	*/
	ComPtr<ID3D12PipelineState> m_pipelineStateWireFrame;


	ComPtr<ID3D12Resource> m_Depthbuffer;

	void SetResourceBarrier(
		ID3D12GraphicsCommandList* CommandList,
		ID3D12Resource* Resource,
		D3D12_RESOURCE_STATES Before,
		D3D12_RESOURCE_STATES After);

	void CreateRootSignature();
	void CreatePipelineStateObject();

	void WaitForCommandQueue(ID3D12CommandQueue* pCommandQueue);
};

// ルートシグネチャーとパイプラインステート参考資料
// https://zenn.dev/airisshusoft/articles/10c24000c03ff4
