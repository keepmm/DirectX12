#include "EditorWindow.hpp"
#include "SceneSerializer.hpp"
#include "Components.hpp"
#include "PrefabLibrary.hpp"
#include "imguiinit.hpp"
#include <filesystem>
#include <cstdio>
#include "RuntimeScene.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include <Psapi.h>
#include "IconLibrary.hpp"
#include "ModelLoader.hpp"
#include "ImGuizmo.h"
#include "PlayState.hpp"
#include <shellapi.h>

#pragma comment(lib, "psapi.lib")

void EditorWindow::DrawAssetPanel(SceneManager& sceneManager)
{
	namespace fs = std::filesystem;
	const fs::path assetRoot = "Assets";

	if (!fs::exists(assetRoot))
	{
		ImGui::Text(u8("Assets フォルダが見つかりません"));
		return;
	}

	// 開いていたフォルダが消されていたらルートに戻す
	if (!fs::exists(m_CurrentAssetDir))
	{
		m_CurrentAssetDir = assetRoot.string();
	}

	// -------------------------//
	//		 ツールバー			//
	// -------------------------//

	// 「↑」ボタン: 親フォルダへ（ルートでは無効化）
	const bool atRoot = fs::equivalent(m_CurrentAssetDir, assetRoot);
	ImGui::BeginDisabled(atRoot);
	if (ImGui::Button(u8("↑##AssetUp")))
	{
		m_CurrentAssetDir = fs::path(m_CurrentAssetDir).parent_path().string();
	}
	ImGui::EndDisabled();

	// 現在のパスを表示
	ImGui::SameLine();
	ImGui::Text("%s", m_CurrentAssetDir.c_str());

	// セルサイズのスライダー
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat(u8("サイズ##AssetCell"), &m_AssetCellSize, 48.0f, 128.0f, "%.0f");

	ImGui::Separator();

	const float cellSize = m_AssetCellSize;
	const ImVec2 tileSize(cellSize, cellSize);

	if (ImGui::BeginChild("AssetList", ImVec2(0.0f, 0.0f), true))
	{
		const float windowRight =
			ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		const ImGuiStyle& style = ImGui::GetStyle();

		// ---------------------------------------//
		// フォルダ→ファイルの順に並べたいので分ける //
		// ---------------------------------------//
		std::vector<fs::directory_entry> folders;
		std::vector<fs::directory_entry> files;
		for (const auto& entry : fs::directory_iterator(m_CurrentAssetDir))
		{
			if (entry.is_directory())
			{
				folders.push_back(entry);
			}
			else
			{
				files.push_back(entry);
			}
		}

		std::string pendingDir;	// イテレーション中にm_CurrentAssetDirを書き換えないため
		std::string pendingScenepath;

		// タイル1個を描く共通処理
		auto drawTile = [&](const fs::directory_entry& entry, bool isFolder)
			{
				const std::string name = entry.path().filename().string();
				const std::string fullPath = entry.path().string();
				const std::string ext = entry.path().extension().string();

				// -------------------------------------//
				// アイコンとフォールバック色を決める	//
				// -------------------------------------//
				std::wstring iconPath;
				const char* label = "FILE";
				ImVec4 color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

				if (isFolder)
				{
					iconPath = L"Assets/Icons/Folder.png";
					label = "DIR";
					color = ImVec4(0.8f, 0.7f, 0.3f, 1.0f);
				}
				else if (ext == ".fbx" || ext == ".obj")
				{
					iconPath = L"Assets/Icons/Model.png";
					label = "3D";
					color = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);
				}
				else if (ext == ".png" || ext == ".jpg" || ext == ".bmp")
				{
					iconPath = entry.path().wstring();	// 画像は自分自身をサムネイルに
					label = "IMG";
					color = ImVec4(0.3f, 0.7f, 0.3f, 1.0f);
				}
				else if (ext == ".json")
				{
					iconPath = L"Assets/Icons/Json.png";
					label = "JSON";
					color = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
				}
				else
				{
					iconPath = L"Assets/Icons/File.png";
				}

				const bool selected = (m_SelectedAsset == fullPath);
				if (selected)
				{
					color.x = std::min(color.x + 0.2f, 1.0f);
					color.y = std::min(color.y + 0.2f, 1.0f);
					color.z = std::min(color.z + 0.2f, 1.0f);
				}

				ImGui::PushID(fullPath.c_str());
				ImGui::BeginGroup();

				// ---- タイル本体 ---- //
				bool clicked = false;
				ImTextureID icon = IconLibrary::Get()->GetOrLoad(iconPath);
				if (icon != 0)
				{
					clicked = ImGui::ImageButton("##tile", icon, tileSize);
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Button, color);
					clicked = ImGui::Button(label, tileSize);
					ImGui::PopStyleColor();
				}

				if (clicked)
				{
					m_SelectedAsset = fullPath;
				}

				// 右クリックでコンテキストメニュー
				if (ImGui::BeginPopupContextWindow("AssetCtx##", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
				{
					if (ImGui::BeginMenu(u8("作成")))
					{


						//if (ImGui::MenuItem(u8("フォルダーの作成")))
						//{

						//}
						//ImGui::EndMenu();

						if (ImGui::MenuItem(u8("C++ スクリプト")))
						{
							// 作成処理(予定)
							m_ShowCreateScriptPopup = true;
						}
						ImGui::EndMenu();
					}
					ImGui::EndPopup();
				}

				// フォルダはダブルクリックで中に入る
				if (isFolder &&
					ImGui::IsItemHovered() &&
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					pendingDir = fullPath;
				}

				// .hpp/.h/.cpp/.hlsl などをダブルクリックで Visual Studio で開く
				if (!isFolder &&
					(ext == ".hpp" || ext == ".h" || ext == ".cpp" || ext == ".c" || ext == ".hlsl") &&
					ImGui::IsItemHovered() &&
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					OpenInEditor(fullPath);
				}

				if (!isFolder && ext == ".json" &&
					ImGui::IsItemHovered() &&
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					pendingScenepath = fullPath;
				}

				// ---- ドラッグ元（ファイルのみ）---- //
				if (!isFolder && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("ASSET_MODEL",
						fullPath.c_str(), fullPath.size() + 1);
					ImGui::Text("%s", name.c_str());
					ImGui::EndDragDropSource();
				}

				// ホバーでフルパス表示
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("%s", fullPath.c_str());
					ImGui::EndTooltip();
				}

				// ---- 名前（タイル幅で折り返し）---- //
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cellSize);
				ImGui::TextUnformatted(name.c_str());
				ImGui::PopTextWrapPos();

				ImGui::EndGroup();

				// ---- 折り返し ---- //
				const float lastTileRight = ImGui::GetItemRectMax().x;
				const float nextTileRight = lastTileRight + style.ItemSpacing.x + cellSize;
				if (nextTileRight < windowRight)
				{
					ImGui::SameLine();
				}

				ImGui::PopID();
			};

		// フォルダ → ファイルの順で描画
		for (const auto& f : folders) { drawTile(f, true); }
		for (const auto& f : files) { drawTile(f, false); }

		// ループ後にフォルダ移動を反映
		if (!pendingDir.empty())
		{
			m_CurrentAssetDir = pendingDir;
		}

		if (!pendingScenepath.empty() && sceneManager.GetFadeAlpha() == 0.0f)
		{
			sceneManager.RequestSceneChangeWithString(pendingScenepath);
			m_SelectedEntity = INVALID_ENTITY;	// シーン切り替えで選択エンティティはリセット
		}

		if (m_ShowCreateScriptPopup) { ImGui::OpenPopup("CreateScript"); m_ShowCreateScriptPopup = false; }
		if (ImGui::BeginPopupModal("CreateScript", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText(u8("クラス名"), m_NewScriptName, sizeof(m_NewScriptName));
			if (ImGui::Button(u8("作成")) && m_NewScriptName[0] != '\0')
			{
				CreateScriptFile(m_CurrentAssetDir, m_NewScriptName);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(u8("キャンセル"))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
}