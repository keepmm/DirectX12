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
#include <DirectXTex.h>
#include "PMXLoader.hpp"
#include "Util.hpp"
#include "Scene.hpp"
#include "AsyncLoader.hpp"
#include "Components.hpp"
#include "MmdPhysics.hpp"

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

	const std::string ext = std::filesystem::path(filepath).extension().string();
    if (_stricmp(ext.c_str(), ".pmx") == 0)
    {
        if (!PMXLoader::Parse(filepath, scale, out))
            return out;   // success=false

        namespace fs = std::filesystem;
        fs::path base(filepath);
        for (auto& entry : fs::directory_iterator(base.parent_path()))
        {
            if (entry.path().extension() == ".vmd")
            {
                AnimationClip vc = LoadVMDClip(entry.path().string(), out.skeleton);
                if (!vc.channels.empty() || !vc.morphChannels.empty()) out.clips.push_back(std::move(vc));
            }
        }
    }
    else
    {

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
            const std::uint32_t indexStart = static_cast<std::uint32_t>(out.indices.size());
            for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
            {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3) { LOG->LogError("三角形ではない面が見つかりました: " + std::to_string(meshIndex)); continue; }
                out.indices.push_back(baseVertex + face.mIndices[0]);
                out.indices.push_back(baseVertex + face.mIndices[1]);
                out.indices.push_back(baseVertex + face.mIndices[2]);
            }
            const std::uint32_t indexCount =
                static_cast<std::uint32_t>(out.indices.size()) - indexStart;

            // このaiMesh = 1サブメッシュ（materialIndexで後述のmaterialsを参照）
            out.subMeshes.push_back(SubMesh{ indexStart, indexCount, mesh->mMaterialIndex });
        }

        NormalizeInfluence(out.skinData.infuences);
        if(scene->mRootNode)
			AddSkeletonNode(scene->mRootNode, -1, out.skeleton);

        //スキン影響を頂点バッファへ転機
        const size_t vcount = out.vertices.size();
        if (out.skinData.infuences.size() == vcount)
        {
            for (size_t i = 0; i < vcount; ++i)
            {
                const auto& inf = out.skinData.infuences[i];
                for (int k = 0; k < 4; ++k)
                {
					out.vertices[i].boneIndices[k] = inf.indices[k];
					out.vertices[i].boneWeights[k] = inf.weights[k];
                }
            }
        }

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
        out.materials.resize(scene->mNumMaterials);
        for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
        {
            const aiMaterial* material = scene->mMaterials[i];
            std::wstring diff = resolveByType(material, aiTextureType_DIFFUSE);
            std::wstring nor = resolveByType(material, aiTextureType_NORMALS);
            if (nor.empty()) nor = deriveSibling(diff, L"_diff", L"_nor_gl");

            out.materials[i].diffuse = diff;
            out.materials[i].normal = nor;
            out.materials[i].metal = deriveSibling(diff, L"_diff", L"_metal");
            out.materials[i].rough = deriveSibling(diff, L"_diff", L"_rough");
        }

        for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
        {
            const aiAnimation* anim = scene->mAnimations[a];
            AnimationClip clip;
            clip.name = anim->mName.C_Str();
            clip.tickPerSecond = (anim->mTicksPerSecond != 0.0) ? (float)anim->mTicksPerSecond : 25.0f;
            clip.duration = (float)(anim->mDuration / clip.tickPerSecond);

            for (unsigned int c = 0; c < anim->mNumChannels; ++c)
            {
                const aiNodeAnim* ch = anim->mChannels[c];
                BoneAnimationChannel channel;
                channel.boneName = ch->mNodeName.C_Str();

                const unsigned int n = ch->mNumPositionKeys;
                for (unsigned int k = 0; k < n; ++k)
                {
                    KeyFrame kf;
                    kf.time = (float)(ch->mPositionKeys[k].mTime / clip.tickPerSecond);
                    const auto& p = ch->mPositionKeys[k].mValue;
                    kf.position = { p.x, p.y, p.z };

                    const unsigned int rk = (k < ch->mNumRotationKeys) ? k : ch->mNumRotationKeys - 1;
                    const auto& q = ch->mRotationKeys[rk].mValue;
                    kf.rotation = { q.x, q.y, q.z, q.w };

                    const unsigned int sk = (k < ch->mNumScalingKeys) ? k : ch->mNumScalingKeys - 1;
                    const auto& s = ch->mScalingKeys[sk].mValue;
                    kf.scale = { s.x, s.y, s.z };

                    channel.keyFrames.push_back(kf);
                }
                clip.channels.push_back(std::move(channel));
            }
            out.clips.push_back(std::move(clip));
        }
    }
    // --- 各マップを RGBA8 にデコード（このParseFileはワーカースレッドで実行される）---
    auto decode = [](const std::wstring& path) -> DecodedImage
        {
            DecodedImage d;
            if (path.empty()) return d;

            DirectX::TexMetadata meta{};
            DirectX::ScratchImage img{};

            const std::wstring ext = std::filesystem::path(path).extension().wstring();
            HRESULT hr;
            if (_wcsicmp(ext.c_str(), L".hdr") == 0)
                hr = DirectX::LoadFromHDRFile(path.c_str(), &meta, img);
            else if (_wcsicmp(ext.c_str(), L".tga") == 0)
                hr = DirectX::LoadFromTGAFile(path.c_str(), &meta, img);
            else if (_wcsicmp(ext.c_str(), L".dds") == 0)
                hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &meta, img);
            else
                hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, &meta, img);

            if (FAILED(hr)) return d;

            const DirectX::Image* src = img.GetImage(0, 0, 0);
            if (src == nullptr) return d;

            // RGBA8 に統一（Material の CreateTextureFromRGBA が R8G8B8A8_UNORM 前提）
            DirectX::ScratchImage converted;
            if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM)
            {
                if (FAILED(DirectX::Convert(*src, DXGI_FORMAT_R8G8B8A8_UNORM,
                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted)))
                    return d;
                src = converted.GetImage(0, 0, 0);
            }

            d.width = static_cast<UINT>(src->width);
            d.height = static_cast<UINT>(src->height);
            d.pixels.assign(src->pixels, src->pixels + src->rowPitch * src->height);
            d.ok = true;
            return d;
        };

    std::unordered_map<std::wstring, std::shared_ptr<DecodedImage>> imgCache;
    auto decodeCached = [&](const std::wstring& path) -> std::shared_ptr<DecodedImage>
        {
            if (path.empty()) return nullptr;
            auto it = imgCache.find(path);
            if (it != imgCache.end()) return it->second;   // 既出は共有
            auto d = std::make_shared<DecodedImage>(decode(path));
            imgCache[path] = d;
            return d;
        };

    for (auto& set : out.materials)
    {
        set.diffuseImage    = decodeCached(set.diffuse);
        set.normalImage     = decodeCached(set.normal);
        set.metalImage      = decodeCached(set.metal);
        set.roughImage      = decodeCached(set.rough);
    }

    // 単一マテリアル互換：法線を持つ最初のマテリアルを従来フィールドにも入れる
    for (auto& m : out.materials)
        if (!m.normal.empty())
        {
            out.diffuseTexturePath = m.diffuse; out.normalTexturePath = m.normal;
            out.metalTexturePath = m.metal;     out.roughTexturePath = m.rough;
            break;
        }

    int wv = 0;
    for (auto& v : out.vertices)
    {
        float s = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
        if (s > 0.01f) ++wv;
    }
    LOG->LogInfo("verts_with_weight=" + std::to_string(wv) + "/" + std::to_string(out.vertices.size()));

    int matched = 0, total = 0;
    if (!out.clips.empty())
    {
        total = (int)out.clips[0].channels.size();
        for (auto& ch : out.clips[0].channels)
            if (out.skeleton.nameToIndex.count(ch.boneName)) ++matched;
    }
    LOG->LogInfo("matched_channels=" + std::to_string(matched) + "/" + std::to_string(total));

	out.success = true;
    return out;
}

