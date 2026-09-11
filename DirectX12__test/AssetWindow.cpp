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
#include "Project.hpp"
#include "DragFiles.hpp"

#pragma comment(lib, "psapi.lib")

namespace
{
	/// @brief 拡張子を小文字化する（大文字の .FBX / .GLB でも同じ扱いにするため）
	std::string ToLowerExt(const std::string& ext)
	{
		std::string out = ext;
		for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	/// @brief ModelLoader が読めるモデル拡張子か（BuildSystem.cpp のリストと揃える）
	bool IsModelExtension(const std::string& extLower)
	{
		static const char* kModelExts[] = {
			".pmx", ".pmd", ".fbx", ".obj", ".gltf", ".glb", ".dae", ".x" };
		for (const char* e : kModelExts)
			if (extLower == e) return true;
		return false;
	}
}

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
		// ---- エクスプローラーからのドロップ ---- //
		// ImGui はOSのドロップを受けないので、WndProc が拾ったものを
		// 自分の矩形内かどうかで判定して取り込む
		if (DropFiles::Get().HasPending())
		{
			const ImVec2 pos = ImGui::GetWindowPos();
			const ImVec2 size = ImGui::GetWindowSize();
			const float dx = static_cast<float>(DropFiles::Get().X());
			const float dy = static_cast<float>(DropFiles::Get().Y());

			if (dx >= pos.x && dx <= pos.x + size.x &&
				dy >= pos.y && dy <= pos.y + size.y)
			{
				ImportAssets(DropFiles::Get().Consume());
			}
		}
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
				auto U8 = [](const std::filesystem::path& p) {
					auto s = p.u8string();
					return std::string(s.begin(), s.end());
					};
				const std::string name = U8(entry.path().filename());
				const std::string fullPath = U8(entry.path());
				const std::string ext = ToLowerExt(U8(entry.path().extension()));

				// -------------------------------------//
				// アイコンとフォールバック色を決める	//
				// -------------------------------------//
				std::wstring iconPath;
				const char* label = "FILE";
				ImVec4 color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

				if (isFolder)
				{
					iconPath = EngineAssetPath(L"Icons/Folder.png").wstring();
					label = "DIR";
					color = ImVec4(0.8f, 0.7f, 0.3f, 1.0f);
				}
				else if (IsModelExtension(ext))
				{
					iconPath = EngineAssetPath(L"Icons/Model.png").wstring();
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
					iconPath = EngineAssetPath(L"Icons/Json.png").wstring();
					label = "JSON";
					color = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
				}
				else
				{
					iconPath = EngineAssetPath(L"Icons/File.png").wstring();
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
					// 拡張子からペイロード種別を決める
					const char* payloadType = "ASSET_FILE";   // 既定（汎用）
					if (IsModelExtension(ext))
						payloadType = "ASSET_MODEL";
					else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
						ext == ".dds" || ext == ".tga" || ext == ".bmp" || ext == ".hdr")
						payloadType = "ASSET_TEXTURE";
					else if (ext == ".ttf" || ext == ".ttc" || ext == ".otf")
						payloadType = "ASSET_FONT";
					else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
						payloadType = "ASSET_AUDIO";

					ImGui::SetDragDropPayload(payloadType,
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

				// ---- アイテム上の右クリック ---- //
				if (ImGui::BeginPopupContextItem("ItemCtx"))
				{
					// 右クリックした時点で選択も移す(Unity と同じ挙動)
					m_SelectedAsset = fullPath;
					m_ContextTarget = fullPath;

					if (isFolder)
					{
						if (ImGui::MenuItem(u8("開く")))
						{
							pendingDir = fullPath;
						}
					}
					else if (ext == ".json")
					{
						if (ImGui::MenuItem(u8("シーンを開く")))
						{
							pendingScenepath = fullPath;
						}
					}
					else if (ext == ".hpp" || ext == ".h" || ext == ".cpp" ||
						ext == ".c" || ext == ".hlsl" || ext == ".hlsli")
					{
						if (ImGui::MenuItem(u8("Visual Studio で開く")))
						{
							OpenInEditor(fullPath);
						}
					}

					if (ImGui::MenuItem(u8("エクスプローラーで表示")))
					{
						RevealInExplorer(fullPath);
					}

					ImGui::Separator();

					if (ImGui::MenuItem(u8("名前を変更"), "F2"))
					{
						std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
						m_ShowRenamePopup = true;
					}

					if (ImGui::MenuItem(u8("複製"), "Ctrl+D"))
					{
						DuplicateAsset(fullPath);
					}

					if (ImGui::MenuItem(u8("削除"), "Delete"))
					{
						m_ShowDeletePopup = true;
					}

					ImGui::Separator();

					if (ImGui::MenuItem(u8("パスをコピー")))
					{
						ImGui::SetClipboardText(fullPath.c_str());
					}

					ImGui::EndPopup();
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

		// ---- 空白部分の右クリック(タイルの上では出さない) ---- //
		if (ImGui::BeginPopupContextWindow("AssetCtx##",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			m_ContextTarget.clear();

			if (ImGui::BeginMenu(u8("作成")))
			{
				if (ImGui::MenuItem(u8("フォルダー")))
				{
					CreateFolder(m_CurrentAssetDir);
				}

				ImGui::Separator();

				if (ImGui::MenuItem(u8("シーン")))
				{
					CreateSceneFile(m_CurrentAssetDir);
				}

				if (ImGui::MenuItem(u8("C++ スクリプト")))
				{
					m_ShowCreateScriptPopup = true;
				}

				ImGui::EndMenu();
			}

			ImGui::Separator();

			if (ImGui::MenuItem(u8("エクスプローラーで開く")))
			{
				RevealInExplorer(m_CurrentAssetDir);
			}

			if (ImGui::MenuItem(u8("プロジェクトのルートを開く")))
			{
				RevealInExplorer(PROJECT->GetRoot().string());
			}

			ImGui::EndPopup();
		}

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

		// ---- 名前を変更 ---- //
		if (m_ShowRenamePopup) { ImGui::OpenPopup("RenameAsset"); m_ShowRenamePopup = false; }
		if (ImGui::BeginPopupModal("RenameAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText(u8("新しい名前"), m_RenameBuffer, sizeof(m_RenameBuffer));

			const bool ok = m_RenameBuffer[0] != '\0' && !m_ContextTarget.empty();

			ImGui::BeginDisabled(!ok);
			if (ImGui::Button(u8("変更")))
			{
				RenameAsset(m_ContextTarget, m_RenameBuffer);
				m_SelectedAsset.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button(u8("キャンセル"))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		// ---- 削除 ---- //
		if (m_ShowDeletePopup) { ImGui::OpenPopup("DeleteAsset"); m_ShowDeletePopup = false; }
		if (ImGui::BeginPopupModal("DeleteAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text(u8("削除しますか？"));
			ImGui::TextDisabled("%s", m_ContextTarget.c_str());
			ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), u8("元に戻せません。"));

			ImGui::Separator();

			if (ImGui::Button(u8("削除")))
			{
				DeleteAsset(m_ContextTarget);
				m_SelectedAsset.clear();
				m_ContextTarget.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(u8("キャンセル"))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
}