/*!*************************************************************
 * \file   MemoryPanel.cpp
 * \brief  メモリ使用量の表示
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "MemoryPanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "DirectX.hpp"
#include "ConstantBufferAllocator.hpp"
#include <dxgi1_4.h>
#include <vector>
#include <Windows.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")


constexpr double BYTES_TO_MB = 1.0 / (1024.0 * 1024.0);

SIZE_T GetPrivateWorkingSetBytes(HANDLE process)
{
	DWORD bufferSize = sizeof(PSAPI_WORKING_SET_INFORMATION) + sizeof(ULONG_PTR) * 4096;
	std::vector<unsigned char> buffer(bufferSize);

	for (;;)
	{
		if (QueryWorkingSet(process, buffer.data(), bufferSize))
		{
			break;
		}

		if (GetLastError() != ERROR_BAD_LENGTH)
		{
			return 0;
		}

		bufferSize *= 2;
		buffer.resize(bufferSize);
	}

	const auto* wsInfo = reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION*>(buffer.data());

	SYSTEM_INFO si = {};
	GetSystemInfo(&si);
	const SIZE_T pageSize = si.dwPageSize;

	SIZE_T privatePages = 0;
	for (ULONG_PTR i = 0; i < wsInfo->NumberOfEntries; ++i)
	{
		const PSAPI_WORKING_SET_BLOCK block = wsInfo->WorkingSetInfo[i];
		if (block.Shared == 0)
		{
			++privatePages;
		}
	}

	return privatePages * pageSize;
}
void MemoryPanel::Draw(EditorContext&)
{
	const HANDLE process = GetCurrentProcess();

	// メモリ使用量の表示
	PROCESS_MEMORY_COUNTERS_EX pmc = {};
	if (GetProcessMemoryInfo(
		process,
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
		sizeof(pmc)))
	{
		const double workingSetMB = static_cast<double>(pmc.WorkingSetSize) * BYTES_TO_MB;
		const double privateCommitMB = static_cast<double>(pmc.PrivateUsage) * BYTES_TO_MB;

		const SIZE_T privateWsBytes = GetPrivateWorkingSetBytes(process);
		const double privateWorkingSetMB = static_cast<double>(privateWsBytes) * BYTES_TO_MB;

		ImGui::Text(u8("メモリ(Private Working Set): %.1f MB"), privateWorkingSetMB);
		ImGui::Text(u8("Working Set(共有含む): %.1f MB"), workingSetMB);
		ImGui::Text(u8("Commit Size(PrivateUsage): %.1f MB"), privateCommitMB);
	}

	// ---- 定数バッファのリング(1 フレームのピーク / 容量) ---- //
	{
		const auto& cb = APP->GetConstantBufferAllocator();
		ImGui::Text(u8("定数バッファ ピーク: %.2f MB / %.0f MB"),
			cb.PeakBytes() * BYTES_TO_MB, ConstantBufferAllocator::CapacityBytes() * BYTES_TO_MB);
	}

	// ---- VRAM(このプロセスが GPU に確保している量) ---- //
	{
		ComPtr<IDXGIFactory4> factory;
		ComPtr<IDXGIAdapter3> adapter;
		if (APP->GetDevice() &&
			SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->EnumAdapterByLuid(APP->GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter))))
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
			adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
			adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);

			ImGui::Text(u8("VRAM(専用): %.1f MB / 予算 %.0f MB"),
				local.CurrentUsage * BYTES_TO_MB, local.Budget * BYTES_TO_MB);
			ImGui::Text(u8("VRAM(共有 = メインメモリ側): %.1f MB"),
				shared.CurrentUsage * BYTES_TO_MB);
		}
	}
}
