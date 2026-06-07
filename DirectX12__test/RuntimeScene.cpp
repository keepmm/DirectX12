#include "RuntimeScene.hpp"
#include "SceneSerializer.hpp"
#include "Logger.hpp"
#include "RenderTexture.hpp"

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
		if(!SceneSerializer::Load(*this, m_SceneFilePath))
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
	tr.position = { 0.0f, 5.0f, -5.0f };
	maincamera.isMainCamera = true;

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
	m_CameraSystem.Update(m_World, deltatime);
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
			if (camera.isMainCamera)
			{
				main = &camera;
			}
		});
	if (main)
	{
		// 行列の更新	
		context.view = main->view;
		context.projection = main->proj;
	}
	else
	{
		DirectX::XMMATRIX identity = DirectX::XMMatrixIdentity();
		DirectX::XMStoreFloat4x4(&context.view, identity);
		DirectX::XMStoreFloat4x4(&context.projection, identity);
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
		// デバッグラインの描画
		m_DebugLineRenderer.Begin();
		DrawGrid();
		DrawLight();

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
		DrawGrid();
		DrawLight();

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
