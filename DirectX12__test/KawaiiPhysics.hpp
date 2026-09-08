#pragma once
/**
 * \file	KawaiiPhysics.hpp
 * \brief	ボーンチェーンの疑似物理（Unreal Engine の KawaiiPhysics 相当）
 *
 *			剛体シミュレーションではなく、Verlet 積分と位置制約でボーン列を揺らす。
 *			軽量で破綻しにくく、パラメータの意味が直感的なので髪・スカート・
 *			アクセサリ等の二次アニメーションに向く。
 *
 * \author	keepmm
 * \date	2026/08/31
 *
 * 更新履歴
 *	2026/08/31	新規作成
 */

#include "Defines.hpp"
#include "ModelData.hpp"

#include <string>
#include <vector>
#include <sstream>

 /// @brief 揺れものチェーン1本分の設定
 ///        パラメータは「根元」と「末端」の2点を持ち、チェーン上で線形補間する。
 ///        （本家はカーブだが、2点補間で実用上の大半をカバーできる）
struct KawaiiChainSetting
{
	std::string rootBone;			// このボーン以下の全子孫を揺らす（分岐可）
	bool  enabled = true;

	float dampingRoot = 0.10f;	// 速度減衰 0=減衰なし 1=即停止
	float dampingTip = 0.40f;
	float stiffnessRoot = 0.20f;	// FK姿勢へ戻す強さ 0=自由 1=完全追従
	float stiffnessTip = 0.05f;
	float radiusRoot = 0.00f;	// コリジョン半径（0でコリジョンなし）
	float radiusTip = 0.00f;
	// FK姿勢からの最大角(度)。負値=無制限、0=完全固定。
	// PMX のジョイントは「制限 0 = 固定」を表現できるので 0 を無制限に使えない。
	float limitAngleRoot = -1.0f;
	float limitAngleTip = -1.0f;

	// キャラ本体の移動・回転への追従率。1=完全追従(慣性なし) 0=ワールドに置き去り
	float worldDampingLocation = 0.85f;
	float worldDampingRotation = 0.85f;

	float dummyBoneLength = 0.05f;	// 末端に延長するダミーボーンの長さ

	// true なら親ボーンをアンカーにして rootBone 自身から揺らす。
	// false（既定）は rootBone を FK 固定のアンカーとして扱う。
	bool simulateRoot = false;
};

/// @brief 球コリジョン（bone に追従する）
struct KawaiiSphereCollider
{
	std::string bone;		// 追従先ボーン（空ならモデルローカル固定）
	float3 offset{ 0.0f,0.0f,0.0f };
	float  radius = 0.1f;
	bool   limitInside = false;	// true なら球の内側に閉じ込める
};

/// @brief カプセルコリジョン
///        向きを決め打ちにせず、ボーンローカルの両端点で持つ。
///        （PMX の剛体は固有の回転を持つので +Y 固定だと脚などが合わない）
struct KawaiiCapsuleCollider
{
	std::string bone;
	float3 offset{ 0.0f,0.0f,0.0f };		// 始点（ボーンローカル）
	float3 offsetTail{ 0.0f,0.3f,0.0f };	// 終点（ボーンローカル）
	float  radius = 0.1f;
};

/// @brief 平面コリジョン（法線側の半空間に押し出す）
struct KawaiiPlanarCollider
{
	std::string bone;						// 空ならワールド固定
	float3 offset{ 0.0f,0.0f,0.0f };
	float3 normal{ 0.0f,1.0f,0.0f };
};

/// @brief ソルバに渡す設定一式（KawaiiPhysicsComponent が保持する）
struct KawaiiPhysicsSettings
{
	std::vector<KawaiiChainSetting>   chains;
	std::vector<KawaiiSphereCollider> spheres;
	std::vector<KawaiiCapsuleCollider> capsules;
	std::vector<KawaiiPlanarCollider> planars;

	// 揺らしてよいボーン名。空なら制限なし（ルートの全子孫を辿る）。
	// 自動生成時は PMX が物理演算指定したボーンだけをここに入れるので、
	// MMD が揺れものと見なしていない関節を巻き込まなくなる。
	std::vector<std::string> simulatedBones;

	float3 gravity{ 0.0f, -9.8f, 0.0f };
	float3 wind{ 0.0f, 0.0f, 0.0f };
	float  timeScale = 1.0f;

	// コリジョン → 角度制限 → 長さ拘束 の反復回数。
	// 1 回だと長さ拘束がコリジョンを押し戻して貫通が残る。
	int    collisionIterations = 3;

	// チェーン外の子孫へ変換差分を伝播させるか。
	// 剛体の無い毛先を親に追従させるための機能だが、チェーンのルートが
	// body 側のボーンだと手足まで引きずるので切れるようにしてある。
	bool   propagateToDescendants = true;
};

