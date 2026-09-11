/**
 * \file	KawaiiPhysics.cpp
 * \brief	ボーンチェーンの疑似物理（Unreal Engine の KawaiiPhysics 相当）の実装
 * \author	keepmm
 * \date	2026/08/31
 *
 * 更新履歴
 *	2026/08/31	新規作成
 */

#include "KawaiiPhysics.hpp"
#include "Logger.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>

using namespace DirectX;

namespace
{
	/// @brief 2つの単位ベクトル間の最短回転
	XMVECTOR ShortestArc(FXMVECTOR from, FXMVECTOR to)
	{
		const float d = XMVectorGetX(XMVector3Dot(from, to));
		if (d > 0.99999f) return XMQuaternionIdentity();

		// ほぼ真逆。任意の直交軸で180度回す
		if (d < -0.99999f)
		{
			XMVECTOR axis = XMVector3Cross(from, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
			if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-8f)
				axis = XMVector3Cross(from, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
			return XMQuaternionRotationAxis(XMVector3Normalize(axis), XM_PI);
		}

		const XMVECTOR axis = XMVector3Cross(from, to);
		return XMQuaternionNormalize(XMVectorSetW(axis, 1.0f + d));
	}

	/// @brief 線分 ab 上で p に最も近い点
	XMVECTOR ClosestPointOnSegment(FXMVECTOR p, FXMVECTOR a, FXMVECTOR b)
	{
		const XMVECTOR ab = XMVectorSubtract(b, a);
		const float lenSq = XMVectorGetX(XMVector3LengthSq(ab));
		if (lenSq < 1e-8f) return a;
		float t = XMVectorGetX(XMVector3Dot(XMVectorSubtract(p, a), ab)) / lenSq;
		t = std::clamp(t, 0.0f, 1.0f);
		return XMVectorAdd(a, XMVectorScale(ab, t));
	}

	/// @brief ボーン名からワールド行列を引く（名前が空 or 未解決なら単位行列）
	XMMATRIX BoneWorld(const Skeleton& skel, const std::vector<XMMATRIX>& global,
		const std::string& name)
	{
		if (name.empty()) return XMMatrixIdentity();
		auto it = skel.nameToIndex.find(name);
		if (it == skel.nameToIndex.end()) return XMMatrixIdentity();
		if (it->second < 0 || it->second >= (int)global.size()) return XMMatrixIdentity();
		return global[it->second];
	}
}

void KawaiiPhysics::Build(const Skeleton& skel, const KawaiiPhysicsSettings& settings)
{
	m_Nodes.clear();
	m_Dirty = false;
	m_NeedsResync = true;

	// BoneNode::children はどのローダも埋めていない（宣言だけの死にフィールド）ので
	// parentIndex から子リストを自前で作る。
	m_Children.assign(skel.nodes.size(), {});
	for (int i = 0; i < (int)skel.nodes.size(); ++i)
	{
		const int p = skel.nodes[i].parentIndex;
		if (p >= 0 && p < (int)skel.nodes.size()) m_Children[p].push_back(i);
	}
	const std::vector<std::vector<int>>& children = m_Children;

	// 揺らしてよいボーンの絞り込み（空なら制限なし）
	std::vector<bool> allowed(skel.nodes.size(), settings.simulatedBones.empty());
	for (const auto& name : settings.simulatedBones)
	{
		auto a = skel.nameToIndex.find(name);
		if (a != skel.nameToIndex.end() &&
			a->second >= 0 && a->second < (int)allowed.size())
			allowed[a->second] = true;
	}

	for (int ci = 0; ci < (int)settings.chains.size(); ++ci)
	{
		const KawaiiChainSetting& chain = settings.chains[ci];
		if (!chain.enabled || chain.rootBone.empty()) continue;

		auto it = skel.nameToIndex.find(chain.rootBone);
		if (it == skel.nameToIndex.end())
		{
			LOG->LogWarning("KawaiiPhysics: ルートボーンが見つかりません: " + chain.rootBone);
			continue;
		}

		const size_t chainBegin = m_Nodes.size();
		std::vector<float> lengthFromRoot;

		// simulateRoot: 親ボーンを支点にして rootBone 自身から揺らす。
		// 支点は位置を FK から取るだけで、global へは書き戻さない。
		int anchorNode = -1;
		if (chain.simulateRoot)
		{
			const int pb = skel.nodes[it->second].parentIndex;
			if (pb >= 0)
			{
				Node a{};
				a.boneIndex = pb;
				a.parent = -1;
				a.chain = ci;
				a.anchorOnly = true;
				anchorNode = (int)m_Nodes.size();
				m_Nodes.push_back(a);
				lengthFromRoot.push_back(0.0f);
			}
		}

		// ルートから子孫を幅優先で収集（分岐そのまま）
		struct QItem { int bone; int parentNode; };
		std::deque<QItem> q{ { it->second, anchorNode } };

		while (!q.empty())
		{
			const QItem cur = q.front();
			q.pop_front();
			if (cur.bone < 0 || cur.bone >= (int)skel.nodes.size()) continue;

			Node n{};
			n.boneIndex = cur.bone;
			n.parent = cur.parentNode;
			n.chain = ci;

			// バインド姿勢のローカル平行移動 = 親からの距離
			if (cur.parentNode >= 0)
			{
				const XMMATRIX lm = XMLoadFloat4x4(&skel.nodes[cur.bone].localTransform);
				n.boneLength = XMVectorGetX(XMVector3Length(lm.r[3]));
			}

			const int myIndex = (int)m_Nodes.size();
			const float lenRoot =
				(cur.parentNode >= 0 ? lengthFromRoot[cur.parentNode - (int)chainBegin] : 0.0f)
				+ n.boneLength;

			m_Nodes.push_back(n);
			lengthFromRoot.push_back(lenRoot);

			for (int c : children[cur.bone])
			{
				if (!allowed[c]) continue;   // 揺れもの指定の無いボーンでチェーンを打ち切る
				q.push_back({ c, myIndex });
			}
		}

		// 末端にダミーボーンを足して、最後の実ボーンにも向きを与える
		const size_t realEnd = m_Nodes.size();
		std::vector<bool> hasChild(realEnd - chainBegin, false);
		for (size_t i = chainBegin; i < realEnd; ++i)
			if (m_Nodes[i].parent >= 0) hasChild[m_Nodes[i].parent - chainBegin] = true;

		for (size_t i = chainBegin; i < realEnd; ++i)
		{
			if (hasChild[i - chainBegin]) continue;

			Node d{};
			d.boneIndex = -1;					// ダミーはスケルトンに存在しない
			d.parent = (int)i;
			d.chain = ci;
			d.boneLength = chain.dummyBoneLength;
			m_Nodes.push_back(d);
			lengthFromRoot.push_back(lengthFromRoot[i - chainBegin] + d.boneLength);
		}

		// 根元 0 〜 末端 1 に正規化
		const float maxLen = *std::max_element(lengthFromRoot.begin(), lengthFromRoot.end());
		for (size_t i = chainBegin; i < m_Nodes.size(); ++i)
			m_Nodes[i].lengthRate =
			(maxLen > 1e-6f) ? lengthFromRoot[i - chainBegin] / maxLen : 0.0f;

		LOG->LogInfo("KawaiiPhysics: chain '" + chain.rootBone + "' bones="
			+ std::to_string(m_Nodes.size() - chainBegin));
	}

	// 実際に global を書き換えるボーンに印を付ける（支点は除く）
	m_Controlled.assign(skel.nodes.size(), 0);
	for (const Node& n : m_Nodes)
		if (!n.anchorOnly && n.boneIndex >= 0 && n.boneIndex < (int)m_Controlled.size())
			m_Controlled[n.boneIndex] = 1;
}

void KawaiiPhysics::SyncToPose()
{
	for (Node& n : m_Nodes)
	{
		n.location = n.poseLocation;
		n.prevLocation = n.poseLocation;
	}
}

void KawaiiPhysics::ResolveColliders(const Skeleton& skel,
	const std::vector<XMMATRIX>& global,
	const KawaiiPhysicsSettings& settings)
{
	m_ResolvedSpheres.clear();
	m_SphereLimitInside.clear();
	m_ResolvedCapsules.clear();
	m_ResolvedPlanes.clear();

	for (const auto& c : settings.spheres)
	{
		const XMMATRIX bw = BoneWorld(skel, global, c.bone);
		DebugSphere d{};
		XMStoreFloat3(&d.center, XMVector3Transform(XMLoadFloat3(&c.offset), bw));
		d.radius = c.radius;
		m_ResolvedSpheres.push_back(d);
		m_SphereLimitInside.push_back(c.limitInside);
	}

	for (const auto& c : settings.capsules)
	{
		const XMMATRIX bw = BoneWorld(skel, global, c.bone);
		DebugCapsule d{};
		XMStoreFloat3(&d.a, XMVector3Transform(XMLoadFloat3(&c.offset), bw));
		XMStoreFloat3(&d.b, XMVector3Transform(XMLoadFloat3(&c.offsetTail), bw));
		d.radius = c.radius;
		m_ResolvedCapsules.push_back(d);
	}

	for (const auto& c : settings.planars)
	{
		const XMMATRIX bw = BoneWorld(skel, global, c.bone);
		DebugPlane d{};
		XMStoreFloat3(&d.origin, XMVector3Transform(XMLoadFloat3(&c.offset), bw));
		XMStoreFloat3(&d.normal,
			XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&c.normal), bw)));
		m_ResolvedPlanes.push_back(d);
	}
}

