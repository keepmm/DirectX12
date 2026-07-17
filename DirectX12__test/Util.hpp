#pragma once
#include <string>
#include <Windows.h>
#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")
#include "Material.hpp"
#include "ModelData.hpp"

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

// --- マテリアル構築ヘルパ --- // 
inline std::vector<std::shared_ptr<Material>> BuildMaterials(
    const ModelLoadResult& model, const std::string& shaderName)
{
    std::vector<std::shared_ptr<Material>> out;
    std::unordered_map<std::wstring, std::shared_ptr<Material>> matCache;

    for (const auto& set : model.materials)
    {
        // 同じdiffuseパスなら既存Materialを再利用(GPUテクスチャの重複生成を回避)
        if (!set.diffuse.empty())
        {
            auto it = matCache.find(set.diffuse);
            if (it != matCache.end()) { out.push_back(it->second); continue; }
        }

        auto m = std::make_shared<Material>();
        m->Init();
        if (set.diffuseImage && set.diffuseImage->ok) m->CreateTextureFromRGBA(set.diffuseImage->width, set.diffuseImage->height, set.diffuseImage->pixels.data());
        if (set.normalImage && set.normalImage->ok)  m->CreateNormalFromRGBA(set.normalImage->width, set.normalImage->height, set.normalImage->pixels.data());
        if (set.metalImage && set.metalImage->ok)   m->CreateMetalFromRGBA(set.metalImage->width, set.metalImage->height, set.metalImage->pixels.data());
        if (set.roughImage && set.roughImage->ok)   m->CreateRoughFromRGBA(set.roughImage->width, set.roughImage->height, set.roughImage->pixels.data());

        if (!set.diffuse.empty()) matCache[set.diffuse] = m;
        out.push_back(m);
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
