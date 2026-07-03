// ---- include ---- //
#include "ModelLoader.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "Logger.hpp"
#include "Mesh.hpp"
#include <cstdlib>
#include <filesystem>
#include "DirectX.hpp"
#include "assimp/material.h"

namespace
{
    // / aiMatrix4x4をfloat4x4に変換
    float4x4 ToFloat4x4(const aiMatrix4x4& mat)
    {
        return float4x4{
            mat.a1,mat.b1,mat.c1,mat.d1,
            mat.a2,mat.b2,mat.c2,mat.d2,
            mat.a3,mat.b3,mat.c3,mat.d3,
            mat.a4,mat.b4,mat.c4,mat.d4,
        };
    }

    /** 
     *  スケルトンノードを追加
     * @param node 
     * @param index 
     * @param skeleton  
     */
    int AddSkeletonNode(const aiNode* node, int parentIndex, Skeleton& skeleton)
    {
        // ノードの取得
        const int index = static_cast<int>(skeleton.nodes.size());

        // ボーンノードの追加
        BoneNode bone{};
        bone.name = node->mName.C_Str();
        if (bone.name.empty())
        {
            bone.name = "node_" + std::to_string(index);
        }
        // 親ノードのインデックスとローカル変換行列を設定
        bone.parentIndex = parentIndex;
        bone.localTransform = ToFloat4x4(node->mTransformation);

        // 子ノードの追加
        skeleton.nodes.push_back(bone);
        skeleton.nameToIndex[bone.name] = index;

        // 子ノードを再帰的に追加
        for (UINT i = 0; i < node->mNumChildren; ++i)
        {
            const int childindex = AddSkeletonNode(node->mChildren[i], index, skeleton);
            skeleton.nodes[index].children.push_back(childindex);
        }

        return index;
    }

    /** 
    *   ボーンの追加 or 既存のボーンのインデックスを取得
     *   @param bone 追加するボーンの情報
     *   @param skinData スキニングデータ
     *   @return 追加したボーンのインデックス、もしくは既存のボーンのインデックス
	 *
    */
    std::uint16_t GetOrAddBoneIndex(const aiBone* bone, SkinData& skinData)
    {
		// ボーン名からインデックスを取得
        const std::string name = bone->mName.C_Str();

		// 既に存在するボーンかどうかを確認
		auto it = skinData.boneNametoIndex.find(name);

        // 見つかった場合そのインデックスを返す
        if (it != skinData.boneNametoIndex.end())
        {
            return it->second;  
        }

		// 新しいボーンを追加
		const std::uint16_t index = static_cast<std::uint16_t>(skinData.boneNames.size());

		// ボーン名とオフセット行列を保存
        skinData.boneNames.push_back(name);
		skinData.offsetMatrices.push_back(ToFloat4x4(bone->mOffsetMatrix));
        skinData.boneNametoIndex.emplace(name, index);
        return index;
    }

    void AddInfluence(BoneInfuence& influence, std::uint16_t boneIndex, float weight)
    {
		// 影響度が0のスロットを探す
        int emptySlot = -1;
        int minSlot = 0;
        float minWeight = influence.weights[0];

		// 空きスロットを探すと同時に、最も影響度の小さいスロットも探す
        for (int i = 0; i < 4; ++i)
        {
            if (influence.weights[i] == 0.0f && emptySlot == -1)
            {
                emptySlot = i;
            }
            if (influence.weights[i] < minWeight)
            {
                minWeight = influence.weights[i];
				minSlot = i;
            }
        }

        const int slot = (emptySlot != -1) ? emptySlot : (weight > minWeight ? minSlot : -1);

        if (slot == -1)
        {
            return;
        }

		// スロットにボーンの影響を追加
        influence.indices[slot] = boneIndex;
		influence.weights[slot] = weight;
    }

