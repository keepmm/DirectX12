// FontAtlas.cpp の先頭で1回だけ
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#include "FontAtlas.hpp"
#include "Util.hpp"

bool FontAtlas::Build(const std::string& fontPathUtf8)
{
    // フォントファイル読み込み
    std::ifstream f(Utf8ToWide(fontPathUtf8),std::ios::binary);

    if (!f) return false;

    std::vector<unsigned char> ttf(std::istreambuf_iterator<char>(f), {});

    stbtt_fontinfo font;
    stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0));
	const float scale = stbtt_ScaleForPixelHeight(&font, kRefHeight);

    int ascent, descent, lineGap;
	stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
	m_LineHeight = (ascent - descent + lineGap) * scale;

    // ASCIIに焼く
    const int ATLAS = 512;
	std::vector<unsigned char> gray(ATLAS * ATLAS, 0);
    int penX = 1, penY = 1, rowH = 0;

    for (int c = 32; c < 127; ++c)
    {
        int w, h, xo, yo;
        unsigned char* bmp = stbtt_GetCodepointBitmap(&font, 0, scale, c, &w, &h, &xo, &yo);
        if (penX + w + 1 >= ATLAS)
        {
            penX = 1;
            penY += rowH + 1;
            rowH = 0;
        }
        // grayへコピー
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                gray[(penY + y) * ATLAS + (penX + x)] = bmp[y * w + x];

		int adv, lsb; 
        stbtt_GetCodepointHMetrics(&font,c, &adv, &lsb);

        Glyph g;
        g.u0 = (float)penX / ATLAS;
        g.v0 = (float)penY / ATLAS;
		g.u1 = (float)(penX + w) / ATLAS;
        g.v1 = (float)(penY + h) / ATLAS;
        g.w = (float)w;
        g.h = (float)h;
        g.wofff = (float)xo;
        g.yoff = (float)yo;
        g.xadvance = adv * scale;
        m_Glyphs[c] = g;


        penX += w + 1;
		rowH = std::max(rowH, h);
		stbtt_FreeBitmap(bmp, nullptr);
    }

    // R8 - > RGBA8に変換
    std::vector<std::uint8_t> rgba(ATLAS * ATLAS * 4);
    for (int i = 0; i < ATLAS * ATLAS; ++i)
    {
        rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = gray[i];
    }
	return m_Material.CreateTextureFromRGBA(ATLAS, ATLAS, rgba.data());
}

void FontAtlas::BuildTextMesh(Mesh& out, const std::string& text, const COLOR& color) const
{
    std::vector<Vertex> vb;
    std::vector<std::uint32_t> ib;
    float penX = 0.0f;
    const float baselise = m_LineHeight * 0.8f;

    for (unsigned char c : text)
    {
        auto it = m_Glyphs.find(c);
		if (it == m_Glyphs.end()) continue;
		const Glyph& g = it->second;

		const float x0 = penX + g.wofff;
		const float y0 = baselise + g.yoff;
        const float x1 = x0 + g.w;
        const float y1 = y0 + g.h;
        std::uint32_t base = (std::uint32_t)vb.size();

        vb.push_back({ {x0,y0,0},{0,0,-1}, color, {g.u0,g.v0} });
        vb.push_back({ {x1,y0,0},{0,0,-1}, color, {g.u1,g.v0} });
        vb.push_back({ {x0,y1,0},{0,0,-1}, color, {g.u0,g.v1} });
        vb.push_back({ {x1,y1,0},{0,0,-1}, color, {g.u1,g.v1} });
        ib.insert(ib.end(), { base + 0, base + 1, base + 2, base + 1, base + 3, base + 2 });

        penX += g.xadvance;
    }
    if (vb.empty()) return;
    out.Init(APP->GetDevice(), vb, ib);
}