void KawaiiPhysics::SolveCollision(const Node& node, XMVECTOR& pos) const
{
	// --- 球 ---
	for (size_t i = 0; i < m_ResolvedSpheres.size(); ++i)
	{
		const XMVECTOR c = XMLoadFloat3(&m_ResolvedSpheres[i].center);
		const float r = m_ResolvedSpheres[i].radius + node.radius;

		const XMVECTOR d = XMVectorSubtract(pos, c);
		const float len = XMVectorGetX(XMVector3Length(d));
		if (len < 1e-6f) continue;

		const bool inside = m_SphereLimitInside[i];
		if ((!inside && len < r) || (inside && len > r))
			pos = XMVectorAdd(c, XMVectorScale(d, r / len));
	}

	// --- カプセル ---
	for (const auto& cap : m_ResolvedCapsules)
	{
		const XMVECTOR p0 = XMLoadFloat3(&cap.a);
		const XMVECTOR p1 = XMLoadFloat3(&cap.b);
		const XMVECTOR closest = ClosestPointOnSegment(pos, p0, p1);
		const float r = cap.radius + node.radius;

		const XMVECTOR d = XMVectorSubtract(pos, closest);
		const float len = XMVectorGetX(XMVector3Length(d));
		if (len < 1e-6f || len >= r) continue;
		pos = XMVectorAdd(closest, XMVectorScale(d, r / len));
	}

	// --- 平面（法線側の半空間に押し出す） ---
	for (const auto& pl : m_ResolvedPlanes)
	{
		const XMVECTOR o = XMLoadFloat3(&pl.origin);
		const XMVECTOR n = XMLoadFloat3(&pl.normal);
		const float dist = XMVectorGetX(XMVector3Dot(XMVectorSubtract(pos, o), n));
		if (dist < node.radius)
			pos = XMVectorAdd(pos, XMVectorScale(n, node.radius - dist));
	}
}

