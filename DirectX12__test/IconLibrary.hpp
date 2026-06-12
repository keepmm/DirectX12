#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "imguiinit.hpp"

class DirectXApp;

/// PNG画像を読み込み、ImGuiで表示できる ImTextureID に変換してキャッシュするクラス
class IconLibrary
{
public:
    static IconLibrary* Get()
    {
        static IconLibrary instance;
        return &instance;
	}

    /// パスのPNGを読み込んで ImTextureID を返す（2回目以降はキャッシュから即返す）
    ImTextureID GetOrLoad(_In_ const std::wstring& path);

private:
    bool LoadTexture(_In_ const std::wstring& path, _Out_ ImTextureID& outId);

    std::unordered_map<std::wstring, ImTextureID> m_Cache;     // パス → ImTextureID
    std::vector<ComPtr<ID3D12Resource>> m_Textures;            // テクスチャ本体を保持（解放防止）
};