ModelLoadResult ModelLoader::upload(const ModelCpuData& cpu)
{
	auto device = APP->GetDevice();
    ModelLoadResult result{};
    if (device == nullptr || !cpu.success) return result;

    auto mesh = std::make_shared<Mesh>();
    mesh->Init(device, cpu.vertices, cpu.indices,
        cpu.subMeshes.empty() ? nullptr : &cpu.subMeshes);
    result.mesh                 = mesh;
	result.subMeshes            = std::move(cpu.subMeshes);
	result.materials            = std::move(cpu.materials);
    result.diffuseTexturePath   = std::move(cpu.diffuseTexturePath);
	result.normalTexturePath    = std::move(cpu.normalTexturePath);
	result.metalTexturePath     = std::move(cpu.metalTexturePath);
	result.roughTexturePath     = std::move(cpu.roughTexturePath);
    result.diffusetextureData   = std::move(cpu.diffuseTextureData); 
	result.clips                = std::move(cpu.clips);
    result.skeleton             = std::move(cpu.skeleton);
    result.skinData             = std::move(cpu.skinData);
	result.morphs               = std::move(cpu.morphs);
	result.physics              = std::move(cpu.physics);
    return result;
}

std::vector<AnimationClip> ModelLoader::LoadAnimationsOnly(const std::string& filepath)
{
    std::vector<AnimationClip> clips;
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate | aiProcess_ConvertToLeftHanded);
    if (!scene) { LOG->LogError("anim load failed: " + filepath); return clips; }

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a)
    {
        const aiAnimation* anim = scene->mAnimations[a];
        AnimationClip clip;
        clip.name = std::filesystem::path(filepath).stem().string();  // ファイル名をクリップ名に
        clip.tickPerSecond = (anim->mTicksPerSecond != 0.0) ? (float)anim->mTicksPerSecond : 30.0f;
        clip.duration = (float)(anim->mDuration / clip.tickPerSecond);

        for (unsigned int c = 0; c < anim->mNumChannels; ++c)
        {
            const aiNodeAnim* ch = anim->mChannels[c];
            BoneAnimationChannel channel;
            channel.boneName = ch->mNodeName.C_Str();
            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k)
            {
                KeyFrame kf;
                kf.time = (float)(ch->mPositionKeys[k].mTime / clip.tickPerSecond);
                const auto& p = ch->mPositionKeys[k].mValue; kf.position = { p.x,p.y,p.z };
                const unsigned int rk = (k < ch->mNumRotationKeys) ? k : ch->mNumRotationKeys - 1;
                const auto& q = ch->mRotationKeys[rk].mValue; kf.rotation = { q.x,q.y,q.z,q.w };
                const unsigned int sk = (k < ch->mNumScalingKeys) ? k : ch->mNumScalingKeys - 1;
                const auto& s = ch->mScalingKeys[sk].mValue; kf.scale = { s.x,s.y,s.z };
                channel.keyFrames.push_back(kf);
            }
            clip.channels.push_back(std::move(channel));
        }
        clips.push_back(std::move(clip));
    }
    LOG->LogInfo("anim loaded: " + filepath + " clips=" + std::to_string(clips.size()));
    return clips;
}

