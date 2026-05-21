// ---- include ---- //
#include "ModelLoader.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "Logger.hpp"
#include "Mesh.hpp"
#include <cstdlib>
#include <filesystem>

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
ModelLoadResult ModelLoader::LoadFromFile(const ComPtr<ID3D12Device>& device, const std::string& filepath, float scale)
{
    ModelLoadResult result{};

    // デバイスがnillptrの場合は処理しない
    if(device == nullptr)
    {
        LOG->LogError("Device is null. Cannot load model: " + filepath);
        return result;
	}
    // 準備
    std::vector<Vertex> Vertices;
	std::vector<std::uint32_t> Indices;
    Assimp::Importer importer;

    const unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FlipUVs |
        aiProcess_ConvertToLeftHanded;

    // モデル読み込み
    const aiScene* scene = importer.ReadFile(filepath, flags);

    // エラー処理
    if (scene == nullptr || !scene->HasMeshes())
    {
        LOG->LogError("Failed to load model: " + filepath + " - " + importer.GetErrorString());
        return result;
    }

    // メッシュの処理
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        // メッシュの取得
        const aiMesh* mesh = scene->mMeshes[meshIndex];

        // メッシュの頂点を取得
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(Vertices.size());

        // 頂点の追加
        Vertices.reserve(Vertices.size() + mesh->mNumVertices);

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            // Vertexに変換
            Vertex v{};
            const aiVector3D& pos = mesh->mVertices[i];
            v.position = { pos.x * scale, pos.y * scale, pos.z * scale };

            // 法線の追加
            if (mesh->HasNormals())
            {
                const aiVector3D& n = mesh->mNormals[i];
				v.normal = { n.x, n.y, n.z };
            }
            else
            {
                // 法線がない場合は上向きの法線を設定
                v.normal = { 0.0f, 1.0f, 0.0f };
            }

            // colorの追加
            if (mesh->HasVertexColors(0))
            {
                const aiColor4D& c = mesh->mColors[0][i];
                v.col = { c.r, c.g, c.b, c.a };
            }
            else
            {
                // 頂点カラーがない場合は白色を設定
                v.col = { 1.0f, 1.0f, 1.0f, 1.0f };
            }

            // UVの追加
            if (mesh->HasTextureCoords(0))
            {
                const aiVector3D& uv = mesh->mTextureCoords[0][i];
                v.uv = { uv.x, uv.y };
            }
            else
            {
                // UVがない場合は0を設定
                v.uv = { 0.0f,0.0f };
            }

            // 頂点の追加
            Vertices.push_back(v);
        }

        // スキンメッシュの追加
        if (result.skinData.infuences.size() < Vertices.size())
        {
            result.skinData.infuences.resize(Vertices.size());
        }

        // ボーンの追加
        for (unsigned int i = 0; i < mesh->mNumBones; ++i)
        {
            // メッシュからボーンの取得
            const aiBone* bone = mesh->mBones[i];
			const std::uint16_t boneIndex = GetOrAddBoneIndex(bone, result.skinData);

			// ボーンの影響を頂点に追加
            for (unsigned int w = 0; w < bone->mNumWeights; ++w)
            {
				const aiVertexWeight& weight = bone->mWeights[w];
                const std::uint32_t vertexID = baseVertex + weight.mVertexId;
                AddInfluence(result.skinData.infuences[vertexID], boneIndex, weight.mWeight);
            }
        }

        // faceの追加
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];

            // 三角形以外の面があった場合はエラー
            if (face.mNumIndices != 3)
            {
                LOG->LogError("Non-triangulated face found in mesh: " + std::to_string(meshIndex));
                continue;
            }

            // インデックスの追加
            Indices.push_back(baseVertex + face.mIndices[0]);
            Indices.push_back(baseVertex + face.mIndices[1]);
            Indices.push_back(baseVertex + face.mIndices[2]);
        }
    }

	NormalizeInfluence(result.skinData.infuences);

    if (Vertices.empty() && Indices.empty())
    {
        LOG->LogError("No valid vertices or indices found in model: " + filepath);
		return result;
    }

	auto mesh = std::make_shared<Mesh>();
	mesh->Init(device, Vertices, Indices, nullptr);
	result.mesh = mesh;

    const std::filesystem::path modelPath(filepath);
	const std::filesystem::path baseDir = modelPath.parent_path();

	// マテリアルの処理
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
		// マテリアルの取得
        const aiMaterial* material = scene->mMaterials[i];
        aiString texPath{};

		// ディフューズテクスチャの取得
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
        {
			const std::string texStr = texPath.C_Str();

			// テクスチャパスが空でなく、かつ '*' で始まらない場合はファイルパスとみなす
            if (!texStr.empty() && texStr[0] != '*')
            {
                const int index = std::atoi(texStr.c_str() + 1);
                if (index >= 0 && index < static_cast<int>(scene->mNumTextures))
                {
                    const aiTexture* tex = scene->mTextures[index];
                    if (tex->mHeight == 0 && tex->mWidth > 0)
                    {
                        const auto* data = reinterpret_cast<const std::uint8_t*>(tex->pcData);
                        result.diffusetextureData.assign(data, data + tex->mWidth);
                        break;
                    }
                    else
                    {
						LOG->LogWarning("Unsupported embedded texture format in model: " + filepath);
                    }
                }
            }
            else if (!texStr.empty())
            {
                std::filesystem::path texFile(texStr);
                if (texFile.is_relative())
                {
					texFile = baseDir / texFile;
                }

				result.diffuseTexturePath = texFile.wstring();
                break;
            }
        }
    }

	return result;
}
