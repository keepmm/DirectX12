/*****************************************************************//**
 * \file   D3D12DescriptorHeap.hpp
 * \brief  CPU専用(非シェーダー可視)デスクリプタヒープ
 * 
 * 作成者 keepmm
 * 作成日 2026/9/30
 * 更新履歴 9.30 作成
 * *********************************************************************/
#pragma once

#include "../../Defines.hpp"

class D3D12DescriptorHeap
{
public:
	static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

	bool Init(
		_In_ ID3D12Device* device,
		_In_ D3D12_DESCRIPTOR_HEAP_TYPE type,
		_In_ uint32_t capacity
	)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;	// CPU 専用
		if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_Heap.ReleaseAndGetAddressOf()))))
		{
			return false;
		}
		m_Start = m_Heap->GetCPUDescriptorHandleForHeapStart();
		m_Stride = device->GetDescriptorHandleIncrementSize(type);
		m_Capacity = capacity;
		m_Next = 0;
		m_FreeList.clear();
		return true;
	}

	/// @brief 1 スロット確保する
/// @return 満杯なら INVALID_INDEX
	uint32_t Allocate()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (!m_FreeList.empty())
		{
			const uint32_t index = m_FreeList.back();
			m_FreeList.pop_back();
			return index;
		}
		if (m_Next >= m_Capacity) return INVALID_INDEX;
		return m_Next++;
	}

	/// @brief INVALID_INDEX を渡しても何もしない
	void Free(_In_ uint32_t index)
	{
		if (index >= m_Capacity) return;
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_FreeList.push_back(index);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Cpu(_In_ uint32_t index) const noexcept
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = m_Start;
		h.ptr += static_cast<SIZE_T>(index) * m_Stride;
		return h;
	}

private:
	ComPtr<ID3D12DescriptorHeap>	m_Heap;
	D3D12_CPU_DESCRIPTOR_HANDLE		m_Start{};
	uint32_t						m_Stride = 0;
	uint32_t						m_Capacity = 0;
	uint32_t						m_Next = 0;
	std::vector<uint32_t>			m_FreeList;
	std::mutex						m_Mutex;
};
