#pragma once

#include "Defines.hpp"
#include "BeamRenderer.hpp"
#include <vector>
#include <random>
#include <cmath>
#include <functional>
#include <algorithm>

class FireworkSystem
{
public:
	enum class Shape : uint8_t
	{
		Peony,      // 牡丹(球)
		Willow,     // 柳(垂れる+トレイル)
		Ring,       // 輪
		Heart,      // ハート
		Senrin,     // 千輪(二段爆発)
		Text,       // 文字花火
	};

	// 効果音フック: (位置, 0=打ち上げ 1=炸裂)
	using SoundCallback = std::function<void(const float3&, int)>;

	void Init(unsigned seed = 12345u) { m_Rng.seed(seed); }
	void SetSoundCallback(SoundCallback cb) { m_OnSound = std::move(cb); }

	void Launch(
		_In_ const float3& groundPos,
		_In_ Shape shape,
		_In_ const float3& color,
		_In_ const std::string& text = "")
	{
		Shell s;
		s.pos = groundPos;
		s.velocity = float3{ Rand(-1.5f, 1.5f), Rand(16.0f, 22.0f), Rand(-1.5f, 1.5f) };
		s.color = color;
		s.shape = shape;
		s.fuse = Rand(1.4f, 1.8f);
		s.text = text;
		m_Shells.push_back(s);
		if (m_OnSound) m_OnSound(groundPos, 0);   // ヒュー
	}

	void Update(float dt)
	{
		constexpr float kGravity = 9.8f;

		// --- 打ち上げ玉 ---
		for (auto& s : m_Shells)
		{
			s.velocity.y -= kGravity * dt;
			s.pos = s.pos + s.velocity * dt;
			s.fuse -= dt;

			SpawnParticle(s.pos,
				float3{ Rand(-0.4f,0.4f), Rand(-0.4f,0.4f), Rand(-0.4f,0.4f) },
				float3{ 1.0f, 0.6f, 0.2f }, float3{ 1.0f, 0.3f, 0.1f },
				0.35f, 0.12f, 3.0f, false, false);
			if (s.fuse <= 0.0f)
				Explode(s);
		}
		m_Shells.erase(std::remove_if(m_Shells.begin(), m_Shells.end(),
			[](const Shell& s) { return s.fuse <= 0.0f; }), m_Shells.end());

		// --- 星 ---
		m_TrailTimer += dt;
		const bool recordTrail = (m_TrailTimer >= kTrailInterval);
		if (recordTrail) m_TrailTimer = 0.0f;

		std::vector<Particle> spawned;   // 千輪の子(ループ中のpush_back回避)
		for (auto& p : m_Particles)
		{
			p.velocity.y -= kGravity * dt * p.gravityScale;
			p.velocity = p.velocity * std::exp(-p.drag * dt);
			p.pos = p.pos + p.velocity * dt;
			p.life -= dt;
			if (p.willow) p.gravityScale = 1.0f;

			// トレイル: 一定間隔で過去位置を記録(リングバッファ)
			if (recordTrail)
			{
				p.trail[p.trailHead] = p.pos;
				p.trailHead = (p.trailHead + 1) % kTrailLen;
				if (p.trailCount < kTrailLen) p.trailCount++;
			}

			// 千輪: 寿命が尽きた瞬間に小爆発
			if (p.subShell && p.life <= 0.0f)
			{
				for (int i = 0; i < 12; ++i)
				{
					const float u = Rand(-1.0f, 1.0f);
					const float th = Rand(0.0f, 6.283185f);
					const float r = sqrtf(1.0f - u * u);
					float3 dir{ r * std::cos(th), u, r * std::sin(th) };
					spawned.push_back(MakeParticle(p.pos, dir * 3.5f,
						float3{ 1.0f,1.0f,1.0f }, p.color1,   // 白閃光→親の色
						Rand(0.4f, 0.7f), 0.13f, 2.5f, false, false));
				}
				if (m_OnSound) m_OnSound(p.pos, 1);
			}
		}
		m_Particles.erase(std::remove_if(m_Particles.begin(), m_Particles.end(),
			[](const Particle& p) { return p.life <= 0.0f; }), m_Particles.end());

		for (auto& p : spawned)
			if (m_Particles.size() < kMaxParticles) m_Particles.push_back(p);
	}

