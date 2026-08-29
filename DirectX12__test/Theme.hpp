/*****************************************************************//**
 * \file   Theme.hpp
 * \brief  ImGuiのテーマカラーを設定する
 * 
 * 作成者 keeeep
 * 作成日 2026/7/3
 * 更新履歴 7.3 息抜きに作成
 * *********************************************************************/
#pragma once

#include "imguiinit.hpp"

enum class uiTheme : uint8_t
{
	Dark,
	Light,
	classic
};

inline void ApplyTheme(
	_In_ const uiTheme newTheme,
	_In_ const ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f))
{
	// テーマごとにカラー設定
	switch (newTheme)
	{
	case uiTheme::Dark:
		ImGui::StyleColorsDark();
		break;
	case uiTheme::Light:
		ImGui::StyleColorsLight();
		break;
	case uiTheme::classic:
		ImGui::StyleColorsClassic();
		break;
	}

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;	// ウィンドウの角を丸くする
	style.FrameRounding  = 3.0f;	// フレームの角を丸くする
	style.WindowBorderSize = 1.0f;	// ウィンドウの境界線の太さ

	// アクセント色をボタン / ヘッダに反映
	style.Colors[ImGuiCol_Header] = accent;
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.8f);
	style.Colors[ImGuiCol_Button] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
	style.Colors[ImGuiCol_ButtonHovered] = accent;
	style.Colors[ImGuiCol_CheckMark] = accent;
	style.Colors[ImGuiCol_SliderGrab] = accent;
	style.Colors[ImGuiCol_TabActive] = accent;
}
