/*!*************************************************************
 * \file   EditorMenuBar.cpp
 * \brief  メニューバー / 再生コントロール / プロジェクト切り替え
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "EditorMenuBar.hpp"

#include <cstdio>
#include <Windows.h>
#include <ShObjIdl.h>

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "DirectX.hpp"
#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include "BuildSystem.hpp"
#include "Project.hpp"
#include "PlayState.hpp"
#include "Components.hpp"
#include "AnimatorClipCache.hpp"

static std::string WideToUTF8(const wchar_t* w)
{
	if (!w) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1) return {};
	std::string result(len - 1, '\0');   // len は終端NUL込み
	WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), len, nullptr, nullptr);
	return result;
}

void EditorMenuBar::Draw(EditorContext& ctx)
{
	Scene* activeScene = ctx.activeScene;


	// メニューバー
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu(u8("ファイル")))
		{
			// プロジェクトの切り替えはランチャーに任せる。
			// プロセス内で CWD だけ変えても、シーンやアセットが古いまま残るため
			if (ImGui::MenuItem(u8("プロジェクトを切り替え...")))
			{
				m_ShowSwitchProject = true;
				m_ProjectError.clear();
			}
			ImGui::Separator();

			if (ImGui::MenuItem(u8("シーンを保存"), "Ctrl+S"))
			{
				if (activeScene)
				{
					const std::string path =
						SceneManager::ScenePathFromName(activeScene->GetSceneName());

					if (SceneSerializer::Save(*activeScene, path))
					{
						LOG->LogInfo("シーンを保存しました: " + path);
					}
					else
					{
						LOG->LogError("シーンの保存に失敗しました: " + path);
					}
				}
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(u8("ワークスペース")))
		{
			if (ImGui::MenuItem(u8("ゲームエンジン"), nullptr, workspace == Workspace::Engine))
			{
				workspace = Workspace::Engine;
				relayoutRequested = true;	// 次のフレームで組み直す
			}
			if (ImGui::MenuItem(u8("ライト編集"), nullptr, workspace == Workspace::Lighting))
			{
				workspace = Workspace::Lighting;
				relayoutRequested = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem(u8("配置をリセット"))) relayoutRequested = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(u8("ウィンドウ")))
		{
			// MenuItem に bool* を渡すと、チェックマーク付きのトグルになる
			for (const auto& t : m_WindowToggles)
			{
				ImGui::MenuItem(t.label.c_str(), nullptr, t.visible);
			}

			ImGui::Separator();
			if (ImGui::MenuItem(u8("すべて表示")))
			{
				for (const auto& t : m_WindowToggles) *t.visible = true;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(u8("ビルド")))
		{
			ImGui::InputText(u8("出力先"), m_BuildOutputDir.data(), m_BuildOutputDir.size());
			ImGui::SameLine();
			if (ImGui::Button(u8("参照...###BuildOutputDir")))
			{
				ComPtr<IFileOpenDialog> dialog;
				if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
					CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
				{
					DWORD opts = 0;
					dialog->GetOptions(&opts);
					dialog->SetOptions(opts | FOS_PICKFOLDERS);
					if (SUCCEEDED(dialog->Show(nullptr)))
					{
						ComPtr<IShellItem> item;
						PWSTR pathW = nullptr;
						if (SUCCEEDED(dialog->GetResult(&item)) &&
							SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)))
						{
							std::string utf8 = WideToUTF8(pathW);
							std::snprintf(m_BuildOutputDir.data(), m_BuildOutputDir.size(), "%s", utf8.c_str());
							CoTaskMemFree(pathW);
						}
					}
				}
			}
			static int configIndex = 0;
			ImGui::Combo(u8("構成"), &configIndex, "Release\0Debug\0");
			ImGui::InputText(u8("ゲーム名"), m_BuildGameName.data(), m_BuildGameName.size());
			// 未設定ならプロジェクトの開始シーンを引き継ぐ。
			// .dxproj で Title にしていても既定値のままビルドしてしまうのを防ぐ
			if (m_BuildStartScene[0] == '\0' && PROJECT->IsOpen())
			{
				std::snprintf(m_BuildStartScene.data(), m_BuildStartScene.size(),
					"%s", PROJECT->GetStartScene().c_str());
			}
			ImGui::InputText(u8("開始シーン"), m_BuildStartScene.data(), m_BuildStartScene.size());
			ImGui::SameLine();
			if(ImGui::Button(u8("参照...###BuildStartScene")))
			{
				ComPtr<IFileOpenDialog> dialog;
				if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
					CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
				{
					DWORD opts = 0;
					dialog->GetOptions(&opts);
					dialog->SetOptions(opts | FOS_FILEMUSTEXIST);
					if (SUCCEEDED(dialog->Show(nullptr)))
					{
						ComPtr<IShellItem> item;
						PWSTR pathW = nullptr;
						if (SUCCEEDED(dialog->GetResult(&item)) &&
							SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)))
						{
							std::string utf8 = WideToUTF8(pathW);
							std::snprintf(m_BuildStartScene.data(), m_BuildStartScene.size(), "%s", utf8.c_str());
							CoTaskMemFree(pathW);
						}
					}
				}
			}

			ImGui::Checkbox(u8("使用アセットのみコピー"), &m_BuildUsedAssetsOnly);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(u8("開始シーンから参照されているアセットだけを出力します。\n"
					"スクリプトから文字列で読むアセットは検出できません。"));

			ImGui::BeginDisabled(BuildSystem::IsBuilding());
			if (ImGui::MenuItem(u8("ゲームをビルド")))
			{
				BuildSetting s;
				s.outputDir = m_BuildOutputDir.data();
				s.gameName = m_BuildGameName.data();
				s.startScene = m_BuildStartScene.data();
				s.configuration = (configIndex == 0) ? "Release" : "Debug";
				s.usedAssetsOnly = m_BuildUsedAssetsOnly;
				BuildSystem::Build(s);
			}
			ImGui::EndDisabled();

			if (BuildSystem::IsBuilding())
				ImGui::TextUnformatted(u8("ビルド中..."));
			ImGui::EndMenu();
		}

		DrawWindowButtons();

		ImGui::EndMainMenuBar();
	}

	DrawProjectDialog();
}

void EditorMenuBar::DrawWindowButtons()
{
	// ImGui の Win32 バックエンドがメインビューポートに HWND を入れている
	const HWND hWnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	if (hWnd == nullptr) return;

	const float h = ImGui::GetFrameHeight();
	const float w = h * 1.6f;	// OS のキャプションボタンに近い横長

	// 右端に 2 つ並べる
	ImGui::SameLine(ImGui::GetWindowWidth() - w * 2.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	// フォントに記号が入っていなくても出るよう、線で描く
	auto captionButton = [&](const char* id, ImU32 hoverColor, bool isClose) -> bool
		{
			const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
			const ImVec2 p0 = ImGui::GetItemRectMin();
			const ImVec2 p1 = ImGui::GetItemRectMax();
			ImDrawList* dl = ImGui::GetWindowDrawList();

			if (ImGui::IsItemHovered()) dl->AddRectFilled(p0, p1, hoverColor);

			const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
			const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
			const float r = h * 0.2f;

			if (isClose)
			{
				dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.5f);
				dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.5f);
			}
			else
			{
				dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.5f);
			}
			return pressed;
		};

	if (captionButton("##WindowMinimize", ImGui::GetColorU32(ImGuiCol_ButtonHovered), false))
	{
		ShowWindow(hWnd, SW_MINIMIZE);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8("最小化"));

	ImGui::SameLine();
	if (captionButton("##WindowClose", IM_COL32(196, 43, 28, 255), true))
	{
		// WM_CLOSE → DefWindowProc が DestroyWindow → WM_DESTROY で PostQuitMessage
		PostMessageW(hWnd, WM_CLOSE, 0, 0);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8("閉じる"));

	ImGui::PopStyleVar();
}

void EditorMenuBar::DrawPlayControl(Scene* activeScene)
{
	const EngineMode mode = PLAY.GetCurrentMode();
	const bool isEditor = (mode == EngineMode::EDITOR);

	if(ImGui::Button(isEditor ? u8("Play") : u8("Stop")))
	{
		if (isEditor)
		{
			APP->WaitForGPUIdle();
			if(activeScene)
				m_PlaySnap = SceneSerializer::SaveToString(*activeScene);
			PLAY.SetMode(EngineMode::Play);
		}
		else
		{
			PLAY.SetMode(EngineMode::EDITOR);
			APP->WaitForGPUIdle();

			// 読み込み済みVMDを退避しておく(復元後に AnimatorSystem が拾い直す)。
			// これをしないと Stop のたびに AsyncLoader から読み直しになる
			if (activeScene)
			{
				auto& cache = AnimatorClipCache();
				cache.clear();
				activeScene->GetWorld().Each<AnimatorComponent, NameComponent>(
					[&cache](Entity, AnimatorComponent& an, NameComponent& n)
					{
						if (an.clips.empty()) return;
						AnimatorClipSnapshot snap;
						snap.clips = std::move(an.clips);
						snap.currentClip = an.currentClip;
						snap.currentClipName = an.currentClipName;
						snap.time = an.time;
						snap.playing = an.playing;
						cache[n.name] = std::move(snap);
					});
			}

			// シーンを作り直す前に音を止める。
			// Voice は AudioEngine 側の寿命なので、Component を捨てるだけでは鳴り続ける
			if (activeScene)
			{
				activeScene->GetWorld().Each<AudioSourceComponent>(
					[](Entity, AudioSourceComponent& src)
					{
						if (src.voice)
						{
							src.voice->Stop();
							src.voice->FlushSourceBuffers();
						}
					});
			}

			if(activeScene && !m_PlaySnap.empty())
			{
				SceneSerializer::LoadFromString(*activeScene, m_PlaySnap);
			}
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(isEditor);
	if(ImGui::Button(mode == EngineMode::PAUSE ? u8("Resume") : u8("Pause")))
	{
		PLAY.SetMode(mode == EngineMode::PAUSE ? EngineMode::Play : EngineMode::PAUSE);
	}
	ImGui::EndDisabled();
}

void EditorMenuBar::DrawProjectDialog()
{
	if (!m_ShowSwitchProject) return;

	ImGui::OpenPopup(u8("プロジェクトを切り替え"));
	if (!ImGui::BeginPopupModal(u8("プロジェクトを切り替え"), &m_ShowSwitchProject,
		ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	ImGui::TextUnformatted(u8("ランチャーを開いてエディタを終了します。"));
	ImGui::TextUnformatted(u8("保存していない変更は失われます。"));

	if (!m_ProjectError.empty())
	{
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", m_ProjectError.c_str());
	}

	ImGui::Separator();

	if (ImGui::Button(u8("ランチャーを開く")))
	{
		if (LaunchLauncher())
		{
			// Engine::Run のメッセージループが WM_QUIT で抜ける
			PostQuitMessage(0);
		}
		else
		{
			m_ProjectError = "Launcher.exe が見つかりません";
		}
	}

	ImGui::SameLine();
	if (ImGui::Button(u8("キャンセル")))
	{
		m_ProjectError.clear();
		m_ShowSwitchProject = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}