	void Emit(
		BeamRenderer& beam,
		_In_ const float3& camRight,
		_In_ const float3& camUp)
	{
		for (const auto& p : m_Particles)
		{
			const float t = p.life / p.maxLife;                    // 1→0
			const float flicker = 0.65f + 0.35f * std::sin(p.life * 40.0f + p.seed);
			const float a = std::sqrt(std::max(t * flicker, 0.0f));

			// 色の時間変化: 誕生直後は白閃光(color0)→本来の色(color1)
			const float ct = 1.0f - t;                             // 0→1
			const float mix = std::min(ct * 4.0f, 1.0f);           // 最初の25%で遷移

			// ランダムな色
			const float3 col{
				p.color1.x * Rand(0.1f, 1.0f),
				p.color1.y * Rand(0.1f, 1.0f),
				p.color1.z* Rand(0.1f, 1.0f) };


			// 本体: 中心が明るい4枚扇(放射グラデ) + 大きく薄いグロー層
			AddGlowQuad(beam, p.pos, camRight, camUp, p.size, col, a, 3.0f);
			AddGlowQuad(beam, p.pos, camRight, camUp, p.size * 3.0f, col, a * 0.25f, 3.0f);

			// トレイル(先端ほど細く・薄く)
			EmitTrail(beam, p, col, a, camRight, camUp);
		}
	}

	size_t ParticleCount() const { return m_Particles.size(); }

private:
	static constexpr int    kTrailLen = 6;
	static constexpr float  kTrailInterval = 0.03f;   // トレイル記録間隔(秒)
	static constexpr size_t kMaxParticles = 1200;

	struct Particle
	{
		float3 pos;
		float3 velocity;
		float3 color0;                 // 誕生時の色(白閃光)
		float3 color1;                 // 本来の色
		float life, maxLife;
		float size, drag;
		float gravityScale, seed;
		bool  willow;
		bool  subShell;                // 千輪の親
		float3 trail[kTrailLen]{};
		int    trailHead = 0;
		int    trailCount = 0;
	};

	struct Shell
	{
		float3 pos, velocity, color;
		Shape  shape;
		float  fuse;
		std::string text;
	};

	// 中心頂点が明るく、四隅がα=0の扇quad。LaserPSのpow(a,3)で放射グローになる
	void AddGlowQuad(
		BeamRenderer& beam, const float3& c,
		const float3& camRight, const float3& camUp,
		float size, const float3& rgb, float alpha, float g)
	{
		constexpr int kSegs = 8;
		const float hs = size * 0.5f;
		const float4 cc{ rgb.x * g, rgb.y * g, rgb.z * g, alpha };  // 中心
		const float4 ce{ rgb.x * g, rgb.y * g, rgb.z * g, 0.0f };   // 端

		float3 ring[kSegs];
		for (int i = 0; i < kSegs; ++i)
		{
			const float th = 6.283185f * i / kSegs;
			ring[i] = c + camRight * (std::cos(th) * hs) + camUp * (std::sin(th) * hs);
		}
		for (int i = 0; i < kSegs; ++i)
			beam.AddTriangle(c, cc, ring[i], ce, ring[(i + 1) % kSegs], ce);
	}

