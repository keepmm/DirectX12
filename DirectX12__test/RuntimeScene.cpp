#include "RuntimeScene.hpp"
#include "SceneSerializer.hpp"
#include "Logger.hpp"
#include "RenderTexture.hpp"
#include "Mesh.hpp"
#include "Material.hpp"

RuntimeScene::RuntimeScene(std::string sceneFilePath, const ComPtr<ID3D12Device>& device, const ComPtr<ID3D12PipelineState>& linePso)
	: m_SceneFilePath(std::move(sceneFilePath)),
	m_Device(device), m_LinePso(linePso)
{
}

void RuntimeScene::OnLoad()
{
	// 既に初期化が終わってる場合は
	// 二重ロードを防ぐために処理しない
	if (m_Initialized)
	{
		return;
	}

	LOG->LogInfo("RuntimeScene : Loading...");

	m_DebugLineRenderer.Init(m_Device, m_LinePso);

	if (!m_SceneFilePath.empty())
	{
		if (!SceneSerializer::Load(*this, m_SceneFilePath))
		{
			LOG->LogWarning("RuntimeScene : シーン読み込みに失敗しました : " + m_SceneFilePath);
		}
	}

	// -------------------------------------//
	//	メインカメラとメインライトの作成	//
	// -------------------------------------//
	auto camera = m_World.CreateEntity();
	auto tr = m_World.AddComponent(camera, TransformComponent{});
	m_World.AddComponent(camera, NameComponent{ "MainCamera" });
	auto maincamera = m_World.AddComponent(camera, CameraComponent{});
	tr.position = { 0.0f, 5.0f, 0.0f };
	maincamera.cameraType = CameraComponent::CameraType::Main;
	m_World.AddComponent(camera, FreeLookComponent{});

	// ライトの作成
	auto light = m_World.CreateEntity();
	m_World.AddComponent(light, TransformComponent{});
	m_World.AddComponent(light, NameComponent{ "MainLight" });
	auto& lightComp = m_World.AddComponent(light, LightComponent{});
	lightComp.type = LightComponent::LightType::Directional;
	lightComp.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightComp.ambientColor = { 0.2f, 0.2f, 0.2f, 1.0f };
	lightComp.intensity = 1.0f;
	lightComp.direction = { -0.5f, -1.0f, -0.5f };

	// -------------------------- //
	//  エディター用カメラの作成  //
	// -------------------------- //
	auto editcam = m_World.CreateEntity();
	auto t = m_World.AddComponent(editcam, TransformComponent{});
	m_World.AddComponent(editcam, NameComponent{ "EditorCamera" });
	auto& editCameraComp = m_World.AddComponent(editcam, CameraComponent{});
	editCameraComp.cameraType = CameraComponent::CameraType::Secondary;
	auto& freeLook = m_World.AddComponent(editcam, FreeLookComponent{});
	t.position = { 0.0f, 5.0f, -10.0f };
	freeLook.Enabled = true;

	tr.RebuildWorld();
	t.RebuildWorld();

	// -----------------------------//
	//  アイコン用マテリアルの作成  //
	// -----------------------------//
	m_IconQuad.CreateQuad(APP->GetDevice());

	m_CameraIcon = std::make_shared<Material>();
	m_CameraIcon->Init();
	m_CameraIcon->SetTextureFromFile(L"Assets/CameraIcon.png");

	m_LightIcon = std::make_shared<Material>();
	m_LightIcon->Init();
	m_LightIcon->SetTextureFromFile(L"Assets/LightIcon.png");

	n_IconReady = true;

	m_Initialized = true;
	LOG->LogInfo("RuntimeScene : Loaded");
}

void RuntimeScene::OnUnload()
{
	LOG->LogInfo("RuntimeScene : Unloading...");
	ResetWorld();
	m_Initialized = false;
}

void RuntimeScene::Update(float deltatime)
{
	m_SpinSystem.Update(m_World, deltatime);
	m_LightSystem.Apply(m_World);
	m_FreeLookSystem.Update(m_World, deltatime);
	m_CameraSystem.Update(m_World, 16.0f / 9.0f);
}

void RuntimeScene::FixedUpdate(float fixedDeltatime)
{
	(void)fixedDeltatime;
}

void RuntimeScene::LateUpdate(float deltatime)
{
	(void)deltatime;
}

