#pragma once

#include "GpuProfiler.hpp"

#include "World.hpp"
#include "Components.hpp"
#include "AnimatorClipCache.hpp"
#include "RenderContext.hpp"
#include "FramePipeline.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "FontAtlas.hpp"
#include "Input.hpp"
#include "MonoBehavior.hpp"
#include "Util.hpp"
#include "Debug.hpp"
#include "imguiinit.hpp"
#include "Animator.hpp"
#include <chrono>
#include "DirectX.hpp"
#include "d3dx12.h"
#include "Time.hpp"
#include "ModelLoader.hpp"
#include <sstream>
#include "AsyncLoader.hpp"

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

/// @brief 対象の周囲に効くライトだけを、影響の強い順に上位 maxLights 灯へ詰め直す
/// @param src    シーン全体のライト
/// @param center 対象の中心(ワールド)
/// @param radius 対象を包む球の半径
/// @param maxLights 残す灯数
/// @param dst    詰め直した結果
/// @note 平行光は距離で切れないので常に残す。影を落とす灯の添字も詰め直しに追従させる
inline void BuildCulledLightCB(const LightCB& src, const float3& center,
	float radius, int maxLights, LightCB& dst)
{
	dst = src;

	const int count = static_cast<int>(src.lightCount.x);
	const int shadowIndex = static_cast<int>(src.shadowParams.w);

	// (スコア, 元の添字)。スコアは「その灯がこの対象をどれだけ照らすか」の目安
	struct Scored { float score; int index; };
	Scored scored[MAX_LIGHTS];
	int n = 0;

	for (int i = 0; i < count && i < static_cast<int>(MAX_LIGHTS); ++i)
	{
		const LightData& l = src.lights[i];
		const float lum = l.color.x * 0.299f + l.color.y * 0.587f + l.color.z * 0.114f;
		if (lum <= 0.0f) continue;

		// 平行光は距離減衰が無いので必ず残す(スコアを最大にする)
		if (static_cast<int>(l.param.x) == 0)
		{
			scored[n++] = { FLT_MAX, i };
			continue;
		}

		const float dx = l.posRange.x - center.x;
		const float dy = l.posRange.y - center.y;
		const float dz = l.posRange.z - center.z;
		const float dist = sqrtf(dx * dx + dy * dy + dz * dz);

		const float range = (std::max)(l.posRange.w, 0.0001f);
		if (dist - radius > range) continue;		// 球に届かない

		// 減衰はシェーダーと同じ形(1 - d/range)^2 を対象の一番近い点で見る
		const float d = (std::max)(dist - radius, 0.0f);
		const float atten = (1.0f - d / range) * (1.0f - d / range);
		scored[n++] = { lum * atten, i };
	}

	const int keep = (std::min)(n, (std::max)(1, maxLights));

	// 上位 keep 灯だけ前に寄せる(全体を並べ替える必要はない)
	std::partial_sort(scored, scored + keep, scored + n,
		[](const Scored& a, const Scored& b) { return a.score > b.score; });

	int newShadow = -1;
	for (int i = 0; i < keep; ++i)
	{
		dst.lights[i] = src.lights[scored[i].index];
		if (scored[i].index == shadowIndex) newShadow = i;
	}

	dst.lightCount.x = static_cast<float>(keep);

	// 影を落とす灯が落選したら、影そのものを切る(別の灯に影が付くと破綻する)
	if (newShadow < 0) dst.shadowParams.y = 0.0f;
	dst.shadowParams.w = static_cast<float>(newShadow);
}

enum class DrawFilter : uint8_t
{
	ALL,
	OPAQUEONLY,
	TRANSPARENTONLY,
	OPAQUE_NOTOON,		// 不透明のうちトゥーン以外(デファードのG-Buffer用)
	OPAQUE_TOON,		// 不透明のうちトゥーンだけ(フォワードで重ねる用)
	REFLECTION,			// 平面反射に映すもの(ReflectionCaster が付いたEntityだけ)
};

// トゥーン系シェーダーか。デファードではG-Bufferに入れず、
// フォワードで従来のシェーダーのまま描くために使う
inline bool IsToonShader(const std::string& name)
{
	return name.find("Toon") != std::string::npos;
}
#ifdef _FRAMEPIPELINE
/// @brief そのフレームで確定した描画1件
/// @note FramePipeline.hpp ではなくここで定義しているのは、
///       Mesh / Material の完全型が要るため(あちらに include すると循環する)
struct FO_DrawItem
{
	float4x4 world{};

	std::shared_ptr<Mesh>     mesh;
	std::shared_ptr<Material> material;
	std::vector<std::shared_ptr<Material>> materials;   // サブメッシュ用(空なら単体)
	std::string shaderName;

	// ボーンパレット。スキン無しは共有の単位行列を指すのでフレームメモリを食わない
	const BoneCB* boneCb = nullptr;

	// 頂点モーフ。nullptr なら無し
	const DirectX::XMFLOAT3* morphOffsets = nullptr;
	UINT morphVertexCount = 0;
};

/// @brief スキン無しエンティティが共有する単位行列パレット
inline const BoneCB& IdentityBoneCB()
{
	static const BoneCB cb = []
		{
			BoneCB c{};
			DirectX::XMFLOAT4X4 id;
			DirectX::XMStoreFloat4x4(&id, DirectX::XMMatrixIdentity());
			for (auto& m : c.boneMatrices) m = id;
			c.morph = 0.0f;
			return c;
		}();
	return cb;
}
#endif

class RenderSystem
{
public:
#ifdef _FRAMEPIPELINE
	/// @brief World を走査して FO_DrawItem を積む(Game フェーズで呼ぶ)
	/// @note ここを通したあと Render 側は World を一切読まない
	static void Publish(_In_ World& world, _In_ FramePipeline& fp)
	{
		world.Each<TransformComponent, MeshComponent, MaterialComponent>(
			[&world, &fp](Entity entity,
				TransformComponent& transform,
				MeshComponent& mesh,
				MaterialComponent& material)
			{
				if (mesh.mesh == nullptr || material.material == nullptr)
				{
					return;
				}

				FO_DrawItem item{};
				item.world = transform.world;
				item.mesh = mesh.mesh;
				item.material = material.material;
				item.materials = material.materials;
				item.shaderName = material.shaderName;

				if (world.HasComponent<AnimatorComponent>(entity))
				{
					const auto& an = world.GetComponent<AnimatorComponent>(entity);

					// スキンありのぶんだけフレームメモリを使う(1体 32KB)
					auto* cb = static_cast<BoneCB*>(
						fp.AllocateFrameMemory(sizeof(BoneCB), alignof(BoneCB)));

					const size_t n = (std::min)(an.palette.size(), static_cast<size_t>(MAX_BONES));
					for (size_t i = 0; i < n; ++i) cb->boneMatrices[i] = an.palette[i];
					for (size_t i = n; i < MAX_BONES; ++i)
						DirectX::XMStoreFloat4x4(&cb->boneMatrices[i], DirectX::XMMatrixIdentity());

					bool anyMorph = false;
					for (float w : an.morphWeights)
						if (fabsf(w) > 1e-6f) { anyMorph = true; break; }
					cb->morph = anyMorph ? 1.0f : 0.0f;

					item.boneCb = cb;

					// モーフはフレームメモリへスナップショットする。
					// World 側の配列を指すと、Game が次フレームを進めた瞬間に壊れる
					const size_t vcount = mesh.mesh->GetVertexCount();
					if (anyMorph && an.morphoffsets.size() == vcount && vcount > 0)
					{
						const size_t bytes = vcount * sizeof(DirectX::XMFLOAT3);
						auto* dst = static_cast<DirectX::XMFLOAT3*>(
							fp.AllocateFrameMemory(bytes, alignof(DirectX::XMFLOAT3)));
						std::memcpy(dst, an.morphoffsets.data(), bytes);

						item.morphOffsets = dst;
						item.morphVertexCount = static_cast<UINT>(vcount);
					}
				}
				else
				{
					// スキン無しは共有の単位行列を指す(フレームメモリを消費しない)
					item.boneCb = &IdentityBoneCB();
				}

				fp.AddFrameObject<FO_DrawItem>(std::move(item));
			});
	}
#endif