	void EmitTrail(
		BeamRenderer& beam, const Particle& p,
		const float3& rgb, float alpha,
		const float3& camRight, const float3& camUp)
	{
		if (p.trailCount < 2) return;
		(void)camUp;
		const float g = 2.5f;

		// 新しい→古い順に辿り、細く・薄くしていく
		float3 prev = p.pos;
		for (int i = 0; i < p.trailCount; ++i)
		{
			const int idx = (p.trailHead - 1 - i + kTrailLen * 2) % kTrailLen;
			const float3 cur = p.trail[idx];
			const float fade = 1.0f - (float)(i + 1) / (p.trailCount + 1);
			const float w = p.size * 0.25f * fade;
			const float a = alpha * fade * 0.6f;
			const float4 col{ rgb.x * g, rgb.y * g, rgb.z * g, a };
			const float4 colEnd{ rgb.x * g, rgb.y * g, rgb.z * g, 0.0f };

			const float3 side = camRight * w;
			beam.AddTriangle(prev - side, colEnd, prev + side, colEnd, cur + side, col);
			beam.AddTriangle(prev - side, colEnd, cur + side, col, cur - side, col);
			prev = cur;
		}
	}

	Particle MakeParticle(
		const float3& pos, const float3& vel,
		const float3& col0, const float3& col1,
		float life, float size, float drag, bool willow, bool subShell)
	{
		Particle p;
		p.pos = pos; p.velocity = vel;
		p.color0 = col0; p.color1 = col1;
		p.life = p.maxLife = life;
		p.size = size; p.drag = drag;
		p.gravityScale = willow ? 1.0f : 0.15f;
		p.seed = Rand(0.0f, 6.28f);
		p.willow = willow; p.subShell = subShell;
		return p;
	}

	void SpawnParticle(
		const float3& pos, const float3& vel,
		const float3& col0, const float3& col1,
		float life, float size, float drag, bool willow, bool subShell)
	{
		if (m_Particles.size() >= kMaxParticles) return;
		m_Particles.push_back(MakeParticle(pos, vel, col0, col1, life, size, drag, willow, subShell));
	}

	void Explode(const Shell& s)
	{
		if (m_OnSound) m_OnSound(s.pos, 1);   // ドーン
		const float3 white{ 1.0f, 1.0f, 1.0f };
		switch (s.shape)
		{
		case Shape::Peony:  ExplodeSphere(s, 180, 12.0f, 1.4f, 2.0f, false, false); break;
		case Shape::Willow: ExplodeSphere(s, 120, 9.0f, 2.2f, 0.6f, true, false); break;
		case Shape::Senrin: ExplodeSphere(s, 60, 8.0f, 1.0f, 2.5f, false, true);  break;
		case Shape::Ring:   ExplodeRing(s);  break;
		case Shape::Heart:  ExplodeHeart(s); break;
		case Shape::Text:   ExplodeText(s);  break;
		}
		(void)white;
	}

	void ExplodeSphere(const Shell& s, int count, float speed,
		float life, float drag, bool willow, bool subShell)
	{
		for (int i = 0; i < count; ++i)
		{
			const float u = Rand(-1.0f, 1.0f);
			const float th = Rand(0.0f, 6.283185f);
			const float r = sqrtf(1.0f - u * u);
			float3 dir{ r * std::cos(th), u, r * std::sin(th) };
			const float sp = speed * Rand(0.85f, 1.0f);
			SpawnParticle(s.pos, dir * sp,
				float3{ 1.0f,1.0f,1.0f }, s.color,        // 白閃光→色
				life * Rand(1.0f, 1.5f), 0.10f, drag, willow, subShell);
		}
	}

	void ExplodeRing(const Shell& s)
	{
		const int n = 140;
		for (int i = 0; i < n; ++i)
		{
			const float a = 6.283185f * i / n;
			float3 dir{ std::cos(a), std::sin(a), 0.0f };
			SpawnParticle(s.pos, dir * 11.0f,
				float3{ 1.0f,1.0f,1.0f }, s.color, 1.5f, 0.15f, 1.0f, false, false);
		}
	}

	void ExplodeHeart(const Shell& s)
	{
		const int n = 160;
		for (int i = 0; i < n; ++i)
		{
			const float a = 6.283185f * i / n;
			const float x = 16.0f * std::pow(std::sin(a), 3.0f);
			const float y = 13.0f * std::cos(a) - 5.0f * std::cos(2.0f * a)
				- 2.0f * std::cos(3.0f * a) - std::cos(4.0f * a);
			float3 dir{ x / 16.0f, y / 16.0f, 0.0f };
			SpawnParticle(s.pos, dir * 10.0f,
				float3{ 1.0f,1.0f,1.0f }, s.color, 1.6f, 0.10f, 1.8f, false, false);
		}
	}