void RuntimeScene::Draw(const RenderContext& renderContext)
{
	RenderContext context = renderContext;
	const CameraComponent* main = nullptr;
	m_World.Each<CameraComponent>([&](Entity, CameraComponent& camera)
		{
			if (camera.cameraType == CameraComponent::CameraType::Main)
			{
				main = &camera;
			}
		});
	
	// -----------------------------//
	//   メインカメラがある場合		//
	//   メインカメラの行列で描画	//
	// -----------------------------//
	if (main)
	{
		// 行列の更新	
		context.view = main->view;
		context.projection = main->proj;
	}
	else
	{
		// メインカメラがない場合はEditorCameraを探す
		m_World.Each<CameraComponent, TransformComponent>([&](Entity, CameraComponent& camera, TransformComponent& transform)
			{
				if (camera.cameraType == CameraComponent::CameraType::Secondary)
				{
					context.view = camera.view;
					context.projection = camera.proj;
				}
			});
	}

	// ビューポート用レンダーテクスチャへの描画設定
	if (renderContext.viewportRenderTexture != nullptr &&
		renderContext.viewportRenderTexture->IsValid())
	{
		auto renderTexture = renderContext.viewportRenderTexture;
		auto* commandList = context.CommandList;

		// リソースバリア: ピクセルシェーダーリソース → レンダーターゲット
		renderTexture->Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// RTV を設定
		auto rtvHandle = renderTexture->GetRTV();
		auto dsvHandle = renderContext.depthStencilView;

		commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,&dsvHandle);
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// ビューポートとシザー矩形を設定
		if (renderContext.viewport)
		{
			commandList->RSSetViewports(1, renderContext.viewport);
		}
		if (renderContext.scissorRect)
		{
			commandList->RSSetScissorRects(1, renderContext.scissorRect);
		}

		// クリア
		renderTexture->Clear(commandList, { 0.2f, 0.2f, 0.2f, 1.0f });


		// ---------------------//
		// デバッグラインの描画 //
		// ---------------------//
		m_DebugLineRenderer.Begin();
		if (context.isSceneView) {
			DrawGrid();
			DrawLight();
			DrawGizmos(context);
		}

		// デバッグラインを追加
		for (const auto& line : m_DebugLines)
		{
			m_DebugLineRenderer.AddLine(line.start, line.end, line.color);
		}
		m_DebugLineRenderer.Draw(context);

		// デバッグラインをクリア
		m_DebugLines.clear();


		// シーン描画
		m_RenderSystem.Draw(m_World, context);

		// リソースバリア: レンダーターゲット → ピクセルシェーダーリソース
		// imguiでimageをサンプルするため
		renderTexture->Transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		// デバッグラインの描画
		m_DebugLineRenderer.Begin();
		if (context.isSceneView) {
			DrawGrid();
			DrawLight();
			DrawGizmos(context);
		}

		// デバッグラインを追加
		for (const auto& line : m_DebugLines)
		{
			m_DebugLineRenderer.AddLine(line.start, line.end, line.color);
		}
		m_DebugLineRenderer.Draw(context);

		// デバッグラインをクリア
		m_DebugLines.clear();

		// 通常描画（メインレンダーターゲット）
		m_RenderSystem.Draw(m_World, context);
	}
}

void RuntimeScene::DrawGizmos(const RenderContext& renderContext)
{
	if (!n_IconReady || renderContext.CommandList == nullptr) return;

	// view行列からカメラのワールド軸を復元（ビルボード用）
	const float4x4& v = renderContext.view;
	const float3 right = { v._11, v._21, v._31 };
	const float3 up = { v._12, v._22, v._32 };
	const float3 fwd = { v._13, v._23, v._33 };

	const float iconSize = 1.0f;

	auto billboard = [&](const float3& pos) -> float4x4
		{
			float4x4 w{};
			w._11 = right.x * iconSize; w._12 = right.y * iconSize; w._13 = right.z * iconSize; w._14 = 0.0f;
			w._21 = up.x * iconSize; w._22 = up.y * iconSize; w._23 = up.z * iconSize; w._24 = 0.0f;
			w._31 = fwd.x;            w._32 = fwd.y;            w._33 = fwd.z;            w._34 = 0.0f;
			w._41 = pos.x;            w._42 = pos.y;            w._43 = pos.z;            w._44 = 1.0f;
			return w;
		};

	auto* iconPso = DirectXApp::GetCurrent()->GetIconPso().Get();

	// カメラアイコン（FreeLookを持つEditorカメラは除外）
	m_World.Each<TransformComponent, CameraComponent>(
		[&](Entity e, TransformComponent& tr, CameraComponent&)
		{
			if (m_World.HasComponent<FreeLookComponent>(e)) return;
			const float4x4 world = billboard(tr.position);
			m_CameraIcon->Apply(renderContext.CommandList, world, renderContext.view, renderContext.projection,
				false, E_VERTEX_SHADER::BASIC, E_PIXEL_SHADER::BASIC, iconPso, renderContext.frameIndex);
			m_IconQuad.Draw(renderContext.CommandList);
		});

	// ライトアイコン
	m_World.Each<TransformComponent, LightComponent>(
		[&](Entity, TransformComponent& tr, LightComponent&)
		{
			const float4x4 world = billboard(tr.position);
			m_LightIcon->Apply(renderContext.CommandList, world, renderContext.view, renderContext.projection,
				false, E_VERTEX_SHADER::BASIC, E_PIXEL_SHADER::BASIC, iconPso, renderContext.frameIndex);
			m_IconQuad.Draw(renderContext.CommandList);
		});
}

