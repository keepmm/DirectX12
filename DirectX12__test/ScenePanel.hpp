/*!*************************************************************
 * \file   ScenePanel.hpp
 * \brief  シーン設定パネル(保存 / 読み込みと Prefab 配置)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#pragma once

#include "EditorPanel.hpp"
#include "EditorContext.hpp"
#include <array>
#include <string>
#include "Defines.hpp"

class ScenePanel : public EditorPanel
{
public:
	const char* Title() const override
	{
		// u8() は一時 std::string の c_str() なので、返すと寿命が切れる。static に保持する
		static const std::string title = IMGUI::ToUTF8("シーン設定");
		return title.c_str();
	}
	void Draw(_In_ EditorContext& ctx) override;

private:
	/// @brief プレハブパネルの描画
	void DrawPrefabPanel(_In_ EditorContext& ctx, _In_ Scene& scene, _In_ World& world);

	std::array<char, 64> m_SceneRegisterName{};
	std::string m_SelectedPrefab;
	float3 m_PrefabPosition{ 0.0f,0.0f,0.0f };
};