	// UTF-8の次の1文字(コードポイント)を取り出す
	static uint32_t NextCodepoint(const std::string& s, size_t& i)
	{
		const unsigned char c = s[i];
		if (c < 0x80) { i += 1; return c; }
		if ((c & 0xE0) == 0xC0 && i + 1 < s.size())
		{
			uint32_t cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; return cp;
		}
		if ((c & 0xF0) == 0xE0 && i + 2 < s.size())
		{
			uint32_t cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3; return cp;
		}
		i += 1; return '?';
	}

	void ExplodeText(const Shell& s)
	{
		const std::string& txt = s.text.empty() ? "HI" : s.text;

		// UTF-8をコードポイント列に分解
		std::vector<uint32_t> cps;
		for (size_t i = 0; i < txt.size();)
			cps.push_back(NextCodepoint(txt, i));

		const float cellW = 6.0f;
		const float totalW = cellW * cps.size();

		for (size_t ci = 0; ci < cps.size(); ++ci)
		{
			const uint8_t* glyph = Glyph5x7(cps[ci]);
			if (!glyph) continue;
			for (int row = 0; row < 7; ++row)
				for (int colBit = 0; colBit < 5; ++colBit)
				{
					if (!(glyph[row] & (1 << (4 - colBit)))) continue;
					const float x = ci * cellW + colBit - totalW * 0.5f;
					const float y = (6 - row) * 1.0f - 3.0f;
					float3 dir{ x * 0.28f, y * 0.35f, 0.0f };
					SpawnParticle(s.pos, dir * 4.0f,
						float3{ 1.0f,1.0f,1.0f }, s.color, 2.0f, 0.15f, 3.5f, false, false);
				}
		}
	}

	// 5x7の簡易グリフ(必要な文字だけ追加していく)
	static const uint8_t* Glyph5x7(uint32_t cp)
	{
		static const uint8_t H[7] = { 0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001 };
		static const uint8_t I[7] = { 0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b11111 };
		static const uint8_t A[7] = { 0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001 };
		static const uint8_t L[7] = { 0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111 };
		static const uint8_t O[7] = { 0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110 };
		static const uint8_t D5[7] = { 0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110 };
		static const uint8_t KU[7] = { 
			0b00010,
			0b00100,
			0b01000,
			0b10000,
			0b01000,
			0b00100,
			0b00010 };  // く
		static const uint8_t DA[7] = {
			0b01010,
			0b11101,
			0b01000,
			0b01011,
			0b10000,
			0b10000,
			0b00011
		};  // だ
		static const uint8_t SA[7] = { 0b00100,0b00110,0b11111,0b00010,0b01100,0b10000,0b01111 };  // さ
		static const uint8_t IH[7] = { 0b10000,0b10010,0b10010,0b10010,0b10000,0b10001,0b01100 };  // い

		if (cp < 0x80)
		{
			switch (std::toupper(static_cast<int>(cp)))
			{
			case 'H': return H; case 'I': return I; case 'A': return A;
			case 'L': return L; case 'O': return O; case '5': return D5;
			}
			return nullptr;
		}
		switch (cp)
		{
		case 0x304F: return KU;   // く
		case 0x3060: return DA;   // だ
		case 0x3055: return SA;   // さ
		case 0x3044: return IH;   // い
		}
		return nullptr;
	}

	float Rand(float lo, float hi)
	{
		std::uniform_real_distribution<float> d(lo, hi);
		return d(m_Rng);
	}

	std::vector<Particle> m_Particles;
	std::vector<Shell>    m_Shells;
	std::mt19937          m_Rng;
	SoundCallback         m_OnSound;
	float                 m_TrailTimer = 0.0f;
};