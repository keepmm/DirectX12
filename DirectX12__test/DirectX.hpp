#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#include "RenderContext.hpp"
#include "ShaderLibrary.hpp"
#include "PipelineStateCache.hpp"

#include "Defines.hpp"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct DescriptorAllocator
{
	ComPtr<ID3D12DescriptorHeap> heap;
	UINT decSize = 0;	// デスクリプタヒープ内のサイズ
	UINT capacity = 0;	// デスクリプタヒープの容量
	std::vector<UINT> freeList;	// 空いているスロットのリスト
	UINT next = 0;

	void Init(
		_In_ ID3D12Device* device,
		_In_ D3D12_DESCRIPTOR_HEAP_TYPE type,
		_In_ UINT count,
		_In_ bool shaderVisible)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type			 = type;
		desc.NumDescriptors = count;
		desc.Flags			= shaderVisible 
			? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
			: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.ReleaseAndGetAddressOf()));
		decSize		= device->GetDescriptorHandleIncrementSize(type);
		capacity	= count;
		next		= 0;
		freeList.clear();
	}

	/// @brief スロットの確保
	/// @param outIndex 確保するIndex
	/// @return 確保できた場合True 
	bool Allocate(UINT& outIndex)
	{
		if (!freeList.empty())
		{
			outIndex = freeList.back();
			freeList.pop_back();
			return true;
		}

		// 次のスロットがキャパシティを超えた場合
		// 確保失敗
		if (next >= capacity)
		{
			return false;
		}

		// 線形確保
		outIndex = next++;
		return true;
	}

	void Free(UINT index)
	{
		if (index < capacity)
		{
			freeList.push_back(index);
		}
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT i)const
	{
		auto h = heap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<SIZE_T>(i) * decSize;
		return h;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT i)const
	{
		auto h = heap->GetGPUDescriptorHandleForHeapStart();
		h.ptr += static_cast<SIZE_T>(i) * decSize;
		return h;
	}
};

class DirectXApp
{
public:
	/// @brief バッファの数
	static constexpr int RTV_NUM = 3;

	/// @brief グローバル SRV ヒープのスロット数
	static constexpr UINT SRV_HEAP_SIZE = 256;

	/// @brief グローバル SRV ヒープを持つ唯一の DirectXApp インスタンスを返す
	static DirectXApp* GetCurrent() noexcept { return s_Instance; }

	static constexpr size_t VERTEX_SHADER_COUNT = static_cast<size_t>(E_VERTEX_SHADER::COUNT);
	static constexpr size_t PIXEL_SHADER_COUNT = static_cast<size_t>(E_PIXEL_SHADER::COUNT);

	using PipelineStateTable = std::array<std::array<ComPtr<ID3D12PipelineState>, PIXEL_SHADER_COUNT>, VERTEX_SHADER_COUNT>;

public:

	DirectXApp(HWND hWnd, int Window_Width, int Window_Height);

	~DirectXApp();

	HRESULT BeginRender();
	HRESULT EndRender();
	void Present();

	bool ReloadShader();

	// ---------------------------------------------------//
	//						Getter						  //
	// ---------------------------------------------------//
	inline ComPtr<ID3D12Device> GetDevice() const noexcept { return m_Device; }
	inline ComPtr<ID3D12GraphicsCommandList> GetCommandList() const noexcept { return m_CommandList; }
	inline ID3D12GraphicsCommandList6* GetCommandList6() const noexcept { return m_CommandList6.Get(); }
	inline ComPtr<ID3D12RootSignature> GetRootSignature() const noexcept { return m_rootSignature; }
	inline ComPtr<ID3D12PipelineState> GetPipelineStateWireFrame() const noexcept { return m_pipelineStateWireFrame; }
	inline ComPtr<ID3D12PipelineState> GetLinePso() const noexcept { return m_LinePso; }
	inline ID3D12PipelineState* GetMeshPso() const noexcept { return m_MeshPso.Get(); }
	inline bool IsMeshShaderSupported() const noexcept { return m_MeshShaderSupported; }
	inline ComPtr<ID3D12CommandQueue> GetCommandQueue() const noexcept { return m_CommandQueue; }
	inline UINT GetFrameIndex() const noexcept { return m_FrameIndex; }

	/// @brief グローバル SRV ヒープを返す（SetDescriptorHeaps 用）
	ID3D12DescriptorHeap* GetSrvHeap() const noexcept { return m_SrvAllocator.heap.Get(); }
	inline const PipelineStateTable& GetPipelineStates() const noexcept { return m_PipelineStates; }
	inline ComPtr<ID3D12PipelineState> GetPipelineState() const noexcept
	{
		return m_PipelineStates[static_cast<size_t>(E_VERTEX_SHADER::BASIC)][static_cast<size_t>(E_PIXEL_SHADER::BASIC)];
	}
	inline ComPtr<ID3D12PipelineState> GetPipelineState(E_VERTEX_SHADER vs, E_PIXEL_SHADER ps) const noexcept
	{
		return m_PipelineStates[static_cast<size_t>(vs)][static_cast<size_t>(ps)];
	}

	inline DescriptorAllocator& GetSrvAllocator() noexcept { return m_SrvAllocator; }
	inline DescriptorAllocator& GetRtvAllocator() noexcept { return m_RtvAllocator; }
	inline DescriptorAllocator& GetDsvAllocator() noexcept { return m_DsvAllocator; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const noexcept { return m_DSV_Handle; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const noexcept { return m_RTV_Handle[m_FrameIndex]; }
	void WaitForGPUIdle();
private:
	static DirectXApp* s_Instance;

	HWND m_Window_hWnd;
	int  m_Window_Width;
	int  m_Window_Height;
	HANDLE m_Fence_Event;
	UINT64 m_NextFenceValue = 0;
	ComPtr<ID3D12Device> m_Device;
	ComPtr<ID3D12Device2> m_Device2;
	ComPtr<ID3D12CommandQueue> m_CommandQueue;
	ComPtr<ID3D12CommandAllocator> m_CommandAllocator[RTV_NUM];
	ComPtr<ID3D12GraphicsCommandList> m_CommandList;
	ComPtr<IDXGISwapChain3> m_SwapChain;
	ComPtr<ID3D12Fence> m_Fence;
	ComPtr<IDXGIFactory3> m_Factory;


	DescriptorAllocator m_SrvAllocator;
	DescriptorAllocator m_RtvAllocator;
	DescriptorAllocator m_DsvAllocator;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DSV_Handle;

	ComPtr<ID3D12Resource> m_RenderTargets[RTV_NUM];
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_Handle[RTV_NUM];
	UINT m_FrameIndex = 0;
	UINT64 m_FenceValue[RTV_NUM] = {};


	ComPtr<ID3D12RootSignature> m_rootSignature;
	PipelineStateTable m_PipelineStates{};
	ComPtr<ID3D12PipelineState> m_pipelineStateWireFrame;
	ComPtr<ID3D12PipelineState> m_LinePso;

	ComPtr<ID3D12GraphicsCommandList6> m_CommandList6;
	ComPtr<ID3D12PipelineState> m_MeshPso;
	bool m_MeshShaderSupported = false;

	void CreateMeshShaderPipelineState();

	ShaderLibrary m_ShaderLibrary;
	PipelineStateCache m_PsoCache;

	ComPtr<ID3D12Resource> m_Depthbuffer;

	void SetResourceBarrier(
		ID3D12GraphicsCommandList* CommandList,
		ID3D12Resource* Resource,
		D3D12_RESOURCE_STATES Before,
		D3D12_RESOURCE_STATES After);

	void CreateRootSignature();
	void CreatePipelineStateObject();
};