void KawaiiPhysics::GetDebugChains(std::vector<DebugSegment>& out) const
{
	out.clear();
	for (const auto& n : m_Nodes)
	{
		if (n.parent < 0) continue;
		DebugSegment seg{};
		seg.a = m_Nodes[n.parent].location;
		seg.b = n.location;
		seg.anchor = m_Nodes[n.parent].anchorOnly;
		out.push_back(seg);
	}
}

void KawaiiPhysics::Apply(
	const Skeleton& skel,
	std::vector<XMMATRIX>& global,
	const XMMATRIX& world,
	const KawaiiPhysicsSettings& settings,
	float dt)
{
	if (m_Dirty) Build(skel, settings);
	if (m_Nodes.empty()) return;

	dt *= settings.timeScale;
	if (dt <= 0.0f) return;

	const size_t count = m_Nodes.size();

	// コリジョンはボーンに追従するだけなので、フレーム頭で1回解決すれば足りる。
	// （毎ボーン・毎反復で名前引きしていたぶんがまるごと消える）
	ResolveColliders(skel, global, settings);

	// ------------------------------------------------ //
	//  1. FK 姿勢（このフレームのあるべき位置）を取り込む
	// ------------------------------------------------ //
	for (size_t i = 0; i < count; ++i)
	{
		Node& n = m_Nodes[i];
		if (n.boneIndex >= 0 && n.boneIndex < (int)global.size())
		{
			XMStoreFloat3(&n.poseLocation, global[n.boneIndex].r[3]);
			continue;
		}

		// ダミー：親の向きにそのまま延長する
		const Node& p = m_Nodes[n.parent];
		XMVECTOR dir;
		if (p.parent >= 0)
		{
			dir = XMVectorSubtract(XMLoadFloat3(&p.poseLocation),
				XMLoadFloat3(&m_Nodes[p.parent].poseLocation));
		}
		else
		{
			// 祖父がいないので親ボーンのローカル +Y を使う
			dir = global[p.boneIndex].r[1];
		}
		dir = XMVector3Normalize(dir);
		XMStoreFloat3(&n.poseLocation,
			XMVectorAdd(XMLoadFloat3(&p.poseLocation), XMVectorScale(dir, n.boneLength)));
	}

	// 初回・シーク直後は慣性を持ち込まずに姿勢へ張り付ける
	if (m_NeedsResync || !m_HasPrevWorld)
	{
		SyncToPose();
		m_NeedsResync = false;
		XMStoreFloat4x4(&m_PrevWorld, world);
		m_HasPrevWorld = true;
		m_PrevDt = dt;
		return;
	}

	// ------------------------------------------------ //
	//  2. 本体の移動・回転差分（すべてモデル空間で扱う）
	// ------------------------------------------------ //
	// global はモデル空間（ワールド行列は頂点シェーダ側で掛かる）なので、
	// ワールド空間で与えられた重力・風・本体の移動はモデル空間へ持ち込む。
	const XMMATRIX prevWorld = XMLoadFloat4x4(&m_PrevWorld);
	const XMMATRIX invWorld = XMMatrixInverse(nullptr, world);

	// ワールドに固定された点が、モデル空間ではどう動いて見えるか。
	// p_model_now = p_model_prev * prevWorld * inverse(world)
	// worldDamping が 0 ならこの変換を丸ごと掛ける = ワールドに置き去り。
	const XMMATRIX lag = XMMatrixMultiply(prevWorld, invWorld);

	XMVECTOR lagScale, lagRot, lagTrans;
	if (!XMMatrixDecompose(&lagScale, &lagRot, &lagTrans, lag))
	{
		lagRot = XMQuaternionIdentity();
		lagTrans = XMVectorZero();
	}

	// 重力と風はワールド単位なので、スケールごとモデル空間へ変換する。
	// （スケール0.1なら、モデル空間では10倍の強さでないと見た目が合わない）
	const XMVECTOR gravityWind = XMVector3TransformNormal(
		XMVectorAdd(XMLoadFloat3(&settings.gravity), XMLoadFloat3(&settings.wind)),
		invWorld);

	// 可変フレームレート補正（Verlet の速度は dt に比例する）
	const float dtScale = (m_PrevDt > 1e-6f) ? (dt / m_PrevDt) : 1.0f;

	// ------------------------------------------------ //
	//  3. 親 → 子の順に 1 パスで解く
	// ------------------------------------------------ //
	for (size_t i = 0; i < count; ++i)
	{
		Node& n = m_Nodes[i];
		const KawaiiChainSetting& cs = settings.chains[n.chain];

		// 根元 0 〜 末端 1 でパラメータを補間（インスペクタの変更が即反映される）
		const float t = n.lengthRate;
		n.damping = std::lerp(cs.dampingRoot, cs.dampingTip, t);
		n.stiffness = std::lerp(cs.stiffnessRoot, cs.stiffnessTip, t);
		n.radius = std::lerp(cs.radiusRoot, cs.radiusTip, t);
		n.limitAngle = std::lerp(cs.limitAngleRoot, cs.limitAngleTip, t);

		// チェーンのルートは FK に完全追従するアンカー
		if (n.parent < 0)
		{
			n.location = n.poseLocation;
			n.prevLocation = n.poseLocation;
			continue;
		}

		XMVECTOR loc = XMLoadFloat3(&n.location);
		XMVECTOR prev = XMLoadFloat3(&n.prevLocation);

		// --- 本体の動きへの追従（1=完全追従で慣性なし, 0=ワールドに置き去り） ---
		// 追従しきれなかったぶんだけ lag 変換を掛けると、その差が揺れになる。
		{
			const float lagR = 1.0f - std::clamp(cs.worldDampingRotation, 0.0f, 1.0f);
			const float lagL = 1.0f - std::clamp(cs.worldDampingLocation, 0.0f, 1.0f);

			if (lagR > 0.0f)
			{
				const XMVECTOR q = XMQuaternionSlerp(XMQuaternionIdentity(), lagRot, lagR);
				loc = XMVector3Rotate(loc, q);    // モデル空間の原点が本体のピボット
				prev = XMVector3Rotate(prev, q);
			}
			if (lagL > 0.0f)
			{
				const XMVECTOR t = XMVectorScale(lagTrans, lagL);
				loc = XMVectorAdd(loc, t);
				prev = XMVectorAdd(prev, t);
			}
		}

		// --- Verlet 積分 ---
		XMVECTOR vel = XMVectorSubtract(loc, prev);
		vel = XMVectorScale(vel, (1.0f - std::clamp(n.damping, 0.0f, 1.0f)) * dtScale);
		XMStoreFloat3(&n.prevLocation, loc);
		loc = XMVectorAdd(loc, vel);
		loc = XMVectorAdd(loc, XMVectorScale(gravityWind, dt * dt));

		// --- Stiffness：FK 姿勢へ引き戻す ---
		loc = XMVectorLerp(loc, XMLoadFloat3(&n.poseLocation),
			std::clamp(n.stiffness, 0.0f, 1.0f));

		const XMVECTOR parentLoc = XMLoadFloat3(&m_Nodes[n.parent].location);

		// コリジョンで押し出しても長さ拘束が押し戻すので、収束するまで数回まわす
		const int iterations = std::max(1, settings.collisionIterations);
		for (int pass = 0; pass < iterations; ++pass)
		{
		// --- コリジョン ---
		SolveCollision(n, loc);

		// --- LimitAngle：FK の向きから離れすぎないようクランプ ---
		if (n.limitAngle >= 0.0f)
		{
			const XMVECTOR poseDir = XMVector3Normalize(XMVectorSubtract(
				XMLoadFloat3(&n.poseLocation), XMLoadFloat3(&m_Nodes[n.parent].poseLocation)));
			XMVECTOR curDir = XMVectorSubtract(loc, parentLoc);

			if (XMVectorGetX(XMVector3LengthSq(curDir)) > 1e-10f)
			{
				curDir = XMVector3Normalize(curDir);
				const float cosCur = XMVectorGetX(XMVector3Dot(poseDir, curDir));
				const float cosMax = cosf(XMConvertToRadians(n.limitAngle));
				if (cosCur < cosMax)
				{
					XMVECTOR axis = XMVector3Cross(poseDir, curDir);
					if (XMVectorGetX(XMVector3LengthSq(axis)) > 1e-10f)
					{
						axis = XMVector3Normalize(axis);
						const XMVECTOR q =
							XMQuaternionRotationAxis(axis, XMConvertToRadians(n.limitAngle));
						curDir = XMVector3Rotate(poseDir, q);
					}
					else
					{
						curDir = poseDir;   // 軸が作れない（制限0など）ので姿勢へ倒す
					}
					loc = XMVectorAdd(parentLoc, XMVectorScale(curDir, n.boneLength));
				}
			}
		}

		// --- 長さ拘束：親からの距離をバインド長に戻す ---
		{
			XMVECTOR d = XMVectorSubtract(loc, parentLoc);
			const float len = XMVectorGetX(XMVector3Length(d));
			if (len > 1e-6f)
				loc = XMVectorAdd(parentLoc, XMVectorScale(d, n.boneLength / len));
		}
		}   // 反復ここまで

		// 最後にもう一度だけ押し出す。
		// 反復の中では「コリジョン → 角度制限 → 長さ拘束」の順なので、
		// 最後に効くのは常に長さ拘束になる。押し出した先とバインド長が両立しない
		// 姿勢だと、締めくくりの長さ拘束がボーンをコライダーの中へ戻してしまう。
		// (スカートが体に近い姿勢＝腰を落とした瞬間などで表面化する)
		// ここで締めれば、多少ボーンが伸び縮みしても貫通だけは残らない
		SolveCollision(n, loc);

		XMStoreFloat3(&n.location, loc);
	}

	// ------------------------------------------------ //
	//  4. 位置 → 回転に変換して global を書き換える
	// ------------------------------------------------ //
	// 各ボーンの「子への向き」を姿勢時とシミュ後で比べて回転を作る。
	// 分岐しているときは子の平均方向を使う。
	std::vector<XMVECTOR> poseDirSum(count, XMVectorZero());
	std::vector<XMVECTOR> simDirSum(count, XMVectorZero());

	for (size_t i = 0; i < count; ++i)
	{
		const Node& n = m_Nodes[i];
		if (n.parent < 0) continue;
		const Node& p = m_Nodes[n.parent];

		const XMVECTOR pd = XMVectorSubtract(
			XMLoadFloat3(&n.poseLocation), XMLoadFloat3(&p.poseLocation));
		const XMVECTOR sd = XMVectorSubtract(
			XMLoadFloat3(&n.location), XMLoadFloat3(&p.location));
		if (XMVectorGetX(XMVector3LengthSq(pd)) < 1e-10f) continue;
		if (XMVectorGetX(XMVector3LengthSq(sd)) < 1e-10f) continue;

		poseDirSum[n.parent] = XMVectorAdd(poseDirSum[n.parent], XMVector3Normalize(pd));
		simDirSum[n.parent] = XMVectorAdd(simDirSum[n.parent], XMVector3Normalize(sd));
	}

	// 親の回転デルタを子へ伝播させる。これが無いとチェーン途中でロールがねじれ、
	// スキニング結果がぐにゃぐにゃになる。
	// m_Nodes は親 → 子の順に並ぶので前方向の1パスで足りる。
	std::vector<XMVECTOR> accum(count, XMQuaternionIdentity());

	for (size_t i = 0; i < count; ++i)
	{
		const Node& n = m_Nodes[i];
		const XMVECTOR parentAccum =
			(n.parent >= 0) ? accum[n.parent] : XMQuaternionIdentity();

		XMVECTOR q = parentAccum;
		if (XMVectorGetX(XMVector3LengthSq(poseDirSum[i])) > 1e-10f &&
			XMVectorGetX(XMVector3LengthSq(simDirSum[i])) > 1e-10f)
		{
			// 親の回転を受け継いだ「あるべき向き」からの差分だけを足す
			const XMVECTOR poseDir =
				XMVector3Rotate(XMVector3Normalize(poseDirSum[i]), parentAccum);
			const XMVECTOR delta = ShortestArc(poseDir, XMVector3Normalize(simDirSum[i]));
			q = XMQuaternionNormalize(XMQuaternionMultiply(parentAccum, delta));
		}
		accum[i] = q;

		if (n.boneIndex < 0 || n.boneIndex >= (int)global.size()) continue;
		if (n.anchorOnly) continue;   // 支点は他のボーンなので触らない

		const XMMATRIX before = global[n.boneIndex];

		XMMATRIX m = before;
		// 行ベクトル規約なので、ワールド回転は右から掛ける
		m.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		m = XMMatrixMultiply(m, XMMatrixRotationQuaternion(q));
		m.r[3] = XMVectorSetW(XMLoadFloat3(&n.location), 1.0f);
		global[n.boneIndex] = m;

		// --- チェーン外の子孫へ差分を伝播 --- //
		// 剛体が無くてチェーンから外れた毛先やアクセサリは、親を動かしても
		// FK のままの位置に取り残される。その間のメッシュが引き伸ばされるので、
		// 同じ差分行列を子孫にも掛けて追従させる。
		// delta = inverse(before) * after （行ベクトル: newChild = oldChild * delta）
		const XMMATRIX delta = XMMatrixMultiply(XMMatrixInverse(nullptr, before), m);

		if (!settings.propagateToDescendants) continue;

		std::vector<int> stack(m_Children[n.boneIndex].begin(), m_Children[n.boneIndex].end());
		while (!stack.empty())
		{
			const int c = stack.back();
			stack.pop_back();
			if (c < 0 || c >= (int)global.size()) continue;
			if (m_Controlled[c]) continue;   // 自前で解くボーンなので触らない

			global[c] = XMMatrixMultiply(global[c], delta);
			for (int gc : m_Children[c]) stack.push_back(gc);

			// 何を引きずっているかを一度だけ出す（手足が混ざっていれば一目で分かる）
			if (!m_LoggedPropagation && c < (int)skel.nodes.size())
				m_PropagatedNames.push_back(skel.nodes[c].name);
		}
	}

	if (!m_LoggedPropagation)
	{
		m_LoggedPropagation = true;
		std::string names;
		for (size_t i = 0; i < m_PropagatedNames.size(); ++i)
			names += (i ? ", " : "") + m_PropagatedNames[i];
		LOG->LogInfo("KawaiiPhysics: 子孫へ伝播 " + std::to_string(m_PropagatedNames.size())
			+ " 本 = " + names);
		m_PropagatedNames.clear();
		m_PropagatedNames.shrink_to_fit();
	}

	XMStoreFloat4x4(&m_PrevWorld, world);
	m_PrevDt = dt;
}

