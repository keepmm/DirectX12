#pragma once

#include "World.hpp"
#include "Components.hpp"
#include "RenderContext.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "FontAtlas.hpp"
#include "Input.hpp"
#include "MonoBehavior.hpp"
#include "Util.hpp"
#include "Debug.hpp"

class SpinSystem
{
public:
	void Update(World& world, float deltatime)
	{
		world.Each<TransformComponent, SpinComponent>(
			[deltatime](Entity, TransformComponent& transform, SpinComponent& spin)
			{
				spin.angle += spin.speed * deltatime;
				const auto q = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, spin.angle, 0.0f);
				DirectX::XMStoreFloat4(&transform.rotation, DirectX::XMQuaternionNormalize(q));
			}
		);
	}
};

class RenderSystem
{
public:

	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext)
	{
		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		if (renderContext.useMeshShader)
		{
			if (!renderContext.meshShaderSupported ||
				renderContext.CommandList6 == nullptr ||
				renderContext.meshShaderPso == nullptr)
			{
				return;
			}

			renderContext.CommandList6->SetPipelineState(renderContext.meshShaderPso);
			renderContext.CommandList6->DispatchMesh(1, 1, 1);
		}

		if (renderContext.cbAllocator != nullptr)
		{
			const UINT frameSlot = renderContext.frameIndex % RTV_NUM;
			const D3D12_GPU_VIRTUAL_ADDRESS b2 = renderContext.cbAllocator->Allocate(
			frameSlot,&renderContext.lightCb,sizeof(LightCB));

			if (b2 != 0)
			{
				renderContext.CommandList->SetGraphicsRootConstantBufferView(2, b2);
			}
		}

		world.Each<TransformComponent, MeshComponent, MaterialComponent>(
			[&renderContext](
				Entity,
				TransformComponent& transform,
				MeshComponent& mesh,
				MaterialComponent& material
				)
			{
				if (mesh.mesh == nullptr || material.material == nullptr)
				{
					return;
				}

				material.material->Apply(
					renderContext.CommandList,
					transform.world,
					renderContext.view,
					renderContext.projection,
					renderContext.wireframe,
					renderContext.frameIndex,
					renderContext.cbAllocator,
					material.shaderName
				);

				mesh.mesh->Draw(renderContext.CommandList);
			}
		);
	}
};

class LightSystem
{
public:
	void Apply(World& world)
	{
		// ライトがない場合のためにデフォルト
		m_Data = {};

		UINT count = 0;

		world.Each<LightComponent>([&](Entity entity, LightComponent& light)
			{
				// 非アクティブ、または上限に達したらスキップ
				if (!light.isActive || count >= MAX_LIGHTS)
				{
					return;
				}

				LightData& dst = m_Data.lights[count];

				// 色 × 強度
				dst.color = light.color;
				dst.color.x *= light.intensity;
				dst.color.y *= light.intensity;
				dst.color.z *= light.intensity;

				// 位置と方向（Transformがあれば回転から導出）
				if (world.HasComponent<TransformComponent>(entity))
				{
					const auto& tr = world.GetComponent<TransformComponent>(entity);
					dst.posRange = float4(
						tr.position.x, tr.position.y, tr.position.z,
						light.range);

					const auto rot = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&tr.rotation));
					const auto fwd = DirectX::XMVector3Rotate(
						DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot);
					DirectX::XMStoreFloat4(&dst.dir, DirectX::XMVector3Normalize(fwd));

					// ギズモ表示用に書き戻す
					DirectX::XMStoreFloat3(&light.direction, DirectX::XMVector3Normalize(fwd));
				}
				else
				{
					const auto dirVec = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
						light.direction.x, light.direction.y, light.direction.z, 0.0f));
					DirectX::XMStoreFloat4(&dst.dir, dirVec);
					dst.posRange.w = light.range;
				}

				// タイプとスポット角
				dst.param.x = static_cast<float>(light.type);
				dst.param.y = cosf(DirectX::XMConvertToRadians(light.spotAngle * 0.5f));

				// 環境光は最初のライトのものを採用
				if (count == 0)
				{
					m_Data.ambientColor = light.ambientColor;
				}

				++count;
			});

		m_Data.lightCount.x = static_cast<float>(count);
	}

	inline const LightCB& GetLightData() const
	{
		return m_Data;
	}

private:
	LightCB m_Data;
};

class ScriptSystem
{
public:
	void Start(World& world)
	{
		world.Each<ScriptComponent>([](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnStart();
			});
	}

	void Update(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnUpdate(deltatime);
			});
	}

	void FixedUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnFixedUpdate(deltatime);
			});
	}

	void LateUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnLateUpdate(deltatime);
			});
	}

	void Draw(World& world, const RenderContext& context)
	{
		world.Each<ScriptComponent>([&context](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
					b->OnDraw(context);
			});
	}
};

