#include "MenuScene.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "imguiinit.hpp"

MenuScene::MenuScene()
	: m_OnSceneChange(nullptr)
{
}

void MenuScene::Update()
{
	m_elapsedTime += TIME->GetDeltaTime();

	// Enterキーで SampleScene に遷移
	if (INPUT->Key.Enter().Down())
	{
		RequestSceneChange("SampleScene");
	}

	// Escapeキーで終了
	if (INPUT->Key.Escape().Down())
	{
		PostQuitMessage(0);
	}
}

void MenuScene::Draw(const RenderContext& renderContext)
{
	DrawUI();
}

void MenuScene::RequestSceneChange(const std::string& sceneName)
{
	if (m_OnSceneChange)
	{
		m_OnSceneChange(sceneName);
	}
}

void MenuScene::DrawUI()
{
#if 1  // ImGuiが有効な場合のみ表示
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoMove))
	{
		ImGui::Text("Game Engine Menu");
		ImGui::Separator();

		ImGui::Text("Elapsed Time: %.2f sec", m_elapsedTime);

		ImGui::Spacing();
		ImGui::Spacing();

		if (ImGui::Button("Start Game (Enter)", ImVec2(350, 50)))
		{
			RequestSceneChange("SampleScene");
		}

		if (ImGui::Button("Physics Demo (2)", ImVec2(350, 50)))
		{
			RequestSceneChange("PhysicsScene");
		}

		ImGui::Spacing();

		if (ImGui::Button("Exit (Escape)", ImVec2(350, 50)))
		{
			PostQuitMessage(0);
		}

		ImGui::End();
	}
#endif
}