	void Draw(
		_In_ World& world,
		_In_ const RenderContext& renderContext,
		_In_ ID3D12PipelineState* overidePso = nullptr,
		_In_ DrawFilter filter = DrawFilter::ALL)
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

		// b2: ライトCB
		if (renderContext.cbAllocator != nullptr)
		{
			const UINT frameSlot = renderContext.frameIndex % RTV_NUM;
			const D3D12_GPU_VIRTUAL_ADDRESS b2 = renderContext.cbAllocator->Allocate(
				frameSlot, &renderContext.lightCb, sizeof(LightCB));
			if (b2 != 0)
			{
				renderContext.CommandList->SetGraphicsRootConstantBufferView(2, b2);
			}
		}

		static ComPtr<ID3D12Resource> s_zeroMorph;
		static D3D12_GPU_VIRTUAL_ADDRESS s_zeroMorphVA = 0;
		if (!s_zeroMorph)
		{
			const UINT ZERO_VERTS = 300000;                  // 最大メッシュ頂点数を余裕でカバー
			const UINT bytes = ZERO_VERTS * sizeof(DirectX::XMFLOAT3);
			CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(bytes);
			APP->GetDevice()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_zeroMorph));
			void* p = nullptr; CD3DX12_RANGE rr(0, 0);
			s_zeroMorph->Map(0, &rr, &p);
			memset(p, 0, bytes);
			s_zeroMorph->Unmap(0, nullptr);
			s_zeroMorphVA = s_zeroMorph->GetGPUVirtualAddress();
		}

		world.Each<TransformComponent, MeshComponent, MaterialComponent>(
			[&world, &renderContext,filter](
				Entity entity,
				TransformComponent& transform,
				MeshComponent& mesh,
				MaterialComponent& material
				)
			{
				if (mesh.mesh == nullptr || material.material == nullptr)
				{
					return;
				}

				// b2: このEntity向けのライトCB。
				// LightCull が付いていれば上位N灯に絞る。付いていなければ全体を張り直す
				// (前のEntityで絞ったものが残らないように、どちらの場合も張る)
				if (renderContext.cbAllocator != nullptr)
				{
					const UINT slot = renderContext.frameIndex % RTV_NUM;
					D3D12_GPU_VIRTUAL_ADDRESS b2 = 0;

					if (world.HasComponent<LightCullComponent>(entity))
					{
						const auto& cull = world.GetComponent<LightCullComponent>(entity);
						LightCB culled{};
						BuildCulledLightCB(renderContext.lightCb,
							transform.position, cull.radius, cull.maxLights, culled);
						b2 = renderContext.cbAllocator->Allocate(slot, &culled, sizeof(LightCB));
					}
					else
					{
						b2 = renderContext.cbAllocator->Allocate(slot,
							&renderContext.lightCb, sizeof(LightCB));
					}

					if (b2 != 0) renderContext.CommandList->SetGraphicsRootConstantBufferView(2, b2);
				}

				// 反射パスは ReflectionCaster が付いたEntityだけを描く
				if (filter == DrawFilter::REFLECTION &&
					(!world.HasComponent<ReflectionCasterComponent>(entity) ||
						!world.GetComponent<ReflectionCasterComponent>(entity).enabled))
				{
					return;
				}

				// --- b4(root 5): ボーン行列パレット + morphActiveフラグ ---
				if (renderContext.cbAllocator)
				{
					const UINT slot = renderContext.frameIndex % RTV_NUM;
					BoneCB cb{};   // 全ゼロ初期化(morph=0含む)

					bool anyMorph = false;
					if (world.HasComponent<AnimatorComponent>(entity))
					{
						auto& an = world.GetComponent<AnimatorComponent>(entity);
						// MAX_BONES を超えると超過ぶんが単位行列になり、そのボーンに
						// 割り当てられた頂点が原点方向へ引き伸ばされる（指などが尖る）
						if (an.palette.size() > MAX_BONES)
						{
							static bool warned = false;
							if (!warned)
							{
								warned = true;
								LOG->LogError("ボーン数が MAX_BONES(" + std::to_string(MAX_BONES)
									+ ") を超えています: " + std::to_string(an.palette.size())
									+ " 超過ぶんは単位行列になりメッシュが破綻します");
							}
						}

						const size_t n = std::min<size_t>(an.palette.size(), MAX_BONES);
						for (size_t i = 0; i < n; ++i) cb.boneMatrices[i] = an.palette[i];
						for (size_t i = n; i < MAX_BONES; ++i)
							DirectX::XMStoreFloat4x4(&cb.boneMatrices[i], DirectX::XMMatrixIdentity());

						for (float w : an.morphWeights)
							if (fabsf(w) > 1e-6f) { anyMorph = true; break; }
					}
					else
					{
						// アニメ無し: 全ボーンidentity(スキンされても原点維持)
						for (size_t i = 0; i < MAX_BONES; ++i)
							DirectX::XMStoreFloat4x4(&cb.boneMatrices[i], DirectX::XMMatrixIdentity());
					}
					cb.morph = anyMorph ? 1.0f : 0.0f;

					auto b4 = renderContext.cbAllocator->Allocate(slot, &cb, sizeof(BoneCB));
					if (b4) renderContext.CommandList->SetGraphicsRootConstantBufferView(5, b4);

					// --- t7(root 6): 頂点モーフ (アニメあり かつ モーフがアクティブな時だけ) ---
					D3D12_GPU_VIRTUAL_ADDRESS morphVA = s_zeroMorphVA;   // 既定はゼロバッファ

					if (anyMorph && world.HasComponent<AnimatorComponent>(entity))
					{
						auto& an = world.GetComponent<AnimatorComponent>(entity);
						const size_t vcount = mesh.mesh->GetVertexCount();
						if (an.morphDirty) { RebuildMorphOffsets(an.morphs, an.morphWeights, vcount, an.morphoffsets); an.morphDirty = false; }
						if (an.morphoffsets.size() == vcount)
						{
							auto va = renderContext.cbAllocator->Allocate(slot, an.morphoffsets.data(),
								an.morphoffsets.size() * sizeof(DirectX::XMFLOAT3));
							if (va) morphVA = va;   // モーフありなら実データで上書き
						}
					}
					renderContext.CommandList->SetGraphicsRootShaderResourceView(6, morphVA);
				}

				const bool multi =
					!material.materials.empty() && mesh.mesh->GetSubMeshCount() > 0;

				// --- 通常描画(全マテリアル・無条件) ---
				if (multi)
				{
					const UINT sub = mesh.mesh->GetSubMeshCount();
					for (UINT s = 0; s < sub; ++s)
					{
						UINT mi = mesh.mesh->GetSubMeshMaterialIndex(s);
						if (mi >= material.materials.size()) mi = 0;
						auto& mat = material.materials[mi];
						if (!mat) continue;

						// サブマテリアル側が空ならコンポーネントの値を継承
						const std::string& sn = mat->shaderName.empty()
							? material.shaderName : mat->shaderName;

						const bool isTransparent = APP->IsShaderAlphaBlend(sn);
						const bool isToon = IsToonShader(sn);
						if (filter == DrawFilter::OPAQUE_NOTOON && (isTransparent || isToon))  continue;
						if (filter == DrawFilter::OPAQUE_TOON && (isTransparent || !isToon)) continue;
						if (filter == DrawFilter::OPAQUEONLY && isTransparent)		 continue; // このエンティティskip
						if (filter == DrawFilter::TRANSPARENTONLY && !isTransparent) continue;

						mat->Apply(renderContext.CommandList, transform.world,
							renderContext.view, renderContext.projection,
							renderContext.wireframe, renderContext.frameIndex,
							renderContext.cbAllocator, sn);
						mesh.mesh->DrawSubMesh(renderContext.CommandList, s);
					}
				}
				else
				{
					// 単一マテリアルもフィルタに従う。見ていないと不透明パスと
					// 半透明パスの両方で描かれてしまう
					const bool isTransparent = APP->IsShaderAlphaBlend(material.shaderName);
					const bool isToon = IsToonShader(material.shaderName);
					const bool skip =
						(filter == DrawFilter::OPAQUEONLY && isTransparent) ||
						(filter == DrawFilter::TRANSPARENTONLY && !isTransparent) ||
						(filter == DrawFilter::OPAQUE_NOTOON && (isTransparent || isToon)) ||
						(filter == DrawFilter::OPAQUE_TOON && (isTransparent || !isToon));

					if (!skip)
					{
						material.material->Apply(renderContext.CommandList, transform.world,
							renderContext.view, renderContext.projection,
							renderContext.wireframe, renderContext.frameIndex,
							renderContext.cbAllocator, material.shaderName);
						mesh.mesh->Draw(renderContext.CommandList);
					}
				}

				// --- アウトラインパス(Genshin_Toonのみ・通常描画の後) ---
				// トゥーン系はアウトラインを描く(SkinnedToon = MMDキャラ)
				const bool wantsOutline =
					(material.shaderName == "Genshin_Toon" || material.shaderName == "SkinnedToon") &&
					filter != DrawFilter::OPAQUE_NOTOON &&
					filter != DrawFilter::TRANSPARENTONLY &&
					filter != DrawFilter::REFLECTION;
				if (wantsOutline && !renderContext.wireframe)
				{
					std::string outlineShaderName = "Genshin_Outline";
					ID3D12PipelineState* outlinePso = APP->GetPipelineStateByName(outlineShaderName);
					if (outlinePso)
					{
						// アウトラインはジオメトリをもう一周ぶん投げるので単独で測る
						GPU_PROFILE_SCOPE(renderContext.CommandList, "Draw/Outline");

						if (multi)
						{
							const UINT sub = mesh.mesh->GetSubMeshCount();
							for (UINT s = 0; s < sub; ++s)
							{
								UINT mi = mesh.mesh->GetSubMeshMaterialIndex(s);
								if (mi >= material.materials.size()) mi = 0;
								auto& mat = material.materials[mi];
								if (!mat) continue;

								// 幅0なら押し出し量が0で何も出ない。描くだけ無駄なので省く
								if (mat->outlineWidth <= 0.0f) continue;

								const bool isTransparent = APP->IsShaderAlphaBlend(material.shaderName);
								if (filter == DrawFilter::OPAQUEONLY && isTransparent)		 continue; // このエンティティskip
								if (filter == DrawFilter::TRANSPARENTONLY && !isTransparent) continue;

								mat->Apply(renderContext.CommandList, transform.world,
									renderContext.view, renderContext.projection,
									false, renderContext.frameIndex,
									renderContext.cbAllocator, "", outlinePso);
								mesh.mesh->DrawSubMesh(renderContext.CommandList, s);
							}
						}
						else if (material.material->outlineWidth > 0.0f)
						{
							material.material->Apply(renderContext.CommandList, transform.world,
								renderContext.view, renderContext.projection,
								false, renderContext.frameIndex,
								renderContext.cbAllocator, "", outlinePso); 
							mesh.mesh->Draw(renderContext.CommandList);
						}
					}
				}
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

		// 環境光をシーンの灯りの色へ寄せるための集計
		float3 tintSum{ 0.0f, 0.0f, 0.0f };
		float  tintWeight = 0.0f;
		float  ambientBlend = 0.0f;

		// 影を落とすライト(先着1つ)。方向ライトでもスポットでもよい
		int   shadowIndex = -1;
		LightComponent::LightType shadowType = LightComponent::LightType::Directional;
		float shadowAngle = 45.0f;
		float shadowRange = 10.0f;
		float3 shadowDir{};
		float3 shadowPos{};

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
					auto fwd = DirectX::XMVector3Rotate(
						DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot);
					//DirectX::XMStoreFloat4(&dst.dir, DirectX::XMVector3Normalize(fwd));

					if (light.swingEnable)
					{
						const float dt = TIME->GetTotalTime();
						const float panRad = DirectX::XMConvertToRadians(light.swingAngle) * 
							sinf(DirectX::XMConvertToRadians(light.swingSpeed) * dt);

						if (light.type != LightComponent::LightType::Point)
						{
							if (light.swingAxis == LightComponent::SwingAxis::Tilt)
							{
								const auto right = DirectX::XMVector3Rotate(
									DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rot);
								fwd = DirectX::XMVector3Rotate(fwd, DirectX::XMQuaternionRotationAxis(right, panRad));
							}
							else
							{
								const auto up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), rot);
								fwd = DirectX::XMVector3Rotate(fwd, DirectX::XMQuaternionRotationAxis(up, panRad));

								if (light.swingAxis == LightComponent::SwingAxis::PanTilt)
								{
									const auto right = DirectX::XMVector3Rotate(DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rot);
									const float tiltRad = DirectX::XMConvertToRadians(light.swingAngle * 0.5f) * 
										sinf(DirectX::XMConvertToRadians(light.swingSpeed) * 0.7f * dt + 1.5f);
									fwd = DirectX::XMVector3Rotate(fwd, DirectX::XMQuaternionRotationAxis(right, tiltRad));
								}
							}
						}
					}

					fwd = DirectX::XMVector3Normalize(fwd);

					// 不正な向き(NaN/Inf/ゼロ)はシェーダ側でNaNになり画面が黒く落ちるので弾く
					{
						float3 chk;
						DirectX::XMStoreFloat3(&chk, fwd);
						if (!std::isfinite(chk.x) || !std::isfinite(chk.y) || !std::isfinite(chk.z) ||
							(chk.x == 0.0f && chk.y == 0.0f && chk.z == 0.0f))
						{
							fwd = DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
						}
					}

					DirectX::XMStoreFloat4(&dst.dir, fwd);

					// ギズモ表示用に書き戻す
					DirectX::XMStoreFloat3(&light.direction, fwd);
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
				dst.param.z = light.beamWidth;
				dst.param.w = light.volumetricIntensity;

				// 環境光の色付け用に、点いている灯りの色を明るさで重み付けして集める
				{
					const float w = dst.color.x * 0.299f + dst.color.y * 0.587f
						+ dst.color.z * 0.114f;
					if (w > 0.0f)
					{
						tintSum.x += dst.color.x * w;
						tintSum.y += dst.color.y * w;
						tintSum.z += dst.color.z * w;
						tintWeight += w;
					}
				}

				// 環境光は最初のライトのものを採用
				if (count == 0)
				{
					ambientBlend = std::clamp(light.ambientFromLights, 0.0f, 1.0f);
					if (APP->HasEnvironment())
					{
						const float3 e = APP->GetEnvAmbient();
						const float k = 0.5f;   // 環境光の強さ（好みで調整）
						m_Data.ambientColor = float4(e.x * k, e.y * k, e.z * k, 1.0f);
					}
					else
					{
						m_Data.ambientColor = light.ambientColor;
					}
				}

				// 影の担当を決める。CastShadows を切れば次のライトへ回る
				if (shadowIndex < 0 && light.castShadows &&
					(light.type == LightComponent::LightType::Directional ||
						light.type == LightComponent::LightType::Spot))
				{
					shadowIndex = static_cast<int>(count);
					shadowType = light.type;
					shadowAngle = light.spotAngle;
					shadowRange = light.range;
					shadowPos = float3{ dst.posRange.x, dst.posRange.y, dst.posRange.z };
					shadowDir = float3{ dst.dir.x, dst.dir.y, dst.dir.z };
				}

				++count;
			});

		// ----------------------------- //
		//		シャドウマッピング	     //
		// ----------------------------- //
		m_Data.shadowParams = { 0.005f, 0.0f, 2048.0f, 0.0f };
		const bool found = (shadowIndex >= 0);

		if (found)
		{
			// 方向がゼロ/不正ならデフォルトへ（NaN行列防止）
			DirectX::XMVECTOR d = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&shadowDir));
			const float dist = 50.0f;
			const float ortho = 30.0f;
			const float mapSize = m_Data.shadowParams.z;   // 2048

			// --- カメラ追従：エディタ(FreeLook)カメラ優先、無ければ通常カメラ ---
			float3 camPos{ 0,0,0 }, camFwd{ 0,0,1 };
			bool got = false, gotEditor = false;
			world.Each<TransformComponent, CameraComponent>(
				[&](Entity e, TransformComponent& tr, CameraComponent&)
				{
					if (gotEditor) return;
					const bool editor = world.HasComponent<FreeLookComponent>(e);
					if (!got || editor)
					{
						camPos = tr.position;
						auto q = DirectX::XMLoadFloat4(&tr.rotation);
						auto fwd = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
						DirectX::XMStoreFloat3(&camFwd, fwd);
						got = true; gotEditor = editor;
					}
				});

			// カメラの少し前方を影の中心に（Yは0=地面基準）
			float3 focus = {
				camPos.x + camFwd.x * (ortho * 0.4f),
				0.0f,
				camPos.z + camFwd.z * (ortho * 0.4f)
			};

			// テクセル単位にスナップ（カメラ移動時の影のシマー防止）
			const float texelWorld = ortho / mapSize;
			focus.x = floorf(focus.x / texelWorld) * texelWorld;
			focus.z = floorf(focus.z / texelWorld) * texelWorld;

			DirectX::XMVECTOR center = DirectX::XMLoadFloat3(&focus);
			DirectX::XMVECTOR lightPos = DirectX::XMVectorSubtract(center, DirectX::XMVectorScale(d, dist));
			DirectX::XMVECTOR up = (fabsf(shadowDir.y) > 0.99f)
				? DirectX::XMVectorSet(1, 0, 0, 0) : DirectX::XMVectorSet(0, 1, 0, 0);

			// ライト視点のビュー×プロジェクション。
			// 他の行列と同じく転置して渡す(シェーダーは mul(頂点, 行列) の順)
			DirectX::XMMATRIX lightView;
			DirectX::XMMATRIX lightProj;

			if (shadowType == LightComponent::LightType::Spot)
			{
				// スポットは自分の位置から円錐方向を透視投影で撮る。
				// 平行投影だと円錐の広がりが再現できず、影の形が合わない
				const float far_ = std::max(shadowRange, 2.0f);
				const DirectX::XMVECTOR sp = DirectX::XMLoadFloat3(&shadowPos);
				const DirectX::XMVECTOR target =
					DirectX::XMVectorAdd(sp, DirectX::XMVectorScale(d, far_));

				lightView = DirectX::XMMatrixLookAtLH(sp, target, up);

				// 円錐の外周まで入るよう、スポット角そのものを画角にする
				const float fov = DirectX::XMConvertToRadians(
					std::min(std::max(shadowAngle, 5.0f), 170.0f));
				lightProj = DirectX::XMMatrixPerspectiveFovLH(fov, 1.0f, 0.5f, far_);
			}
			else
			{
				lightView = DirectX::XMMatrixLookAtLH(lightPos, center, up);
				lightProj = DirectX::XMMatrixOrthographicLH(ortho, ortho, 1.0f, dist * 2.0f);
			}

			DirectX::XMStoreFloat4x4(&m_Data.lightviewproj,
				DirectX::XMMatrixTranspose(lightView * lightProj));

			m_Data.shadowParams.y = 1.0f;                          // 影を有効化
			m_Data.shadowParams.w = static_cast<float>(shadowIndex); // どのライトが落とすか
		}

		// --- 環境光をそのときの灯りの色へ寄せる ---
		// 明るさは ambientColor のまま保ち、色味だけ差し替える。
		// これでライトの色がセクションで変わると、キャラの影側とフォグも一緒に動く
		if (tintWeight > 0.0f && ambientBlend > 0.0f)
		{
			float3 tint{ tintSum.x / tintWeight, tintSum.y / tintWeight,
						 tintSum.z / tintWeight };

			const float tintLuma = tint.x * 0.299f + tint.y * 0.587f + tint.z * 0.114f;
			if (tintLuma > 1e-4f)
			{
				// 輝度で割って色味だけ取り出す(明るさは環境光側の値を尊重する)
				tint.x /= tintLuma; tint.y /= tintLuma; tint.z /= tintLuma;

				auto& a = m_Data.ambientColor;
				a.x = std::lerp(a.x, a.x * tint.x, ambientBlend);
				a.y = std::lerp(a.y, a.y * tint.y, ambientBlend);
				a.z = std::lerp(a.z, a.z * tint.z, ambientBlend);
			}
		}

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
				{
					if (!b) continue;
					b->SyncEnableState();
					b->OnStart();
				}
			});
	}

	void Update(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b) continue;
					// enabled の切り替わりはここで拾う。
					// 無効側でも呼ぶのは OnDisable を落とさないため
					b->SyncEnableState();
					if (!b->enabled) continue;
					b->TickInvokes(deltatime);   // 予約された処理を先に消化する
					b->OnUpdate(deltatime);
				}
			});
	}

	void FixedUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->OnFixedUpdate(deltatime);
				}
			});
	}

	void LateUpdate(World& world, float deltatime)
	{
		world.Each<ScriptComponent>([deltatime](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->OnLateUpdate(deltatime);
				}
			});
	}

	void Draw(World& world, const RenderContext& context)
	{
		world.Each<ScriptComponent>([&context](Entity, ScriptComponent& sc)
			{
				for (auto& b : sc.behaviors)
				{
					if (!b || !b->enabled) continue;
					b->OnDraw(context);
				}
			});
	}

	/// @brief 破棄予約されている Entity の OnDestroy を呼ぶ
	/// @note World::FlushDestroyQueue の直前に呼ぶこと。
	///       実際に消えたあとでは behaviors ごと無くなっている
	void NotifyPendingDestroy(World& world)
	{
		if (!world.HasPendingDestroy()) return;

		world.Each<ScriptComponent>([&world](Entity e, ScriptComponent& sc)
			{
				if (world.IsPendingDestroy(e) == false) return;
				for (auto& b : sc.behaviors)
				{
					if (!b) continue;
					b->OnDestroy();
				}
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
		// ビューポート上で右クリック中のみカメラ操作＆カーソルロック
		const bool active = INPUT->MouseInput.Right().IsPressed()
			&& INPUT->IsViewportHovered();

		if (active && !m_CursorHidden)
		{
			INPUT->SetCursorLock(true);
			INPUT->ShowCursor(false);
			m_CursorHidden = true;
		}
		else if (!active && m_CursorHidden)
		{
			INPUT->SetCursorLock(false);
			INPUT->ShowCursor(true);
			m_CursorHidden = false;
		}

		world.Each<TransformComponent, FreeLookComponent>(
			[&](Entity entity, TransformComponent& tr, FreeLookComponent& fl)
			{
				// FreeLookが有効でない場合処理しない
				if (!fl.Enabled) return;

				// このEntityのカメラ種別が対象でなければスキップ
				if (!world.HasComponent<CameraComponent>(entity)) return;
				if (world.GetComponent<CameraComponent>(entity).cameraType != targetType) return;

				if (!active) return;

				// 回転
				fl.yaw += (float)INPUT->MouseInput.DeltaX() * fl.rotateSpeed;
				fl.pitch += (float)INPUT->MouseInput.DeltaY() * fl.rotateSpeed;
				fl.pitch = std::clamp(fl.pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);

				vector q = DirectX::XMQuaternionRotationRollPitchYaw(fl.pitch, fl.yaw, 0.0f);
				DirectX::XMStoreFloat4(&tr.rotation, DirectX::XMQuaternionNormalize(q));

				// 移動（WASD）
				float speed = 1.0f;
				vector move = DirectX::XMVectorZero();
				if (INPUT->Key.Shift().IsPressed()) speed *= 5.0f;
				if (INPUT->Key.W().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0, 0, speed, 0));
				if (INPUT->Key.S().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(0, 0, -speed, 0));
				if (INPUT->Key.A().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(-speed, 0, 0, 0));
				if (INPUT->Key.D().IsPressed()) move = DirectX::XMVectorAdd(move, DirectX::XMVectorSet(speed, 0, 0, 0));

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
private:
	bool m_CursorHidden = false;
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

			DirectX::XMMATRIX w = DirectX::XMLoadFloat4x4(&tr.world);

			// スケール除去
			DirectX::XMVECTOR s, q, t;
			if(!DirectX::XMMatrixDecompose(&s, &q, &t, w))
			{
				// 失敗時はローカル値へ
				q = DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&tr.rotation));
				t = DirectX::XMLoadFloat3(&tr.position);
			}

			vector forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
			vector up = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), q);
			DirectX::XMStoreFloat4x4(&camera.view, DirectX::XMMatrixLookToLH(t, forward, up));

			matrix p = (camera.projection == CameraComponent::Projection::Perspective)
				? DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(camera.fovY), aspect, camera.nearZ, camera.farZ)
				: DirectX::XMMatrixOrthographicLH(camera.orhoSize * aspect, camera.orhoSize, camera.nearZ, camera.farZ);
			DirectX::XMStoreFloat4x4(&camera.proj, p);
			});
	}
};

