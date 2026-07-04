#include "PMXLoader.hpp"
#include "Logger.hpp"
#include <fstream>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <windows.h>
#include "Util.hpp"

namespace {

	// バイナリ読み取りヘルパー
	struct Reader
	{
		const std::uint8_t* p;
		const std::uint8_t* end;

		template<class T> T Read()
		{
			T v{};
			std::memcpy(&v, p, sizeof(T));
			p += sizeof(T);
			return v;
		}
		// 可変長インデックス（1/2/4バイト、signed可）
		int ReadIndex(int size, bool sign)
		{
			if (size == 1) return sign ? (int)Read<std::int8_t>() : (int)Read<std::uint8_t>();
			if (size == 2) return sign ? (int)Read<std::int16_t>() : (int)Read<std::uint16_t>();
			return Read<std::int32_t>();
		}
		// PMXテキスト（int32長 + バイト列、encoding: 0=UTF16LE, 1=UTF8）
		std::wstring ReadText(int encoding)
		{
			const std::int32_t len = Read<std::int32_t>();
			std::wstring out;
			if (len <= 0) { return out; }
			if (encoding == 0) // UTF-16LE
			{
				out.assign(reinterpret_cast<const wchar_t*>(p), len / 2);
			}
			else // UTF-8 → wstring
			{
				int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)p, len, nullptr, 0);
				out.resize(wlen);
				MultiByteToWideChar(CP_UTF8, 0, (const char*)p, len, out.data(), wlen);
			}
			p += len;
			return out;
		}
		void Skip(size_t n) { p += n; }
	};

} // namespace

