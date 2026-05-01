#include "SampleScene.hpp"
#include <cmath>
#include "Time.hpp"
#include "Input.hpp"
#include "Components.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Camera.hpp"
#include "imguiinit.hpp"

SampleScene::~SampleScene()
{
}

SampleScene::SampleScene(
	const ComPtr<ID3D12Device>& device,
	const DirectXApp::PipelineStateTable pipelinestates,
	const ComPtr<ID3D12PipelineState>& wirePso)
{
	m_Camera = MakeUnique<Camera>();

	m_CubeEntity = m_World.CreateEntity();

	TransformComponent transform{};
	DirectX::XMStoreFloat4x4(&transform.world, DirectX::XMMatrixIdentity());
	m_World.AddComponent<TransformComponent>(m_CubeEntity, transform);

	SpinComponent spin{};
	spin.angle = 0.0f;
	spin.speed = 1.0f;
	m_World.AddComponent<SpinComponent>(m_CubeEntity, spin);

	auto mesh = MakeShared<Mesh>();
	mesh->CreateCube(device);
	m_World.AddComponent<MeshComponent>(m_CubeEntity, MeshComponent{ mesh });

	auto material = MakeShared<Material>();
	material->Init(device, pipelinestates, wirePso);
	material->SetTextureFromFile(L"Assets/Mutant_diffuse.png");
	m_World.AddComponent<MaterialComponent>(m_CubeEntity, MaterialComponent{ material });
}

void SampleScene::Update()
{
	if (INPUT->Key.Escape().Down())
	{
		PostQuitMessage(0);
	}

	if (INPUT->Key.Enter().Down())
	{
		auto& settings = RenderSettings::Get();
		settings.wireframe = !settings.wireframe;
	}

	if (m_Camera != nullptr)
	{
		m_Camera->Update();
	}

	const float deltaTime = TIME->GetDeltaTime();
	m_SpinSystem.Update(m_World, deltaTime);

	static float lightAngle = 0.0f;
	lightAngle += 0.5f * deltaTime;

	const float3 dir = {
		std::cos(lightAngle),
		-0.5f,
		std::sin(lightAngle)
	};

	float4 lightColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightColor.x = (std::cos(lightAngle) + 1.0f) * 0.5f;

	static float ambientAngle = 0.0f;
	ambientAngle += 0.01f * deltaTime;
	float4 ambientColor = { 0.2f, 0.2f, 0.2f, 1.0f };
	ambientColor.x = (std::cos(ambientAngle) + 1.0f) * 0.5f;

	m_World.Each<MaterialComponent>(
		[&](Entity, MaterialComponent& materialComponent)
		{
			if (materialComponent.material == nullptr)
			{
				return;
			}

			materialComponent.material->SetLightDir(dir);
			materialComponent.material->SetLightColor(lightColor);
			materialComponent.material->SetAmbientColor(ambientColor);
		}
	);
}

void SampleScene::Draw(const RenderContext& renderContext)
{
	RenderContext context = renderContext;
	context.view = m_Camera->GetViewMatrix(false);
	context.projection = m_Camera->GetProjectionMatrix(false);

	m_RenderSystem.Draw(m_World, context);

#if _DEBUG
	DebugWindow();
	m_Camera->DebugWindow();
#endif
}

void SampleScene::DebugWindow()
{
	if (ImGui::Begin(u8("デバッグ")))
	{
		ImGui::Text(u8("SampleSceneのデバッグウィンドウ"));

		auto& settings = RenderSettings::Get();
		ImGui::Checkbox(u8("ワイヤーフレーム"), &settings.wireframe);


		const char* vsItems[] = { "BasicVS" };
		int vsIndex = static_cast<int>(settings.vertexShader);
		if (ImGui::Combo(u8("Vertex Shader"), &vsIndex, vsItems, static_cast<int>(sizeof(vsItems) / sizeof(vsItems[0]))))
		{
			settings.vertexShader = static_cast<E_VERTEX_SHADER>(vsIndex);
		}

		const char* psItems[] = { "BasicPS", "ToonPS" };
		int psIndex = static_cast<int>(settings.pixelShader);
		if (ImGui::Combo(u8("Pixel Shader"), &psIndex, psItems, static_cast<int>(sizeof(psItems) / sizeof(psItems[0]))))
		{
			settings.pixelShader = static_cast<E_PIXEL_SHADER>(psIndex);
		}
	}
	ImGui::End();
}
