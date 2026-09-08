#pragma once
#include <string>
#include <Windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")
#include "Material.hpp"
#include "ModelData.hpp"
#include "Debug.hpp"

// ---- UTF-8 <-> wide 変換 ---- //
inline std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}

inline std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

// ---- ファイル選択ダイアログ ---- //
// filter は "Image\0*.png;*.jpg\0All\0*.*\0" 形式（ダブルNUL終端）
inline bool OpenFileDialog(std::wstring& out, const wchar_t* filter)
{
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{ sizeof(ofn) };
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) { out = file; return true; }
    return false;
}

inline std::vector<std::shared_ptr<Material>> BuildMaterials(
    const ModelLoadResult& model, const std::string& shaderName)
{
    std::vector<std::shared_ptr<Material>> out;
    // テクスチャの共有元Material(リソースの実体を持つ)をパスごとに記録
    std::unordered_map<std::wstring, Material*> texOwner;

    for (const auto& set : model.materials)
    {
        auto m = std::make_shared<Material>();
        m->Init();
		m->baseAlpha = set.diffuseColor.w;
		m->baseColor = set.diffuseColor;
        m->baseColor.w = 1.0f;

        // 肌系マテリアルは既定で SSS を有効化（PMX の日本語名 / FBX の英語名）
        {
            // set.name は UTF-8。/utf-8 オプションが無く素の "肌" は CP932 になるため、UTF-8 バイトを直接書く
            static const char* kSkinKeys[] = {
                "\xE8\x82\x8C",   // hada (肌)
                "\xE9\xA1\x94",   // kao (顔)
                "\xE4\xBD\x93",   // karada (体)
                "skin", "Skin", "face", "Face", "body", "Body"
            };
            for (const char* key : kSkinKeys)
            {
                if (set.name.find(key) != std::string::npos)
                {
                    m->sssStrength = 0.6f;
                    m->sssWrap     = 0.4f;
                    m->sssTrans    = 0.15f;
                    m->sssColor    = { 0.95f, 0.55f, 0.45f, 1.0f }; // 赤すぎない血色
                    m->roughness   = 0.7f;                          // 肌はテカらせない
                    break;
                }
            }
        }

        if (!set.diffuse.empty())
        {
            auto it = texOwner.find(set.diffuse);
            if (it != texOwner.end())
            {
                m->ShareDiffuseTexture(*it->second);   // GPUリソースだけ共有
            }
            else if (set.diffuseImage && set.diffuseImage->ok)
            {
                m->CreateTextureFromRGBA(set.diffuseImage->width,
                    set.diffuseImage->height, set.diffuseImage->pixels.data());
                texOwner[set.diffuse] = m.get();
            }
        }
        else if (set.diffuseImage && set.diffuseImage->ok)
        {
            m->CreateTextureFromRGBA(set.diffuseImage->width,
                set.diffuseImage->height, set.diffuseImage->pixels.data());
        }

        if (set.normalImage && set.normalImage->ok) m->CreateNormalFromRGBA(set.normalImage->width, set.normalImage->height, set.normalImage->pixels.data());
        if (set.metalImage && set.metalImage->ok)   m->CreateMetalFromRGBA(set.metalImage->width, set.metalImage->height, set.metalImage->pixels.data());
        if (set.roughImage && set.roughImage->ok)   m->CreateRoughFromRGBA(set.roughImage->width, set.roughImage->height, set.roughImage->pixels.data());

        out.push_back(m);
        LOG->LogInfo("SubMat: " + set.name
            + " tex=" + WideToUtf8(set.diffuse)
            + " imgOk=" + std::to_string(set.diffuseImage && set.diffuseImage->ok));
    }
    return out;
}

inline std::string ShiftJisUtf8(const std::string& sjis)
{
    int wlen = MultiByteToWideChar(932, 0, sjis.c_str(), (int)sjis.size(), nullptr, 0);
	std::wstring w(wlen, L'\0');
	MultiByteToWideChar(932, 0, sjis.c_str(), (int)sjis.size(), w.data(), wlen);
	return WideToUtf8(w);
}

inline std::filesystem::path ResolveAssetPath(const std::filesystem::path& path)
{
	// 絶対パスまたは存在するパスならそのまま返す
    if (path.is_absolute() || std::filesystem::exists(path))
    {
        return path;
    }

    wchar_t exe[MAX_PATH]{};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path() / path;
}
