/*****************************************************************//**
 * \file   PipelineStateCache.hpp
 * \brief  PSOキャッシュ管理
 * 
 * 作成者 keeep
 * 作成日 2026/5/8
 * 更新履歴 5.8 作成
 * *********************************************************************/
#pragma once

#include "Defines.hpp"

class PipelineStateCache
{
public:
	ComPtr<ID3D12PipelineState> GetOrCreate(
		_In_ const std::string& key,
		_In_ ID3D12Device* device,
		_In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);

	void Clear();
private:
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PipelineStates;
};

