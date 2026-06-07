//#include "SampleScene.hpp"
//#include <cmath>
//#include "Time.hpp"
//#include "Input.hpp"
//#include "Components.hpp"
//#include "Mesh.hpp"
//#include "Material.hpp"
//#include "imguiinit.hpp"
//#include "ModelLoader.hpp"
//#include "Logger.hpp"
//#include "Player.hpp"
//
//SampleScene::SampleScene(
//	const ComPtr<ID3D12Device>& device,
//	const DirectXApp::PipelineStateTable pipelinestates,
//	const ComPtr<ID3D12PipelineState>& wirePso,
//	const ComPtr<ID3D12PipelineState>& linePso)
//	: m_Device(device)
//	, m_PipelineStates(pipelinestates)
//	, m_WirePso(wirePso)
//	, m_LinePso(linePso)
//	, m_IsInitialized(false)
//{
//}
//
//SampleScene::~SampleScene()
//{
//}
//
//void SampleScene::OnLoad()
//{
//	// リソースロード時：初期化処理
//	if (m_IsInitialized)
//	{
//		return;
//	}
//
//	LOG->LogInfo("SampleScene: Loading...");
//
//	m_Camera = MakeUnique<Camera>();
//	m_DebugLineRenderer.Init(m_Device, m_LinePso);
//
//	// Cube エンティティ作成
//	m_CubeEntity = m_World.CreateEntity();
//
//	TransformComponent transform{};
//	DirectX::XMStoreFloat4x4(&transform.world, DirectX::XMMatrixIdentity());
//	m_World.AddComponent<TransformComponent>(m_CubeEntity, transform);
//
//	SpinComponent spin{};
//	spin.angle = 0.0f;
//	spin.speed = 1.0f;
//	m_World.AddComponent<SpinComponent>(m_CubeEntity, spin);
//
//	// モデルロード
//	auto loadResult = ModelLoader::LoadFromFile(m_Device, "Assets/Player.fbx", 0.01f);
//	auto mesh = loadResult.mesh;
//	if (mesh == nullptr)
//	{
//		mesh = MakeShared<Mesh>();
//		mesh->CreateCube(m_Device);
//	}
//
//	m_Skeleton = loadResult.skeleton;
//
//	m_World.AddComponent<MeshComponent>(m_CubeEntity, MeshComponent{ mesh });
//
//	auto material = MakeShared<Material>();
//	material->Init(m_Device, m_PipelineStates, m_WirePso);
//
//	if (!loadResult.diffusetextureData.empty())
//	{
//		material->SetTextureFromMemory(
//			loadResult.diffusetextureData.data(),
//			loadResult.diffusetextureData.size());
//	}
//	else if (!loadResult.diffuseTexturePath.empty())
//	{
//		material->SetTextureFromFile(loadResult.diffuseTexturePath);
//	}
//	else
//	{
//		material->SetTextureFromFile(L"Assets/Mutant_diffuse.png");
//	}
//
//	m_World.AddComponent<MaterialComponent>(m_CubeEntity, MaterialComponent{ material });
//
//	// Player 作成
//	m_Player = MakeUnique<Player>(m_World, m_Device, m_PipelineStates, m_WirePso);
//	m_Player->SetPosition(float3(0.0f, 1.0f, -5.0f));
//
//	m_IsInitialized = true;
//	LOG->LogInfo("SampleScene: Loaded");
//}
//
//void SampleScene::OnStart()
//{
//	LOG->LogInfo("SampleScene: Started");
//}
//
//void SampleScene::OnActivate()
//{
//	LOG->LogInfo("SampleScene: Activated");
//}
//
//void SampleScene::OnDeactivate()
//{
//	LOG->LogInfo("SampleScene: Deactivated");
//}
//
//void SampleScene::OnUnload()
//{
//	LOG->LogInfo("SampleScene: Unloading...");
//	m_IsInitialized = false;
//	m_Player.reset();
//	m_Camera.reset();
//}
//
//void SampleScene::Update(float deltatime)
//{
//	if (INPUT->Key.Escape().Down())
//	{
//		PostQuitMessage(0);
//	}
//
//	if (INPUT->Key.Enter().Down())
//	{
//		auto& settings = RenderSettings::Get();
//		settings.wireframe = !settings.wireframe;
//	}
//
//	if (m_Camera != nullptr)
//	{
//		m_Camera->Update();
//	}
//
//	UpdateLighting(deltatime);
//	UpdatePlayer(deltatime);
//
//	// ECS更新
//	m_SpinSystem.Update(m_World, deltatime);
//
//	// ライト設定を全マテリアルに反映
//	m_World.Each<MaterialComponent>(
//		[this](Entity, MaterialComponent& materialComponent)
//		{
//			if (materialComponent.material == nullptr)
//			{
//				return;
//			}
//
//			materialComponent.material->SetLightDir(m_LightDir);
//			materialComponent.material->SetLightColor(m_LightColor);
//			materialComponent.material->SetAmbientColor(m_AmbientColor);
//		}
//	);
//}
//
//void SampleScene::FixedUpdate(float fixedDeltatime)
//{
//	// 物理更新など
//	if (m_Player)
//	{
//
//	}
//}
//
//void SampleScene::LateUpdate(float deltatime)
//{
//	// カメラ更新など後処理
//}
//
//void SampleScene::OnPause()
//{
//	LOG->LogInfo("SampleScene: Paused");
//}
//
//void SampleScene::OnResume()
//{
//	LOG->LogInfo("SampleScene: Resumed");
//}
//
//void SampleScene::Draw(const RenderContext& renderContext)
//{
//	RenderContext context = renderContext;
//	if (m_Camera)
//	{
//		context.view = m_Camera->GetViewMatrix();
//		context.projection = m_Camera->GetProjectionMatrix();
//	}
//
//	m_DebugLineRenderer.Begin();
//	DrawGrid();
//	m_DebugLineRenderer.Draw(context);
//
//	m_RenderSystem.Draw(m_World,context);
//	DrawSkeleton(context);
//}
//
//void SampleScene::UpdateLighting(float deltatime)
//{
//	m_LightAngle += 0.5f * deltatime;
//	m_AmbientAngle += 0.01f * deltatime;
//
//	m_LightDir = {
//		std::cos(m_LightAngle),
//		-0.5f,
//		std::sin(m_LightAngle)
//	};
//
//	m_LightColor.x = (std::cos(m_LightAngle) + 1.0f) * 0.5f;
//	m_AmbientColor.x = (std::cos(m_AmbientAngle) + 1.0f) * 0.5f;
//}
//
//void SampleScene::UpdatePlayer(float deltatime)
//{
//	if (!m_Player)
//	{
//		return;
//	}
//
//	m_Player->Update(deltatime);
//
//	// キー入力による移動
//	if (INPUT->Key.W().IsPressed())
//	{
//		m_Player->Move(float3(0.0f, 0.0f, 5.0f * deltatime));
//	}
//	if (INPUT->Key.S().IsPressed())
//	{
//		m_Player->Move(float3(0.0f, 0.0f, -5.0f * deltatime));
//	}
//	if (INPUT->Key.A().IsPressed())
//	{
//		m_Player->Move(float3(-5.0f * deltatime, 0.0f, 0.0f));
//	}
//	if (INPUT->Key.D().IsPressed())
//	{
//		m_Player->Move(float3(5.0f * deltatime, 0.0f, 0.0f));
//	}
//}
//
//void SampleScene::DrawGrid()
//{
//	// グリッドの数
//	constexpr int gridCount = 10;
//
//	// グリッドの間隔
//	constexpr float gridSize = 1.0f;
//
//	// グリッドの色
//	const COLOR gridColor = { 0.5f, 0.5f, 0.5f, 1.0f };
//	const float max = gridSize * gridCount;
//
//	for (int i = 0; i <= gridCount; ++i)
//	{
//		const float x = i * gridSize;
//		const float z = i * gridSize;
//
//		m_DebugLineRenderer.AddLine(float3{x,0.0f,0.0f},float3{x,0.0f,max},gridColor);
//		m_DebugLineRenderer.AddLine(float3{0.0f,0.0f,z},float3{max,0.0f,z},gridColor);
//	}
//}
//
//void SampleScene::DrawSkeleton(const RenderContext& renderContext)
//{
//	
//}
//
//void SampleScene::DebugWindow()
//{
//	// デバッグウィンドウ処理
//}