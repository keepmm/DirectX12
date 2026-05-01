#include "DebugWindow.hpp"
#include <Psapi.h>
#include "imguiinit.hpp"
#include "Time.hpp"
#include "Logger.hpp"

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

void DebugWindow::Draw()
{
	if (!ImGui::Begin(u8("DebugWindow")))
	{
		ImGui::End();
		return;
	}

	// FPS表示
	ImGui::Text("FPS: %d", TIME->GetFPS());
	int fps = TIME->GetFPS();
	if (ImGui::InputInt(u8("FPS制限"), &fps))
	{
		TIME->SetFPS(fps);
	}

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

	ImGui::Separator();
	ImGui::TextUnformatted("Logs");
	const auto& logs = LOG->GetRecentLogs();
	for (const auto& line : logs)
	{
		ImGui::TextUnformatted(line.c_str());
	}

	ImGui::End();
}