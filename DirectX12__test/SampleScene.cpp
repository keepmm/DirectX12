#include "SampleScene.hpp"
#include <cmath>
#include "Time.hpp"
#include "Input.hpp"
#include "Components.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "Camera.hpp"
#include "imguiinit.hpp"
#include "ModelLoader.hpp"
#include "Logger.hpp"
#include "Player.hpp"

SampleScene::~SampleScene()
{
}

SampleScene::SampleScene(
	const ComPtr<ID3D12Device>& device,
	const DirectXApp::PipelineStateTable pipelinestates,
	const ComPtr<ID3D12PipelineState>& wirePso,
	const ComPtr<ID3D12PipelineState>& linePso)
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

	auto loadResult = ModelLoader::LoadFromFile(device, "Assets/Player.fbx", 0.01f);

	auto mesh = loadResult.mesh;
	if (mesh == nullptr)
	{
		mesh = MakeShared<Mesh>();
		mesh->CreateCube(device);
	}

	m_Skeleton = loadResult.skeleton;
	m_DebugLineRenderer.Init(device, linePso);

	m_World.AddComponent<MeshComponent>(m_CubeEntity, MeshComponent{ mesh });

	auto material = MakeShared<Material>();
	material->Init(device, pipelinestates, wirePso);

	if (!loadResult.diffusetextureData.empty())
	{
		material->SetTextureFromMemory(
			loadResult.diffusetextureData.data(),
			loadResult.diffusetextureData.size());
	}
	else if (!loadResult.diffuseTexturePath.empty())
	{
		material->SetTextureFromFile(loadResult.diffuseTexturePath);
	}
	else
	{
		material->SetTextureFromFile(L"Assets/Mutant_diffuse.png");
	}

	m_World.AddComponent<MaterialComponent>(m_CubeEntity, MaterialComponent{ material });
	m_Player = MakeUnique<Player>(m_World, device, pipelinestates, wirePso);
	m_Player->SetPosition(float3(0.0f, 1.0f, -5.0f));
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


	// --- Playerの更新 ---- //
	if (m_Player)
	{
		m_Player->Update(deltaTime);

		// ---- キー入力で移動
		if (INPUT->Key.W().IsPressed())
		{
			m_Player->Move(float3(0.0f, 0.0f, 5.0f * deltaTime));
		}
		if (INPUT->Key.S().IsPressed())
		{
			m_Player->Move(float3(0.0f, 0.0f, -5.0f * deltaTime));
		}
		if (INPUT->Key.A().IsPressed())
		{
			m_Player->Move(float3(-5.0f * deltaTime, 0.0f, 0.0f));
		}
		if (INPUT->Key.D().IsPressed())
		{
			m_Player->Move(float3(5.0f * deltaTime, 0.0f, 0.0f));
		}
	}

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
	DrawSkeleton(context);

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

		if (m_Player)
		{
			ImGui::Separator();
			ImGui::Text(u8("Player"));
			auto pos = m_Player->GetPosition();
			ImGui::SliderFloat(u8("位置X##player"), &pos.x, -10.0f, 10.0f);
			ImGui::SliderFloat(u8("位置Y##player"), &pos.y, -10.0f, 10.0f);
			ImGui::SliderFloat(u8("位置Z##player"), &pos.z, -10.0f, 10.0f);
			m_Player->SetPosition(pos);
		}

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

		ImGui::Checkbox(u8("Mesh Shaderを使用"), &settings.meshShader);
	}
	ImGui::End();
}

void SampleScene::DrawSkeleton(const RenderContext& renderContext)
{
	// スケルトンのノードがない場合は処理しない
	if (m_Skeleton.nodes.empty())
	{
		return;
	}

	// キューブエンティティにTransformComponentがない場合も処理しない
	if(!m_World.HasComponent<TransformComponent>(m_CubeEntity))
	{
		return;
	}

	const auto& transform = m_World.GetComponent<TransformComponent>(m_CubeEntity);
	const auto modelworld = DirectX::XMLoadFloat4x4(&transform.world);

	std::vector<float4x4> globalTransforms(m_Skeleton.nodes.size());

	for (size_t i = 0; i < m_Skeleton.nodes.size(); ++i)
	{
		const auto& node = m_Skeleton.nodes[i];
		const auto local = DirectX::XMLoadFloat4x4(&node.localTransform);

		if (node.parentIndex < 0)
		{
			DirectX::XMStoreFloat4x4(&globalTransforms[i], local);
		}
		else
		{
			const auto parent = DirectX::XMLoadFloat4x4(&globalTransforms[node.parentIndex]);
			const auto global = local * parent;
			DirectX::XMStoreFloat4x4(&globalTransforms[i], global);
		}
	}

	m_DebugLineRenderer.Begin();

	const float4 color = { 1.0f,0.9f,0.2f,1.0f };

	for(size_t i = 0; i < m_Skeleton.nodes.size(); ++i)
	{
		const auto& node = m_Skeleton.nodes[i];
		if (node.parentIndex < 0)
		{
			continue;
		}

		const auto parrentMat = DirectX::XMLoadFloat4x4(&globalTransforms[node.parentIndex]);
		const auto currentMat = DirectX::XMLoadFloat4x4(&globalTransforms[i]);

		const auto parentWorld = DirectX::XMMatrixMultiply(parrentMat, modelworld);
		const auto currentWorld = DirectX::XMMatrixMultiply(currentMat, modelworld);

		float3 parentPos{};
		float3 childPos{};
		DirectX::XMStoreFloat3(&parentPos, DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(),parentWorld));
		DirectX::XMStoreFloat3(&childPos, DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(), currentWorld));

		m_DebugLineRenderer.AddLine(parentPos, childPos, color);
	}

	m_DebugLineRenderer.Draw(renderContext);
}