class SpriteRenderSystem
{
public:
	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext,
		_In_ Mesh& quad,
		_In_ Material& spriteMaterial)
	{
		if (renderContext.CommandList == nullptr)
		{
			return;
		}

		world.Each<TransformComponent, SpriteComponent>([&](Entity, TransformComponent& transform, SpriteComponent& sprite)
			{

				// material 未生成 & パスあり
				if (!sprite.material && !sprite.texturePath.empty())
				{
					auto mat = std::make_shared<Material>();
					mat->Init();
					mat->SetTextureFromFile(sprite.texturePath);
					sprite.material = mat;
				}

				// material 未生成なら描画しない
				if (!sprite.material) return;

				transform.ApplyEuler();
				sprite.material->UpdateTextureIfNeeded(renderContext.CommandList);

				sprite.material->Apply(renderContext.CommandList,
					transform.world,renderContext.view,renderContext.projection,
					renderContext.wireframe,renderContext.frameIndex);

				quad.Draw(renderContext.CommandList);
			});
	}
};

class FreeLookSystem
{
public:
	void Update(World& world, float deltatime,CameraComponent::CameraType targetType)
	{
		world.Each<TransformComponent, FreeLookComponent>(
			[&](Entity entity, TransformComponent& tr, FreeLookComponent& fl)
			{
				// FreeLookが有効でない場合処理しない
				if (!fl.Enabled) return;

				// このEntityのカメラ種別が対象でなければスキップ
				if (!world.HasComponent<CameraComponent>(entity)) return;
				if (world.GetComponent<CameraComponent>(entity).cameraType != targetType) return;

				//--------------- //
				// 右クリックで回転  //
				//--------------- //
				if (INPUT->MouseInput.Right().IsPressed())
				{
					fl.yaw += (float)INPUT->MouseInput.DeltaX() * fl.rotateSpeed;
					fl.pitch += (float)INPUT->MouseInput.DeltaY() * fl.rotateSpeed;
					fl.pitch = std::clamp(fl.pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
				}

				// 回転の更新
				vector q = DirectX::XMQuaternionRotationRollPitchYaw(fl.pitch, fl.yaw, 0.0f);
				DirectX::XMStoreFloat4(&tr.rotation, DirectX::XMQuaternionNormalize(q));

				float speed = 1.0f;

				// 移動量
				vector move = DirectX::XMVectorZero();
				if (INPUT->Key.Shift().IsPressed()) speed *= 5.0f;
				if (INPUT->Key.W().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0.0f, 0.0f, speed, 0.0f));
				if (INPUT->Key.S().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0.0f, 0.0f, -speed, 0.0f));
				if (INPUT->Key.A().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(-speed, 0.0f, 0.0f, 0.0f));
				if (INPUT->Key.D().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(speed, 0.0f, 0.0f, 0.0f));

				if (!DirectX::XMVector3Equal(move, DirectX::XMVectorZero()))
				{
					move = DirectX::XMVector3Normalize(move);
					move = DirectX::XMVectorScale(move, fl.moveSpeed * deltatime);
					vector pos = XMLoadFloat3(&tr.position);
					pos = DirectX::XMVectorAdd(pos, DirectX::XMVector3Rotate(move, q));
					DirectX::XMStoreFloat3(&tr.position, pos);
				}
			});
	}
};

class CameraSystem
{
public:
	void Update(World& world, float aspect)
	{
		world.Each<TransformComponent,CameraComponent>([aspect]
		(Entity, TransformComponent& tr, CameraComponent& camera) {
			if (!camera.isActive)
			{
				return;
			}

			vector pos = XMLoadFloat3(&tr.position);
			vector q = XMLoadFloat4(&tr.rotation);
			vector forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
			vector up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), q);
			DirectX::XMStoreFloat4x4(&camera.view, DirectX::XMMatrixLookToLH(pos, forward, up));

			matrix p = (camera.projection == CameraComponent::Projection::Perspective)
				? DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(camera.fovY), aspect, camera.nearZ, camera.farZ)
				: DirectX::XMMatrixOrthographicLH(camera.orhoSize * aspect, camera.orhoSize, camera.nearZ, camera.farZ);
			DirectX::XMStoreFloat4x4(&camera.proj, p);
			});
	}
};

class NameSytem
{
public:
	static std::string GetName(World& world, Entity entity)
	{
		world.Each<NameComponent>([&](Entity e, NameComponent& name)
			{
				if (e == entity)
				{
					return name.name;
				}
			});
		return std::to_string(entity);
	}