class CameraAnimationSystem
{
public:
	void Update(World& world, float deltatime, bool isPlaying)
	{
		world.Each<TransformComponent, CameraComponent, CameraAnimationComponent>(
			[&](Entity, TransformComponent& tr, CameraComponent& cam, CameraAnimationComponent& anim)
			{
				if (!anim.loaded && !anim.vmdPath.empty())
				{
					anim.clip = ModelLoader::LoadVMDCameraClip(anim.vmdPath);
					anim.loaded = true;
				}
				if (!anim.playing || !isPlaying || anim.clip.keys.empty()) return;

				anim.time += deltatime;
				if (anim.time > anim.clip.duration)
					anim.time = anim.loop ? std::fmod(anim.time, anim.clip.duration) : anim.clip.duration;

				// 前後キーを検索して線形補間
				const auto& keys = anim.clip.keys;
				auto it = std::lower_bound(keys.begin(), keys.end(), anim.time,
					[](const CameraKeyFrame& k, float t) { return k.time < t; });
				CameraKeyFrame k;
				if (it == keys.begin())      k = keys.front();
				else if (it == keys.end())   k = keys.back();
				else
				{
					const auto& k1 = *it; const auto& k0 = *(it - 1);
					// MMDのカット切替(同フレーム or 1フレーム差)は補間しない
					const float span = k1.time - k0.time;
					float t = (span <= 1.0f / 30.0f + 1e-4f) ? 0.0f
						: (anim.time - k0.time) / span;
					k.distance = std::lerp(k0.distance, k1.distance, t);
					k.target = { std::lerp(k0.target.x, k1.target.x, t),
								   std::lerp(k0.target.y, k1.target.y, t),
								   std::lerp(k0.target.z, k1.target.z, t) };
					k.rotation = { std::lerp(k0.rotation.x, k1.rotation.x, t),
								   std::lerp(k0.rotation.y, k1.rotation.y, t),
								   std::lerp(k0.rotation.z, k1.rotation.z, t) };
					k.fovY = std::lerp(k0.fovY, k1.fovY, t);
				}

				using namespace DirectX;
				XMVECTOR q = XMQuaternionRotationRollPitchYaw(-k.rotation.x, k.rotation.y, k.rotation.z);
				XMVECTOR fwd = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
				XMVECTOR eye = XMVectorAdd(XMLoadFloat3((XMFLOAT3*)&k.target),
					XMVectorScale(fwd, k.distance));

				XMStoreFloat3((XMFLOAT3*)&tr.position, eye);
				XMStoreFloat4((XMFLOAT4*)&tr.rotation, q);   // Transformの回転がクォータニオンの場合
				cam.fovY = k.fovY;
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

/// @brief UIボタンのホバー / クリック判定
/// @note CanvasRenderSystem と同じ矩形の求め方をしている。
///       あちらを変えたらこちらも合わせること
class UIButtonSystem
{
public:
	/// @param screenW / screenH 描画に使っているスクリーンサイズ
	/// @param mouseX / mouseY ビューポート左上を原点としたマウス座標
	/// @param enabled 押下を受け付けるか(エディタでゲーム画面が非表示のときなど)
	void Update(
		_In_ World& world,
		_In_ float screenW,
		_In_ float screenH,
		_In_ float mouseX,
		_In_ float mouseY,
		_In_ bool enabled)
	{
		(void)screenW;
		(void)screenH;

		const bool down = enabled && INPUT->GetMouseButtonDown(0);
		const bool held = enabled && INPUT->GetMouseButton(0);
		const bool up = enabled && INPUT->GetMouseButtonUp(0);

		// クリック確定は走査の外で呼ぶ。
		// onClick がエンティティを増減させると Each が壊れるため
		std::vector<std::function<void()>> fired;

		world.Each<RectTransformComponent, UIButtonComponent>(
			[&](Entity e, RectTransformComponent& rt, UIButtonComponent& btn)
			{
				if (!btn.interactable)
				{
					btn.isHovered = false;
					btn.isPressed = false;
					return;
				}

				// CanvasRenderSystem と同じ: AnchoredPosition が左上、SizeDelta が大きさ
				const float x0 = rt.AnchoredPosition.x;
				const float y0 = rt.AnchoredPosition.y;
				const float x1 = x0 + rt.SizeDelta.x;
				const float y1 = y0 + rt.SizeDelta.y;

				const bool inside =
					enabled &&
					mouseX >= x0 && mouseX <= x1 &&
					mouseY >= y0 && mouseY <= y1;

				btn.isHovered = inside;

				// 状態色を UIImage へ渡す。基準色(color)は残したまま掛け合わせる
				if (world.HasComponent<UIImageComponent>(e))
				{
					auto& img = world.GetComponent<UIImageComponent>(e);
					const COLOR st = StateColor(btn);
					img.runtimeColor = COLOR{
						img.color.x * st.x, img.color.y * st.y,
						img.color.z * st.z, img.color.w * st.w };
				}

				if (inside && down)
				{
					btn.isPressed = true;
				}
				else if (!held)
				{
					// 押したまま外へ出て離した場合はクリック扱いにしない
					if (btn.isPressed && inside && up && btn.onClick)
					{
						fired.push_back(btn.onClick);
					}
					btn.isPressed = false;
				}
			});

		for (auto& fn : fired)
		{
			fn();
		}
	}

	/// @brief 状態に応じた色を返す(UIImage の色に掛ける)
	static COLOR StateColor(const UIButtonComponent& btn)
	{
		if (!btn.interactable) return btn.disabledColor;
		if (btn.isPressed)     return btn.pressedColor;
		if (btn.isHovered)     return btn.hoverColor;
		return btn.normalColor;
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
				// マテリアルの遅延生成。
				// テクスチャが無くても Init() の既定(2x2白)を色で塗って板として使う
				if (!img.material)
				{
					auto mat = std::make_shared<Material>();
					mat->Init();
					if (!img.texturePath.empty())
					{
						mat->SetTextureFromFile(Utf8ToWide(img.texturePath));
					}
					img.material = mat;
					img.uploadedColor = COLOR{ -1.0f, -1.0f, -1.0f, -1.0f };   // 次で必ず塗る
				}

				if (!img.material) return;

				// 単色運用のときだけ塗り替える。画像がある場合は色を掛けられない
				if (img.texturePath.empty())
				{
					// UIButton が無いエンティティは runtimeColor が更新されないので color を使う
					const COLOR& want = world.HasComponent<UIButtonComponent>(e)
						? img.runtimeColor : img.color;

					if (want.x != img.uploadedColor.x || want.y != img.uploadedColor.y ||
						want.z != img.uploadedColor.z || want.w != img.uploadedColor.w)
					{
						img.material->SetSolidColor(want);
						img.uploadedColor = want;
					}
				}

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
	/// @param isPlaying 再生中か
	/// @param isPaused ポーズ中か
	/// @note 停止(EDITOR復帰)は音を捨てるが、ポーズは位置を保って止めるだけ。
	///       ライブ中に ESC で止めたとき曲が頭に戻らないようにするため
	void Update(World& world, bool isPlaying, bool isPaused = false)
	{
		//static int f = 0;
		//if ((f++ % 60) == 0)
		//{
		//	int srcCount = 0, listenerCount = 0;
		//	world.Each<AudioSourceComponent>([&](Entity, AudioSourceComponent&) { srcCount++; });
		//	world.Each<AudioListenerComponent>([&](Entity, AudioListenerComponent&) { listenerCount++; });

		//	char b[256];
		//	sprintf_s(b, "[Audio] Update playing=%d sources=%d listeners=%d\n",
		//		(int)isPlaying, srcCount, listenerCount);
		//	OutputDebugStringA(b);
		//}

		// ポーズ中は「再生していない」が「停止でもない」。
		// 停止と同じ扱いにすると曲が捨てられて頭から鳴り直しになる
		const bool justStarted = (isPlaying && !m_PrevPlaying && !m_PrevPaused);
		const bool justStopped = (!isPlaying && !isPaused && m_PrevPlaying);
		const bool justPaused = (isPaused && !m_PrevPaused);
		const bool justResumed = (!isPaused && m_PrevPaused);

		m_PrevPlaying = isPlaying;
		m_PrevPaused = isPaused;

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
					// 絶対パスで保存されたシーンでも Assets の中なら拾えるようにする
					src.clip = AudioEngine::Get().Load(ResolveAssetPath(src.clipPath));
					if (src.clip)
						src.voice = AudioEngine::Get().CreateVoice(src.clip->format);
				}
				if (!src.voice || !src.clip) return;

				// ---- playOnStart / 再生・停止（前回と同じ） ---- //
				if (justStarted && src.playOnStart) src.playRequested = true;
				if (justStopped) src.stopRequested = true;
				if (justPaused)  src.pauseRequested = true;
				if (justResumed) src.resumeRequested = true;

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

				// ---- シーク(PlayBeginでバッファを投げ直す) ---- //
				if (src.seekRequested)
				{
					src.seekRequested = false;

					const WAVEFORMATEX& fmt = src.clip->format;
					const UINT32 blockAlign = (fmt.nBlockAlign > 0) ? fmt.nBlockAlign : 1;
					const UINT32 totalSamples = (UINT32)(src.clip->data.size() / blockAlign);

					UINT32 sampleOffset = (UINT32)(std::max(0.0f, src.seekSeconds) * fmt.nSamplesPerSec);
					if (totalSamples == 0) sampleOffset = 0;
					else if (sampleOffset >= totalSamples) sampleOffset = totalSamples - 1;

					src.voice->Stop();
					src.voice->FlushSourceBuffers();

					XAUDIO2_BUFFER buf{};
					buf.AudioBytes = (UINT32)src.clip->data.size();
					buf.pAudioData = src.clip->data.data();
					buf.Flags = XAUDIO2_END_OF_STREAM;
					buf.LoopCount = src.loop ? XAUDIO2_LOOP_INFINITE : 0;
					buf.PlayBegin = sampleOffset;   // ここから再生
					src.voice->SubmitSourceBuffer(&buf);
					src.voice->Start();
				}

				// ---- 一時停止 / 再開(位置は保持される) ---- //
				if (src.pauseRequested) { src.pauseRequested = false; src.voice->Stop(); }
				if (src.resumeRequested) { src.resumeRequested = false; src.voice->Start(); }

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
	bool m_PrevPaused = false;
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
		// 親が自分自身でなく、実在する場合のみ
		if (tr.parent != INVALID_ENTITY && tr.parent != e &&
			world.HasComponent<TransformComponent>(tr.parent))
		{
			auto& p = world.GetComponent<TransformComponent>(tr.parent);
			UpdateWorld(world, tr.parent, p);
			worldMat = local * XMLoadFloat4x4(&p.world);
		}
		XMStoreFloat4x4(&tr.world, worldMat);
	}
};

class ShadowSystem
{
public:
	void Draw(
		World& world, 
		const RenderContext& ctx,
		ID3D12PipelineState* shadowPso)
	{
		if(!shadowPso || !ctx.CommandList || !ctx.cbAllocator)
		{
			return;
		}
		auto* cmd = ctx.CommandList;
		cmd->SetPipelineState(shadowPso);
		cmd->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		const UINT slot = ctx.frameIndex % RTV_NUM;

		auto b2 = ctx.cbAllocator->Allocate(slot, &ctx.lightCb, sizeof(LightCB));
		if (b2)
		{
			cmd->SetGraphicsRootConstantBufferView(2, b2);
		}

		world.Each<TransformComponent, MeshComponent>(
			[&](Entity e, TransformComponent& tr, MeshComponent& mc)
			{
				if (!mc.mesh) return;

				// スキンメッシュ用の骨パレット。渡さないとキャラの影が
				// バインドポーズのまま固まる
				{
					BoneCB bone{};
					const size_t n = world.HasComponent<AnimatorComponent>(e)
						? std::min<size_t>(world.GetComponent<AnimatorComponent>(e).palette.size(), MAX_BONES)
						: 0;
					if (n > 0)
					{
						const auto& palette = world.GetComponent<AnimatorComponent>(e).palette;
						for (size_t i = 0; i < n; ++i) bone.boneMatrices[i] = palette[i];
					}
					for (size_t i = n; i < MAX_BONES; ++i)
						DirectX::XMStoreFloat4x4(&bone.boneMatrices[i], DirectX::XMMatrixIdentity());

					auto b4 = ctx.cbAllocator->Allocate(slot, &bone, sizeof(BoneCB));
					if (b4) cmd->SetGraphicsRootConstantBufferView(5, b4);
				}

				struct { float4x4 world; } obj{};
				const auto w = DirectX::XMLoadFloat4x4(&tr.world);
				DirectX::XMStoreFloat4x4(&obj.world, DirectX::XMMatrixTranspose(w));

				auto b1 = ctx.cbAllocator->Allocate(slot, &obj, sizeof(obj));
				if (b1) cmd->SetGraphicsRootConstantBufferView(1, b1);
				mc.mesh->Draw(cmd);
			});
	}
};

class AnimatorSystem
{
public:
	void Update(World& world, float dt)
	{
		world.Each<AnimatorComponent>([&](Entity e, AnimatorComponent& an)
			{
				using clk = std::chrono::high_resolution_clock;
				auto t0 = clk::now();

				// Play/Stop で退避したクリップがあれば、読み直さずに拾い直す
				if (!an.clipsRestored && !an.skeleton.nodes.empty() &&
					world.HasComponent<NameComponent>(e))
				{
					auto& cache = AnimatorClipCache();
					const auto it = cache.find(world.GetComponent<NameComponent>(e).name);
					if (it != cache.end() && !it->second.clips.empty())
					{
						an.clips = std::move(it->second.clips);
						an.currentClip = it->second.currentClip;
						an.currentClipName = it->second.currentClipName;
						an.time = it->second.time;
						an.playing = it->second.playing;
						an.clipsRestored = true;   // 非同期ロードで二重に積まない
						an.physicsResetRequest = true;
						cache.erase(it);
					}
				}

				// 保存されたVMDパスからクリップを復元(スケルトン準備後に1回だけ)
				if (!an.clipsRestored && !an.clipPathsStr.empty() &&
					!an.skeleton.nodes.empty())
				{
					an.clipsRestored = true;
					std::stringstream ss(an.clipPathsStr);
					std::string path;
					std::vector<std::string> seen;   // 重複したパスは1回だけ読む
					while (std::getline(ss, path, '|'))
					{
						if (path.empty()) continue;
						if (std::find(seen.begin(), seen.end(), path) != seen.end()) continue;
						seen.push_back(path);
						AsyncLoader::Get().LoadVMDAsync(path, an.skeleton,
							[&world, e](AnimationClip vc)
							{
								if (vc.channels.empty() && vc.morphChannels.empty()) return;
								if (!world.IsEntityAlive(e) ||
									!world.HasComponent<AnimatorComponent>(e)) return;
								auto& a = world.GetComponent<AnimatorComponent>(e);
								a.clips.push_back(std::move(vc));
							});
					}
				}

				// ---- モーフオフセットの確定 ---- //
				// 以前は RenderSystem::Draw の中で再計算していたが、
				// 描画から World を書き換えることになり Game/Render を分けられない。
				// クリップが無くてもモーフだけ動かすケースがあるので、
				// 下の early return より前で処理する
				if (an.morphDirty)
				{
					size_t vcount = 0;
					if (world.HasComponent<MeshComponent>(e))
					{
						const auto& mc = world.GetComponent<MeshComponent>(e);
						if (mc.mesh) vcount = mc.mesh->GetVertexCount();
					}

					if (vcount > 0)
					{
						RebuildMorphOffsets(an.morphs, an.morphWeights, vcount, an.morphoffsets);
					}
					an.morphDirty = false;
				}

				if (an.clips.empty()) return;

				// 保存された名前を正として添字を引き直す。
				// クリップは非同期に届くので、並び順は毎回同じとは限らない
				if (!an.currentClipName.empty() &&
					(an.currentClip < 0 || an.currentClip >= (int)an.clips.size() ||
						an.clips[an.currentClip].name != an.currentClipName))
				{
					for (int i = 0; i < (int)an.clips.size(); ++i)
					{
						if (an.clips[i].name == an.currentClipName)
						{
							an.currentClip = i;
							break;
						}
					}
				}

				if (an.currentClip < 0 || an.currentClip >= (int)an.clips.size()) return;
				const AnimationClip& clip = an.clips[an.currentClip];
				if (an.playing && clip.duration > 0.0f)
				{
					an.time += dt * an.speed;
					if (an.loop)
					{
						an.time = fmodf(an.time, clip.duration);
						if (an.time < 0.0f) an.time += clip.duration;
					}
					else if (an.time >= clip.duration)
					{
						an.time = clip.duration;
						an.playing = false;
					}
				}

				MmdPhysics* phys = nullptr;
				if (world.HasComponent<MmdPhysicsComponent>(e))
					phys = world.GetComponent<MmdPhysicsComponent>(e).impl.get();

				// ---- 揺れもの(Kawaii Physics) ---- //
				KawaiiPhysics* kawaii = nullptr;
				const KawaiiPhysicsSettings* kawaiiSettings = nullptr;
				const float4x4* entityWorld = nullptr;
				if (world.HasComponent<KawaiiPhysicsComponent>(e))
				{
					auto& kp = world.GetComponent<KawaiiPhysicsComponent>(e);
					if (!kp.impl) kp.impl = std::make_shared<KawaiiPhysics>();

					// シーンから読んだ設定文字列を一度だけ展開する
					if (!kp.configRestored)
					{
						KawaiiDeserialize(kp.configStr, kp.settings);
						kp.impl->MarkDirty();
						kp.configRestored = true;
					}
					// シーク・スクラブ中は慣性を持ち込ませない
					if (an.physicsResetRequest || an.scrubbing) kp.impl->RequestResync();

					if (kp.enabled)
					{
						kawaii = kp.impl.get();
						kawaiiSettings = &kp.settings;
					}
				}
				if (world.HasComponent<TransformComponent>(e))
					entityWorld = &world.GetComponent<TransformComponent>(e).world;

				// シーク直後は剛体を現在のボーン姿勢へ再同期(爆発防止)
				if (an.physicsResetRequest)
				{
					if (phys) phys->Reset();
					an.physicsResetRequest = false;
				}

				// 揺れものは Kawaii Physics へ全面移行したので MmdPhysics は渡さない
				// スライダーをドラッグしている間はFK/IKのみ(物理を進めない)
				ComputePalette(an.skeleton, an.skinData, clip, an.time, an.palette,
					nullptr, dt,
					an.scrubbing ? nullptr : kawaii, kawaiiSettings, entityWorld);

				// 表情モーフをVMDから駆動する。
				// morphClip が有効ならそちらを使う(体と表情でVMDが別のケース)
				if (!an.morphs.morphs.empty())
				{
					const int mi = (an.morphClip >= 0 && an.morphClip < (int)an.clips.size())
						? an.morphClip : an.currentClip;

					std::vector<std::string> missing;
					if (SampleMorphWeights(an.clips[mi], an.time, an.morphs,
						an.morphWeights, &missing))
					{
						an.morphDirty = true;
					}

					// 解決できなかったモーフ名は一度だけ報告する
					if (!missing.empty())
					{
						static std::unordered_set<std::string> warned;
						for (const auto& n : missing)
						{
							if (warned.insert(n).second)
								LOG->LogInfo("morph not found in model: " + n);
						}
					}
				}

				auto t1 = clk::now();
				static int c = 0;
				if ((c++ % 60) == 0) {
					double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
					LOG->LogInfo("Anim+Physics: " + std::to_string(ms) + " ms");
				}
			});
	}
};

class ParticleSystem
{
public:
	void Update(World& world, float dt)
	{
		world.Each<TransformComponent, ParticleEmitterComponent>(
			[&](Entity e, TransformComponent& tr, ParticleEmitterComponent& pe)
			{
				float coneLen = 3.0f, coneRad = 0.3f;
				float3 origin = tr.position;
				DirectX::XMVECTOR dirV = DirectX::XMVector3Rotate(
					DirectX::XMVectorSet(0, 0, 1, 0),
					DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&tr.rotation)));

				if (pe.followLight && world.HasComponent<LightComponent>(e))
				{
					const auto& light = world.GetComponent<LightComponent>(e);
					coneLen = light.range;
					if (light.type == LightComponent::LightType::Laser)
						coneRad = light.beamWidth;
					else
						coneRad = tanf(DirectX::XMConvertToRadians(light.spotAngle * 0.5f)) * coneLen;

					dirV = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&light.direction));
				}

				float3 dir;
				DirectX::XMStoreFloat3(&dir, dirV);

				// --- 既存パーティクルの更新 ---
				for (auto& p : pe.particles)
				{
					p.age += dt;
					p.pos = p.pos + p.velocity * dt + pe.gravity * dt;
				}
				// 寿命切れを削除（swap-and-pop、再アロケーションなし）
				pe.particles.erase(
					std::remove_if(pe.particles.begin(), pe.particles.end(),
						[](const ParticleEmitterComponent::Particle& p) { return p.age >= p.life; }),
					pe.particles.end());

				// --- 新規発生 ---
				if (pe.emitting && pe.emitRate > 0.0f)
				{
					pe.spawnAccumulator += dt * pe.emitRate;
					while (pe.spawnAccumulator >= 1.0f &&
						static_cast<int>(pe.particles.size()) < pe.maxParticles)
					{
						pe.spawnAccumulator -= 1.0f;
						SpawnInCone(pe, origin, dir, coneLen, coneRad);
					}
				}
			});
	}

private:
	void SpawnInCone(ParticleEmitterComponent& pe, const float3& origin,
		const float3& dir, float len, float radius)
	{
		auto rnd = []() { return (float)rand() / RAND_MAX; };

		const float t = rnd();        
		const float rimR = radius * t;
		const float ang = rnd() * DirectX::XM_2PI;
		const float rr = sqrtf(rnd()) * rimR;

		DirectX::XMVECTOR dV = DirectX::XMLoadFloat3(&dir);
		float3 up = (fabsf(dir.y) > 0.99f) ? float3{ 1,0,0 } : float3{ 0,1,0 };
		DirectX::XMVECTOR rV = DirectX::XMVector3Normalize(
			DirectX::XMVector3Cross(dV, DirectX::XMLoadFloat3(&up)));
		DirectX::XMVECTOR uV = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(dV, rV));

