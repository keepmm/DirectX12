/*!*************************************************************
 * \file   ProfilerPanel.cpp
 * \brief  処理時間の内訳の表示
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "ProfilerPanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "Theme.hpp"
#include "Profiler.hpp"
#include "DirectX.hpp"


void ProfilerPanel::Draw(EditorContext&)
{
	Profiler& prof = Profiler::Get();

	bool enabled = prof.IsEnabled();
	if (ImGui::Checkbox(u8("計測する"), &enabled)) prof.SetEnabled(enabled);
	ImGui::SameLine();
	if (ImGui::SmallButton(u8("項目をクリア"))) prof.Clear();

	const double frame = prof.FrameAvg();

	// CPU行とGPU行を分ける("GPU/" で始まるものが GpuProfiler 由来)
	std::vector<Profiler::Entry> cpuRows;
	std::vector<Profiler::Entry> gpuRows;
	double gpuFrame = 0.0;
	for (const auto& e : prof.Entries())
	{
		if (e.name != nullptr && std::strncmp(e.name, "GPU/", 4) == 0)
		{
			if (std::strcmp(e.name, "GPU/Frame total") == 0) gpuFrame = e.avg;
			gpuRows.push_back(e);
		}
		else
		{
			cpuRows.push_back(e);
		}
	}

	auto byAvg = [](const Profiler::Entry& a, const Profiler::Entry& b) { return a.avg > b.avg; };
	std::sort(cpuRows.begin(), cpuRows.end(), byAvg);
	std::sort(gpuRows.begin(), gpuRows.end(), byAvg);

	ImGui::Text(u8("フレーム: %.2f ms  (%.0f FPS)"),
		frame, frame > 0.0 ? 1000.0 / frame : 0.0);

	if (gpuFrame > 0.0)
	{
		// GPUがフレーム時間をほぼ埋めていれば描画待ち。そうでなければCPU側を見る
		const bool gpuBound = (frame > 0.0) && (gpuFrame > frame * 0.8);
		ImGui::SameLine();
		ImGui::TextColored(gpuBound ? ImVec4(1.0f, 0.55f, 0.35f, 1.0f)
								    : ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
			gpuBound ? u8("GPU %.2f ms / GPU律速") : u8("GPU %.2f ms"), gpuFrame);
	}
	ImGui::Separator();

	const ImGuiTableFlags flags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;

	const double base = (frame > 0.0) ? frame : 1.0;

	auto drawTable = [&](const char* id, const std::vector<Profiler::Entry>& rows)
	{
		if (!ImGui::BeginTable(id, 4, flags)) return;

		ImGui::TableSetupColumn(u8("処理"), ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 64.0f);
		ImGui::TableSetupColumn(u8("割合"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn(u8("回数"), ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableHeadersRow();

		for (const auto& e : rows)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.name);
			ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", e.avg);
			ImGui::TableSetColumnIndex(2);
			ImGui::ProgressBar((float)(e.avg / base), ImVec2(-1.0f, 0.0f), "");
			ImGui::TableSetColumnIndex(3); ImGui::Text("%d", e.calls);
		}
		ImGui::EndTable();
	};

	if (ImGui::CollapsingHeader(u8("GPU (タイムスタンプ)"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (gpuRows.empty())
		{
			ImGui::TextDisabled(u8("計測待ち(結果が出るまで数フレームかかる)"));
		}
		else
		{
			drawTable("##profilerGpu", gpuRows);
		}
	}

	if (ImGui::CollapsingHeader(u8("CPU"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		drawTable("##profilerCpu", cpuRows);
	}

	ImGui::TextDisabled(u8("GPU行はコマンド投入から RTV_NUM フレーム遅れて確定する"));
}
