#pragma once
#include "Material.hpp"
#include "Mesh.hpp"
#include "Defines.hpp"

struct Glyph
{
	float u0, v0, u1, v1;	// アトラス内UV
	float w, h;				// ピクセルサイズ
	float wofff, yoff;		// べ－スラインからのオフセット
	float xadvance;			// 次の文字までの送り
};

class FontAtlas
{
public:
	bool Build(
		_In_ const std::string& fontPathUtf8
	);

	static constexpr float kRefHeight = 64.0f;

	float RefHeight() const { return kRefHeight; }

	void BuildTextMesh(
		_Inout_ Mesh& out,
		_In_ const std::string& text,
		_In_ const COLOR& color
	) const;

	const Glyph* Get(int codepoint) const
	{
		auto it = m_Glyphs.find(codepoint);
		if (it != m_Glyphs.end())
		{
			return &it->second;
		}
		return nullptr;
	}
	inline Material& GetMaterial()
	{
		return m_Material;
	}
	inline float GetLineHeight() const
	{
		return m_LineHeight;
	}
private:
	std::unordered_map<int, Glyph> m_Glyphs;
	Material m_Material;
	float m_LineHeight = 0;
};

class FontLibrary
{
public:
	static FontAtlas* Get(const std::string& fontPath)
	{
		auto& map = Instance().m_Atlases;
		auto it = map.find(fontPath);
		if (it != map.end()) return it->second.get();

		auto atlas = std::make_unique<FontAtlas>();
		if (!atlas->Build(fontPath)) return nullptr;   // 1回だけ焼く
		FontAtlas* ptr = atlas.get();
		map[fontPath] = std::move(atlas);
		return ptr;
	}

	// デバイス破棄前に呼ぶ（任意・後述）
	static void Clear() { Instance().m_Atlases.clear(); }

private:
	static FontLibrary& Instance() { static FontLibrary i; return i; }
	std::unordered_map<std::string, std::unique_ptr<FontAtlas>> m_Atlases;
};