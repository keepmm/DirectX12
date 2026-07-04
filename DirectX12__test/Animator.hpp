#pragma once
#include "ModelData.hpp"
#include <DirectXMath.h>

// クリップを time秒 でサンプルし、スキン行列パレット(転置済み)を outPalette に生成
inline void ComputePalette(
	const Skeleton& skel, const SkinData& skin,
	const AnimationClip& clip, float time,
	std::vector<float4x4>& outPalette)
{
	using namespace DirectX;

	// チャンネル名 -> チャンネル
	auto findChannel = [&](const std::string& name) -> const BoneAnimationChannel*
		{
			for (auto& c : clip.channels) if (c.boneName == name) return &c;
			return nullptr;
		};

	// キーフレーム補間
	auto sample = [&](const BoneAnimationChannel& ch) -> KeyFrame
		{
			const auto& ks = ch.keyFrames;
			if (ks.size() == 1) return ks[0];
			float t = fmodf(time, clip.duration);
			for (size_t i = 0; i + 1 < ks.size(); ++i)
			{
				if (t < ks[i + 1].time)
				{
					const KeyFrame& a = ks[i]; const KeyFrame& b = ks[i + 1];
					float f = (t - a.time) / std::max(b.time - a.time, 1e-6f);
					KeyFrame r;
					XMStoreFloat3(&r.position, XMVectorLerp(XMLoadFloat3(&a.position), XMLoadFloat3(&b.position), f));
					XMStoreFloat4(&r.rotation, XMQuaternionSlerp(XMLoadFloat4(&a.rotation), XMLoadFloat4(&b.rotation), f));
					XMStoreFloat3(&r.scale, XMVectorLerp(XMLoadFloat3(&a.scale), XMLoadFloat3(&b.scale), f));
					return r;
				}
			}
			return ks.back();
		};

	const size_t nodeCount = skel.nodes.size();
	std::vector<XMMATRIX> local(nodeCount), global(nodeCount);
	std::vector<XMVECTOR> boneRot(nodeCount), boneTrans(nodeCount);

	// FK ローカル回転 / 移動を計算
	for (size_t i = 0; i < nodeCount; ++i)
	{
		const BoneNode& node = skel.nodes[i];
		const BoneAnimationChannel* ch = findChannel(node.name);
		if (ch && !ch->keyFrames.empty())
		{
			KeyFrame kf = sample(*ch);
			boneRot[i] = XMQuaternionNormalize(XMLoadFloat4(&kf.rotation));
			boneTrans[i] = XMVectorSet(kf.position.x, kf.position.y, kf.position.z, 0.0f);
		}
		else
		{
			// バインド (PMXは平行移動のみ)
			XMMATRIX m = XMLoadFloat4x4(&node.localTransform);
			boneRot[i] = XMQuaternionIdentity();
			boneTrans[i] = m.r[3];	// 平行移動成分
		}
	}

	// FK ローカル行列を計算
	auto buildlocal = [&](size_t i)
		{
			local[i] = XMMatrixRotationQuaternion(boneRot[i]);
			local[i] = XMMatrixMultiply(local[i], XMMatrixTranslationFromVector(boneTrans[i]));
		};
	for (size_t i = 0; i < nodeCount; ++i)
	{
		buildlocal(i);
	}

	auto ComputeGlobal = [&]()
		{
			for (size_t i = 0; i < nodeCount; ++i)
			{
				int p = skel.nodes[i].parentIndex;
				global[i] = (p >= 0) ? local[i] * global[p] : local[i];
			}
		};
	ComputeGlobal();

	// CCD IK
	{
		static bool logged = false;
		if (!logged) {
			logged = true;
			LOG->LogInfo("IK count = " + std::to_string(skel.iks.size()));
			for (auto& ik : skel.iks)
			{
				std::string tn = (ik.targetIndex >= 0 && ik.targetIndex < (int)skel.nodes.size())
					? skel.nodes[ik.targetIndex].name : "?";
				std::string bn = (ik.boneIndex >= 0 && ik.boneIndex < (int)skel.nodes.size())
					? skel.nodes[ik.boneIndex].name : "?";
				LOG->LogInfo("  IK bone=" + bn + " target=" + tn
					+ " links=" + std::to_string(ik.links.size()));
			}
		}
	}
	for (const auto& ik : skel.iks)
	{
		// IKのボーンとターゲットが有効でなければスキップ
		if (ik.boneIndex < 0 || ik.targetIndex < 0) continue;

		// FKベイクモーション対策
		const BoneAnimationChannel* ikCh = findChannel(skel.nodes[ik.boneIndex].name);
		if (!ikCh || ikCh->keyFrames.size() <= 1) continue;

		// IKの反復回数だけループ
		for (int loop = 0; loop < ik.loopCount; ++loop)
		{
			for (const auto& link : ik.links)
			{
				// リンクボーンが有効でなければスキップ
				const int li = link.boneIndex;
				if (li < 0) continue;

				const XMVECTOR targetPos = global[ik.boneIndex].r[3];
				// IKターゲットボーン(左足首など)の位置 = 現在のエフェクタ点 (Effector)
				const XMVECTOR effPos = global[ik.targetIndex].r[3];
				if (XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(effPos, targetPos))) < 1e-6f) break;	// エフェクタが目標に到達したら終了

				const XMMATRIX invG = XMMatrixInverse(nullptr, global[li]);
				//XMVECTOR linkPosLocal = XMVector3TransformCoord(global[li].r[3], invG); 
				XMVECTOR effPosLocal = XMVector3TransformCoord(effPos, invG);
				XMVECTOR targetPosLocal = XMVector3TransformCoord(targetPos, invG);
				XMVECTOR linkPosLocal = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);	// リンクボーンのローカル座標系では原点がリンクボーンの位置

				XMVECTOR le = XMVector3Normalize(XMVectorSubtract(effPosLocal, linkPosLocal));
				XMVECTOR lt = XMVector3Normalize(XMVectorSubtract(targetPosLocal, linkPosLocal));

				// 正規化
				float dot = std::clamp(XMVectorGetX(XMVector3Dot(le, lt)), -1.0f, 1.0f);
				float angle = acosf(dot);
				if (angle < 1e-6f) continue;	// 角度が小さすぎる場合はスキップ
				angle = std::min(angle, ik.limitAngle);	// 制限角を適用

				// 回転軸を計算
				XMVECTOR axis = XMVector3Cross(le, lt);
				if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-8f) continue;	// 回転軸が小さすぎる場合はスキップ)
				axis = XMVector3Normalize(axis);

				// 回転をリンクボーンに適用
				XMVECTOR rot = XMQuaternionRotationAxis(axis, angle);
				boneRot[li] = XMQuaternionNormalize(XMQuaternionMultiply(boneRot[li], rot));

				//// ひざ等の角度制限
				if (link.hasLimit)
				{
					// // 回転をオイラー角に変換
					XMFLOAT4 q;
					XMStoreFloat4(&q, boneRot[li]);

					float pitch = asinf(std::clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f));	// X軸回転
					float yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
					float roll = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.z * q.z + q.x * q.x));

					if (link.lowerLimit.y > -1e-5f && link.upperLimit.y < 1e-5f &&
						link.lowerLimit.z > -1e-5f && link.upperLimit.z < 1e-5f)
					{
						// 現在のボーンのローカル回転からX軸回転量だけを抽出
						float pitch = 2.0f * atan2f(q.x, q.w);
						pitch = std::clamp(pitch, link.lowerLimit.x, link.upperLimit.x);

						// X軸のみのクォータニオンとして再構成
						boneRot[li] = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
					}
					else
					{
						float pitch = asinf(std::clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f));
						float yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
						float roll = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.z * q.z + q.x * q.x));

						pitch = std::clamp(pitch, link.lowerLimit.x, link.upperLimit.x);
						yaw = std::clamp(yaw, link.lowerLimit.y, link.upperLimit.y);
						roll = std::clamp(roll, link.lowerLimit.z, link.upperLimit.z);


						boneRot[li] = XMQuaternionRotationRollPitchYaw(pitch, yaw, roll);
					}
				}

				buildlocal(li);	// ローカル行列を更新
				ComputeGlobal();	// グローバル行列を更新
			}
		}
	}

	// ボーンの継承を適用
	for (size_t i = 0; i < nodeCount; ++i)
	{
		const BoneNode& node = skel.nodes[i];
		const int sp = node.appendParentIndex;

		// 継承なし
		if (sp < 0)continue;

		bool changed = false;

		// 回転継承
		if (node.appendRotation)
		{
			// 付与親のローカルアニメ回転をrateで補間して合成
			XMVECTOR add = XMQuaternionSlerp(XMQuaternionIdentity(), boneRot[sp], node.appendRate);
			boneRot[i] = XMQuaternionNormalize(XMQuaternionMultiply(add, boneRot[i]));
			changed = true;
		}

		// 移動継承
		if (node.appendMove)
		{
			// 付与親のローカルアニメ移動をrateで補間して合成
			XMVECTOR bindSp = XMLoadFloat4x4(&skel.nodes[sp].localTransform).r[3];
			XMVECTOR deltaT = XMVectorSubtract(boneTrans[sp], bindSp);
			boneTrans[i] = XMVectorAdd(boneTrans[i], XMVectorScale(deltaT, node.appendRate));
			changed = true;
		}

		// ローカル行列を更新
		if (changed)
		{
			buildlocal(i);
		}
	}
	ComputeGlobal();	// グローバル行列を更新

	// パレット
	outPalette.resize(skin.boneNames.size());
	for (size_t b = 0; b < skin.boneNames.size(); ++b)
	{
		XMMATRIX g = XMMatrixIdentity();
		auto it = skel.nameToIndex.find(skin.boneNames[b]);
		if(it != skel.nameToIndex.end())
		{
			int bi = it->second;
			g = global[bi];
		}
		XMMATRIX offset = XMLoadFloat4x4(&skin.offsetMatrices[b]);
		XMStoreFloat4x4(&outPalette[b], XMMatrixTranspose(offset * g));
	}
}