/// @brief ボーンチェーンの疑似物理ソルバ
///        ComputePalette の中から global(ワールド行列配列) を直接書き換える。
class KawaiiPhysics
{
public:
	/// @brief チェーン構成が変わったので次回 Apply で作り直す
	void MarkDirty() noexcept { m_Dirty = true; }

	/// @brief 次回 Apply で FK 姿勢へ強制同期する（シーク・ワープ時に呼ぶ）
	void RequestResync() noexcept { m_NeedsResync = true; }

	/// @brief デバッグ描画用: 解決済みの球コリジョン（ワールド座標）
	struct DebugSphere { float3 center; float radius; };
	/// @brief デバッグ描画用: 解決済みのカプセルコリジョン（ワールド座標）
	struct DebugCapsule { float3 a; float3 b; float radius; };
	/// @brief デバッグ描画用: 解決済みの平面コリジョン（ワールド座標）
	struct DebugPlane { float3 origin; float3 normal; };
	/// @brief デバッグ描画用: チェーンの1区間（親→子、ワールド座標）
	struct DebugSegment { float3 a; float3 b; bool anchor; };

	const std::vector<DebugSphere>& GetDebugSpheres()  const noexcept { return m_ResolvedSpheres; }
	const std::vector<DebugCapsule>& GetDebugCapsules() const noexcept { return m_ResolvedCapsules; }
	const std::vector<DebugPlane>& GetDebugPlanes() const noexcept { return m_ResolvedPlanes; }
	void GetDebugChains(_Inout_ std::vector<DebugSegment>& out) const;

	/// @brief 1フレーム進めて global を書き換える
	/// @param skel		スケルトン
	/// @param global	FK/IK/付与親を適用済みのワールド行列配列（書き換える）
	/// @param world	エンティティのワールド行列（本体の移動・回転の検出に使う）
	/// @param settings	チェーンとコリジョンの設定
	/// @param dt		デルタタイム(秒)
	void Apply(
		_In_ const Skeleton& skel,
		_Inout_ std::vector<DirectX::XMMATRIX>& global,
		_In_ const DirectX::XMMATRIX& world,
		_In_ const KawaiiPhysicsSettings& settings,
		float dt);

private:
	/// @brief シミュレーション対象の1ボーン
	struct Node
	{
		int   boneIndex = -1;	// Skeleton 上のインデックス（ダミーは -1）
		int   parent = -1;	// m_Nodes 上の親インデックス（根は -1）
		int   chain = 0;	// 所属チェーン（settings.chains の添字）
		bool  anchorOnly = false;	// 支点として位置だけ使い、global へは書き戻さない

		float3 location{};		// シミュレーション位置（ワールド）
		float3 prevLocation{};
		float3 poseLocation{};	// FK 結果のワールド位置

		float boneLength = 0.0f;	// 親からの距離（バインド長）
		float lengthRate = 0.0f;	// 根元 0 〜 末端 1

		// lengthRate で補間済みのパラメータ
		float damping = 0.0f;
		float stiffness = 0.0f;
		float radius = 0.0f;
		float limitAngle = 0.0f;
	};

	void Build(const Skeleton& skel, const KawaiiPhysicsSettings& settings);
	void SyncToPose();

	/// @brief コリジョンをワールド座標へ1回だけ解決する（毎ボーン引き直さないため）
	void ResolveColliders(const Skeleton& skel,
		const std::vector<DirectX::XMMATRIX>& global,
		const KawaiiPhysicsSettings& settings);

	void SolveCollision(const Node& node, DirectX::XMVECTOR& pos) const;

	std::vector<Node> m_Nodes;
	// スケルトンの子リストと、チェーンが制御しているボーンの印。
	// チェーン外の子孫へ変換差分を伝播させるのに使う。
	std::vector<std::vector<int>> m_Children;
	std::vector<std::uint8_t>     m_Controlled;
	std::vector<std::string>      m_PropagatedNames;   // 伝播対象の確認用（1回だけ）
	bool                          m_LoggedPropagation = false;
	std::vector<DebugSphere>  m_ResolvedSpheres;
	std::vector<DebugCapsule> m_ResolvedCapsules;
	std::vector<DebugPlane>   m_ResolvedPlanes;
	std::vector<bool>         m_SphereLimitInside;
	bool   m_Dirty = true;
	bool   m_NeedsResync = true;
	float  m_PrevDt = 0.0f;
	DirectX::XMFLOAT4X4 m_PrevWorld{};
	bool   m_HasPrevWorld = false;
};

// ---------------------------------------------------------------- //
//  シーン保存用のシリアライズ
//  FieldList が配列型を扱えないので、AnimatorComponent::clipPathsStr と
//  同じく「文字列1本に畳んで Reflect に載せる」方式にする。
//  1行1エントリ、先頭のタグで種別を判別する。
// ---------------------------------------------------------------- //

