#include "RuntimeScene.hpp"
#include "SceneSerializer.hpp"
#include "Logger.hpp"
#include "RenderTexture.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "PlayState.hpp"

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

	s_Current = this;

	LOG->LogInfo("RuntimeScene : Loading...");

	m_DebugLineRenderer.Init(m_Device, m_LinePso);
	m_BeamRenderer.Init(m_Device, APP->GetBeamPso());

	if (!m_SceneFilePath.empty())
	{
		if (!SceneSerializer::Load(*this, m_SceneFilePath))
		{
			LOG->LogWarning("RuntimeScene : シーン読み込みに失敗しました : " + m_SceneFilePath);
		}
	}

	bool hasMain = false, hasEditor = false, hasLight = false;
	m_World.Each<CameraComponent>([&](Entity, CameraComponent& c) {
		if (c.cameraType == CameraComponent::CameraType::Main)      hasMain = true;
		if (c.cameraType == CameraComponent::CameraType::Secondary) hasEditor = true;
		});
	m_World.Each<LightComponent>([&](Entity, LightComponent&) { hasLight = true; });

	if(!hasMain)
	{
		// -------------------------------------//
		//	メインカメラとメインライトの作成	//
		// -------------------------------------//
		auto camera = m_World.CreateEntity();
		auto& tr = m_World.AddComponent(camera, TransformComponent{});
		m_World.AddComponent(camera, NameComponent{ "MainCamera" });
		auto& maincamera = m_World.AddComponent(camera, CameraComponent{});
		tr.position = { 0.0f, 5.0f, 0.0f };
		maincamera.cameraType = CameraComponent::CameraType::Main;
		m_World.AddComponent(camera, FreeLookComponent{});
		tr.RebuildWorld();
	}

	if(!hasLight)
	{
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
	}

	if(!hasEditor)
	{
		// -------------------------- //
		//  エディター用カメラの作成  //
		// -------------------------- //
		auto editcam = m_World.CreateEntity();
		auto& t = m_World.AddComponent(editcam, TransformComponent{});
		m_World.AddComponent(editcam, NameComponent{ "EditorCamera" });
		auto& editCameraComp = m_World.AddComponent(editcam, CameraComponent{});
		editCameraComp.cameraType = CameraComponent::CameraType::Secondary;
		auto& freeLook = m_World.AddComponent(editcam, FreeLookComponent{});
		t.position = { 0.0f, 5.0f, -10.0f };
		freeLook.Enabled = true;
		t.RebuildWorld();
	}

	// -----------------------------//
	//  アイコン用マテリアルの作成  //
	// -----------------------------//
	m_IconQuad.CreateQuad(APP->GetDevice());

	m_UIQuad.CreateQuad(APP->GetDevice());

	m_CameraIcon = std::make_shared<Material>();
	m_CameraIcon->Init();
	m_CameraIcon->SetTextureFromFile(L"Assets/Icons/CameraIcon.png");

	m_LightIcon = std::make_shared<Material>();
	m_LightIcon->Init();
	m_LightIcon->SetTextureFromFile(L"Assets/Icons/LightIcon.png");
	n_IconReady = true;

	// -----------------------------//
	//      スカイボックスの用意     //
	// -----------------------------//
	m_SkyboxCube.CreateCube(APP->GetDevice());
	ApplySkybox();

	// -----------------------------//
	// 花火のシステムの初期化		// 
	// -----------------------------//
	m_FireworkSystem.Init();
	m_FireworkSystem.SetSoundCallback(
		[this](const float3& pos, int type)
		{
			(void)pos;
			//m_SePlayer.Play("Assets/Audio/FireworkBurst.wav", 0.6f);
		});
	m_FireworkBeamRenderer.Init(m_Device, APP->GetFireworkPso());

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
	if (!m_ScriptSystemStarted)
	{
		m_ScriptSystem.Start(m_World);
		m_ScriptSystemStarted = true;
	}

	m_ScriptSystem.Update(m_World, deltatime);
	m_SpinSystem.Update(m_World, deltatime);
	m_LightSystem.Apply(m_World);
	m_AudioSystem.Update(m_World, PLAY.isPlaying());
	m_MusicSyncSystem.Update(m_World, PLAY.isPlaying());
	m_FreeLookSystem.Update(m_World, deltatime, CameraComponent::CameraType::Secondary);
	m_CameraAnimationSystem.Update(m_World, deltatime,PLAY.isPlaying());
	m_TransformSystem.Update(m_World);
	m_CameraSystem.Update(m_World, 16.0f / 9.0f);
	m_AnimatorSystem.Update(m_World, deltatime);
	m_FireworkSystem.Update(deltatime);

	//// --- ワールド各所へ花火を打ち上げる ---
	//static float acc = 0.0f; acc += deltatime;
	//if (acc > 0.7f) {                       // 0.7秒ごとに一発
	//	acc = 0.0f;

	//	const FireworkSystem::Shape shapes[] = {
	//		FireworkSystem::Shape::Peony,  FireworkSystem::Shape::Willow,
	//		FireworkSystem::Shape::Ring,   FireworkSystem::Shape::Heart,
	//		FireworkSystem::Shape::Senrin, FireworkSystem::Shape::Text };

	//	// 発射位置をワールドに散らす(半径 R の円内、地面 y=0 から）
	//	const float R = 10.0f;
	//	const float px = ((rand() % 2000) / 1000.0f - 1.0f) * R; // -R..R
	//	const float pz = ((rand() % 2000) / 1000.0f - 1.0f) * R;
	//	const float3 launchPos{ px, 0.0f, pz };

	//	// 鮮やかな色に寄せる(彩度低い暗色を避ける)
	//	static const float3 palette[] = {
	//		{1.0f, 0.25f, 0.25f}, {0.25f, 0.6f, 1.0f}, {1.0f, 0.85f, 0.2f},
	//		{0.4f, 1.0f, 0.4f},   {1.0f, 0.4f, 1.0f},  {0.3f, 1.0f, 1.0f} };
	//	const float3 color = palette[rand() % 6];

	//	const auto shape = shapes[rand() % 6];
	//	m_FireworkSystem.Launch(launchPos, shape, color,
	//		shape == FireworkSystem::Shape::Text ? "HALO" : "");
	//}
}