AnimationClip ModelLoader::LoadVMDClip(const std::string& path, const Skeleton& skeleton)
{
    AnimationClip clip;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { LOG->LogError("VMD open failed: " + path); return clip; }
    std::vector<char> buf((std::istreambuf_iterator<char>(ifs)), {});
    const char* d = buf.data();
    if (buf.size() < 54) return clip;

    size_t o = 50;   // 30(signature)+20(model name)
    std::uint32_t count; std::memcpy(&count, d + o, 4); o += 4;

    std::unordered_map<std::string, BoneAnimationChannel> channels;
    float maxTime = 0.0f;

    for (std::uint32_t i = 0; i < count; ++i)
    {
        char nameBuf[16] = {};
        std::memcpy(nameBuf, d + o, 15); o += 15;
        const std::string name = ShiftJisUtf8(std::string(nameBuf));

        std::uint32_t frame; std::memcpy(&frame, d + o, 4); o += 4;
        float pos[3];  std::memcpy(pos, d + o, 12); o += 12;
        float quat[4]; std::memcpy(quat, d + o, 16); o += 16;  // x,y,z,w
        o += 64;   // 補間パラメータ(今回は線形/slerp)

        auto it = skeleton.nameToIndex.find(name);
        if (it == skeleton.nameToIndex.end()) continue;   // モデルに無いボーンはスキップ
        const auto& node = skeleton.nodes[it->second];

        // バインドオフセット = ローカル変換の平行移動成分
        const float3 bindOff = { node.localTransform._41, node.localTransform._42, node.localTransform._43 };

        KeyFrame kf;
        kf.time = frame / 30.0f;
        kf.position = { bindOff.x + pos[0], bindOff.y + pos[1], bindOff.z + pos[2] };
        kf.rotation = { quat[0], quat[1], quat[2], quat[3] };
        kf.scale = { 1,1,1 };

        auto& ch = channels[name];
        ch.boneName = name;
        ch.keyFrames.push_back(kf);
        maxTime = std::max(maxTime, kf.time);
    }

    // ---- 表情モーフセクション ----
    // ボーンキーの直後に [4]表情数 + [23 x N]表情キー が続く
    // (15:名前 + 4:フレーム + 4:重み)
    if (o + 4 <= buf.size())
    {
        std::uint32_t morphCount; std::memcpy(&morphCount, d + o, 4); o += 4;

        std::unordered_map<std::string, MorphAnimationChannel> morphChannels;
        for (std::uint32_t i = 0; i < morphCount; ++i)
        {
            if (o + 23 > buf.size()) break;   // 壊れたVMD対策

            char nameBuf[16] = {};
            std::memcpy(nameBuf, d + o, 15); o += 15;
            const std::string name = ShiftJisUtf8(std::string(nameBuf));

            std::uint32_t frame; std::memcpy(&frame, d + o, 4); o += 4;
            float weight;        std::memcpy(&weight, d + o, 4); o += 4;

            MorphKeyFrame kf;
            kf.time = frame / 30.0f;
            kf.weight = weight;

            auto& mc = morphChannels[name];
            mc.morphName = name;
            mc.keyFrames.push_back(kf);
            maxTime = std::max(maxTime, kf.time);
        }

        for (auto& [name, mc] : morphChannels)
        {
            std::sort(mc.keyFrames.begin(), mc.keyFrames.end(),
                [](const MorphKeyFrame& a, const MorphKeyFrame& b) { return a.time < b.time; });
            clip.morphChannels.push_back(std::move(mc));
        }
    }

    for (auto& [name, ch] : channels)
    {
        std::sort(ch.keyFrames.begin(), ch.keyFrames.end(),
            [](const KeyFrame& a, const KeyFrame& b) { return a.time < b.time; });
        clip.channels.push_back(std::move(ch));
    }
    clip.name = std::filesystem::path(path).stem().string();
    clip.tickPerSecond = 30.0f;
    clip.duration = maxTime;
    LOG->LogInfo("VMD loaded: " + path + " ch=" + std::to_string(clip.channels.size()) + " morph=" + std::to_string(clip.morphChannels.size())
        + " dur=" + std::to_string(clip.duration));
    return clip;
}