/// @brief PMX の剛体情報から揺れものチェーンとコリジョンを自動生成する
///        physicsType != 0（物理演算）の剛体が付いたボーンを揺れもの、
///        physicsType == 0（ボーン追従）の剛体をコリジョンとして扱う。
/// @param skel     スケルトン
/// @param phys     PMX の剛体・ジョイント
/// @param out      生成先（chains / spheres / capsules / planars を上書きする）
void KawaiiAutoSetupFromPmx(
	_In_ const Skeleton& skel,
	_In_ const PmxPhysics& phys,
	_Inout_ KawaiiPhysicsSettings& out);

inline std::string KawaiiSerialize(const KawaiiPhysicsSettings& s)
{
	constexpr char SEP = '\t';   // フィールド区切り
	constexpr char EOL = '\n';   // エントリ区切り

	std::ostringstream o;
	for (const auto& c : s.chains)
	{
		o << "chain" << SEP << c.rootBone << SEP << (c.enabled ? 1 : 0) << SEP
			<< c.dampingRoot << SEP << c.dampingTip << SEP
			<< c.stiffnessRoot << SEP << c.stiffnessTip << SEP
			<< c.radiusRoot << SEP << c.radiusTip << SEP
			<< c.limitAngleRoot << SEP << c.limitAngleTip << SEP
			<< c.worldDampingLocation << SEP << c.worldDampingRotation << SEP
			<< c.dummyBoneLength << SEP << (c.simulateRoot ? 1 : 0) << EOL;
	}
	for (const auto& c : s.spheres)
	{
		o << "sphere" << SEP << c.bone << SEP << c.offset.x << SEP << c.offset.y << SEP
			<< c.offset.z << SEP << c.radius << SEP << (c.limitInside ? 1 : 0) << EOL;
	}
	for (const auto& c : s.capsules)
	{
		o << "capsule" << SEP << c.bone << SEP << c.offset.x << SEP << c.offset.y << SEP
			<< c.offset.z << SEP << c.offsetTail.x << SEP << c.offsetTail.y << SEP
			<< c.offsetTail.z << SEP << c.radius << EOL;
	}
	if (!s.simulatedBones.empty())
	{
		o << "allow";
		for (const auto& b : s.simulatedBones) o << SEP << b;
		o << EOL;
	}
	for (const auto& c : s.planars)
	{
		o << "plane" << SEP << c.bone << SEP << c.offset.x << SEP << c.offset.y << SEP
			<< c.offset.z << SEP << c.normal.x << SEP << c.normal.y << SEP << c.normal.z << EOL;
	}
	return o.str();
}

inline void KawaiiDeserialize(const std::string& text, KawaiiPhysicsSettings& s)
{
	constexpr char SEP = '\t';

	s.chains.clear();
	s.spheres.clear();
	s.capsules.clear();
	s.planars.clear();
	s.simulatedBones.clear();

	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty()) continue;
		std::istringstream ls(line);
		std::string tag;
		if (!std::getline(ls, tag, SEP)) continue;

		auto field = [&ls, SEP]() { std::string v; std::getline(ls, v, SEP); return v; };
		auto num = [&field]() { const std::string v = field(); return v.empty() ? 0.0f : std::stof(v); };

		if (tag == "chain")
		{
			KawaiiChainSetting c{};
			c.rootBone = field();
			c.enabled = num() != 0.0f;
			c.dampingRoot = num();   c.dampingTip = num();
			c.stiffnessRoot = num();   c.stiffnessTip = num();
			c.radiusRoot = num();   c.radiusTip = num();
			c.limitAngleRoot = num();   c.limitAngleTip = num();
			c.worldDampingLocation = num();   c.worldDampingRotation = num();
			c.dummyBoneLength = num();
			c.simulateRoot = num() != 0.0f;
			s.chains.push_back(c);
		}
		else if (tag == "sphere")
		{
			KawaiiSphereCollider c{};
			c.bone = field();
			c.offset.x = num(); c.offset.y = num(); c.offset.z = num();
			c.radius = num();
			c.limitInside = num() != 0.0f;
			s.spheres.push_back(c);
		}
		else if (tag == "capsule")
		{
			KawaiiCapsuleCollider c{};
			c.bone = field();
			c.offset.x = num(); c.offset.y = num(); c.offset.z = num();
			c.offsetTail.x = num(); c.offsetTail.y = num(); c.offsetTail.z = num();
			c.radius = num();
			s.capsules.push_back(c);
		}
		else if (tag == "allow")
		{
			for (std::string b = field(); !b.empty(); b = field())
				s.simulatedBones.push_back(b);
		}
		else if (tag == "plane")
		{
			KawaiiPlanarCollider c{};
			c.bone = field();
			c.offset.x = num(); c.offset.y = num(); c.offset.z = num();
			c.normal.x = num(); c.normal.y = num(); c.normal.z = num();
			s.planars.push_back(c);
		}
	}
}
