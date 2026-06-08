//#pragma once
//#include "Defines.hpp"
//#include "imgui.h"
//#include <unordered_map>
//#include <string>
//#include <vector>
//
//class DirectXApp;
//
///// PNG画像を読み込み、ImGuiで表示できる ImTextureID に変換してキャッシュするクラス
//class IconLibrary
//{
//public:
//    /// 最初に1度だけ呼ぶ
//    void Init(_In_ DirectXApp* app) { m_App = app; }
//
//    /// パスのPNGを読み込んで ImTextureID を返す（2回目以降はキャッシュから即返す）
//    ImTextureID GetOrLoad(_In_ const std::wstring& path);
//
//private:
//    bool LoadTexture(_In_ const std::wstring& path, _Out_ ImTextureID& outId);
//
//    DirectXApp* m_App = nullptr;
//    std::unordered_map<std::wstring, ImTextureID> m_Cache;     // パス → ImTextureID
//    std::vector<ComPtr<ID3D12Resource>> m_Textures;            // テクスチャ本体を保持（解放防止）
//};