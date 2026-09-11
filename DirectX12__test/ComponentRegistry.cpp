#include "ComponentRegistry.hpp"
#include "Components.hpp"
#include "imguiinit.hpp"
#include "DirectX.hpp"
#include "Util.hpp"
#include "ModelLoader.hpp"
#include "Debug.hpp"
#include "AsyncLoader.hpp"
#include "MmdPlayerUI.hpp"

using json = nlohmann::json;

// 外部ファイルなら Assets/Motions/ へコピーして取り込み、Assets相対パスを返す。
// 既にプロジェクト配下なら相対化だけ行う
static std::string ImportToAssets(const std::string& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path abs = fs::absolute(path, ec);
    const fs::path rel = fs::relative(abs, fs::current_path(), ec);

    // プロジェクト配下 → 相対化のみ
    if (!ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0)
    {
        return rel.generic_string();
    }

    // プロジェクト外 → Assets/Motions/ にコピーして取り込む
    const fs::path destDir = "Assets/Motions";
    fs::create_directories(destDir, ec);
    const fs::path dest = destDir / abs.filename();

    fs::copy_file(abs, dest, fs::copy_options::update_existing, ec);
    if (ec)
    {
        LOG->LogError(u8("VMDの取り込みに失敗: ") + path + " (" + ec.message() + ")");
        return path;
    }
    LOG->LogInfo(u8("VMDをAssetsに取り込みました: ") + dest.generic_string());
    return dest.generic_string();
}

template <typename T>
static void DrawExtraUI(World&, Entity, T&) {}

template<>
void DrawExtraUI<AudioSourceComponent>(World&, Entity, AudioSourceComponent& src)
{
    if (ImGui::Button("Play"))  src.playRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Stop"))  src.stopRequested = true;
}

