/*!*************************************************************
 * ile   EditorPanel.hpp
 * rief  エディタのパネル 1 枚を表す基底クラス
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow の分割にあわせて作成
 *
 * 
ote ImGui::Begin / End は EditorWindow 側が行う。派生クラスは
 *       ウィンドウの中身だけを描く。パネルを増やすときは
 *       EditorWindow のコンストラクタで AddPanel するだけでよい
 * *********************************************************************/
#pragma once

struct EditorContext;

class EditorPanel
{
public:
	virtual ~EditorPanel() = default;

	/// @brief ImGui ウィンドウのタイトル(UTF-8)
	virtual const char* Title() const = 0;

	/// @brief ウィンドウの中身を描く
	virtual void Draw(EditorContext& ctx) = 0;

	/// @brief 「ウィンドウ」メニューの表示チェック
	bool visible = true;
};