	static void SetName(World& world, Entity entity, const std::string& name)
	{
		// -------------------------//
		// 同じ名前が存在してる場合	//
		// 名前 + _番号にする		//
		// -------------------------//
		std::string newName = GenerateName(world, entity, name, 1);
		if (world.HasComponent<NameComponent>(entity))
			world.GetComponent<NameComponent>(entity).name = newName;   // 書き戻す
		else
			world.AddComponent<NameComponent>(entity, NameComponent{ newName });
	}

	/// @brief 名前を捜索する
	static std::string GenerateName(World& world, Entity entity,const std::string& name,int num)
	{
		std::string result = name;
		bool conflict = true;
		while (conflict)
		{
			conflict = false;
			world.Each<NameComponent>([&](Entity e, NameComponent& nameComp)
				{
					// 自分自身は除外
					if(e != entity && nameComp.name == result)
					{
						conflict = true;
					}
				});
			if (conflict)
			{
				result = name + "_" + std::to_string(num++);
			}
		}

		return result;
	}
};

class CanvasRenderSystem
{
public:
	void Draw(
		_In_ World& world,
		_In_ const RenderContext& ctx,
		_In_ Mesh& quad,
		_In_ ID3D12PipelineState* uiPso,
		_In_ float screenW,
		_In_ float screenH)
	{
		// スクリーン正射影
		float4x4 view, proj;
		DirectX::XMStoreFloat4x4(&view, DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(&proj, DirectX::XMMatrixOrthographicOffCenterLH(
			0,screenW,screenH,0,0.0f,1.0f));

		world.Each<RectTransformComponent, UIImageComponent>(
			[&](Entity e, RectTransformComponent& rt, UIImageComponent& img)
			{
				// テクスチャ遅延ロード
				if(!img.material && !img.texturePath.empty())
				{
					auto mat = std::make_shared<Material>();
					mat->Init();
					mat->SetTextureFromFile(Utf8ToWide(img.texturePath));
					img.material = mat;
				}

				if (!img.material) return;

				// RectTransform -> ピクセル空間world座標
				// quad は左上座標 + 半サイズで中心に置く
				const float w = rt.SizeDelta.x;
				const float h = rt.SizeDelta.y;
				const float cx = rt.AnchoredPosition.x + w * 0.5f;
				const float cy = rt.AnchoredPosition.y + h * 0.5f;
				float4x4 worldf;
				DirectX::XMStoreFloat4x4(&worldf,
					DirectX::XMMatrixScaling(w, h, 1.0f) * DirectX::XMMatrixTranslation(cx, cy, 0.0f));

				img.material->Apply(
					ctx.CommandList, worldf, view, proj, false,
					ctx.frameIndex, ctx.cbAllocator,"",
					uiPso);

				quad.Draw(ctx.CommandList);
			});

		// text描画
		world.Each<RectTransformComponent, UITextComponent>(
			[&](Entity, RectTransformComponent& rt, UITextComponent& txt)
			{
				FontAtlas* atlas = FontLibrary::Get(txt.fontPath);
				if (!atlas) return;

				if (!txt.mesh) txt.mesh = std::make_shared<Mesh>();

				// 文字が変わった時だけメッシュ再構築（fontSizeは含めない）
				if (txt.isDirty || txt.text != txt._lastText)
				{
					APP->WaitForGPUIdle();
					atlas->BuildTextMesh(*txt.mesh, txt.text, txt.color);
					txt._lastText = txt.text;
					txt.isDirty = false;
				}

				// fontSize はワールドのスケールで効かせる（再ベイク・再構築なし＝軽い）
				const float s = txt.fontSize / atlas->RefHeight();
				float4x4 worldf;
				DirectX::XMStoreFloat4x4(&worldf,
					DirectX::XMMatrixScaling(s, s, 1.0f) *
					DirectX::XMMatrixTranslation(rt.AnchoredPosition.x, rt.AnchoredPosition.y, 0.0f));

				atlas->GetMaterial().Apply(ctx.CommandList, worldf, view, proj, false,
					 ctx.frameIndex, ctx.cbAllocator,"",uiPso);
				txt.mesh->Draw(ctx.CommandList);
			});
	}
};

class AudioSystem
{
public:
	void Update(World& world, bool isPlaying)
	{
		static int f = 0;
		if ((f++ % 60) == 0)
		{
			int srcCount = 0, listenerCount = 0;
			world.Each<AudioSourceComponent>([&](Entity, AudioSourceComponent&) { srcCount++; });
			world.Each<AudioListenerComponent>([&](Entity, AudioListenerComponent&) { listenerCount++; });

			char b[256];
			sprintf_s(b, "[Audio] Update playing=%d sources=%d listeners=%d\n",
				(int)isPlaying, srcCount, listenerCount);
			OutputDebugStringA(b);
		}

		const bool justStarted = (isPlaying && !m_PrevPlaying);
		m_PrevPlaying = isPlaying;

		// ---- リスナー（耳）を1つ探す ---- //
		X3DAUDIO_LISTENER listener{};
		bool hasListener = false;
		world.Each<AudioListenerComponent, TransformComponent>(
			[&](Entity, AudioListenerComponent&, TransformComponent& tr)
			{
				if (hasListener) return;
				hasListener = true;

				listener.Position = { tr.position.x, tr.position.y, tr.position.z };

				// 向き（回転クォータニオンからforward/upを出す）
				using namespace DirectX;
				XMVECTOR q = XMLoadFloat4(&tr.rotation);
				XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
				XMVECTOR up = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);
				XMFLOAT3 f, u; XMStoreFloat3(&f, fwd); XMStoreFloat3(&u, up);
				listener.OrientFront = { f.x, f.y, f.z };
				listener.OrientTop = { u.x, u.y, u.z };
			});

