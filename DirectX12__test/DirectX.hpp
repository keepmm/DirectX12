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
#include "ShadowMap.hpp"
#include "GBuffer.hpp"

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

	UINT AllocateRange(UINT count)
	{
		if (next + count > capacity)
		{
			return UINT_MAX; // 確保失敗
		}
		UINT startIndex = next;
		next += count;
		return startIndex;
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

struct RegisteredPass
{
	ShaderPassDef def;
	ComPtr<ID3D12PipelineState> pso;
};

#include "ConstantBufferAllocator.hpp"

#define APP DirectXApp::GetCurrent()

class DirectXApp
{
public:
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
	void BeginGeometryPass();
	void DeferredLightingPass(const RenderContext& ctx);
	HRESULT EndRender();
	void Present();

	bool ReloadShader();

	bool CreateShadeFromSource(
		_In_ const std::string& name,
		_In_ const std::string& hlslCode,
		_In_ std::string& psEntry,
		_Out_ std::string& outError,
		_In_ bool alphaBlend = false
	);

	void DeferredLightingPass(
		_In_ const RenderContext& ctx,
		D3D12_CPU_DESCRIPTOR_HANDLE targetRtv,
		_In_ const D3D12_VIEWPORT& viewport,
		_In_ const D3D12_RECT& sc
	);

	void PostProcessCopy(
		RenderTexture& src,
		D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
		const D3D12_VIEWPORT& vp, const D3D12_RECT& sc
	);

	void PostProcessBloom(D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
		const D3D12_VIEWPORT& vp, const D3D12_RECT& sc);

	ID3D12PipelineState* RegisterShaderPass(
		_In_ const std::string& name,
		_In_ const ShaderPassDef& def
	);
	ID3D12PipelineState* GetPipelineStateByName(
		_In_ std::string& name
	)const;
	std::vector<std::string> GetShaderNames() const;

	// ---------------------------------------------------//
	//						Getter						  //
	// ---------------------------------------------------//
	inline ComPtr<ID3D12Device> GetDevice() const noexcept { return m_Device; }
	inline ComPtr<ID3D12GraphicsCommandList> GetCommandList() const noexcept { return m_CommandList; }
	inline ID3D12GraphicsCommandList6* GetCommandList6() const noexcept { return m_CommandList6.Get(); }
	inline ComPtr<ID3D12RootSignature> GetRootSignature() const noexcept { return m_rootSignature; }
	inline ComPtr<ID3D12PipelineState> GetPipelineStateWireFrame() const noexcept { return m_pipelineStateWireFrame; }
	inline ComPtr<ID3D12PipelineState> GetLinePso() const noexcept { return m_LinePso; }
	inline ComPtr<ID3D12PipelineState> GetIconPso() const noexcept { return m_IconPso; }
	inline ID3D12PipelineState* GetMeshPso() const noexcept { return m_MeshPso.Get(); }
	inline ID3D12PipelineState* GetUIPso() const noexcept { return m_UIPso.Get(); }
	inline ID3D12PipelineState* GetSkyPso() const noexcept { return m_SkyPso.Get(); }
	inline ID3D12PipelineState* GetShadowPso() const noexcept { return m_ShadowPso.Get(); }
	inline ID3D12PipelineState* GetBeamPso() const noexcept { return m_BeamPso.Get(); }
	inline ID3D12PipelineState* GetGBufferPso() const noexcept { return m_GBufferPso.Get(); }
	inline ID3D12PipelineState* GetDeferredPso() const noexcept { return m_DeferredPso.Get(); }
	inline ID3D12PipelineState* GetPostPso() const noexcept { return m_PostPso.Get(); }
	inline ID3D12PipelineState* GetBeamHdrPso() const noexcept { return m_BeamHdrPso.Get(); }
	inline ID3D12PipelineState* GetCopyPso() const noexcept { return m_CopyPso.Get(); }
	inline ID3D12PipelineState* GetVolumetricPso() const noexcept { return m_VolumetricPso.Get(); }
	inline ID3D12PipelineState* GetVolumetricAddPso() const noexcept {return m_VolumetricAddPso.Get(); }
	inline ID3D12PipelineState* GetFireworkPso() const noexcept { return m_FireworkPso.Get(); }

	inline bool IsMeshShaderSupported() const noexcept { return m_MeshShaderSupported; }
	inline ComPtr<ID3D12CommandQueue> GetCommandQueue() const noexcept { return m_CommandQueue; }
	inline UINT GetFrameIndex() const noexcept { return m_FrameIndex; }
	inline ConstantBufferAllocator& GetConstantBufferAllocator() noexcept { return m_CBAllocator; }
	inline RenderTexture& GetHdrScene() { return m_HdrScene; }

	bool IsShaderAlphaBlend(const std::string& name) const
	{
		auto it = m_ShaderRegistry.find(name);
		return it != m_ShaderRegistry.end() && it->second.def.alphaBlend;
	}

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
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetShadowSrvCpu() const noexcept { return m_ShadowSrvCpu; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetEnvSrvCpu() const noexcept { return m_EnvSrvCpu; }
	bool LoadEnvironment(const std::wstring& hdrpath);
	ID3D12Resource* GetEnvTexture() const { return m_EnvTexture.Get(); }
	UINT GetEnvMipLevels() const { return m_EnvMipLevels; }
	const float3& GetEnvAmbient() const { return m_EnvAmbient; }
	bool HasEnvironment() const { return m_EnvTexture != nullptr; }
	inline ShadowMap& GetShadowMap() noexcept { return m_ShadowMap; }

	void WaitForGPUIdle();
	void DeferRelease(ComPtr<ID3D12Resource>&& r)
	{
		if (r) m_DeferredReleases[m_FrameIndex].push_back(std::move(r));
	}
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

	ConstantBufferAllocator m_CBAllocator;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DSV_Handle;

	ComPtr<ID3D12Resource> m_RenderTargets[RTV_NUM];
	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV_Handle[RTV_NUM];
	UINT m_FrameIndex = 0;
	UINT64 m_FenceValue[RTV_NUM] = {};


	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12RootSignature> m_DeferredRootSignature;
	ComPtr<ID3D12RootSignature> m_PostRootSignature;

	PipelineStateTable m_PipelineStates{};
	ComPtr<ID3D12PipelineState> m_pipelineStateWireFrame;
	ComPtr<ID3D12PipelineState> m_LinePso;
	ComPtr<ID3D12PipelineState> m_IconPso;
	ComPtr<ID3D12PipelineState> m_UIPso;
	ComPtr<ID3D12PipelineState> m_SkyPso;
	ComPtr<ID3D12PipelineState> m_ShadowPso;
	ComPtr<ID3D12PipelineState> m_BeamPso;
	ComPtr<ID3D12PipelineState> m_BeamHdrPso;
	ComPtr<ID3D12PipelineState> m_GBufferPso;
	ComPtr<ID3D12PipelineState> m_DeferredPso;
	ComPtr<ID3D12PipelineState> m_PostPso;
	ComPtr<ID3D12PipelineState> m_CopyPso;
	ComPtr<ID3D12PipelineState> m_VolumetricPso;
	ComPtr<ID3D12PipelineState> m_VolumetricAddPso;

	ComPtr<ID3D12PipelineState> m_FireworkPso;

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
	void CreateDeferredRootSignature();
	void CreatePostRootSignature();
	void CreatePipelineStateObject();

	std::unordered_map<std::string, RegisteredPass> m_ShaderRegistry;

	void RegisterBuiltinShaders();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeBasePsoDesc()const;

	// Environment Map
	ComPtr<ID3D12Resource> m_EnvTexture;
	UINT m_EnvMipLevels = 1;

	float3 m_EnvAmbient = { 0.1f,0.1f,0.1f };
	ShadowMap m_ShadowMap{};
	std::vector<ComPtr<ID3D12Resource>> m_DeferredReleases[RTV_NUM];

	void CreateGbufferPSO();
	void CreateDeferredPSO();
	void CreatePostPSO();
	void CreateCopyPSO();
	void CreateVolumetricPSO();
	void CreateShadowSrv();
	void CreateEnvSrv();

	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthSrvHandleCpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE m_DepthSrvHandleGpu{};

	GBuffer m_GBuffer{};
	UINT m_ShadowSrvIndex = UINT_MAX;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSrvCpu{};
	UINT m_EnvSrvIndex = UINT_MAX;
	D3D12_CPU_DESCRIPTOR_HANDLE m_EnvSrvCpu{};

	RenderTexture m_HdrScene;
	RenderTexture m_BloomA;
	RenderTexture m_BloomB;
	RenderTexture m_VolumetricHalf;
	ComPtr<ID3D12PipelineState> m_BrightPso;
	ComPtr<ID3D12PipelineState> m_BlurPso;
	ComPtr<ID3D12PipelineState> m_CompositePso;
	D3D12_GPU_DESCRIPTOR_HANDLE m_CompositeSrvStart{};

	void CreateBloomPSOs();
	void BuildCompositeSrvTable();
	void PostPass(ID3D12PipelineState* pso, D3D12_GPU_DESCRIPTOR_HANDLE srvTable,
		D3D12_GPU_VIRTUAL_ADDRESS cb, D3D12_CPU_DESCRIPTOR_HANDLE dstRtv,
		UINT w, UINT h);
};