template<>
void DrawExtraUI<CameraAnimationComponent>(World&, Entity, CameraAnimationComponent& ca)
{
    auto load = [&ca](const std::string& path)
        {
            CameraClip clip = ModelLoader::LoadVMDCameraClip(path);
            if (clip.keys.empty())
            {
                LOG->LogError(u8("カメラVMDにカメラキーがありません: ") + path);
                return;
            }
            ca.vmdPath = path;
            ca.clip = std::move(clip);
            ca.loaded = true;
            ca.time = 0.0f;
        };

    // --- ファイルダイアログ ---
    if (ImGui::Button(u8("カメラVMD読み込み...")))
    {
        std::wstring picked;
        if (OpenFileDialog(picked, L"VMD Motion\0*.vmd\0All\0*.*\0"))
        {
            load(ImportToAssets(WideToUtf8(picked)));
        }
    }

    // --- Assetsからのドラッグ&ドロップ ---
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            std::string path((const char*)p->Data, p->DataSize - 1);
            if (path.size() > 4 &&
                _stricmp(path.c_str() + path.size() - 4, ".vmd") == 0)
            {
                load(path);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // --- 状態表示 ---
    if (ca.loaded && !ca.clip.keys.empty())
    {
        ImGui::Text(u8("%s  キー数:%d  長さ:%.1f秒"),
            std::filesystem::path(ca.vmdPath).filename().string().c_str(),
            (int)ca.clip.keys.size(), ca.clip.duration);
    }
    else
    {
        ImGui::TextDisabled(u8("カメラVMD未読み込み"));
        return;
    }

    // --- 再生コントロール ---
    if (ImGui::Button(ca.playing ? u8("一時停止") : u8("再生")))
        ca.playing = !ca.playing;
    ImGui::SameLine();
    if (ImGui::Button(u8("最初から")))
        ca.time = 0.0f;

    if (ca.clip.duration > 0.0f)
        ImGui::ProgressBar(ca.time / ca.clip.duration, ImVec2(-1, 0));
}

template<>
void DrawExtraUI<AnimatorComponent>(World& world, Entity e, AnimatorComponent& an)
{
    auto onLoaded = [&world, e](AnimationClip vc)
        {
            if (vc.channels.empty() && vc.morphChannels.empty()) return;
            if (!world.IsEntityAlive(e) || !world.HasComponent<AnimatorComponent>(e)) return;
            auto& a = world.GetComponent<AnimatorComponent>(e);
            a.clips.push_back(std::move(vc));
            a.currentClip   = (int)a.clips.size() - 1;
            a.currentClipName = a.clips[a.currentClip].name;   // 手動で読んだものを選択状態にする
            a.time          = 0.0f;
            a.playing       = true;
        };

    // 読み込み開始時にパスを記録する(成功/失敗に関わらず記録でOK。失敗はログに出る)
    auto loadAndRecord = [&an, &onLoaded](const std::string& rawPath)
        {
            const std::string path = ImportToAssets(rawPath);
            AsyncLoader::Get().LoadVMDAsync(path, an.skeleton, onLoaded);

            // 同じVMDを二重に記録しない(保存のたびにパスが増え続けるため)
            const std::string token = "|" + path + "|";
            const std::string haystack = "|" + an.clipPathsStr + "|";
            if (haystack.find(token) == std::string::npos)
            {
                if (!an.clipPathsStr.empty()) an.clipPathsStr += "|";
                an.clipPathsStr += path;
            }
            an.clipsRestored = true;   // いま手で読んだ分を復元処理が二重ロードしないように
        };

    // --- VMD読み込みボタン(非同期) ---
    if (ImGui::Button(u8("VMD読み込み...")))
    {
        std::wstring picked;
        if (OpenFileDialog(picked, L"VMD Motion\0*.vmd\0All\0*.*\0"))
        {
            loadAndRecord(WideToUtf8(picked));
        }
    }

    // Assetsからのドラッグ&ドロップ(非同期)
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            std::string path((const char*)p->Data, p->DataSize - 1);
            if (path.size() > 4 &&
                _stricmp(path.c_str() + path.size() - 4, ".vmd") == 0)
            {
                loadAndRecord(path);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // vmd一覧表示
    if (!an.clips.empty())
    {
        if (an.clips.empty())
        {
            ImGui::TextDisabled(u8("クリップ読み込み中..."));
            return;
        }
        // 範囲外でも書き換えない(非同期ロード中に選択を潰さないため)
        if (an.currentClip < 0 || an.currentClip >= (int)an.clips.size())
        {
            ImGui::TextDisabled(u8("クリップ読み込み中..."));
        }
        else
        {
            std::vector<const char*> names;
            for (const auto& c : an.clips) names.push_back(c.name.c_str());
            if (ImGui::Combo(u8("Clip"), &an.currentClip, names.data(), (int)names.size()))
                an.currentClipName = an.clips[an.currentClip].name;
        }
	}

    // --- 再生コントロール ---
    DrawMmdPlayerControls(world,an);
}

// パス系フィールドの共通描画（表示・対応ペイロード受け取り・ダイアログ・Apply）
static void DrawPathField(const ReflectedField& f,
    const char* payloadType,
    const wchar_t* dialogFilter)
{
    auto* path = (std::string*)f.ptr;
    auto* res = (std::shared_ptr<void>*)f.ptr2;   // 任意リソース（reset用）

    auto applyReset = [&]()
        {
            if (res && *res) { APP->WaitForGPUIdle(); res->reset(); }
        };

    ImGui::Text("%s: %s", f.name.c_str(), path->empty() ? "(none)" : path->c_str());

    // Assetsからのドラッグ&ドロップ受け取り
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(payloadType))
        {
            *path = std::string((const char*)p->Data, p->DataSize - 1); // 末尾NUL除く
            applyReset();
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Spacing();
    if (ImGui::SmallButton((std::string("...##") + f.name).c_str()))
    {
        std::wstring picked;
        if (OpenFileDialog(picked, dialogFilter)) { *path = MakeAssetRelative(WideToUtf8(picked)); applyReset(); }
    }
}

// ---- 汎用フィールド描画（Inspectorと共有） ---- //
void DrawFieldList(const FieldList& fl)
{
    for (const auto& f : fl.fields)
    {
        const char* n = f.name.c_str();
        switch (f.type)
        {
        case FieldType::Int:
            ImGui::DragInt(n, (int*)f.ptr); break;
        case FieldType::Float:
            if (f.maxValue > f.minValue) ImGui::SliderFloat(n, (float*)f.ptr, f.minValue, f.maxValue);
            else               ImGui::DragFloat(n, (float*)f.ptr, 0.01f);
            break;
        case FieldType::Float2: ImGui::DragFloat2(n, (float*)f.ptr, 0.01f); break;
        case FieldType::Float3: ImGui::DragFloat3(n, (float*)f.ptr, 0.01f); break;
        case FieldType::Float4: ImGui::DragFloat4(n, (float*)f.ptr, 0.01f); break;
        case FieldType::Color:  ImGui::ColorEdit4(n, (float*)f.ptr);        break;
        case FieldType::Bool:   ImGui::Checkbox(n, (bool*)f.ptr);          break;
        case FieldType::String:
        {
            auto* s = (std::string*)f.ptr;
            char buf[256]; strncpy_s(buf, s->c_str(), sizeof(buf) - 1);
            if (ImGui::InputText(n, buf, sizeof(buf))) *s = buf;
            break;
        }
        case FieldType::Enum:
        {
            int* cur = (int*)f.ptr;
            const char* preview = (*cur >= 0 && *cur < (int)f.enumValues.size())
                ? f.enumValues[*cur].c_str() : "?";

            if (ImGui::BeginCombo(n, preview))
            {
                for (int i = 0; i < (int)f.enumValues.size(); ++i)
                {
                    bool selected = (*cur == i);
                    if (ImGui::Selectable(f.enumValues[i].c_str(), selected))
                        *cur = i;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldType::Texture:
            DrawPathField(f, "ASSET_TEXTURE",
                L"Image\0*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp\0All\0*.*\0");
            break;

        case FieldType::Font:
            DrawPathField(f, "ASSET_FONT",
                L"Font\0*.ttf;*.ttc;*.otf\0All\0*.*\0");
            break;

        case FieldType::Audio:
            DrawPathField(f, "ASSET_AUDIO",
                L"Audio\0*.wav;*.mp3;*.ogg\0All\0*.*\0");
            break;
        default: break; // Texture/Entity は専用UIなのでここでは扱わない
        }
    }


}

// ---- FieldValue(ReflectedField) <-> json ---- //
static void FieldToJson(json& j, const ReflectedField& f)
{
    switch (f.type)
    {
    case FieldType::Int:    j = *(int*)f.ptr; break;
    case FieldType::Float:  j = *(float*)f.ptr; break;
    case FieldType::Float2: { auto* v = (float*)f.ptr; j = { v[0], v[1] }; } break;
    case FieldType::Float3:
    case FieldType::Vector3: { auto* v = (float*)f.ptr; j = { v[0], v[1], v[2] }; } break;
    case FieldType::Color:
    case FieldType::Float4:
    case FieldType::Vector4: { auto* v = (float*)f.ptr; j = { v[0], v[1], v[2], v[3] }; } break;
    case FieldType::Bool:   j = *(bool*)f.ptr; break;
    case FieldType::Texture:
    case FieldType::Font:
    case FieldType::Audio:
    case FieldType::String: j = *(std::string*)f.ptr; break;
    case FieldType::Enum: j = *(int*)f.ptr; break;
    default: break; // Texture/Entity はここでは保存しない（専用処理が必要なら別途）
    }
}

static void JsonToField(const json& j, const ReflectedField& f)
{
    switch (f.type)
    {
    case FieldType::Int:    *(int*)f.ptr = j.get<int>(); break;
    case FieldType::Float:  *(float*)f.ptr = j.get<float>(); break;
    case FieldType::Float2: { auto* v = (float*)f.ptr; v[0] = j[0]; v[1] = j[1]; } break;
    case FieldType::Float3:
    case FieldType::Vector3: { auto* v = (float*)f.ptr; v[0] = j[0]; v[1] = j[1]; v[2] = j[2]; } break;
    case FieldType::Color:
    case FieldType::Float4:
    case FieldType::Vector4: { auto* v = (float*)f.ptr; v[0] = j[0]; v[1] = j[1]; v[2] = j[2]; v[3] = j[3]; } break;
    case FieldType::Bool:   *(bool*)f.ptr = j.get<bool>(); break;
    case FieldType::Texture:
    case FieldType::Font:
    case FieldType::Audio:
    case FieldType::String:
        if (j.is_string()) *(std::string*)f.ptr = j.get<std::string>();
        break;
	case FieldType::Enum: *(int*)f.ptr = j.get<int>(); break;
    default: break;
    }
}

// Kawaii Physics: チェーンとコリジョンは配列なので FieldList では描けない。
// ここで手編集し、変更があれば CommitConfig() で configStr へ畳み直す。
template<>
void DrawExtraUI<KawaiiPhysicsComponent>(World& world, Entity e, KawaiiPhysicsComponent& kp)
{
    bool changed = false;

    // PMX の剛体から丸ごと作り直す（モデルロード時にも同じ処理が走っている）
    if (!kp.sourcePhysics.rigidBodies.empty() && world.HasComponent<AnimatorComponent>(e))
    {
        if (ImGui::Button(u8("PMX の剛体から再生成")))
        {
            const auto& an = world.GetComponent<AnimatorComponent>(e);
            KawaiiAutoSetupFromPmx(an.skeleton, kp.sourcePhysics, kp.settings);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(u8("剛体 %d 個"), (int)kp.sourcePhysics.rigidBodies.size());
    }

    // ボーン名の入力（std::string を ImGui に渡すための小道具）
    auto boneInput = [&](const char* label, std::string& target)
    {
        char buf[128]{};
        strncpy_s(buf, sizeof(buf), target.c_str(), _TRUNCATE);
        if (ImGui::InputText(label, buf, sizeof(buf))) { target = buf; return true; }
        return false;
    };
    // 根元/末端をまとめて編集する2値スライダ
    auto pairSlider = [&](const char* label, float& root, float& tip, float lo, float hi)
    {
        float v[2] = { root, tip };
        if (ImGui::SliderFloat2(label, v, lo, hi)) { root = v[0]; tip = v[1]; return true; }
        return false;
    };

    // ---------------- チェーン ---------------- //
    ImGui::SeparatorText(u8("チェーン"));
    for (int i = 0; i < (int)kp.settings.chains.size(); ++i)
    {
        auto& c = kp.settings.chains[i];
        ImGui::PushID(i);
        const std::string title = (c.rootBone.empty() ? std::string(u8("(ルート未設定)")) : c.rootBone);
        if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= boneInput(u8("ルートボーン"), c.rootBone);
            changed |= ImGui::Checkbox(u8("有効"), &c.enabled);
            changed |= pairSlider(u8("減衰 根/先"), c.dampingRoot, c.dampingTip, 0.0f, 1.0f);
            changed |= pairSlider(u8("剛性 根/先"), c.stiffnessRoot, c.stiffnessTip, 0.0f, 1.0f);
            changed |= pairSlider(u8("半径 根/先"), c.radiusRoot, c.radiusTip, 0.0f, 1.0f);
            changed |= pairSlider(u8("角度制限 根/先(負=無制限)"), c.limitAngleRoot, c.limitAngleTip, -1.0f, 180.0f);
            changed |= ImGui::SliderFloat(u8("本体追従(移動)"), &c.worldDampingLocation, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat(u8("本体追従(回転)"), &c.worldDampingRotation, 0.0f, 1.0f);
            changed |= ImGui::DragFloat(u8("ダミー長"), &c.dummyBoneLength, 0.001f, 0.001f, 1.0f);

            if (ImGui::SmallButton(u8("このチェーンを削除")))
            {
                kp.settings.chains.erase(kp.settings.chains.begin() + i);
                ImGui::TreePop();
                ImGui::PopID();
                kp.CommitConfig();
                return;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button(u8("チェーンを追加")))
    {
        kp.settings.chains.push_back(KawaiiChainSetting{});
        changed = true;
    }

    // ---------------- コリジョン ---------------- //
    ImGui::SeparatorText(u8("コリジョン"));

    for (int i = 0; i < (int)kp.settings.spheres.size(); ++i)
    {
        auto& c = kp.settings.spheres[i];
        ImGui::PushID(1000 + i);
        ImGui::Text(u8("球 %d"), i);
        changed |= boneInput(u8("追従ボーン##s"), c.bone);
        changed |= ImGui::DragFloat3(u8("オフセット##s"), &c.offset.x, 0.01f);
        changed |= ImGui::DragFloat(u8("半径##s"), &c.radius, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::Checkbox(u8("内側に閉じ込める"), &c.limitInside);
        if (ImGui::SmallButton(u8("削除##s")))
        {
            kp.settings.spheres.erase(kp.settings.spheres.begin() + i);
            ImGui::PopID();
            kp.CommitConfig();
            return;
        }
        ImGui::PopID();
        ImGui::Separator();
    }
    for (int i = 0; i < (int)kp.settings.capsules.size(); ++i)
    {
        auto& c = kp.settings.capsules[i];
        ImGui::PushID(2000 + i);
        ImGui::Text(u8("カプセル %d"), i);
        changed |= boneInput(u8("追従ボーン##c"), c.bone);
        changed |= ImGui::DragFloat3(u8("オフセット##c"), &c.offset.x, 0.01f);
        changed |= ImGui::DragFloat3(u8("終点##c"), &c.offsetTail.x, 0.01f);
        changed |= ImGui::DragFloat(u8("半径##c"), &c.radius, 0.01f, 0.0f, 10.0f);
        if (ImGui::SmallButton(u8("削除##c")))
        {
            kp.settings.capsules.erase(kp.settings.capsules.begin() + i);
            ImGui::PopID();
            kp.CommitConfig();
            return;
        }
        ImGui::PopID();
        ImGui::Separator();
    }
    for (int i = 0; i < (int)kp.settings.planars.size(); ++i)
    {
        auto& c = kp.settings.planars[i];
        ImGui::PushID(3000 + i);
        ImGui::Text(u8("平面 %d"), i);
        changed |= boneInput(u8("追従ボーン##p"), c.bone);
        changed |= ImGui::DragFloat3(u8("オフセット##p"), &c.offset.x, 0.01f);
        changed |= ImGui::DragFloat3(u8("法線##p"), &c.normal.x, 0.01f);
        if (ImGui::SmallButton(u8("削除##p")))
        {
            kp.settings.planars.erase(kp.settings.planars.begin() + i);
            ImGui::PopID();
            kp.CommitConfig();
            return;
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    if (ImGui::Button(u8("球を追加"))) { kp.settings.spheres.push_back({}); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button(u8("カプセルを追加"))) { kp.settings.capsules.push_back({}); changed = true; }
    ImGui::SameLine();
    if (ImGui::Button(u8("平面を追加"))) { kp.settings.planars.push_back({}); changed = true; }

    ImGui::Spacing();
    ImGui::SliderInt(u8("拘束の反復回数"), &kp.settings.collisionIterations, 1, 8);
    if (ImGui::Button(u8("姿勢へ再同期")) && kp.impl) kp.impl->RequestResync();

    if (changed) kp.CommitConfig();
}

template<typename T>
static ComponentMeta MakeMeta(const std::string& name)
{
    return {
        name,
        [](World& w, Entity e) { return w.HasComponent<T>(e); },
        [](World& w, Entity e) { w.AddComponent<T>(e, T{}); },

        // draw：あれば見出しごと表示、Reflect結果を汎用描画
        [name](World& w, Entity e)
        {
            if (!w.HasComponent<T>(e)) return;
            if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
				auto& comp = w.GetComponent<T>(e);
                
                FieldList fl;
                w.GetComponent<T>(e).Reflect(fl);
                DrawFieldList(fl);

                DrawExtraUI<T>(w, e, comp);

                ImGui::Spacing();
                if (ImGui::SmallButton(("Remove##" + name).c_str()))
                    w.DeleteComponent<T>(e);
            }
        },

        // save：Reflectで申告されたフィールドをそのままjsonへ
        [name](World& w, Entity e, json& out)
        {
            if (!w.HasComponent<T>(e)) return;
            FieldList fl;
            w.GetComponent<T>(e).Reflect(fl);
            json fields;
            for (auto& f : fl.fields) FieldToJson(fields[f.name], f);
            out[name] = fields;
        },

        // load：jsonからReflectのポインタへ書き戻す
        [name](World& w, Entity e, const json& in)
        {
            if (!in.contains(name)) return;
            if (!w.HasComponent<T>(e)) w.AddComponent<T>(e, T{});
            FieldList fl;
            w.GetComponent<T>(e).Reflect(fl);
            const auto& fields = in[name];
            for (auto& f : fl.fields)
                if (fields.contains(f.name)) JsonToField(fields[f.name], f);
        }
    };
}


//   （Inspector・AddComponent・シーンSave/Load全部に自動反映）
static const std::vector<ComponentMeta> g_Components =
{
	MakeMeta<AnimatorComponent>("Animator"),
	MakeMeta<AudioSourceComponent>("Audio Source"),
	MakeMeta<AudioListenerComponent>("Audio Listener"),
	MakeMeta<CameraComponent>("Camera"),
	MakeMeta<CameraAnimationComponent>("Camera Animation"),
    MakeMeta<CanvasComponent>("Canvas"),
	MakeMeta<ColliderComponent>("Collider"),
    MakeMeta<LightComponent>("Light"),
    MakeMeta<FreeLookComponent>("Free Look"),
    MakeMeta<SpinComponent>("Spin"),
    MakeMeta<RectTransformComponent>("Rect Transform"),
    MakeMeta<UIImageComponent>("UI Image"),
    MakeMeta<UITextComponent>("UI Text"),
    MakeMeta<UIButtonComponent>("UI Button"),
    MakeMeta<TagComponent>("Tag"),
	MakeMeta<MusicSyncComponent>("Music Sync"),
	MakeMeta<RigidBodyComponent>("Rigid Body"),
	MakeMeta<ParticleEmitterComponent>("Particle Emitter"),
	MakeMeta<KawaiiPhysicsComponent>("Kawaii Physics"),
	MakeMeta<DepthOfFieldComponent>("Depth Of Field"),
	MakeMeta<LightCullComponent>("Light Cull"),
	MakeMeta<PlanarReflectionComponent>("Planar Reflection"),
	MakeMeta<ReflectionCasterComponent>("Reflection Caster"),
};

const std::vector<ComponentMeta>& ComponentRegistry::All()
{
    return g_Components;
}