		world.Each<AudioSourceComponent, TransformComponent>(
			[&](Entity, AudioSourceComponent& src, TransformComponent& tr)
			{
				// ---- ロード ---- //
				if (!src.clip && !src.clipPath.empty())
				{
					src.clip = AudioEngine::Get().Load(src.clipPath);
					if (src.clip)
						src.voice = AudioEngine::Get().CreateVoice(src.clip->format);
				}
				if (!src.voice || !src.clip) return;

				// ---- playOnStart / 再生・停止（前回と同じ） ---- //
				if (justStarted && src.playOnStart) src.playRequested = true;

				if (src.playRequested)
				{
					src.playRequested = false;
					src.voice->Stop();
					src.voice->FlushSourceBuffers();
					XAUDIO2_BUFFER buf{};
					buf.AudioBytes = (UINT32)src.clip->data.size();
					buf.pAudioData = src.clip->data.data();
					buf.Flags = XAUDIO2_END_OF_STREAM;
					buf.LoopCount = src.loop ? XAUDIO2_LOOP_INFINITE : 0;
					src.voice->SubmitSourceBuffer(&buf);
					src.voice->Start();
				}
				if (src.stopRequested)
				{
					src.stopRequested = false;
					src.voice->Stop();
					src.voice->FlushSourceBuffers();
				}

				// ---- 3D定位 ---- //
				if (src.is3D && hasListener)
				{
					X3DAUDIO_EMITTER emitter{};
					emitter.Position = { tr.position.x, tr.position.y, tr.position.z };
					emitter.OrientFront = { 0, 0, 1 };
					emitter.OrientTop = { 0, 1, 0 };
					emitter.ChannelCount = 1;                 // モノラル前提（3D音源は基本モノラル）
					emitter.CurveDistanceScaler = src.maxDistance;
					emitter.DopplerScaler = 1.0f;

					const UINT32 outCh = AudioEngine::Get().OutputChannels();
					float matrix[8] = {};                     // 最大8chスピーカー分
					X3DAUDIO_DSP_SETTINGS dsp{};
					dsp.SrcChannelCount = 1;
					dsp.DstChannelCount = outCh;
					dsp.pMatrixCoefficients = matrix;

					X3DAudioCalculate(
						AudioEngine::Get().X3D(), &listener, &emitter,
						X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER,
						&dsp);

					// 出力マトリクス（定位＋距離減衰）とドップラーを反映
					src.voice->SetOutputMatrix(nullptr, 1, outCh, matrix);
					src.voice->SetFrequencyRatio(dsp.DopplerFactor);
					src.voice->SetVolume(src.volume);
				}
				else
				{
					// 2D（従来通り）
					src.voice->SetVolume(src.volume);
				}
			});
	}
private:
	bool m_PrevPlaying = false;
};

class TransformSystem
{
public:
	void Update(World& world)
	{
		// 各エンティティのworldを、親をたどって計算
		world.Each<TransformComponent>([&](Entity e, TransformComponent& tr)
			{
				UpdateWorld(world, e, tr);
			});
	}

private:
	void UpdateWorld(World& world, Entity e, TransformComponent& tr)
	{
		using namespace DirectX;

		XMMATRIX local =
			XMMatrixScaling(tr.scale.x, tr.scale.y, tr.scale.z) *
			XMMatrixRotationQuaternion(XMVector4Normalize(XMLoadFloat4(&tr.rotation))) *
			XMMatrixTranslation(tr.position.x, tr.position.y, tr.position.z);

		XMMATRIX worldMat = local;
		if (tr.parent != INVALID_ENTITY && world.HasComponent<TransformComponent>(tr.parent))
		{
			auto& p = world.GetComponent<TransformComponent>(tr.parent);
			UpdateWorld(world, tr.parent, p);
			worldMat = local * XMLoadFloat4x4(&p.world);
		}
		XMStoreFloat4x4(&tr.world, worldMat);
	}
};