CameraClip ModelLoader::LoadVMDCameraClip(const std::string& path)
{
    CameraClip clip;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
    {
        LOG->LogError("VMD camera open failed: " + path);
		return clip;
    }
    std::vector<char> buf((std::istreambuf_iterator<char>(ifs)), {});
    const char* d = buf.data();
    const size_t size = buf.size();
    if (size < 54) return clip;

	size_t o = 50;   // 30(signature)+20(model name)

    // ボーンフレームを読み飛ばす
    std::uint32_t boneCount;
	std::memcpy(&boneCount, d + o, 4); o += 4;
    o += (size_t)boneCount * 111;

    // モーフフレームも読み飛ばす
    if (o + 4 > size) return clip;
    std::uint32_t morphCount;
	std::memcpy(&morphCount, d + o, 4); o += 4;
    o += (size_t)morphCount * 23;

    // カメラフレームを読み込む
    if (o + 4 > size) return clip;
	std::uint32_t cameraCount;
    std::memcpy(&cameraCount, d + 0, 4); o += 4;

    for (std::uint32_t i = 0; i < cameraCount; ++i)
    {
        if (o + 61 > size) break;
        CameraKeyFrame kf{};
        std::uint32_t frame; std::memcpy(&frame, d + o, 4);  o += 4;
        std::memcpy(&kf.distance, d + o, 4);                 o += 4;
        std::memcpy(&kf.target, d + o, 12);                  o += 12;
        std::memcpy(&kf.rotation, d + o, 12);                o += 12;
        o += 24;                                             // 補間ベジェ(まずは線形)
        std::uint32_t fov; std::memcpy(&fov, d + o, 4);      o += 4;
        o += 1;                                              // パースペクティブフラグ
        kf.time = frame / 30.0f;
        kf.fovY = (float)fov;
        clip.keys.push_back(kf);
        clip.duration = std::max(clip.duration, kf.time);
    }
    std::sort(clip.keys.begin(),clip.keys.end(),
        [](const CameraKeyFrame& a, const CameraKeyFrame& b) {return a.time < b.time; });
    LOG->LogInfo("VMD camera loaded: " + path + " keys=" + std::to_string(clip.keys.size()));
    return clip;
}

