/*!*************************************************************
 * \file   InspectorPanel.cpp
 * \brief  インスペクタ(選択エンティティのコンポーネント編集)
 *
 * 作成者 keeep
 * 作成日 2026/6/20
 * 更新履歴	6.20 EditorWindow.cpp から分割
 *			6.26 InspectorのAddComponentボタンの挙動を修正
 *			9.12 EditorPanel 派生のクラスへ
 * *********************************************************************/
#include "InspectorPanel.hpp"
#include "SceneSerializer.hpp"
#include "Components.hpp"
#include "PrefabLibrary.hpp"
#include "imguiinit.hpp"
#include <filesystem>
#include <cstdio>
#include "RuntimeScene.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include <Psapi.h>
#include "IconLibrary.hpp"
#include "ModelLoader.hpp"
#include "ImGuizmo.h"
#include "PlayState.hpp"
#include <shellapi.h>
#include "RegisterScript.hpp"
#include "ScriptHost.hpp"	
#include "ScriptField.hpp"
#include <functional>
#include <commdlg.h>
#include "json.hpp"
#include "ComponentRegistry.hpp"
#include "Util.hpp"
#include "MaterialPreview.hpp"
#include "MaterialLibrary.hpp"

// IsToonShader は Systems.hpp のものを使う（name に "Toon" を含むか）

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "psapi.lib")

/// @brief マテリアルの質感パラメータ(シェーダーごとに出す項目が変わる)
/// @note エンティティのサブマテリアルと、.mat の編集画面で共有する
static void DrawMaterialParamsUI(Material& target, const std::string& effShader)
{
	ImGui::Separator();
	ImGui::Text(u8("マテリアル質感パラメータ"));

	// サブマテリアルの表示/非表示(材質モーフで隠す衣装パーツ用)
	bool visible = target.baseAlpha > 0.5f;
	if (ImGui::Checkbox(u8("表示##SubMatVisible"), &visible))
		target.baseAlpha = visible ? 1.0f : 0.0f;

	if (effShader == "PBR" || effShader == "SkinnedPBR")
	{
		// ---- 汎用 PBR ----
		ImGui::SliderFloat(u8("Roughness##Mat"), &target.roughness, 0.0f, 1.0f);
		ImGui::SliderFloat(u8("Metallic##Mat"), &target.metallic, 0.0f, 1.0f);
		ImGui::ColorEdit4(u8("RimColor##Mat"), &target.rimColor.x);

		ImGui::SeparatorText(u8("発光"));
		ImGui::ColorEdit3(u8("発光色##Mat"), &target.emissiveColor.x);
		ImGui::SliderFloat(u8("発光強度##Mat"), &target.emissiveStrength, 0.0f, 10.0f);

		// 床など、平面反射を映すサブマテリアルだけ強度を上げる
		ImGui::SeparatorText(u8("平面反射"));
		ImGui::SliderFloat(u8("反射強度##Mat"), &target.reflectStrength, 0.0f, 1.5f);
		if (target.reflectStrength > 0.0f)
		{
			ImGui::SliderFloat(u8("反射フェード距離##Mat"), &target.reflectFade, 1.0f, 40.0f);
			ImGui::SliderFloat(u8("反射ぼかし##Mat"), &target.reflectBlur, 0.0f, 8.0f);
		}

		// ---- 肌・布（PMX キャラ向け。既定は閉じる）----
		// 使っているマテリアルだけ自動で開く
		const bool usingSss = (target.sssStrength > 0.0f) || (target.sheen > 0.0f);
		ImGui::SetNextItemOpen(usingSss, ImGuiCond_Once);
		if (ImGui::CollapsingHeader(u8("肌 / 布（サブサーフェス）##MatSss")))
		{
			ImGui::SliderFloat(u8("SSS強度##Mat"), &target.sssStrength, 0.0f, 1.0f);
			ImGui::SliderFloat(u8("SSSラップ##Mat"), &target.sssWrap, 0.0f, 1.0f);
			ImGui::SliderFloat(u8("逆光透過##Mat"), &target.sssTrans, 0.0f, 2.0f);
			ImGui::ColorEdit3(u8("散乱色##Mat"), &target.sssColor.x);
			ImGui::SliderFloat(u8("布シーン##Mat"), &target.sheen, 0.0f, 2.0f);
		}
	}
	if (effShader == "Rim" || effShader == "SkinnedRim")
	{
		ImGui::ColorEdit4(u8("RimColor##Mat"), &target.rimColor.x);
	}

	if (effShader == "Fresnel" || effShader == "SkinnedFresnel")
	{
		ImGui::SliderFloat(u8("Roughness##Mat"), &target.roughness, 0.0f, 1.0f);
		ImGui::ColorEdit4(u8("RimColor##Mat"), &target.rimColor.x);
	}

	if (effShader == "Dissolve" || effShader == "SkinnedDissolve")
	{
		ImGui::SliderFloat(u8("ノイズの細かさ##Mat"), &target.roughness, 0.0f, 1.0f);
		ImGui::SliderFloat(u8("Dissolve具合##Mat"), &target.metallic, 0.0f, 1.0f);
		ImGui::ColorEdit4(u8("解け際の発行色##Mat"), &target.rimColor.x);
	}

	if (effShader == "BlinnPhong" || effShader == "SkinnedBlinnPhong")
	{
		ImGui::SliderFloat(u8("Roughness##Mat"), &target.roughness, 0.0f, 1.0f);
		ImGui::SliderFloat(u8("Metallic##Mat"), &target.metallic, 0.0f, 1.0f);
	}

	if (effShader == "Glass" || effShader == "SkinnedGlass")
	{
		ImGui::SliderFloat(u8("映り込みのボケ##Mat"), &target.roughness, 0.0f, 1.0f);
		ImGui::ColorEdit3(u8("ガラス色##Mat"), &target.baseColor.x);
		ImGui::SliderFloat(u8("正面の不透明度##Mat"), &target.baseColor.w, 0.0f, 1.0f);
	}

	if (effShader == "Genshin_Toon")
	{
		ImGui::SliderFloat(u8("ハイライトの広さ##Mat"), &target.roughness, 0.0f, 1.0f);
		ImGui::SliderFloat(u8("ハイライトの強さ##Mat"), &target.metallic, 0.0f, 1.0f);
		ImGui::Checkbox(u8("フェイス描画##Mat"), &target.isFace);
		ImGui::SliderFloat(u8("アウトライン幅##Mat"), &target.outlineWidth, 0.0f, 10.0f);
	}
}