		float3 r_, u_;
		DirectX::XMStoreFloat3(&r_, rV);
		DirectX::XMStoreFloat3(&u_, uV);

		ParticleEmitterComponent::Particle np{};
		np.pos = origin
			+ float3{ dir.x * len * t, dir.y * len * t, dir.z * len * t }
		+ r_ * (cosf(ang) * rr) + u_ * (sinf(ang) * rr);

		const float spd = pe.speed + (rnd() * 2.0f - 1.0f) * pe.speedVariance;

		np.velocity = float3{ dir.x * spd, dir.y * spd, dir.z * spd }
			+ r_ * ((rnd() * 2.0f - 1.0f) * pe.drift)
			+ u_ * ((rnd() * 2.0f - 1.0f) * pe.drift);

		np.life = pe.lifeTime + (rnd() * 2.0f - 1.0f) * pe.lifeTimeVariance;
		np.age = 0.0f;

		pe.particles.push_back(np);
	}
};

class MusicSyncSystem
{
public:
	void Update(World& world, bool isPlaying)
	{
		// isPlaying は見ない。音源が実際に鳴っているかどうかだけで判断するので、
		// エディタで曲を流している間も musicTime が進む
		(void)isPlaying;

		world.Each<AudioSourceComponent, MusicSyncComponent>(
			[&](Entity, AudioSourceComponent& src, MusicSyncComponent& sync)
			{
				if (!src.voice || !src.clip)
				{
					sync.started = false;
					return;
				}

				// シーク直後は再生開始点を取り直す
				if (sync.resyncRequested)
				{
					sync.resyncRequested = false;
					sync.started = false;
				}

				XAUDIO2_VOICE_STATE st{};
				src.voice->GetState(&st);

				// 再生開始の瞬間のSamplesPlayedを基準として記録
				// (voiceは過去の再生分もカウントし続けるため)
				if (!sync.started)
				{
					if (st.BuffersQueued == 0) return;  // まだ再生が始まっていない
					sync.startSamples = st.SamplesPlayed;
					sync.started = true;
				}
				if (st.BuffersQueued == 0) return;      // 再生終了

				const float rate = (float)src.clip->format.nSamplesPerSec;
				const float musicTime =
					(float)(st.SamplesPlayed - sync.startSamples) / rate
					+ sync.seekBase + sync.offset;

				sync.musicTime = musicTime;

				// 曲位置を正として時刻を上書き
				if (sync.syncAnimators)
				{
					world.Each<AnimatorComponent>(
						[&](Entity, AnimatorComponent& an)
						{
							if (an.playing) an.time = musicTime;
						});
				}
				if (sync.syncCamera)
				{
					world.Each<CameraAnimationComponent>(
						[&](Entity, CameraAnimationComponent& ca)
						{
							if (ca.playing) ca.time = musicTime;
						});
				}
			});
	}
};