void RuntimeScene::DrawGrid()
{
	constexpr int gridCount = 10;
	constexpr float gridSize = 1.0f;

	const float4 gridColor = { 0.5f, 0.5f, 0.5f, 1.0f };  // グリッドは薄くする
	const float half = (gridSize * gridCount) * 0.5f;

	for (int i = 0; i <= gridCount; ++i)
	{
		const float offset = -half + i * gridSize;

		m_DebugLineRenderer.AddLine(float3{ offset, 0.0f, -half }, float3{ offset, 0.0f, half }, gridColor);
		m_DebugLineRenderer.AddLine(float3{ -half, 0.0f, offset }, float3{ half, 0.0f, offset }, gridColor);
	}
}

void RuntimeScene::DrawLight()
{
	m_World.Each<TransformComponent, LightComponent>(
		[this](Entity entity, TransformComponent& transform, LightComponent& light)
		{
			if (!light.isShow)
			{
				return;
			}


			// デバッグlineの色を設定
			COLOR color = { 1.0f,1.0f,1.0f,1.0f };

			// 位置取得
			const POSITION pos = transform.position;

			// 方向を正規化
			vector V = DirectX::XMVector3Normalize(
				DirectX::XMLoadFloat3(&light.direction));
			float3 dir;
			DirectX::XMStoreFloat3(&dir, V);

			const int seg = 24;
			const float pi = 3.14159265f;

			// --------//
			//	 種類別 //
			// --------//
			switch (light.type)
			{
				// --------------------------//
				//		Directional Light	 //
				// --------------------------//
			case LightComponent::LightType::Directional:
			{
				const float len = 3.0f;
				const float3 tip = pos + float3{
					dir.x * len,
					dir.y * len,
					dir.z * len,
				};

				// 矢じり
				float3 up = (fabsf(dir.y) > 0.99f) ? float3{ 1,0,0 } : float3{ 0,1,0 };
				vector right = DirectX::XMVector3Normalize(
					DirectX::XMVector3Cross(V, DirectX::XMLoadFloat3(&up)));
				float3 r;
				DirectX::XMStoreFloat3(&r, right);
				const float3 back = tip - float3{
					r.x,
					r.y,
					r.z,
				};
				m_DebugLines.push_back({ tip,back + r * 0.5f,color });
				m_DebugLines.push_back({ tip,back - r * 0.5f,color });
				break;
			}
			// ------------------//
			//		Point Light	 //
			// ------------------//
			case LightComponent::LightType::Point:
			{
				const float radius = light.range;
				for (int axis = 0; axis < 3; ++axis)
				{
					for (int i = 0; i < seg; ++i)
					{
						float a0 = (float)i / seg * 2 * pi;
						float a1 = (float)(i + 1) / seg * 2 * pi;
						float3 p1, p2;
						if (axis == 0)
						{
							p1 = pos + float3{ cosf(a0) * radius,sinf(a0) * radius,0.0f };
							p2 = pos + float3{ cosf(a1) * radius,sinf(a1) * radius,0.0f };
						}
						else if (axis == 1)
						{
							p1 = pos + float3{ cosf(a0) * radius,0.0f,sinf(a0) * radius };
							p2 = pos + float3{ cosf(a1) * radius,0.0f,sinf(a1) * radius };
						}
						else
						{
							p1 = pos + float3{ 0.0f,cosf(a0) * radius,sinf(a0) * radius };
							p2 = pos + float3{ 0.0f,cosf(a1) * radius,sinf(a1) * radius };
						}
						m_DebugLines.push_back({ p1,p2,color });
					}
				}
				break;
			}
			// ------------------//
			//		Spot Light	 //
			// ------------------//
			case LightComponent::LightType::Spot:
			{
				const float len = light.range;
				const float half = DirectX::XMConvertToRadians(light.spotAngle * 0.5f);
				const float rEnd = tanf(half) * len;	// 底面半径
				const float3 apex = pos;	// 頂点	
				const float3 center = pos + dir * len;

				// dirに直行する2軸
				float3 up = (fabsf(dir.y) > 0.99f) ? float3{ 1,0,0 } : float3{ 0,1,0 };
				vector rV = DirectX::XMVector3Normalize(
					DirectX::XMVector3Cross(V, DirectX::XMLoadFloat3(&up)));
				vector uV = DirectX::XMVector3Normalize(
					DirectX::XMVector3Cross(V, rV));
				float3 u, r;
				DirectX::XMStoreFloat3(&r, rV);
				DirectX::XMStoreFloat3(&u, uV);

				float3 prev;
				for (int i = 0; i <= seg; ++i)
				{
					float a = (float)i / seg * 2 * pi;
					float3 rim = center + r * (cosf(a) * rEnd) + u * (sinf(a) * rEnd);
					if (i > 0)
						m_DebugLines.push_back({ prev,rim,color });	// 底面
					if (1 % (seg / 4) == 0)
						m_DebugLines.push_back({ apex,rim,color });	// 頂点から底面への線
					prev = rim;
				}
				break;
			}
			}
		});
}
