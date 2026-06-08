#include "Application.hpp"
#include "RuntimeScene.hpp"
#include "Polygon.hpp"
#include "Logger.hpp"
#include "PrefabLibrary.hpp"
#include "Components.hpp"
#include "ModelLoader.hpp"

Application::Application()
{
}

Application* Application::GetInstance()
{
	static Application instance;
	return &instance;
}

HRESULT Application::OnInit()
{
	// ---- 各ウィンドウの初期化 ---- //
	m_EditorWindow = std::make_unique<EditorWindow>(*m_DirectX);


	// RuntimeSceneの登録
	auto runtimeScene = MakeUnique<RuntimeScene>("Assets/Scenes/SampleScene.json",
		m_DirectX->GetDevice(),
		m_DirectX->GetLinePso());
	m_SceneManager.RegisterScene("RuntimeScene", std::move(runtimeScene));

	// RuntimeSceneから開始
	m_SceneManager.LoadScene("RuntimeScene");

	// プレハブの初期化
	OnInitPrefabs();

	// ポリゴン初期化
	Polygon::Init(
		m_DirectX->GetDevice(),
		m_DirectX->GetCommandList(),
		m_DirectX->GetPipelineState(E_VERTEX_SHADER::BASIC, E_PIXEL_SHADER::BASIC),
		m_DirectX->GetPipelineStateWireFrame());
	Polygon::CreatePolygon();

	return S_OK;
}

void Application::OnUpdate()
{
	if(m_EditorWindow)
	{
		m_EditorWindow->Draw(m_SceneManager);
	}
}

void Application::OnShutDown()
{
}

void Application::OnInitPrefabs()
{
	auto sharedBoxMesh = MakeShared<Mesh>();
	sharedBoxMesh->CreateCube(m_DirectX->GetDevice());

	auto sharedBoxMaterial = MakeShared<Material>();
	sharedBoxMaterial->Init(
		m_DirectX->GetDevice(),
		m_DirectX->GetPipelineStates(),
		m_DirectX->GetPipelineStateWireFrame());
	sharedBoxMaterial->SetTextureFromFile(L"Assets/Mutant_diffuse.png");

	PrefabLibrary::Get().RegisterPrefab("Box",
		[sharedBoxMesh, sharedBoxMaterial](Scene& scene, World& world, Entity entity)
		{
			TransformComponent transform{};
			transform.position = float3(0.0f, 0.0f, 0.0f);
			transform.rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
			transform.scale = float3(1.0f, 1.0f, 1.0f);
			transform.RebuildWorld();

			world.AddComponent<TransformComponent>(entity, transform);
			world.AddComponent<MeshComponent>(entity, MeshComponent{ sharedBoxMesh });
			world.AddComponent<MaterialComponent>(entity, MaterialComponent{ sharedBoxMaterial });

			RigidBodyComponent rb{};
			ColliderComponent collider{};
			collider.shapeType = ColliderComponent::ShapeType::Box;
			collider.size = float3(1.0f, 1.0f, 1.0f);

			world.AddComponent<RigidBodyComponent>(entity, rb);
			world.AddComponent<ColliderComponent>(entity, collider);

			auto& physicsWorld = scene.EnsurePhysicsWorld();
			physicsWorld.AddRigidbody(entity, rb, collider);
			physicsWorld.SetActorPose(entity, transform.position, transform.rotation);
		});

	auto modelData = ModelLoader::LoadFromFile(m_DirectX->GetDevice(), "Assets/Player.fbx", 0.01f);
	auto modelMesh = modelData.mesh;
	if (modelMesh == nullptr)
	{
		modelMesh = sharedBoxMesh;
	}

	auto modelMaterial = MakeShared<Material>();
	modelMaterial->Init(
		m_DirectX->GetDevice(),
		m_DirectX->GetPipelineStates(),
		m_DirectX->GetPipelineStateWireFrame());

	if (!modelData.diffusetextureData.empty())
	{
		modelMaterial->SetTextureFromMemory(modelData.diffusetextureData.data(), modelData.diffusetextureData.size());
	}
	else if (!modelData.diffuseTexturePath.empty())
	{
		modelMaterial->SetTextureFromFile(modelData.diffuseTexturePath);
	}
	else
	{
		modelMaterial->SetTextureFromFile(L"Assets/Mutant_diffuse.png");
	}

	PrefabLibrary::Get().RegisterPrefab("PlayerModel",
		[modelMesh, modelMaterial](Scene&, World& world, Entity entity)
		{
			TransformComponent transform{};
			transform.position = float3(0.0f, 0.0f, 0.0f);
			transform.rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
			transform.scale = float3(1.0f, 1.0f, 1.0f);
			transform.RebuildWorld();

			world.AddComponent<TransformComponent>(entity, transform);
			world.AddComponent<MeshComponent>(entity, MeshComponent{ modelMesh });
			world.AddComponent<MaterialComponent>(entity, MaterialComponent{ modelMaterial });
		});

	// RigidBodyを持たないBox
	PrefabLibrary::Get().RegisterPrefab("StaticBox",
		[sharedBoxMesh, sharedBoxMaterial](Scene& scene, World& world, Entity entity)
		{
			TransformComponent transform{};
			transform.position = float3(0.0f, 0.0f, 0.0f);
			transform.rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);
			transform.scale = float3(1.0f, 1.0f, 1.0f);
			transform.RebuildWorld();
			world.AddComponent<TransformComponent>(entity, transform);
			world.AddComponent<MeshComponent>(entity, MeshComponent{ sharedBoxMesh });
			world.AddComponent<MaterialComponent>(entity, MaterialComponent{ sharedBoxMaterial });
		});
}