// 曲位置に沿って LiveCue を発火させるシステム。
// MusicSyncSystem(時刻確定)の後、LightSystem::Apply / CameraAnimationSystem の前に回す
class LiveDirectorSystem
{
public:
	void Update(World& world, bool isPlaying)
	{
		// 曲位置を取得(MusicSyncComponent が唯一の時間ソース)
		float musicTime = 0.0f;
		bool  hasMusic = false;
		world.Each<MusicSyncComponent>(
			[&](Entity, MusicSyncComponent& s)
			{
				if (!hasMusic) { musicTime = s.musicTime; hasMusic = true; }
			});
		if (!hasMusic) return;

		world.Each<LiveDirectorComponent>(
			[&](Entity, LiveDirectorComponent& d)
			{
				if (!d.enabled || d.timelinePath.empty()) return;

				// パスが変わったら読み直す(Inspectorで差し替えた場合)
				if (d.timelinePath != d.loadedPath)
				{
					d.loadedPath = d.timelinePath;
					d.loadFailed = !d.timeline.LoadJson(d.timelinePath);
					d.lastTime = -1.0f;
				}
				if (d.loadFailed) return;

				const float t = musicTime * d.timeScale;

				// シーク/巻き戻しを検出したら fired を張り直す
				if (t + 1e-3f < d.lastTime) d.timeline.ResetFired(t);
				d.lastTime = t;

				// トラックは連続値なので、停止中(スクラブ中)も反映する
				ApplyTracks(world, d.timeline, t);

				if (!isPlaying) return;

				// cues は時刻昇順なので、未来のキューに当たった時点で打ち切れる
				for (auto& c : d.timeline.cues)
				{
					if (c.time > t) break;
					if (c.fired) continue;
					Fire(world, c);
					c.fired = true;
				}
			});
	}