void RuntimeScene::FixedUpdate(float fixedDeltatime)
{
	m_ScriptSystem.FixedUpdate(m_World, fixedDeltatime);
	(void)fixedDeltatime;
}

void RuntimeScene::LateUpdate(float deltatime)
{
	m_ScriptSystem.LateUpdate(m_World, deltatime);
	(void)deltatime;
}

void RuntimeScene::Draw(const RenderContext& renderContext)
{
	RenderContext context = renderContext;
	context.lightCb = m_LightSystem.GetLightData();

	const CameraComponent::CameraType wantType =
		context.isSceneView ? CameraComponent::CameraType::Secondary
		: CameraComponent::CameraType::Main;

	const CameraComponent* cam = nullptr;
	const CameraComponent* fallback = nullptr;

	m_World.Each<CameraComponent>([&](Entity, CameraComponent& camera)
		{
			if (camera.cameraType == wantType) cam = &camera;
			else                               fallback = &camera;
		});

	if (!cam) cam = fallback;

	if (cam)
	{
		context.view = cam->view;
		context.projection = cam->proj;
	}

	// ビューポート用レンダーテクスチャへの描画設定
	if (renderContext.viewportRenderTexture != nullptr &&
		renderContext.viewportRenderTexture->IsValid())
	{
		auto renderTexture = renderContext.viewportRenderTexture;
		auto* commandList = context.CommandList;

		// ピクセルシェーダーリソース → レンダーターゲット
		renderTexture->Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// シャドウマップの描画
		if (context.lightCb.shadowParams.y > 0.5f)
		{
			APP->GetShadowMap().BeginRender(commandList);
			m_ShadowSystem.Draw(m_World, context, APP->GetShadowPso());
			APP->GetShadowMap().EndRender(commandList);
		}

		auto rtvHandle = renderTexture->GetRTV();
		auto dsvHandle = renderContext.depthStencilView;

		commandList->SetGraphicsRootSignature(APP->GetRootSignature().Get());

		// --- ボーン行列パレット(b4)を単位行列でバインド（両モード共通）---
		if (renderContext.cbAllocator)
		{
			static BoneCB s_IdentityBones = [] {
				BoneCB cb{};
				DirectX::XMFLOAT4X4 id;
				DirectX::XMStoreFloat4x4(&id, DirectX::XMMatrixIdentity());
				for (auto& m : cb.boneMatrices) m = id;
				return cb;
				}();

			const UINT slot = renderContext.frameIndex % RTV_NUM;
			auto b4 = renderContext.cbAllocator->Allocate(slot, &s_IdentityBones, sizeof(BoneCB));
			if (b4) commandList->SetGraphicsRootConstantBufferView(5, b4);
		}

		const bool deferred = RenderSettings::Get().deferred;

		if (deferred)
		{
			// ---- 不透明 -> G-Buffer ----
			const UINT fw = APP->GetHdrScene().GetWidth();
			const UINT fh = APP->GetHdrScene().GetHeight();
			D3D12_VIEWPORT fullvp{ 0.0f,0.0f, (float)fw, (float)fh, 0.0f, 1.0f };
			D3D12_RECT fullsc{ 0,0, (LONG)fw, (LONG)fh };

			APP->GetHdrScene().Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
			auto hdrRtv = APP->GetHdrScene().GetRTV();

			APP->BeginGeometryPass();
			commandList->RSSetViewports(1, &fullvp);
			commandList->RSSetScissorRects(1, &fullsc);
			m_RenderSystem.Draw(m_World, context, APP->GetGBufferPso(), DrawFilter::OPAQUEONLY);

			// ---- ライティング -> HDR(R16F) ----
			APP->DeferredLightingPass(context, hdrRtv, fullvp, fullsc);

			commandList->OMSetRenderTargets(1, &hdrRtv, FALSE, nullptr);
			if (renderContext.viewport)    commandList->RSSetViewports(1, &fullvp);
			if (renderContext.scissorRect) commandList->RSSetScissorRects(1, &fullsc);
			commandList->SetGraphicsRootSignature(APP->GetRootSignature().Get());
			DrawLaserBeams(context, APP->GetBeamHdrPso());

			// ---- Bloom + トーンマップ合成 -> renderTexture(R8) ----
			APP->PostProcessBloom(rtvHandle,
				*renderContext.viewport, *renderContext.scissorRect);

			commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
			if (renderContext.viewport)    commandList->RSSetViewports(1, renderContext.viewport);
			if (renderContext.scissorRect) commandList->RSSetScissorRects(1, renderContext.scissorRect);
			commandList->SetGraphicsRootSignature(APP->GetRootSignature().Get());

			// b4 再バインド
			if (renderContext.cbAllocator)
			{
				static BoneCB s_IdentityBones = [] {
					BoneCB cb{};
					DirectX::XMFLOAT4X4 id;
					DirectX::XMStoreFloat4x4(&id, DirectX::XMMatrixIdentity());
					for (auto& m : cb.boneMatrices) m = id;
					return cb;
					}();
				const UINT slot = renderContext.frameIndex % RTV_NUM;
				auto b4 = renderContext.cbAllocator->Allocate(slot, &s_IdentityBones, sizeof(BoneCB));
				if (b4) commandList->SetGraphicsRootConstantBufferView(5, b4);
			}

			// skybox（深度==1の背景へ。深度は不透明ジオメトリのものが入っている）
			if (m_SkyBox)
			{
				float4x4 identity;
				DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
				m_SkyBox->Apply(context.CommandList, identity, context.view, context.projection,
					false, context.frameIndex, context.cbAllocator, "", APP->GetSkyPso());
				m_SkyboxCube.Draw(context.CommandList);
			}

			// 半透明
			m_RenderSystem.Draw(m_World, context, nullptr, DrawFilter::TRANSPARENTONLY);
		}
		else
		{
			// ===== フォワード（従来）=====
			commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			if (renderContext.viewport)    commandList->RSSetViewports(1, renderContext.viewport);
			if (renderContext.scissorRect) commandList->RSSetScissorRects(1, renderContext.scissorRect);

			renderTexture->Clear(commandList, { 0.2f, 0.2f, 0.2f, 1.0f });

			if (m_SkyBox)
			{
				float4x4 identity;
				DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
				m_SkyBox->Apply(context.CommandList, identity, context.view, context.projection,
					false, context.frameIndex, context.cbAllocator, "", APP->GetSkyPso());
				m_SkyboxCube.Draw(context.CommandList);
			}

			m_RenderSystem.Draw(m_World, context);   // 従来通り全部
		}

		//{
		//	DirectX::XMVECTOR det;
		//	const auto iv = DirectX::XMMatrixInverse(&det, DirectX::XMLoadFloat4x4(&context.view));
		//	float3 camRight, camUp;
		//	DirectX::XMStoreFloat3(&camRight, iv.r[0]);
		//	DirectX::XMStoreFloat3(&camUp, iv.r[1]);

		//	m_FireworkBeamRenderer.Begin();
		//	m_FireworkSystem.Emit(m_FireworkBeamRenderer, camRight, camUp);
		//	m_FireworkBeamRenderer.Draw(context);   // Init時のBeamPso(深度なし)
		//}

		// ===== デバッグライン / ビーム / UI（両モード共通・現在バインド中のRTへ）=====
		m_DebugLineRenderer.Begin();
		if (context.isSceneView) { DrawGrid(); DrawLight(); DrawGizmos(context); DrawColliders(); }
		for (const auto& line : m_DebugLines)
			m_DebugLineRenderer.AddLine(line.start, line.end, line.color);
		m_DebugLineRenderer.Draw(context);
		DrawLaserBeams(context);
		m_DebugLines.clear();
		m_CanvasRenderSystem.Draw(m_World, context, m_UIQuad, APP->GetUIPso(),
			(float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);

		// renderTexture → PIXEL_SHADER_RESOURCE（ImGui表示用）
		renderTexture->Transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		// skybox描画
		if (m_SkyBox)
		{
			float4x4 identity;
			DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
			m_SkyBox->Apply(
				context.CommandList, identity, context.view, context.projection,
				false, context.frameIndex, context.cbAllocator, "",
				APP->GetSkyPso());
			m_SkyboxCube.Draw(context.CommandList);
		}

		// 通常描画（メインレンダーターゲット）
		m_RenderSystem.Draw(m_World, context,nullptr);

		// デバッグラインの描画
		m_DebugLineRenderer.Begin();
		if (context.isSceneView) {
			DrawGrid();
			DrawLight();
			DrawGizmos(context);
			DrawColliders();
		}

		for (const auto& line : m_DebugLines)
		{
			m_DebugLineRenderer.AddLine(line.start, line.end, line.color);
		}
		m_DebugLineRenderer.Draw(context);
		//{
		//	DirectX::XMVECTOR det;
		//	const auto iv = DirectX::XMMatrixInverse(&det, DirectX::XMLoadFloat4x4(&context.view));
		//	float3 camRight, camUp;
		//	DirectX::XMStoreFloat3(&camRight, iv.r[0]);
		//	DirectX::XMStoreFloat3(&camUp, iv.r[1]);

		//	m_FireworkBeamRenderer.Begin();
		//	m_FireworkSystem.Emit(m_FireworkBeamRenderer, camRight, camUp);
		//	m_FireworkBeamRenderer.Draw(context);
		//}
		m_BeamRenderer.Draw(context);
		DrawLaserBeams(context);

		m_DebugLines.clear();

		m_CanvasRenderSystem.Draw(m_World, context, m_UIQuad, APP->GetUIPso(),
			(float)WINDOW_WIDTH, (float)WINDOW_HEIGHT);
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
				false, renderContext.frameIndex, renderContext.cbAllocator,"",iconPso);
			m_IconQuad.Draw(renderContext.CommandList);
		});

	// ライトアイコン
	m_World.Each<TransformComponent, LightComponent>(
		[&](Entity, TransformComponent& tr, LightComponent&)
		{
			const float4x4 world = billboard(tr.position);
			m_LightIcon->Apply(renderContext.CommandList, world, renderContext.view, renderContext.projection,
				false, renderContext.frameIndex, renderContext.cbAllocator,"",iconPso);
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
				const float4 arrowColor = { 1.0f, 0.9f, 0.2f, 1.0f };  // 黄色で目立たせる
				const float len = 3.0f;
				const float3 tip = pos + float3{ dir.x * len, dir.y * len, dir.z * len };

				// 軸線（光源位置 -> 進行方向）
				m_DebugLines.push_back({ pos, tip, arrowColor });

				// dirに直交する2軸（矢じり用）
				float3 upv = (fabsf(dir.y) > 0.99f) ? float3{ 1,0,0 } : float3{ 0,1,0 };
				vector rV = DirectX::XMVector3Normalize(
					DirectX::XMVector3Cross(V, DirectX::XMLoadFloat3(&upv)));
				vector uV = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(V, rV));
				float3 r, u;
				DirectX::XMStoreFloat3(&r, rV);
				DirectX::XMStoreFloat3(&u, uV);

				// 矢じり（先端から手前へ4本）
				const float head = 0.4f;
				const float3 back = tip - float3{ dir.x * head, dir.y * head, dir.z * head };
				m_DebugLines.push_back({ tip, back + r * head, arrowColor });
				m_DebugLines.push_back({ tip, back - r * head, arrowColor });
				m_DebugLines.push_back({ tip, back + u * head, arrowColor });
				m_DebugLines.push_back({ tip, back - u * head, arrowColor });
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
					if (i % (seg / 4) == 0)
						m_DebugLines.push_back({ apex,rim,color });	// 頂点から底面への線
					prev = rim;
				}
			}
				break;
			case LightComponent::LightType::Laser:
			{
				const float len = light.range;
				const float r = std::max(light.beamWidth, 0.01f);
				const float4 laserColor = { 1.0f, 0.15f, 0.15f, 1.0f }; // 目立つ赤系
				const float3 tip = pos + dir * len;

				// 中心の1本
				m_DebugLines.push_back({ pos, tip, laserColor });

				// 太さを表現する細い円柱アウトライン
				float3 up = (fabsf(dir.y) > 0.99f) ? float3{ 1,0,0 } : float3{ 0,1,0 };
				vector rV = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(V, DirectX::XMLoadFloat3(&up)));
				vector uV = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(V, rV));
				float3 r_, u_;
				DirectX::XMStoreFloat3(&r_, rV);
				DirectX::XMStoreFloat3(&u_, uV);

				for (int i = 0; i < 4; ++i)
				{
					float a = (float)i / 4 * 2 * pi;
					float3 offset = r_ * (cosf(a) * r) + u_ * (sinf(a) * r);
					m_DebugLines.push_back({ pos + offset, tip + offset, laserColor });
				}
				break;
			}
			}
		});
}