bool PMXLoader::Parse(const std::string& filepath, float scale, ModelCpuData& out)
{
	std::ifstream ifs(filepath, std::ios::binary);
	if (!ifs) { LOG->LogError("PMX: ファイルを開けません: " + filepath); return false; }
	std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(ifs)), {});
	if (buf.size() < 4) return false;

	Reader r{ buf.data(), buf.data() + buf.size() };

	// --- ヘッダ ---
	char magic[4]; std::memcpy(magic, r.p, 4); r.Skip(4);
	if (std::memcmp(magic, "PMX ", 4) != 0) { LOG->LogError("PMX: 不正なヘッダ"); return false; }

	const float version = r.Read<float>();
	const std::uint8_t globalsCount = r.Read<std::uint8_t>();
	std::uint8_t g[16] = {};
	for (int i = 0; i < globalsCount; ++i) g[i] = r.Read<std::uint8_t>();

	const int encoding = g[0];   // 0=UTF16, 1=UTF8
	const int addUV = g[1];
	const int vIdxSize = g[2];
	const int texIdxSize = g[3];
	const int matIdxSize = g[4];
	const int boneIdxSize = g[5];
	// g[6]=morph, g[7]=rigidbody（今回未使用）

	// --- モデル情報（名前・コメント）読み飛ばし ---
	r.ReadText(encoding); // name JP
	r.ReadText(encoding); // name EN
	r.ReadText(encoding); // comment JP
	r.ReadText(encoding); // comment EN

	// --- 頂点 ---
	const std::int32_t vertexCount = r.Read<std::int32_t>();
	out.vertices.reserve(vertexCount);
	for (int i = 0; i < vertexCount; ++i)
	{
		Vertex v{};
		v.position = { r.Read<float>() * scale, r.Read<float>() * scale, r.Read<float>() * scale };
		v.normal = { r.Read<float>(), r.Read<float>(), r.Read<float>() };
		v.uv = { r.Read<float>(), r.Read<float>() };
		v.col = { 1,1,1,1 };
		v.tangent = { 1,0,0 };

		// 追加UV（読み飛ばし）
		for (int a = 0; a < addUV; ++a) { r.Read<float>(); r.Read<float>(); r.Read<float>(); r.Read<float>(); }

		// ウェイト変形（今回はスキップ、後でSkinDataに）
		// ウェイト変形 → 頂点に格納
		const std::uint8_t weightType = r.Read<std::uint8_t>();
		int   bi[4] = { 0, 0, 0, 0 };
		float bw[4] = { 0, 0, 0, 0 };
		switch (weightType)
		{
		case 0: // BDEF1
			bi[0] = r.ReadIndex(boneIdxSize, true); bw[0] = 1.0f;
			break;
		case 1: // BDEF2
			bi[0] = r.ReadIndex(boneIdxSize, true);
			bi[1] = r.ReadIndex(boneIdxSize, true);
			bw[0] = r.Read<float>(); bw[1] = 1.0f - bw[0];
			break;
		case 3: // SDEF（ウェイトはBDEF2扱い、C/R0/R1はスキップ）
			bi[0] = r.ReadIndex(boneIdxSize, true);
			bi[1] = r.ReadIndex(boneIdxSize, true);
			bw[0] = r.Read<float>(); bw[1] = 1.0f - bw[0];
			r.Skip(sizeof(float) * 9);
			break;
		case 2: // BDEF4
		case 4: // QDEF
			for (int k = 0; k < 4; ++k) bi[k] = r.ReadIndex(boneIdxSize, true);
			for (int k = 0; k < 4; ++k) bw[k] = r.Read<float>();
			break;
		}
		for (int k = 0; k < 4; ++k)
		{
			v.boneIndices[k] = (std::uint32_t)(bi[k] < 0 ? 0 : bi[k]);
			v.boneWeights[k] = bw[k];
		}
		r.Read<float>(); // edge scale

		out.vertices.push_back(v);
	}

	// --- 面（インデックス）---
	const std::int32_t indexCount = r.Read<std::int32_t>();
	out.indices.reserve(indexCount);
	for (int i = 0; i < indexCount; ++i)
		out.indices.push_back((std::uint32_t)r.ReadIndex(vIdxSize, false));

	// --- テクスチャテーブル ---
	const std::filesystem::path baseDir = std::filesystem::path(filepath).parent_path();
	const std::int32_t texCount = r.Read<std::int32_t>();
	std::vector<std::wstring> texPaths(texCount);
	for (int i = 0; i < texCount; ++i)
	{
		std::wstring rel = r.ReadText(encoding);
		texPaths[i] = (baseDir / rel).wstring();
	}

	// --- マテリアル → サブメッシュ ---
	const std::int32_t matCount = r.Read<std::int32_t>();
	out.materials.resize(matCount);
	std::uint32_t indexOffset = 0;
	for (int i = 0; i < matCount; ++i)
	{
		std::wstring name = r.ReadText(encoding);            // name JP
		r.ReadText(encoding);            // name EN
		r.Skip(sizeof(float) * 4);       // diffuse
		r.Skip(sizeof(float) * 3);       // specular
		r.Skip(sizeof(float));           // specularity
		r.Skip(sizeof(float) * 3);       // ambient
		r.Read<std::uint8_t>();          // drawing flags
		r.Skip(sizeof(float) * 4);       // edge color
		r.Skip(sizeof(float));           // edge size
		const int texIndex = r.ReadIndex(texIdxSize, true);
		r.ReadIndex(texIdxSize, true);   // sphere texture
		r.Read<std::uint8_t>();          // sphere mode
		const std::uint8_t toonFlag = r.Read<std::uint8_t>();
		if (toonFlag == 0) r.ReadIndex(texIdxSize, true); else r.Read<std::uint8_t>(); // toon
		r.ReadText(encoding);            // memo
		const std::int32_t surfaceCount = r.Read<std::int32_t>();

		// マテリアルの名前もセット
		out.materials[i].name = WideToUtf8(name);

		// diffuseテクスチャをセット
		if (texIndex >= 0 && texIndex < texCount)
			out.materials[i].diffuse = texPaths[texIndex];

		// サブメッシュ（この material の面範囲）
		SubMesh sm{};
		sm.indexStart = indexOffset;
		sm.indexCount = (UINT)surfaceCount;
		sm.materialIndex = (UINT)i;
		out.subMeshes.push_back(sm);
		indexOffset += surfaceCount;
	}

	// --- ボーン ---
	using namespace DirectX;
	const std::int32_t boneCount = r.Read<std::int32_t>();
	std::vector<float3> bonePos(boneCount);
	std::vector<int>    boneParent(boneCount);

	out.skeleton.nodes.resize(boneCount);
	out.skinData.boneNames.resize(boneCount);
	out.skinData.offsetMatrices.resize(boneCount);

	for (int i = 0; i < boneCount; ++i)
	{
		std::wstring wname = r.ReadText(encoding);
		r.ReadText(encoding);   // name EN
		float3 pos = { r.Read<float>() * scale, r.Read<float>() * scale, r.Read<float>() * scale };
		int parent = r.ReadIndex(boneIdxSize, true);
		r.Read<std::int32_t>(); // layer
		std::uint16_t flags = r.Read<std::uint16_t>();

		// 接続先
		if (flags & 0x0001) r.ReadIndex(boneIdxSize, true);   // 接続ボーンindex
		else                r.Skip(sizeof(float) * 3);        // 接続オフセット

		// 付与（回転/移動）
		if (flags & (0x0100 | 0x0200))
		{
			const int appendParent					= r.ReadIndex(boneIdxSize, true);
			const float appendRate					= r.Read<float>();
			out.skeleton.nodes[i].appendParentIndex = appendParent;
			out.skeleton.nodes[i].appendRate		= appendRate;
			out.skeleton.nodes[i].appendRotation	= (flags & 0x0100) != 0;
			out.skeleton.nodes[i].appendMove		= (flags & 0x0200) != 0;
		}
		// 軸固定
		if (flags & 0x0400) r.Skip(sizeof(float) * 3);
		// ローカル軸
		if (flags & 0x0800) r.Skip(sizeof(float) * 6);
		// 外部親変形
		if (flags & 0x2000) r.Read<std::int32_t>();
		// IK
		if (flags & 0x0020)
		{
			IkData ik;
			ik.boneIndex = i;									// このボーンがIKボーン
			ik.targetIndex = r.ReadIndex(boneIdxSize, true);	// ターゲット(足首)
			ik.loopCount = r.Read<std::int32_t>();				// 反復回数
			ik.limitAngle = r.Read<float>();					// 制限角
			const std::int32_t linkCount = r.Read<std::int32_t>();
			for (int L = 0; L < linkCount; ++L)
			{
				// IKリンク
				IkLink link;
				// ボーンインデックス
				link.boneIndex = r.ReadIndex(boneIdxSize, true);
				// 回転制限の有無
				const std::uint8_t hasLimit = r.Read<std::uint8_t>();
				// 回転制限の有無がある場合は下限・上限を読み込む
				link.hasLimit = (hasLimit != 0);
				if (link.hasLimit)
				{
					link.lowerLimit = { r.Read<float>(),r.Read<float>(),r.Read<float>() };
					link.upperLimit = { r.Read<float>(),r.Read<float>(),r.Read<float>() };
				}
				// IKリンクをIKデータに追加
				ik.links.push_back(link);
			}
			out.skeleton.iks.push_back(ik);
		}

		bonePos[i] = pos;
		boneParent[i] = parent;

		const std::string name = WideToUtf8(wname);
		out.skeleton.nodes[i].name = name;
		out.skeleton.nodes[i].parentIndex = parent;
		out.skeleton.nameToIndex[name] = i;
		out.skinData.boneNames[i] = name;
		out.skinData.boneNametoIndex[name] = (std::uint16_t)i;
	}

	// ローカル変換（親からの相対移動）＋ offset（逆バインド＝-worldPos）
	for (int i = 0; i < boneCount; ++i)
	{
		const float3 p = bonePos[i];
		const float3 pp = (boneParent[i] >= 0 && boneParent[i] < boneCount)
			? bonePos[boneParent[i]] : float3{ 0,0,0 };

		XMMATRIX local = XMMatrixTranslation(p.x - pp.x, p.y - pp.y, p.z - pp.z);
		XMStoreFloat4x4(&out.skeleton.nodes[i].localTransform, local);

		XMMATRIX offset = XMMatrixTranslation(-p.x, -p.y, -p.z);
		XMStoreFloat4x4(&out.skinData.offsetMatrices[i], offset);
	}

	// 単一マテリアル互換（先頭のdiffuse）
	for (auto& m : out.materials)
		if (!m.diffuse.empty()) { out.diffuseTexturePath = m.diffuse; break; }

	out.success = true;
	LOG->LogInfo("PMX: 読み込み成功 頂点=" + std::to_string(vertexCount)
		+ " 面=" + std::to_string(indexCount / 3) + " 材質=" + std::to_string(matCount));
	return true;
}