void KawaiiAutoSetupFromPmx(const Skeleton& skel, const PmxPhysics& phys,
	KawaiiPhysicsSettings& out)
{
	out.chains.clear();
	out.spheres.clear();
	out.capsules.clear();
	out.planars.clear();
	out.simulatedBones.clear();

	const int boneCount = (int)skel.nodes.size();
	if (boneCount == 0 || phys.rigidBodies.empty()) return;

	// --- バインド姿勢のワールド位置（PMX のボーンは平行移動のみ） --- //
	// 親が自分より後ろに並ぶ PMX もあるのでメモ化再帰で解く。
	std::vector<float3> bindPos(boneCount);
	std::vector<bool> resolved(boneCount, false);
	std::function<XMVECTOR(int)> bindOf = [&](int i) -> XMVECTOR
		{
			if (resolved[i]) return XMLoadFloat3(&bindPos[i]);
			resolved[i] = true;   // 循環参照よけ（先に立てる）

			XMVECTOR t = XMLoadFloat4x4(&skel.nodes[i].localTransform).r[3];
			const int p = skel.nodes[i].parentIndex;
			if (p >= 0 && p < boneCount && p != i) t = XMVectorAdd(t, bindOf(p));

			XMStoreFloat3(&bindPos[i], t);
			return t;
		};
	for (int i = 0; i < boneCount; ++i) bindOf(i);

	// --- 剛体の半径（形状ごとの代表値） --- //
	auto shapeRadius = [](const PmxRigidBody& rb) -> float
		{
			if (rb.shape == 0) return rb.size.x;					// 球
			if (rb.shape == 2) return rb.size.x;					// カプセル
			return std::min(rb.size.x, rb.size.z);					// 箱
		};

	// --- 物理演算剛体が付いたボーン = 揺れもの --- //
	std::vector<bool> simulated(boneCount, false);
	std::vector<float> boneRadius(boneCount, 0.0f);
	for (const auto& rb : phys.rigidBodies)
		if (rb.physicsType != 0 && rb.boneIndex >= 0 && rb.boneIndex < boneCount)
		{
			simulated[rb.boneIndex] = true;
			boneRadius[rb.boneIndex] = shapeRadius(rb);
		}

	// 子リスト（BoneNode::children は空なので parentIndex から作る）
	std::vector<std::vector<int>> childList(boneCount);
	for (int i = 0; i < boneCount; ++i)
	{
		const int p = skel.nodes[i].parentIndex;
		if (p >= 0 && p < boneCount && p != i) childList[p].push_back(i);
	}

	// --- ジョイントの回転制限 → ボーンごとの最大角(度) --- //
	// ジョイントは剛体Bを子として繋ぐので、Bのボーンに制限を割り当てる。
	constexpr float LIMIT_UNSET = -1.0f;
	std::vector<float> boneLimit(boneCount, LIMIT_UNSET);
	for (const auto& jt : phys.joints)
	{
		if (jt.rigidBodyB < 0 || jt.rigidBodyB >= (int)phys.rigidBodies.size()) continue;
		const int bi = phys.rigidBodies[jt.rigidBodyB].boneIndex;
		if (bi < 0 || bi >= boneCount) continue;

		// 6DOF の軸ごとの制限を、円錐1つ分の角度に丸める。
		// Y はツイスト軸でボーンの向きを変えないので除外する。これを含めると
		// ツイスト制限（普通いちばん緩い）に引きずられて円錐が広がりすぎる。
		const float rad = std::max({
			std::fabs(jt.rotLimitLower.x), std::fabs(jt.rotLimitUpper.x),
			std::fabs(jt.rotLimitLower.z), std::fabs(jt.rotLimitUpper.z) });
		// PMX のスカートは1ジョイントあたりの制限を極端に小さくして、段数で振り幅を
		// 稼ぐ作りが多い。これをそのまま円錐制限に使うとチェーンが固まって動かないので、
		// 小さすぎる値は「制限なし」として既定値に任せる
		constexpr float MIN_JOINT_LIMIT = 8.0f;   // 度
		const float deg = XMConvertToDegrees(rad);
		boneLimit[bi] = (deg >= MIN_JOINT_LIMIT) ? deg : LIMIT_UNSET;
	}

	// --- 親が揺れものでないものがチェーンの先頭 --- //
	for (int i = 0; i < boneCount; ++i)
	{
		if (!simulated[i]) continue;
		const int p = skel.nodes[i].parentIndex;
		if (p >= 0 && p < boneCount && simulated[p]) continue;   // 途中のボーン

		KawaiiChainSetting c{};
		c.rootBone = skel.nodes[i].name;
		c.simulateRoot = true;   // 親を支点にして自分から揺らす

		// 半径は剛体のサイズをそのまま使う。0 のままだとボーンが点として
		// 扱われ、脚のコリジョンをすり抜けて服が貫通する。
		c.radiusRoot = boneRadius[i];
		{
			int tip = i;
			while (true)   // 揺れものが続く限り末端まで降りる
			{
				int next = -1;
				for (int ch : childList[tip]) if (simulated[ch]) { next = ch; break; }
				if (next < 0) break;
				tip = next;
			}
			c.radiusTip = boneRadius[tip];

			// 角度制限。ジョイントが無いボーンは既定 45 度に留めておく
			// （無制限のままだと関節が折れて「ぐにゃぐにゃ」になる）
			constexpr float DEFAULT_LIMIT = 45.0f;
			c.limitAngleRoot = (boneLimit[i] >= 0.0f) ? boneLimit[i] : DEFAULT_LIMIT;
			c.limitAngleTip = (boneLimit[tip] >= 0.0f) ? boneLimit[tip] : DEFAULT_LIMIT;
		}
		out.chains.push_back(c);
	}

	// --- ボーン追従剛体 = コリジョン --- //
	for (const auto& rb : phys.rigidBodies)
	{
		if (rb.physicsType != 0) continue;
		if (rb.boneIndex < 0 || rb.boneIndex >= boneCount) continue;

		const std::string& bone = skel.nodes[rb.boneIndex].name;
		const float3& bp = bindPos[rb.boneIndex];
		const float3 off{ rb.position.x - bp.x, rb.position.y - bp.y, rb.position.z - bp.z };

		if (rb.shape == 0)   // 球
		{
			KawaiiSphereCollider s{};
			s.bone = bone;
			s.offset = off;
			s.radius = rb.size.x;
			out.spheres.push_back(s);
			continue;
		}

		// カプセル(x=半径 y=高さ)。箱は内接カプセルで近似する。
		// 剛体は固有の回転を持つので、軸を回してから両端点を作る。
		KawaiiCapsuleCollider cap{};
		cap.bone = bone;
		cap.radius = shapeRadius(rb);

		const float half = (rb.shape == 2)
			? rb.size.y * 0.5f								// カプセルは y=高さ
			: std::max(0.0f, rb.size.y - cap.radius);		// 箱は y=半径

		const XMMATRIX rbRot = XMMatrixRotationRollPitchYaw(
			rb.rotation.x, rb.rotation.y, rb.rotation.z);
		const XMVECTOR axis = XMVector3Normalize(rbRot.r[1]);	// PMX のカプセル軸は +Y

		const XMVECTOR o = XMVectorSet(off.x, off.y, off.z, 0.0f);
		XMStoreFloat3(&cap.offset, XMVectorSubtract(o, XMVectorScale(axis, half)));
		XMStoreFloat3(&cap.offsetTail, XMVectorAdd(o, XMVectorScale(axis, half)));
		out.capsules.push_back(cap);
	}

	// 揺らしてよいボーン = 物理演算剛体が付いたボーンだけ
	for (int i = 0; i < boneCount; ++i)
		if (simulated[i]) out.simulatedBones.push_back(skel.nodes[i].name);

	// どのボーンが揺れもの対象になったかを生成時点で出す（VMD 再生を待たずに確認できる）
	{
		std::string names;
		for (size_t i = 0; i < out.chains.size(); ++i)
			names += (i ? ", " : "") + out.chains[i].rootBone;
		LOG->LogInfo("KawaiiPhysics: chain roots = " + names);
	}

	LOG->LogInfo("KawaiiPhysics: 自動生成 chains=" + std::to_string(out.chains.size())
		+ " spheres=" + std::to_string(out.spheres.size())
		+ " capsules=" + std::to_string(out.capsules.size()));
}