	/// @brief 名前から Entity を引く(タイムラインは Entity 名で対象を指す)
	static Entity FindEntityByName(World& world, const std::string& name)
	{
		Entity found = INVALID_ENTITY;
		world.Each<NameComponent>(
			[&](Entity e, NameComponent& n)
			{
				if (found == INVALID_ENTITY && n.name == name) found = e;
			});
		return found;
	}

	/// @brief World の外にあるリソース向けキュー(花火など)を毎フレーム回収する
	std::vector<LiveCue> ConsumeFireworks()
	{
		std::vector<LiveCue> out;
		out.swap(m_PendingFirework);
		return out;
	}

private:
	/// @brief 全トラックを評価して Transform / Light に書き戻す
	void ApplyTracks(World& world, const LiveTimeline& timeline, float t)
	{
		// 名前引きをトラックごとにやると「トラック数 × エンティティ数」の走査になる。
		// トラックは灯数ぶん増えるので、1フレームに1回だけ表を作って引く
		m_NameCache.clear();
		world.Each<NameComponent>(
			[&](Entity e, NameComponent& n)
			{
				m_NameCache.emplace(n.name, e);   // 同名は先勝ち(従来の挙動と同じ)
			});

		for (const auto& track : timeline.tracks)
		{
			if (!track.enabled || track.target.empty()) continue;

			float4 v{};
			if (!track.Evaluate(t, v)) continue;

			const auto it = m_NameCache.find(track.target);
			if (it == m_NameCache.end()) continue;
			const Entity e = it->second;

			switch (track.property)
			{
			case LiveTrack::Property::Position:
				if (world.HasComponent<TransformComponent>(e))
				{
					auto& tr = world.GetComponent<TransformComponent>(e);
					tr.position = POSITION{ v.x, v.y, v.z };
				}
				break;

			case LiveTrack::Property::EulerAngles:
				if (world.HasComponent<TransformComponent>(e))
				{
					auto& tr = world.GetComponent<TransformComponent>(e);
					tr.EulerAngles = float3{ v.x, v.y, v.z };
					tr.ApplyEuler();
				}
				break;

			case LiveTrack::Property::Color:
				if (world.HasComponent<LightComponent>(e))
					world.GetComponent<LightComponent>(e).color = COLOR{ v.x, v.y, v.z, v.w };
				break;

			case LiveTrack::Property::Intensity:
				if (world.HasComponent<LightComponent>(e))
					world.GetComponent<LightComponent>(e).intensity = v.x;
				break;

			case LiveTrack::Property::Range:
				if (world.HasComponent<LightComponent>(e))
					world.GetComponent<LightComponent>(e).range = v.x;
				break;

			case LiveTrack::Property::SpotAngle:
				if (world.HasComponent<LightComponent>(e))
					world.GetComponent<LightComponent>(e).spotAngle = v.x;
				break;

			default: break;
			}
		}
	}