void RuntimeScene::DrawColliders()
{
	const float4 green = { 0.0f, 1.0f, 0.0f, 1.0f };   // コライダーは緑

	m_World.Each<TransformComponent, ColliderComponent>(
		[&](Entity, TransformComponent& tr, ColliderComponent& col)
		{
			// 回転クォータニオンをロード
			const auto rot = DirectX::XMQuaternionNormalize(
				DirectX::XMLoadFloat4(&tr.rotation));
			const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&tr.position);

			// ローカル座標 → ワールド座標に変換するヘルパー
			auto toWorld = [&](float x, float y, float z) -> float3
				{
					DirectX::XMVECTOR p = DirectX::XMVectorSet(x, y, z, 0.0f);
					p = DirectX::XMVector3Rotate(p, rot);
					p = DirectX::XMVectorAdd(p, origin);
					float3 out;
					DirectX::XMStoreFloat3(&out, p);
					return out;
				};

			auto line = [&](float3 a, float3 b)
				{
					m_DebugLineRenderer.AddLine(a, b, green);
				};

			switch (col.shapeType)
			{
			case ColliderComponent::ShapeType::Box:
			{
				const float hx = col.size.x * 0.5f;
				const float hy = col.size.y * 0.5f;
				const float hz = col.size.z * 0.5f;

				// 8頂点
				float3 v[8] = {
					toWorld(-hx, -hy, -hz), toWorld(hx, -hy, -hz),
					toWorld(hx, -hy,  hz), toWorld(-hx, -hy,  hz),
					toWorld(-hx,  hy, -hz), toWorld(hx,  hy, -hz),
					toWorld(hx,  hy,  hz), toWorld(-hx,  hy,  hz),
				};
				// 下面
				line(v[0], v[1]); line(v[1], v[2]); line(v[2], v[3]); line(v[3], v[0]);
				// 上面
				line(v[4], v[5]); line(v[5], v[6]); line(v[6], v[7]); line(v[7], v[4]);
				// 縦
				line(v[0], v[4]); line(v[1], v[5]); line(v[2], v[6]); line(v[3], v[7]);
				break;
			}
			case ColliderComponent::ShapeType::Sphere:
			case ColliderComponent::ShapeType::Mesh:   // Meshは暫定でSphere扱い
			{
				const float r = col.radius;
				constexpr int seg = 24;
				// XY / YZ / XZ の3リング
				for (int i = 0; i < seg; ++i)
				{
					const float a = DirectX::XM_2PI * i / seg;
					const float b = DirectX::XM_2PI * (i + 1) / seg;
					line(toWorld(cosf(a) * r, sinf(a) * r, 0), toWorld(cosf(b) * r, sinf(b) * r, 0));
					line(toWorld(0, cosf(a) * r, sinf(a) * r), toWorld(0, cosf(b) * r, sinf(b) * r));
					line(toWorld(cosf(a) * r, 0, sinf(a) * r), toWorld(cosf(b) * r, 0, sinf(b) * r));
				}
				break;
			}
			case ColliderComponent::ShapeType::Capsule:
			{
				const float r = col.radius;
				const float hh = col.size.y * 0.5f;   // 半分の円柱高さ
				constexpr int seg = 24;

				// 上下の円（Y軸方向のカプセル）
				for (int i = 0; i < seg; ++i)
				{
					const float a = DirectX::XM_2PI * i / seg;
					const float b = DirectX::XM_2PI * (i + 1) / seg;
					line(toWorld(cosf(a) * r, hh, sinf(a) * r), toWorld(cosf(b) * r, hh, sinf(b) * r));
					line(toWorld(cosf(a) * r, -hh, sinf(a) * r), toWorld(cosf(b) * r, -hh, sinf(b) * r));
				}
				// 側面の4本
				line(toWorld(r, -hh, 0), toWorld(r, hh, 0));
				line(toWorld(-r, -hh, 0), toWorld(-r, hh, 0));
				line(toWorld(0, -hh, r), toWorld(0, hh, r));
				line(toWorld(0, -hh, -r), toWorld(0, hh, -r));

				// 上下の半球（縦アーチ）
				for (int i = 0; i < seg / 2; ++i)
				{
					const float a = DirectX::XM_PI * i / (seg / 2);
					const float b = DirectX::XM_PI * (i + 1) / (seg / 2);
					// 上半球
					line(toWorld(cosf(a) * r, hh + sinf(a) * r, 0), toWorld(cosf(b) * r, hh + sinf(b) * r, 0));
					line(toWorld(0, hh + sinf(a) * r, cosf(a) * r), toWorld(0, hh + sinf(b) * r, cosf(b) * r));
					// 下半球
					line(toWorld(cosf(a) * r, -hh - sinf(a) * r, 0), toWorld(cosf(b) * r, -hh - sinf(b) * r, 0));
					line(toWorld(0, -hh - sinf(a) * r, cosf(a) * r), toWorld(0, -hh - sinf(b) * r, cosf(b) * r));
				}
				break;
			}
			}
		});
}

