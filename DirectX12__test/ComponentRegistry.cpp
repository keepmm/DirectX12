#include "ComponentRegistry.hpp"
#include "Components.hpp"
#include "imguiinit.hpp"
#include "DirectX.hpp"
#include "Util.hpp"
#include "ModelLoader.hpp"
#include "Debug.hpp"

using json = nlohmann::json;

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
void DrawExtraUI<AnimatorComponent>(World&, Entity, AnimatorComponent& an)
{
    // --- VMD読み込みボタン (クリップが無くても押せるように先頭に置く) ---
    if (ImGui::Button(u8("VMD読み込み...")))
    {
        std::wstring picked;
        // フィルタはダブルNUL終端
        if (OpenFileDialog(picked, L"VMD Motion\0*.vmd\0All\0*.*\0"))
        {
            const std::string path = WideToUtf8(picked);
            AnimationClip vc = ModelLoader::LoadVMDClip(path, an.skeleton);
            if (!vc.channels.empty())
            {
                an.clips.push_back(std::move(vc));
                an.currentClip = (int)an.clips.size() - 1;   // 追加したクリップを選択
                an.time = 0.0f;
                an.playing = true;
            }
            else
            {
                LOG->LogWarning(u8("VMD読み込み失敗またはボーン不一致: ") + path);
            }
        }
    }

    // Assetsからのドラッグ&ドロップでもVMDを受け取る
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            std::string path((const char*)p->Data, p->DataSize - 1);
            if (path.size() > 4 &&
                _stricmp(path.c_str() + path.size() - 4, ".vmd") == 0)
            {
                AnimationClip vc = ModelLoader::LoadVMDClip(path, an.skeleton);
                if (!vc.channels.empty())
                {
                    an.clips.push_back(std::move(vc));
                    an.currentClip = (int)an.clips.size() - 1;
                    an.time = 0.0f;
                    an.playing = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Separator();

    if (an.clips.empty())
    {
        ImGui::TextDisabled(u8("クリップなし"));
        return;
    }

    // --- クリップ選択ドロップダウン ---
    const char* preview = (an.currentClip >= 0 && an.currentClip < (int)an.clips.size())
        ? an.clips[an.currentClip].name.c_str() : "(none)";

    if (ImGui::BeginCombo(u8("Clip"), preview))
    {
        for (int i = 0; i < (int)an.clips.size(); ++i)
        {
            const bool selected = (an.currentClip == i);
            std::string label = an.clips[i].name
                + " (" + std::to_string((int)an.clips[i].duration) + "s)";
            if (ImGui::Selectable(label.c_str(), selected))
            {
                an.currentClip = i;
                an.time = 0.0f;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // --- 再生コントロール ---
    if (ImGui::Button(an.playing ? u8("一時停止") : u8("再生")))
        an.playing = !an.playing;
    ImGui::SameLine();
    if (ImGui::Button(u8("最初から")))
        an.time = 0.0f;

    const auto& clip = an.clips[an.currentClip];
    if (clip.duration > 0.0f)
        ImGui::ProgressBar(an.time / clip.duration, ImVec2(-1, 0));
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
        if (OpenFileDialog(picked, dialogFilter)) { *path = WideToUtf8(picked); applyReset(); }
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
    MakeMeta<LightComponent>("Light"),
    MakeMeta<FreeLookComponent>("Free Look"),
    MakeMeta<SpinComponent>("Spin"),
    MakeMeta<CanvasComponent>("Canvas"),
    MakeMeta<RectTransformComponent>("Rect Transform"),
    MakeMeta<UIImageComponent>("UI Image"),
    MakeMeta<UITextComponent>("UI Text"),
	MakeMeta<AudioSourceComponent>("Audio Source"),
	MakeMeta<AudioListenerComponent>("Audio Listener"),
	MakeMeta<CameraComponent>("Camera"),
	MakeMeta<RigidBodyComponent>("Rigid Body"),
	MakeMeta<ColliderComponent>("Collider"),
	MakeMeta<AnimatorComponent>("Animator"),
};

const std::vector<ComponentMeta>& ComponentRegistry::All()
{
    return g_Components;
}