	void Fire(World& world, const LiveCue& cue)
	{
		switch (cue.type)
		{
		case LiveCue::Type::CameraCut:
			world.Each<CameraComponent, NameComponent>(
				[&](Entity, CameraComponent& cam, NameComponent& n)
				{
					cam.isActive = (n.name == cue.target);
				});
			break;

		case LiveCue::Type::Blackout:
			world.Each<LightComponent>(
				[&](Entity, LightComponent& l) { l.intensity = 0.0f; });
			break;

		case LiveCue::Type::LightColor:
		case LiveCue::Type::LightIntensity:
		case LiveCue::Type::SwingEnable:
			world.Each<LightComponent, NameComponent>(
				[&](Entity, LightComponent& l, NameComponent& n)
				{
					if (!cue.target.empty() && n.name != cue.target) return;
					switch (cue.type)
					{
					case LiveCue::Type::LightColor:     l.color = cue.color;                break;
					case LiveCue::Type::LightIntensity: l.intensity = cue.value;            break;
					case LiveCue::Type::SwingEnable:    l.swingEnable = (cue.value > 0.5f); break;
					default: break;
					}
				});
			break;

		case LiveCue::Type::Firework:
			// FireworkSystem は World の外にあるので、ここではフラグだけ立てる
			m_PendingFirework.push_back(cue);
			break;

		default: break;
		}
	}

	std::vector<LiveCue> m_PendingFirework;

	// 名前 → Entity。毎フレーム作り直すが clear() で容量は残るので確保は起きない
	std::unordered_map<std::string, Entity> m_NameCache;
};