void ModelLoader::PopulateModelEntity(
    World& world, std::uint32_t entity,
    const std::string& modelpath, Scene* scene,
    int restoreClip, bool restorePlaying,
    const std::vector<std::string>& extraVmds,
    bool applyDefaultTransformScale)
{
    AsyncLoader::Get().LoadModelAsync(modelpath, 1.0f,
        [&world, entity, modelpath, scene, restoreClip, restorePlaying, extraVmds, applyDefaultTransformScale]
        (ModelLoadResult result)
        {
            if (!world.IsEntityAlive(entity) || !result.mesh) return;

            // Transformスケール(新規スポーン時のみ設定。復元時は保存値を尊重)
            if (applyDefaultTransformScale && world.HasComponent<TransformComponent>(entity))
            {
                auto& tr = world.GetComponent<TransformComponent>(entity);
                if (modelpath.ends_with(".fbx"))      tr.scale = float3(0.01f, 0.01f, 0.01f);
                else if (modelpath.ends_with(".pmx"))  tr.scale = float3(0.1f, 0.1f, 0.1f);
                tr.RebuildWorld();
            }

            // Mesh
            MeshComponent meshc{ result.mesh };
            meshc.FilePath = modelpath;
            world.AddComponent<MeshComponent>(entity, meshc);

            MaterialComponent mc{};

            // --- アニメーション/物理/モーフ ---
            if (!result.clips.empty() || !result.skinData.boneNames.empty())
            {
                AnimatorComponent an;
                an.skeleton = std::move(result.skeleton);
                an.skinData = std::move(result.skinData);
                an.clips = std::move(result.clips);   // フォルダVMDはParseFileで既に格納済み
                an.morphs = std::move(result.morphs);
                an.morphWeights.assign(an.morphs.morphs.size(), 0.0f);

                if (world.HasComponent<AnimatorComponent>(entity))
                {
					const auto& old = world.GetComponent<AnimatorComponent>(entity);
					an.clipPathsStr = old.clipPathsStr;
                    an.clipsRestored = false;
					an.time = old.time;
                    an.playing = old.playing;
                }

                // 物理
                if (scene)
                {
                    auto& physicsWorld = scene->EnsurePhysicsWorld();
                    if (physicsWorld.GetPhysics() == nullptr) physicsWorld.Init();
                    if (physicsWorld.GetPhysics())
                    {
                        auto physComp = MmdPhysicsComponent{};
                        physComp.impl = std::make_shared<MmdPhysics>();
                        physComp.impl->Init(physicsWorld.GetPhysics(), result.physics, an.skeleton);
                        world.AddComponent<MmdPhysicsComponent>(entity, physComp);
                    }
                }

                // 兄弟FBXアニメ("stem_..."形式)を追加
                namespace fs = std::filesystem;
                fs::path base(modelpath);
                std::string stem = base.stem().string();
                for (auto& entry : fs::directory_iterator(base.parent_path()))
                {
                    std::string fn = entry.path().stem().string();
                    if (entry.path().extension() == ".fbx" && fn != stem &&
                        fn.rfind(stem + "_", 0) == 0)
                    {
                        auto extra = ModelLoader::LoadAnimationsOnly(entry.path().string());
                        for (auto& c : extra) an.clips.push_back(std::move(c));
                    }
                }

                // --- シーン復元: 手動追加VMDの再ロード＋再生状態 ---
                an.extraClipNames = extraVmds;
                for (const auto& vmd : extraVmds)
                {
                    AnimationClip vc = ModelLoader::LoadVMDClip(vmd, an.skeleton);
                    if (!vc.channels.empty() || !vc.morphChannels.empty()) an.clips.push_back(std::move(vc));
                }
                if (restoreClip >= 0 && restoreClip < (int)an.clips.size())
                    an.currentClip = restoreClip;
                an.playing = restorePlaying;

                world.AddComponent<AnimatorComponent>(entity, std::move(an));
                mc.shaderName = "SkinnedToon";
            }

            // --- シーン復元値の引き継ぎ---
            std::vector<SubMaterialRestore> pending;
            if (world.HasComponent<MaterialComponent>(entity))
            {
                const auto& old = world.GetComponent<MaterialComponent>(entity);
                if (!old.shaderName.empty() && old.shaderName != "Basic")
                    mc.shaderName = old.shaderName;         // 復元値を優先
                mc.FilePath = old.FilePath;
                mc.RampFilePath = old.RampFilePath;
                pending = old.pendingSubs;
            }

            // マテリアル
            mc.materials = BuildMaterials(result, "PBR");
            for (size_t i = 0; i < mc.materials.size(); ++i)
                mc.materialnames.push_back(result.materials[i].name);
            if (!mc.materials.empty()) mc.material = mc.materials[0];

            // --- 保留していたサブマテリアル復元値を適用 ---
            for (size_t i = 0; i < pending.size() && i < mc.materials.size(); ++i)
            {
                auto& sm = mc.materials[i];
                if (!sm) continue;
                sm->shaderName = pending[i].shaderName;
                sm->roughness = pending[i].roughness;
                sm->metallic = pending[i].metallic;
                sm->sssStrength = pending[i].sssStrength;
                sm->sssWrap = pending[i].sssWrap;
                sm->sssTrans = pending[i].sssTrans;
                sm->sheen = pending[i].sheen;
                sm->sssColor = pending[i].sssColor;
				sm->baseAlpha = pending[i].baseAlpha;
            }

            world.AddComponent<MaterialComponent>(entity, mc);
        });
}