void InspectorPanel::DrawInspector(EditorContext& ctx, World& world, Scene* scene)
{
	if (!world.IsEntityAlive(ctx.selectedEntity))
	{
		ctx.selectedEntity = INVALID_ENTITY;
	}

	if (ctx.selectedEntity == INVALID_ENTITY)
	{
		// エンティティを選んでおらず .mat を選んでいれば、その編集画面
		if (MaterialLibrary::IsMaterialPath(ctx.selectedAsset))
		{
			DrawMaterialAsset(ctx.selectedAsset);
			return;
		}
		ImGui::Text(u8("エンティティを選択してください"));
		return;
	}

	ImGui::Text(u8("詳細情報"));
	ImGui::Separator();
	ImGui::Text("Entity ID: %u", ctx.selectedEntity);
	ImGui::Separator();

	// ---- Name Component ---- //
	if (ImGui::CollapsingHeader(u8("Name Component"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (world.HasComponent<NameComponent>(ctx.selectedEntity))
		{
			char nameBuffer[256];
			memcpy(nameBuffer, world.GetComponent<NameComponent>(ctx.selectedEntity).name.c_str(), sizeof(nameBuffer));
			auto& nameComp = world.GetComponent<NameComponent>(ctx.selectedEntity);
			if (ImGui::InputText(u8("##NameInput"), nameBuffer, sizeof(nameBuffer)))
			{
				// 入力された名前が空でないことを確認
				if (!nameBuffer[0] == '\0')
				{
					nameComp.name = std::string(nameBuffer);
				}
				else
				{
					nameComp.name = "Entity " + std::to_string(ctx.selectedEntity);
				}
			}
			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##NameComponent")))
			{
				world.DeleteComponent<NameComponent>(ctx.selectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##NameComponent")))
			{
				world.AddComponent<NameComponent>(ctx.selectedEntity, NameComponent{ "Entity " });
			}
		}
	}

	// ---- Transform Component ---- //
	if (ImGui::CollapsingHeader(u8("Transform Component"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (world.HasComponent<TransformComponent>(ctx.selectedEntity))
		{
			auto& transform = world.GetComponent<TransformComponent>(ctx.selectedEntity);

			bool dirty = false;
			dirty |= ImGui::DragFloat3(u8("位置##Pos"), &transform.position.x, 0.1f);
			float3 euler = transform.EulerAngles;
			if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
			{
				transform.EulerAngles = euler;
				transform.ApplyEuler();
			}
			dirty |= ImGui::DragFloat3(u8("スケール##Scale"), &transform.scale.x, 0.1f);

			if (dirty)
			{
				transform.RebuildWorld();

				// コライダーの当たり判定も更新
				if (world.HasComponent<ColliderComponent>(ctx.selectedEntity))
				{
					auto& collider = world.GetComponent<ColliderComponent>(ctx.selectedEntity);

					if (collider.shapeType == ColliderComponent::ShapeType::Box)
					{
						collider.size = transform.scale;
					}
					else if (collider.shapeType == ColliderComponent::ShapeType::Sphere)
					{
						collider.radius = std::max(transform.scale.x, std::max(transform.scale.y, transform.scale.z)) * 0.5f;
					}

					// PhysicsWorld へは反映しなくてよい。設定が変わると、
					// 次の固定更新で PhysicsWorld::SyncFromWorld が作り直す
				}
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##TransformComponent")))
			{
				TransformComponent tr{};
				tr.RebuildWorld();
				world.AddComponent<TransformComponent>(ctx.selectedEntity, tr);
			}
		}
	}

	// ---- Mesh Component ---- //
	if (ImGui::CollapsingHeader(u8("Mesh Component")))
	{
		if (world.HasComponent<MeshComponent>(ctx.selectedEntity))
		{
			auto& meshComp = world.GetComponent<MeshComponent>(ctx.selectedEntity);
			ImGui::Text(u8("メッシュコンポーネント"));

			ImGui::Separator();

			// このフレームで差し替えるパス。meshComp への参照が
			// PopulateModelEntity の中で作り直されるので、UIを描き終えてから実行する
			std::string pendingSwap;

			// ファイルパスの設定
			char filepathBuffer[256];
			// 元のパスをコピー
			snprintf(filepathBuffer, sizeof(filepathBuffer), "%s", meshComp.FilePath.c_str());

			if (ImGui::InputText(u8("ファイルパス##MeshFilePath"), filepathBuffer, sizeof(filepathBuffer)))
			{
				meshComp.FilePath = filepathBuffer;
			}

			// -------------------------------------
			// アセットパネルからドロップを受け付け（落とした時点で差し替える）
			// -------------------------------------
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
				if (payload != nullptr)
				{
					pendingSwap = std::string(static_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropTarget();
			}

			// ファイル選択ダイアログから差し替え
			if (ImGui::Button(u8("モデルを選択...##MeshPick")))
			{
				std::wstring picked;
				if (OpenFileDialog(picked,
					L"Model\0*.pmx;*.fbx;*.obj;*.gltf;*.glb;*.dae\0"
					L"MMD Model (*.pmx)\0*.pmx\0"
					L"All\0*.*\0"))
				{
					pendingSwap = MakeAssetRelative(WideToUtf8(picked));
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(u8("読み込み##MeshReload")))
			{
				pendingSwap = meshComp.FilePath;   // 手打ちしたパスを読む
			}

			// 拡張子ごとの既定スケール(.pmx=0.1 / .fbx=0.01)を当て直すか
			static bool s_MeshSwapAutoScale = true;
			ImGui::Checkbox(u8("差し替え時にスケールを合わせる##MeshAutoScale"), &s_MeshSwapAutoScale);

			ImGui::Separator();


			ImGui::InputFloat(u8("スケール倍率##MeshScale"), &meshComp.scale);

			if (meshComp.scale <= 0.0f)
			{
				meshComp.scale = 0.01f;
			}

			// --- モデル差し替え ---
			// メッシュだけ入れ替えると、スケルトン/モーフ/剛体/マテリアルが
			// 前のモデルのまま残って破綻する。シーン読み込みと同じ
			// PopulateModelEntity を通して一式作り直す
			if (!pendingSwap.empty())
			{
				const std::string prevPath = meshComp.FilePath;

				// 再生位置と、手で足したVMDは引き継ぐ。
				// clipPathsStr も PopulateModelEntity 側で引き継がれるので、
				// MMD標準ボーン同士ならダンスを流したままモデルだけ変えられる
				int  clip = 0;
				bool playing = false;
				std::vector<std::string> extras;
				if (world.HasComponent<AnimatorComponent>(ctx.selectedEntity))
				{
					const auto& an = world.GetComponent<AnimatorComponent>(ctx.selectedEntity);
					clip = an.currentClip;
					playing = an.playing;
					extras = an.extraClipNames;
				}

				// 形式が変わるときだけ既定スケールを当て直す。
				// 同じ形式なら、ユーザーが調整したTransformを尊重する
				namespace fs = std::filesystem;
				const bool extChanged =
					fs::path(prevPath).extension() != fs::path(pendingSwap).extension();
				const bool applyScale = s_MeshSwapAutoScale && extChanged;

				meshComp.FilePath = pendingSwap;
				ModelLoader::PopulateModelEntity(world, ctx.selectedEntity, pendingSwap,
					scene, clip, playing, extras, applyScale);

				LOG->LogInfo("モデルを差し替えました: " + pendingSwap);
			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##MeshComponent")))
			{
				world.DeleteComponent<MeshComponent>(ctx.selectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##MeshComponent")))
			{
				MeshComponent mesh{};
				world.AddComponent<MeshComponent>(ctx.selectedEntity, mesh);

				// TransformとMaterialもないならついでに作る
				if (!world.HasComponent<TransformComponent>(ctx.selectedEntity))
				{
					TransformComponent tr{};
					tr.RebuildWorld();
					world.AddComponent<TransformComponent>(ctx.selectedEntity, tr);
				}

				if (!world.HasComponent<MaterialComponent>(ctx.selectedEntity))
				{
					MaterialComponent material{};
					material.material = std::make_shared<Material>();
					material.material->Init();
					world.AddComponent<MaterialComponent>(ctx.selectedEntity, material);
					LOG->LogInfo(("マテリアルを生成しました"));
				}
			}
		}
	}

	// ---- Material Component ---- //
	if (ImGui::CollapsingHeader(u8("Material Component")))
	{
		if (world.HasComponent<MaterialComponent>(ctx.selectedEntity))
		{
			auto& materialComp = world.GetComponent<MaterialComponent>(ctx.selectedEntity);

			// ファイルパスの設定
			char filepathBuffer[256];
			// 元のパスをコピー
			snprintf(filepathBuffer, sizeof(filepathBuffer), "%s", materialComp.FilePath.c_str());
			if (ImGui::InputText(u8("ファイルパス##MaterialFilePath"), filepathBuffer, sizeof(filepathBuffer)))
			{
				materialComp.FilePath = filepathBuffer;
			}

			// アセットパネルからドロップを受け付け
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE");
				if (payload != nullptr)
				{
					materialComp.FilePath = std::string(static_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropTarget();
			}

			// 適用
			if (ImGui::Button(u8("適用##MaterialApply")))
			{
				// マテリアルが未生成なら作る
				if (!materialComp.material)
				{
					materialComp.material = std::make_shared<Material>();
					materialComp.material->Init();
				}

				std::wstring wpath = std::filesystem::path(materialComp.FilePath).wstring();
				if (!materialComp.material->SetTextureFromFile(wpath))
				{
					LOG->LogError("テクスチャの読み込みに失敗しました");
				}
				else
				{
					LOG->LogInfo(("テクスチャを適用しました"));
				}
			}

			const std::vector<std::string> names = APP->GetShaderNames();
			if (ImGui::BeginCombo(u8("Shader##Mat"), materialComp.shaderName.c_str()))
			{
				for (const auto& n : names)
				{
					bool selected = (materialComp.shaderName == n);
					if (ImGui::Selectable(n.c_str(), selected))
						materialComp.shaderName = n;
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			// マルチマテリアルのスロット一覧（読み取り表示）
			Material* target = materialComp.material.get();

			if (!materialComp.materials.empty())
			{
				static int selectedSub = 0;   // 編集中のサブマテリアル
				const int count = (int)materialComp.materials.size();
				if (selectedSub >= count) selectedSub = 0;

				ImGui::Separator();
				ImGui::Text(u8("サブマテリアル: %d"), count);
				for (int i = 0; i < count; ++i)
				{
					// materialnamesに名前が登録されている場合
					if(!materialComp.materialnames[i].empty())
					{
						ImGui::PushID(i);
						if (ImGui::Selectable((materialComp.materialnames[i] + "##Material").c_str(),
							selectedSub == i))
							selectedSub = i;
						ImGui::PopID();
					}
					else
					{
						ImGui::PushID(i);
						if (ImGui::Selectable((std::string("SubMaterial ") + std::to_string(i) + "##Material").c_str(),
							selectedSub == i))
							selectedSub = i;
						ImGui::PopID();
					}
				}
				if (materialComp.materials[selectedSub])
					// 選択中のサブマテリアルをtargetに設定
					target = materialComp.materials[selectedSub].get();

				// マテリアルプレビュー
				{
					const auto& previewMat = materialComp.materials.empty()
						? materialComp.material : materialComp.materials[selectedSub];

					const auto srv = MaterialPreview::Get().Request(previewMat);
					if (srv.ptr != 0)
					{
						ImGui::Image(static_cast<ImTextureID>(srv.ptr), ImVec2(128, 128));
						ImGui::Separator();
					}
				}

				// ---- マテリアルアセット(.mat) ---- //
				{
					materialComp.materialAssets.resize(materialComp.materials.size());
					std::string& assetPath = materialComp.materialAssets[selectedSub];

					// このスロットのマテリアルを差し替える
					auto assign = [&](std::shared_ptr<Material> m, const std::string& path)
						{
							if (!m) return;
							APP->WaitForGPUIdle();
							materialComp.materials[selectedSub] = m;
							if (selectedSub == 0) materialComp.material = m;
							assetPath = path;
							target = m.get();
						};

					ImGui::SeparatorText(u8("マテリアルアセット"));
					if (assetPath.empty())
						ImGui::TextDisabled(u8("(モデル内蔵のマテリアル)  .mat をドロップで割り当て"));
					else
						ImGui::Text("%s", assetPath.c_str());

					// アセットブラウザから .mat をドロップ
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_MATERIAL"))
						{
							const std::string dropped((const char*)p->Data, p->DataSize - 1);
							assign(MaterialLibrary::Get().Load(dropped), dropped);
						}
						ImGui::EndDragDropTarget();
					}

					if (assetPath.empty())
					{
						// 今の見た目のまま .mat にして、このスロットに割り当てる
						if (ImGui::Button(u8(".mat として書き出す")))
						{
							const std::string& subName = materialComp.materialnames[selectedSub];
							const std::string name = !subName.empty()
								? subName : "SubMaterial" + std::to_string(selectedSub);

							const std::string file = MaterialLibrary::Get().CreateFromMaterial(
								*materialComp.materials[selectedSub], materialComp.shaderName,
								ctx.currentAssetDir, name);
							if (!file.empty())
								assign(MaterialLibrary::Get().Load(file), file);
						}
					}
					else
					{
						ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
							u8("共有マテリアル: 変更はこの .mat を使うすべてに反映されます"));

						if (ImGui::Button(u8("保存##MatAsset")))
							MaterialLibrary::Get().Save(assetPath);

						ImGui::SameLine();
						if (ImGui::Button(u8("割り当てを解除")))
						{
							// 今の値を引き継いだ独立コピーに戻す(共有インスタンスは他が使っているので触らない)
							assign(MaterialLibrary::Get().Clone(*materialComp.materials[selectedSub]), "");
						}
					}
				}
			}

			// 選択中サブマテリアルシェーダ(個別)
			if (target && !materialComp.materials.empty())
			{
				const char* cur = target->shaderName.empty()
					? u8("(全体設定を継承)") : target->shaderName.c_str();
				if (ImGui::BeginCombo(u8("Shader(個別)##SUBmat"), cur))
				{
					// 継承に戻す選択肢
					if (ImGui::Selectable("use default", target->shaderName.empty()))
						target->shaderName.clear();

					for (const auto& n : names)
					{
						bool selected = (target->shaderName == n);
						if (ImGui::Selectable(n.c_str(), selected))
							target->shaderName = n;
						if (selected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}



			// --- マテリアル質感パラメータ ---
			if (target)
			{
				const std::string& effShader =
					!target->shaderName.empty() ? target->shaderName : materialComp.shaderName;
				DrawMaterialParamsUI(*target, effShader);
			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##MaterialComponent")))
			{
				world.DeleteComponent<MaterialComponent>(ctx.selectedEntity);
			}

			// マテリアルの状態表示
			ImGui::Separator();
			if (materialComp.material)
			{
				ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), u8("マテリアル初期化済み"));
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8("マテリアル未初期化"));
			}
			// トゥーンランプはトゥーン系シェーダーでしか使わない
			if (IsToonShader(materialComp.shaderName) ||
				(target != nullptr && IsToonShader(target->shaderName)))
			{
				// トゥーンランプテクスチャ
				char toonRampBuffer[256];
				snprintf(toonRampBuffer, sizeof(toonRampBuffer), "%s", materialComp.RampFilePath.c_str());
				if (ImGui::InputText(u8("トゥーンランプテクスチャ##ToonRampFilePath"), toonRampBuffer, sizeof(toonRampBuffer)))
				{
					materialComp.RampFilePath = toonRampBuffer;
				}


				if (ImGui::BeginDragDropTarget())
				{
					const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE");
					if (payload != nullptr)
					{
						materialComp.RampFilePath = static_cast<const char*>(payload->Data);
					}
					ImGui::EndDragDropTarget();
				}

				if (ImGui::Button(u8("ランプ適用##MaterialRampApply")))
				{
					if (materialComp.material)
					{
						std::wstring wpath = std::filesystem::path(materialComp.RampFilePath).wstring();
						if (!materialComp.material->SetToonRampTexture(wpath))
						{
							LOG->LogError(u8("ランプテクスチャの読み込みに失敗しました"));
						}
					}
				}
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##MaterialComponent")))
			{
				MaterialComponent material{};
				world.AddComponent<MaterialComponent>(ctx.selectedEntity, material);
			}
		}
	}

	for(const auto& compMeta : ComponentRegistry::All())
	{
		compMeta.draw(world, ctx.selectedEntity);
	}

	// ---- Script Component ---- //
	if (world.HasComponent<ScriptComponent>(ctx.selectedEntity))
	{
		if (ImGui::CollapsingHeader(u8("Script Component")))
		{
			auto& sc = world.GetComponent<ScriptComponent>(ctx.selectedEntity);

			// 追加済みスクリプト一覧
			int removeIdx = -1;
			for (int i = 0; i < (int)sc.scriptNames.size(); i++)
			{
				const std::string& name = sc.scriptNames[i];
				ImGui::PushID(i);

				// enabled は behaviors 側に持っている。DLL未ロード中は触れない
				MonoBehavior* behavior =
					(i < (int)sc.behaviors.size()) ? sc.behaviors[i].get() : nullptr;

				if (behavior)
				{
					ImGui::Checkbox("##enabled", &behavior->enabled);
					ImGui::SameLine();
				}

				ImGui::Text("%s", name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) removeIdx = i;

				// このスクリプト1個分だけ描画（二重ループ解消）
				auto& descs = sc.fieldDescs[name];
				auto& vals = sc.values[name];
				for (auto& d : descs)
				{
					FieldValue& v = vals[d.name];
					v.type = d.type;

					// Range が指定されていればスライダーにする
					const bool hasRange = (d.rangeMin != d.rangeMax);

					switch (d.type)
					{
					case FieldType::Int:
						if (hasRange) ImGui::SliderInt(d.name.c_str(), &v.i, d.rangeMin, d.rangeMax);
						else          ImGui::DragInt(d.name.c_str(), &v.i);
						break;
					case FieldType::Float:
						if (hasRange) ImGui::SliderFloat(d.name.c_str(), &v.f[0],
							(float)d.rangeMin, (float)d.rangeMax);
						else          ImGui::DragFloat(d.name.c_str(), &v.f[0]);
						break;
					case FieldType::Float2: ImGui::DragFloat2(d.name.c_str(), v.f); break;
					case FieldType::Float3:
					case FieldType::Vector3:ImGui::DragFloat3(d.name.c_str(), v.f); break;
					case FieldType::Color:  ImGui::ColorEdit3(d.name.c_str(), v.f); break;
					case FieldType::Float4:
					case FieldType::Vector4:ImGui::DragFloat4(d.name.c_str(), v.f); break;
					case FieldType::Bool:   ImGui::Checkbox(d.name.c_str(), &v.b); break;
					case FieldType::String:
					{
						char buf[256];
						std::snprintf(buf, sizeof(buf), "%s", v.s.c_str());
						if (ImGui::InputText(d.name.c_str(), buf, sizeof(buf)))
						{
							v.s = buf;
						}
						break;
					}
					case FieldType::Entity:
					{
						Entity cur = (Entity)v.i;
						std::string label = "(None)";
						if (cur != INVALID_ENTITY)
						{
							label = world.HasComponent<NameComponent>(cur)
								? world.GetComponent<NameComponent>(cur).name + " (" + std::to_string(cur) + ")"
								: "Entity " + std::to_string(cur);
						}

						if (ImGui::BeginCombo(d.name.c_str(), label.c_str()))
						{
							if (ImGui::Selectable("(None)", cur == INVALID_ENTITY))
								v.i = (int)INVALID_ENTITY;

							// NameComponentを持つ全オブジェクトを名前付きで列挙
							world.Each<NameComponent>([&](Entity e, NameComponent& nm)
								{
									std::string n = nm.name + " (" + std::to_string(e) + ")";
									if (ImGui::Selectable(n.c_str(), e == cur))
										v.i = (int)e;
								});
							ImGui::EndCombo();
						}
						break;
					}
					}
				}

				ImGui::PopID();
			}
			if (removeIdx >= 0)
			{
				const std::string nm = sc.scriptNames[removeIdx];
				sc.scriptNames.erase(sc.scriptNames.begin() + removeIdx);
				sc.fieldDescs.erase(nm);
				sc.values.erase(nm);
			}

			ImGui::Separator();

			// Unity の Add Script ボタン
			if (ImGui::Button(u8("Add Script"), ImVec2(-1, 0)))
			{
				m_ScriptSerachBuffer[0] = '\0';
				m_FocusScriptSearch = true;
				ImGui::OpenPopup("ScriptSearchPopup");
			}

			if (ImGui::BeginPopup("ScriptSearchPopup"))
			{
				if (m_FocusScriptSearch)
				{
					ImGui::SetKeyboardFocusHere();
					m_FocusScriptSearch = false;
				}
				ImGui::SetNextItemWidth(240.0f);
				ImGui::InputTextWithHint("##ScriptSearch", u8("検索..."),
					m_ScriptSerachBuffer, sizeof(m_ScriptSerachBuffer));
				ImGui::Separator();

				const auto& names = ScriptHost::GetScriptNames();
				bool any = false;
				for (const auto& n : names)
				{
					// フィルタリング
					if (m_ScriptSerachBuffer[0] != '\0')
					{
						std::string lower = n, query = m_ScriptSerachBuffer;
						std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
						std::transform(query.begin(), query.end(), query.begin(), ::tolower);
						if (lower.find(query) == std::string::npos) continue;
					}

					bool alreadyAdded = std::find(sc.scriptNames.begin(), sc.scriptNames.end(), n) != sc.scriptNames.end();
					if (alreadyAdded) ImGui::BeginDisabled();
					if (ImGui::Selectable(n.c_str()))
					{
						sc.scriptNames.push_back(n);
						ImGui::CloseCurrentPopup();
					}
					if (alreadyAdded) ImGui::EndDisabled();
					any = true;
				}
				if (!any)
					ImGui::TextDisabled(u8("見つかりません"));

				ImGui::EndPopup();
			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##ScriptComponent")))
				world.DeleteComponent<ScriptComponent>(ctx.selectedEntity);
		}
	}
	else
	{
		if (ImGui::Button(u8("Add Component##ScriptComponent")))
			world.AddComponent<ScriptComponent>(ctx.selectedEntity, ScriptComponent{});
	}

	DrawAddComponentPopup(world, ctx.selectedEntity);
}

void InspectorPanel::DrawMaterialAsset(const std::string& path)
{
	auto mat = MaterialLibrary::Get().Load(path);
	if (!mat)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), u8("読み込めません: %s"), path.c_str());
		return;
	}

	ImGui::Text(u8("マテリアル: %s"), path.c_str());
	ImGui::Separator();

	// プレビュー(1フレーム遅れて出る)
	const auto srv = MaterialPreview::Get().Request(mat);
	if (srv.ptr != 0)
	{
		ImGui::Image(static_cast<ImTextureID>(srv.ptr), ImVec2(160, 160));
	}

	// シェーダー
	const std::vector<std::string> names = APP->GetShaderNames();
	if (ImGui::BeginCombo(u8("Shader##MatAsset"), mat->shaderName.c_str()))
	{
		for (const auto& n : names)
		{
			const bool selected = (mat->shaderName == n);
			if (ImGui::Selectable(n.c_str(), selected)) mat->shaderName = n;
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	DrawMaterialParamsUI(*mat, mat->shaderName);

	// テクスチャ: アセットブラウザから画像をドロップして差し替える
	ImGui::SeparatorText(u8("テクスチャ"));
	static const std::pair<UINT, const char*> kTexUi[] =
	{
		{ TexSlot::Albedo, "Albedo" }, { TexSlot::Normal, "Normal" },
		{ TexSlot::Metal, "Metal" },   { TexSlot::Rough, "Rough" },
		{ TexSlot::Emissive, "Emissive" }, { TexSlot::Occlusion, "Occlusion" },
		{ TexSlot::Ramp, "Toon Ramp" },
	};
	for (const auto& [slot, label] : kTexUi)
	{
		const std::string& src = mat->Textures().SourcePath(slot);
		ImGui::Text("%s: %s", label, src.empty() ? "(none)" : src.c_str());

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
			{
				const std::string dropped((const char*)p->Data, p->DataSize - 1);
				MaterialLibrary::Get().SetTexture(path, slot, dropped);
			}
			ImGui::EndDragDropTarget();
		}
	}

	ImGui::Separator();
	if (ImGui::Button(u8("保存"), ImVec2(-1, 0)))
	{
		MaterialLibrary::Get().Save(path);
	}
}

void InspectorPanel::DrawAddComponentPopup(World& world, Entity entity)
{
	ImGui::Separator();
	if (ImGui::Button(u8("Add Component"), ImVec2(-1, 0)))
	{
		m_AddCompSearchBuffer[0] = '\0';
		m_FocusAddCompSearch = true;
		ImGui::OpenPopup("AddComponentPopup");
	}
	if (!ImGui::BeginPopup("AddComponentPopup")) return;

	if (m_FocusAddCompSearch) { ImGui::SetKeyboardFocusHere(); m_FocusAddCompSearch = false; }
	ImGui::SetNextItemWidth(260.0f);
	ImGui::InputTextWithHint("##acsearch", u8("検索..."), m_AddCompSearchBuffer, sizeof(m_AddCompSearchBuffer));
	ImGui::Separator();

	auto match = [&](const std::string& s)
		{
			if (m_AddCompSearchBuffer[0] == '\0') return true;
			std::string a = s, q = m_AddCompSearchBuffer;
			std::transform(a.begin(), a.end(), a.begin(), ::tolower);
			std::transform(q.begin(), q.end(), q.begin(), ::tolower);
			return a.find(q) != std::string::npos;
		};

	// コンポーネント
	for (const auto& c : ComponentRegistry::All())
	{
		if (!match(c.name)) continue;
		bool has = c.has(world, entity);
		if (has) ImGui::BeginDisabled();
		if (ImGui::Selectable(c.name.c_str())) { c.add(world, entity); ImGui::CloseCurrentPopup(); }
		if (has) ImGui::EndDisabled();
	}

	// スクリプト
	ImGui::SeparatorText("Scripts");
	ImGui::Text("Open = %d names =%d", (int)ScriptHost::isOpen(), (int)ScriptHost::GetScriptNames().size());
	for (const auto& n : ScriptHost::GetScriptNames())
	{
		if (!match(n)) continue;
		bool added = false;
		if (world.HasComponent<ScriptComponent>(entity))
		{
			auto& sc = world.GetComponent<ScriptComponent>(entity);
			added = std::find(sc.scriptNames.begin(), sc.scriptNames.end(), n) != sc.scriptNames.end();
		}
		if (added) ImGui::BeginDisabled();
		if (ImGui::Selectable((n + "##sc").c_str()))
		{
			if (!world.HasComponent<ScriptComponent>(entity))
				world.AddComponent<ScriptComponent>(entity, ScriptComponent{});
			world.GetComponent<ScriptComponent>(entity).scriptNames.push_back(n);
			ImGui::CloseCurrentPopup();
		}
		if (added) ImGui::EndDisabled();
	}
	ImGui::EndPopup();
}

void InspectorPanel::Draw(EditorContext& ctx)
{
	if (ctx.activeScene == nullptr)
	{
		ImGui::Text(u8("アクティブなシーンがありません"));
		return;
	}
	DrawInspector(ctx, ctx.activeScene->GetWorld(), ctx.activeScene);
}
