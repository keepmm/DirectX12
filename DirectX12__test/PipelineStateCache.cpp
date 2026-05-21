#include "PipelineStateCache.hpp"

ComPtr<ID3D12PipelineState> PipelineStateCache::GetOrCreate(const std::string& key, ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
	auto it = m_PipelineStates.find(key);
	if (it != m_PipelineStates.end())
	{
		return it->second;
	}

	if (device == nullptr)
	{
		return nullptr;
	}

	ComPtr<ID3D12PipelineState> pso;
	const HRESULT hr = device->CreateGraphicsPipelineState(&desc,
		IID_PPV_ARGS(pso.GetAddressOf()));
	if(FAILED(hr))
	{
		assert(false);
		return nullptr;
	}

	// ƒLƒƒƒbƒVƒ…‚É•Û‘¶‚µ‚Ä•Ô‚·
	m_PipelineStates.emplace(key, pso);
	return pso;
}

void PipelineStateCache::Clear()
{
    m_PipelineStates.clear();
}