void RuntimeScene::DrawLaserBeams(const RenderContext& context, ID3D12PipelineState* psoOverride, bool emitFirework)
{
	// カメラワールド位置をview行列から復元（ビルボード計算用）
	const auto viewMat = DirectX::XMLoadFloat4x4(&context.view);
	DirectX::XMVECTOR det;
	const auto invView = DirectX::XMMatrixInverse(&det, viewMat);
	float3 camPos;
	DirectX::XMStoreFloat3(&camPos, invView.r[3]);

	m_BeamRenderer.Begin();

	constexpr int kColumns = 6; // ビーム断面の分割数（芯のグローを滑らかにするため）

	m_World.Each<TransformComponent, LightComponent>(
		[&](Entity, TransformComponent& tr, LightComponent& light)
		{
			const bool isLaser = (light.type == LightComponent::LightType::Laser);
			const bool isSpotBeam = (light.type == LightComponent::LightType::Spot && light.ShowBeam);
			if ((!isLaser && !isSpotBeam) || !light.isActive)
				return;

			const float3 origin = tr.position;
			const float3 dir = light.direction; // LightSystemで正規化・首振り済み
			const float3 tip = origin + dir * light.range;

			// ビーム軸・カメラ方向に直交する「幅方向」ベクトル(既存のまま)
			DirectX::XMVECTOR dV = DirectX::XMLoadFloat3(&dir);
			DirectX::XMVECTOR toCamV = DirectX::XMVector3Normalize(
				DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&camPos), DirectX::XMLoadFloat3(&origin)));
			DirectX::XMVECTOR widthV = DirectX::XMVector3Cross(dV, toCamV);
			if (DirectX::XMVectorGetX(DirectX::XMVector3Length(widthV)) < 1e-4f)
			{
				DirectX::XMVECTOR up = (fabsf(dir.y) > 0.99f)
					? DirectX::XMVectorSet(1, 0, 0, 0)
					: DirectX::XMVectorSet(0, 1, 0, 0);
				widthV = DirectX::XMVector3Cross(dV, up);
			}
			widthV = DirectX::XMVector3Normalize(widthV);
			float3 widthAxis;
			DirectX::XMStoreFloat3(&widthAxis, widthV);

			// 半幅: レーザーは一定、スポットは円錐に沿って開く
			const float hwStart = std::max(light.beamWidth, 0.001f) * 0.5f;
			float hwEnd = hwStart;
			float tipAlpha = 1.0f;
			if (isSpotBeam)
			{
				const float half = DirectX::XMConvertToRadians(light.spotAngle * 0.5f);
				hwEnd = tanf(half) * light.range;   // 円錐の底面半径
				tipAlpha = light.beamColorEnd.w;    // 先端の残り具合(0で空中に消える)
			}

			const float g = std::max(light.glowIntensity, 1.0f);

			for (int c = 0; c < kColumns; ++c)
			{
				const float x0 = -1.0f + 2.0f * c / kColumns;
				const float x1 = -1.0f + 2.0f * (c + 1) / kColumns;
				const float core0 = 1.0f - fabsf(x0);
				const float core1 = 1.0f - fabsf(x1);

				const float3 o0 = origin + widthAxis * (x0 * hwStart);
				const float3 o1 = origin + widthAxis * (x1 * hwStart);
				const float3 t0 = tip + widthAxis * (x0 * hwEnd);
				const float3 t1 = tip + widthAxis * (x1 * hwEnd);

				const float4 cO0 = { light.color.x * g, light.color.y * g, light.color.z * g, core0 };
				const float4 cO1 = { light.color.x * g, light.color.y * g, light.color.z * g, core1 };
				const float4 cT0 = { light.beamColorEnd.x * g, light.beamColorEnd.y * g, light.beamColorEnd.z * g, core0 * tipAlpha };
				const float4 cT1 = { light.beamColorEnd.x * g, light.beamColorEnd.y * g, light.beamColorEnd.z * g, core1 * tipAlpha };

				m_BeamRenderer.AddTriangle(o0, cO0, o1, cO1, t1, cT1);
				m_BeamRenderer.AddTriangle(o0, cO0, t1, cT1, t0, cT0);
			}
		});
	m_BeamRenderer.Draw(context, psoOverride);
}

void RuntimeScene::EditorUpdate(float dt)
{
	m_LightSystem.Apply(m_World);
	m_FreeLookSystem.Update(m_World, dt,CameraComponent::CameraType::Secondary);   // エディタカメラ操作
	m_CameraSystem.Update(m_World, 16.0f / 9.0f);
	m_TransformSystem.Update(m_World);
	m_AudioSystem.Update(m_World, false);
	// m_AnimatorSystem.Update(m_World, dt);
}

void RuntimeScene::ApplySkybox()
{
	const std::string& path = GetSkyboxPath();
	if (path.empty())
	{
		m_SkyBox.reset();   // 空パス=スカイボックスなし(描画側はnullチェック済み)
		return;
	}
	m_SkyBox = std::make_shared<Material>();
	m_SkyBox->Init();
	m_SkyBox->SetTextureFromFile(std::wstring(path.begin(), path.end()));
}