    void NormalizeInfluence(std::vector<BoneInfuence>& influence)
    {
        for (auto& influence : influence)
        {
            const float sum = 
				influence.weights[0] + 
                influence.weights[1] + 
                influence.weights[2] + 
                influence.weights[3];

			// 0で割るのを防ぐため、合計が0以下の場合は正規化をスキップ
            if (sum <= 0.0f)
            {
                continue;
            }

            for (float& w : influence.weights)
            {
                w /= sum;
            }
        }
    }
}

/*
*     ファイルからモデルをロード
*/
ModelLoadResult ModelLoader::LoadFromFile(const ComPtr<ID3D12Device>& device,
    const std::string& filepath, float scale)
{
    if (device == nullptr)
    {
        LOG->LogError("Device is null. Cannot load model: " + filepath);
        return {};
    }
    return upload(ParseFile(filepath, scale));
}

ModelCpuData ModelLoader::ParseFile(const std::string& filepath, float scale)
{
    ModelCpuData out{};

    Assimp::Importer importer;

    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FlipUVs |
        aiProcess_ConvertToLeftHanded;

    // モデル読み込み
    const aiScene* scene = importer.ReadFile(filepath, flags);

    // エラー判定
    if (scene == nullptr || !scene->HasMeshes())
    {
        LOG->LogError("Failed to load model: " + filepath + " - " + importer.GetErrorString());
        return out;   // success=false
    }

    // メッシュの処理
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(out.vertices.size());

        out.vertices.reserve(out.vertices.size() + mesh->mNumVertices);

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            Vertex v{};
            const aiVector3D& pos = mesh->mVertices[i];
            v.position = { pos.x * scale, pos.y * scale, pos.z * scale };

            if (mesh->HasNormals())
            {
                const aiVector3D& n = mesh->mNormals[i];
                v.normal = { n.x, n.y, n.z };
            }
            else
            {
                v.normal = { 0.0f, 1.0f, 0.0f };
            }

            if (mesh->HasTangentsAndBitangents())
            {
                const aiVector3D& t = mesh->mTangents[i];
				v.tangent = { t.x, t.y, t.z };
            }
            else
            {
				v.tangent = { 1.0f, 0.0f, 0.0f };
            }

            if (mesh->HasVertexColors(0))
            {
                const aiColor4D& c = mesh->mColors[0][i];
                v.col = { c.r, c.g, c.b, c.a };
            }
            else
            {
                v.col = { 1.0f, 1.0f, 1.0f, 1.0f };
            }

            if (mesh->HasTextureCoords(0))
            {
                const aiVector3D& uv = mesh->mTextureCoords[0][i];
                v.uv = { uv.x, uv.y };
            }
            else
            {
                v.uv = { 0.0f, 0.0f };
            }

            out.vertices.push_back(v);
        }

        // スキンの影響度バッファ拡張
        if (out.skinData.infuences.size() < out.vertices.size())
        {
            out.skinData.infuences.resize(out.vertices.size());
        }

        // ボーンの処理
        for (unsigned int i = 0; i < mesh->mNumBones; ++i)
        {
            const aiBone* bone = mesh->mBones[i];
            const std::uint16_t boneIndex = GetOrAddBoneIndex(bone, out.skinData);

            for (unsigned int w = 0; w < bone->mNumWeights; ++w)
            {
                const aiVertexWeight& weight = bone->mWeights[w];
                const std::uint32_t vertexID = baseVertex + weight.mVertexId;
                AddInfluence(out.skinData.infuences[vertexID], boneIndex, weight.mWeight);
            }
        }

        // 面（インデックス）の処理
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3)
            {
                LOG->LogError("Non-triangulated face found in mesh: " + std::to_string(meshIndex));
                continue;
            }
            out.indices.push_back(baseVertex + face.mIndices[0]);
            out.indices.push_back(baseVertex + face.mIndices[1]);
            out.indices.push_back(baseVertex + face.mIndices[2]);
        }
    }

    NormalizeInfluence(out.skinData.infuences);

    if (out.vertices.empty() && out.indices.empty())
    {
        LOG->LogError("No valid vertices or indices found in model: " + filepath);
        return out;   // success=false
    }

    // --- マテリアル（テクスチャパス）処理：GPUは触らずパス/データだけ抽出 ---
    const std::filesystem::path modelPath(filepath);
    const std::filesystem::path baseDir = modelPath.parent_path();

    // ファイル名を basename 化し textures/ or 直下から実在パスを探す（.exr→.png）
    auto findInFolders = [&](std::filesystem::path fname) -> std::wstring
        {
            if (fname.extension() == ".exr") fname.replace_extension(".png");
            for (const auto& dir : { baseDir / "textures", baseDir })
            {
                std::filesystem::path cand = dir / fname;
                if (std::filesystem::exists(cand)) return cand.wstring();
            }
            return L"";
        };

    // assimpが返すパスは basename 化して解決
    auto resolveByType = [&](const aiMaterial* mat, aiTextureType type) -> std::wstring
        {
            aiString p{};
            if (mat->GetTexture(type, 0, &p) != AI_SUCCESS) return L"";
            std::string s = p.C_Str();
            if (s.empty() || s[0] == '*') return L"";
            return findInFolders(std::filesystem::path(s).filename());
        };

    // diffuseパスから兄弟マップを命名規則で派生（_diff→_metal 等）。拡張子は複数試す
    auto deriveSibling = [&](const std::wstring& diffuse,
        const std::wstring& fromKey, const std::wstring& toKey) -> std::wstring
        {
            if (diffuse.empty()) return L"";
            std::filesystem::path pd(diffuse);
            std::wstring stem = pd.stem().wstring();           // 例: bolt_..._diff_4k
            size_t at = stem.find(fromKey);
            if (at == std::wstring::npos) return L"";
            stem.replace(at, fromKey.size(), toKey);           // _diff → _metal
            for (const wchar_t* ext : { L".png", L".jpg", L".jpeg", L".tga" })
            {
                std::filesystem::path cand = pd.parent_path() / (stem + ext);
                if (std::filesystem::exists(cand)) return cand.wstring();
            }
            return L"";
        };

    // 法線マップを持つ（＝本体）マテリアルを優先して確定
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial* material = scene->mMaterials[i];

        std::wstring diff = resolveByType(material, aiTextureType_DIFFUSE);
        std::wstring nor = resolveByType(material, aiTextureType_NORMALS);
        if (nor.empty()) nor = deriveSibling(diff, L"_diff", L"_nor_gl");
        std::wstring metal = deriveSibling(diff, L"_diff", L"_metal");
        std::wstring rough = deriveSibling(diff, L"_diff", L"_rough");

        if (!nor.empty())   // 本体確定
        {
            out.diffuseTexturePath = diff;
            out.normalTexturePath = nor;
            out.metalTexturePath = metal;
            out.roughTexturePath = rough;
            break;
        }
        if (out.diffuseTexturePath.empty() && !diff.empty())
            out.diffuseTexturePath = diff;   // フォールバック
    }

    out.success = true;
    return out;
}

ModelLoadResult ModelLoader::upload(const ModelCpuData& cpu)
{
	auto device = APP->GetDevice();
    ModelLoadResult result{};
    if (device == nullptr || !cpu.success) return result;

    auto mesh = std::make_shared<Mesh>();
    mesh->Init(device, cpu.vertices, cpu.indices, nullptr);   // GPUバッファ作成
    result.mesh = mesh;

    result.diffuseTexturePath = cpu.diffuseTexturePath;
	result.normalTexturePath = cpu.normalTexturePath;
	result.metalTexturePath = cpu.metalTexturePath;
	result.roughTexturePath = cpu.roughTexturePath;
    result.diffusetextureData = cpu.diffuseTextureData;   // ※既存メンバ名（小文字t）
    result.skeleton = cpu.skeleton;
    result.skinData = cpu.skinData;
    return result;
}