/*
*	@brief 描画に必要な情報をRenderContextにセットする
*/
void Application::ConfigureContext(RenderContext& renderContext)
{
	// --------------------------------------//
	// エディターウィンドウが存在しない場合
	// 情報をセットせず戻る
	// --------------------------------------//
	if (!m_EditorWindow)
	{
		renderContext.viewportRenderTexture = nullptr;
		renderContext.viewport = nullptr;
		renderContext.scissorRect = nullptr;
		return;
	}

	// ===== パス1: Scene View（先に描画）=====
	auto* editorTex = m_EditorWindow->GetEditorRenderTexture();
	if (editorTex && editorTex->IsValid())
	{
		RenderContext sceneCtx = renderContext;
		m_EditorViewport = editorTex->GetViewport();
		m_EditorScissorRect = editorTex->GetScissorRect();

		sceneCtx.viewportRenderTexture = editorTex;
		sceneCtx.viewport = &m_EditorViewport;
		sceneCtx.scissorRect = &m_EditorScissorRect;
		sceneCtx.isSceneView = true;
		sceneCtx.depthStencilView = m_DirectX->GetDsvHandle();

		m_SceneManager.Draw(sceneCtx);
	}

	// ===== パス2: Game View（呼び出し元で描画される）=====
	auto* gameTex = m_EditorWindow->GetGameRenderTexture();
	if (!gameTex)
	{
		renderContext.viewportRenderTexture = nullptr;
		renderContext.viewport = nullptr;
		renderContext.scissorRect = nullptr;
		return;
	}

	m_GameViewport = gameTex->GetViewport();
		m_GameScissorRect = gameTex->GetScissorRect();

	renderContext.viewportRenderTexture = gameTex;
		renderContext.viewport = &m_GameViewport;
	renderContext.scissorRect = &m_GameScissorRect;
	renderContext.isSceneView = false;
	renderContext.depthStencilView = m_DirectX->GetDsvHandle();
}

void Application::ConfigureContext(RenderContext& renderContext, RenderTexture& renderTexture, D3D12_VIEWPORT& viewport, D3D12_RECT& scissorRect, bool isSceneView)
{
	// --------------------------------------//
// エディターウィンドウが存在しない場合
// 情報をセットせず戻る
// --------------------------------------//
	if (!m_EditorWindow)
	{
		renderContext.viewportRenderTexture = nullptr;
		renderContext.viewport = nullptr;
		renderContext.scissorRect = nullptr;
		return;
	}

	viewport = renderTexture.GetViewport();
	scissorRect = renderTexture.GetScissorRect();

	renderContext.viewportRenderTexture = &renderTexture;
	renderContext.viewport = &viewport;
	renderContext.scissorRect = &scissorRect;
	renderContext.isSceneView = isSceneView;
}
