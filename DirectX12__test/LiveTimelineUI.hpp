/*****************************************************************//**
 * \file   LiveTimelineUI.hpp
 * \brief  ライブ演出タイムラインの編集UI(ライト作成 / キー打ち / 保存)
 *
 * 作成者 keepmm
 * 作成日 2026/9/4
 * 更新履歴
 *   2026/9/4 新規作成
 *   2026/9/4 タイムラインをトラックヘッダ + レーン構成に変更
 *            (スナップ / パン / ズーム / キー選択)
 * *********************************************************************/
#pragma once

#include "Components.hpp"
#include "World.hpp"
#include "MmdPlayerUI.hpp"
#include "Util.hpp"
#include "imguiinit.hpp"
#include "imgui.h"
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ライトを1つ作る(Transform / Name / Light を付けた Entity を返す)
inline Entity CreateLightEntity(World& world, LightComponent::LightType type)
{
	static int num = 1;

	Entity e = world.CreateEntity();

	TransformComponent tr{};
	tr.position = POSITION{ 0.0f, 5.0f, 0.0f };
	tr.EulerAngles = float3{ 90.0f, 0.0f, 0.0f };	// 下向き
	tr.ApplyEuler();
	tr.RebuildWorld();
	world.AddComponent<TransformComponent>(e, tr);

	const char* prefix = "Light";
	switch (type)
	{
	case LightComponent::LightType::Directional: prefix = "Dir";   break;
	case LightComponent::LightType::Point:       prefix = "Point"; break;
	case LightComponent::LightType::Spot:        prefix = "Spot";  break;
	case LightComponent::LightType::Laser:       prefix = "Laser"; break;
	default: break;
	}
	world.AddComponent<NameComponent>(e,
		NameComponent{ std::string(prefix) + "_" + std::to_string(num++) });

	LightComponent light{};
	light.type = type;
	light.intensity = 3.0f;
	light.range = 20.0f;
	if (type == LightComponent::LightType::Spot || type == LightComponent::LightType::Laser)
	{
		light.ShowBeam = true;	// ステージ演出はビームが見えたほうが分かりやすい
	}
	world.AddComponent<LightComponent>(e, light);

	return e;
}

/// @brief ライブ編集に必要なEntityを、足りないものだけ作る
/// @param world Worldの参照
/// @return 何か作ったら true(呼び出し側でログを出す用)
///
/// ワークスペース切替のたびに呼ばれるので、既にあるものは絶対に作らない。
inline bool EnsureLiveRig(World& world)
{
	bool created = false;

	// --- 指揮者(LiveDirector + MusicSync + AudioSource) ---
	Entity director = INVALID_ENTITY;
	world.Each<LiveDirectorComponent>(
		[&](Entity e, LiveDirectorComponent&)
		{
			if (director == INVALID_ENTITY) director = e;
		});

	if (director == INVALID_ENTITY)
	{
		// MusicSync が既にあるならそこへ相乗りする(曲の時刻ソースは1つに保つ)
		world.Each<MusicSyncComponent>(
			[&](Entity e, MusicSyncComponent&)
			{
				if (director == INVALID_ENTITY) director = e;
			});

		if (director == INVALID_ENTITY)
		{
			director = world.CreateEntity();
			world.AddComponent<NameComponent>(director, NameComponent{ "LiveDirector" });

			TransformComponent tr{};
			tr.RebuildWorld();
			world.AddComponent<TransformComponent>(director, tr);

			AudioSourceComponent audio{};
			audio.is3D = false;			// BGMなので距離減衰なし
			audio.playOnStart = true;
			world.AddComponent<AudioSourceComponent>(director, audio);

			world.AddComponent<MusicSyncComponent>(director, MusicSyncComponent{});
		}

		if (!world.HasComponent<LiveDirectorComponent>(director))
			world.AddComponent<LiveDirectorComponent>(director, LiveDirectorComponent{});

		created = true;
	}

	// --- ステージを映すカメラ ---
	bool hasCamera = false;
	world.Each<CameraComponent>([&](Entity, CameraComponent&) { hasCamera = true; });

	if (!hasCamera)
	{
		Entity cam = world.CreateEntity();
		world.AddComponent<NameComponent>(cam, NameComponent{ "LiveCamera" });

		TransformComponent tr{};
		tr.position = POSITION{ 0.0f, 12.0f, -22.0f };
		tr.EulerAngles = float3{ 12.0f, 0.0f, 0.0f };	// 少し見下ろす
		tr.ApplyEuler();
		tr.RebuildWorld();
		world.AddComponent<TransformComponent>(cam, tr);

		CameraComponent c{};
		c.cameraType = CameraComponent::CameraType::Main;
		c.fovY = 40.0f;
		c.farZ = 500.0f;
		world.AddComponent<CameraComponent>(cam, c);

		// カメラVMDを読ませたくなった時のために枠だけ用意しておく
		world.AddComponent<CameraAnimationComponent>(cam, CameraAnimationComponent{});

		created = true;
	}

	// --- 基本の照明(ライトが1つも無いときだけ、3灯セットを組む) ---
	int lightCount = 0;
	world.Each<LightComponent>([&](Entity, LightComponent&) { ++lightCount; });

	if (lightCount == 0)
	{
		struct RigLight
		{
			const char* name;
			LightComponent::LightType type;
			POSITION pos;
			float3 euler;
			COLOR color;
			float intensity;
		};

		// キー(下手前上)・フィル(上手前上)・バック(後方からの逆光)
		const RigLight rig[] = {
			{ "Spot_Key",  LightComponent::LightType::Spot, { -6.0f, 12.0f, -8.0f },
			  { 50.0f,  30.0f, 0.0f }, { 1.0f, 0.95f, 0.9f, 1.0f }, 5.0f },
			{ "Spot_Fill", LightComponent::LightType::Spot, {  6.0f, 12.0f, -8.0f },
			  { 50.0f, -30.0f, 0.0f }, { 0.75f, 0.85f, 1.0f, 1.0f }, 3.0f },
			{ "Spot_Back", LightComponent::LightType::Spot, {  0.0f, 13.0f,  10.0f },
			  { 55.0f, 180.0f, 0.0f }, { 1.0f, 0.5f,  0.85f, 1.0f }, 4.0f },
		};

		for (const auto& r : rig)
		{
			Entity e = world.CreateEntity();
			world.AddComponent<NameComponent>(e, NameComponent{ r.name });

			TransformComponent tr{};
			tr.position = r.pos;
			tr.EulerAngles = r.euler;
			tr.ApplyEuler();
			tr.RebuildWorld();
			world.AddComponent<TransformComponent>(e, tr);

			LightComponent l{};
			l.type = r.type;
			l.color = r.color;
			l.intensity = r.intensity;
			l.range = 60.0f;
			l.spotAngle = 30.0f;
			l.ShowBeam = true;
			world.AddComponent<LightComponent>(e, l);
		}
		created = true;
	}

	return created;
}

// 名前から Entity を引く(Systems.hpp に依存しないようUI側にも持つ)
inline Entity LiveFindEntityByName(World& world, const std::string& name)
{
	Entity found = INVALID_ENTITY;
	world.Each<NameComponent>(
		[&](Entity e, NameComponent& n)
		{
			if (found == INVALID_ENTITY && n.name == name) found = e;
		});
	return found;
}

// Entity から名前を引く(無ければ空文字)
inline std::string LiveEntityName(World& world, Entity e)
{
	if (e == INVALID_ENTITY || !world.IsEntityAlive(e)) return {};
	if (!world.HasComponent<NameComponent>(e))          return {};
	return world.GetComponent<NameComponent>(e).name;
}

// いまの Entity の状態からトラックへ書き込む値を取り出す
inline bool LiveCaptureValue(World& world, Entity e, LiveTrack::Property prop, float4& out)
{
	switch (prop)
	{
	case LiveTrack::Property::Position:
		if (!world.HasComponent<TransformComponent>(e)) return false;
		{
			const auto& tr = world.GetComponent<TransformComponent>(e);
			out = float4{ tr.position.x, tr.position.y, tr.position.z, 0.0f };
		}
		return true;

	case LiveTrack::Property::EulerAngles:
		if (!world.HasComponent<TransformComponent>(e)) return false;
		{
			const auto& tr = world.GetComponent<TransformComponent>(e);
			out = float4{ tr.EulerAngles.x, tr.EulerAngles.y, tr.EulerAngles.z, 0.0f };
		}
		return true;

	case LiveTrack::Property::Color:
		if (!world.HasComponent<LightComponent>(e)) return false;
		{
			const auto& l = world.GetComponent<LightComponent>(e);
			out = float4{ l.color.x, l.color.y, l.color.z, l.color.w };
		}
		return true;

	case LiveTrack::Property::Intensity:
		if (!world.HasComponent<LightComponent>(e)) return false;
		out = float4{ world.GetComponent<LightComponent>(e).intensity, 0.0f, 0.0f, 0.0f };
		return true;

	case LiveTrack::Property::Range:
		if (!world.HasComponent<LightComponent>(e)) return false;
		out = float4{ world.GetComponent<LightComponent>(e).range, 0.0f, 0.0f, 0.0f };
		return true;

	case LiveTrack::Property::SpotAngle:
		if (!world.HasComponent<LightComponent>(e)) return false;
		out = float4{ world.GetComponent<LightComponent>(e).spotAngle, 0.0f, 0.0f, 0.0f };
		return true;

	default: return false;
	}
}

inline const char* LivePropertyName(LiveTrack::Property p)
{
	switch (p)
	{
	case LiveTrack::Property::Position:    return u8("位置");
	case LiveTrack::Property::EulerAngles: return u8("回転(度)");
	case LiveTrack::Property::Color:       return u8("色");
	case LiveTrack::Property::Intensity:   return u8("強度");
	case LiveTrack::Property::Range:       return u8("届く距離");
	case LiveTrack::Property::SpotAngle:   return u8("スポット角");
	default: return "?";
	}
}

// ---------------------------------------------------------------- //
//  タイムラインの表示状態(全レーンで共有)
// ---------------------------------------------------------------- //

struct LiveLaneView
{
	float start = 0.0f;		// 左端の時刻(秒)
	float end = 10.0f;		// 右端の時刻(秒)
	bool  snap = true;		// スナップ(磁石)
	float snapStep = 0.25f;	// スナップの基準グリッド(秒)
	bool  followHead = true;	// 再生ヘッドを画面内に追い続ける

	// ドラッグ中のキー(トラック添字とキー添字。レーンをまたいで1つだけ)
	int dragTrack = -1;
	int dragKey = -1;
	// 選択中のキー(Deleteで消せる)
	int selTrack = -1;
	int selKey = -1;

	float Span()  const { return std::max(end - start, 1e-3f); }
	float PixelsPerSecond(float w) const { return w / Span(); }

	float ToX(float t, float x0, float w)  const { return x0 + (t - start) / Span() * w; }
	float ToTime(float x, float x0, float w) const { return start + (x - x0) / std::max(w, 1.0f) * Span(); }
};

inline LiveLaneView& LiveGetLaneView()
{
	static LiveLaneView view;
	return view;
}

// 目盛りの間隔を決める(1,2,5,10,... のいずれか。ラベルが潰れない幅を選ぶ)
inline float LiveNiceStep(float spanSeconds, float widthPixels)
{
	const float rough = spanSeconds / std::max(widthPixels / 80.0f, 1.0f);
	const float mag = std::pow(10.0f, std::floor(std::log10(std::max(rough, 1e-3f))));
	const float norm = rough / mag;
	const float step = (norm <= 1.0f) ? 1.0f : (norm <= 2.0f) ? 2.0f : (norm <= 5.0f) ? 5.0f : 10.0f;
	return step * mag;
}

/// @brief ホイールでズーム / 中ドラッグ(Alt+左ドラッグ)でパン
inline void LiveHandleViewInput(bool hovered, float x0, float w)
{
	LiveLaneView& view = LiveGetLaneView();
	ImGuiIO& io = ImGui::GetIO();
	if (!hovered) return;

	// ズーム(マウス位置を軸に保つ)
	if (io.MouseWheel != 0.0f)
	{
		const float pivot = view.ToTime(io.MousePos.x, x0, w);
		const float scale = (io.MouseWheel > 0.0f) ? 0.85f : 1.0f / 0.85f;
		view.start = pivot + (view.start - pivot) * scale;
		view.end = pivot + (view.end - pivot) * scale;
		if (view.Span() < 0.05f) view.end = view.start + 0.05f;
	}

	// パン
	const bool panning =
		ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
		(io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left));
	if (panning)
	{
		const ImGuiMouseButton btn = ImGui::IsMouseDragging(ImGuiMouseButton_Middle)
			? ImGuiMouseButton_Middle : ImGuiMouseButton_Left;
		const float dx = ImGui::GetMouseDragDelta(btn).x;
		const float dt = -dx / std::max(view.PixelsPerSecond(w), 1e-3f);
		view.start += dt;
		view.end += dt;
		ImGui::ResetMouseDragDelta(btn);
	}
}

/// @brief スナップ先を探す(グリッド / 他のキー / 再生ヘッド / 0秒)
/// @param excludeTrack 自分自身のキーを吸着対象から外すためのトラック添字(-1で無効)
inline float LiveSnapTime(float t, const LiveTimeline& tl, int excludeTrack, int excludeKey,
	float playhead, float pixelsPerSecond)
{
	const LiveLaneView& view = LiveGetLaneView();
	if (!view.snap || ImGui::GetIO().KeyCtrl) return t;	// Ctrl押下中はスナップ無効

	const float threshold = 8.0f / std::max(pixelsPerSecond, 1e-3f);	// 8px相当

	float best = t;
	float bestDiff = threshold;

	auto tryCandidate = [&](float c)
		{
			const float d = std::fabs(c - t);
			if (d < bestDiff) { bestDiff = d; best = c; }
		};

	tryCandidate(0.0f);
	tryCandidate(playhead);
	tryCandidate(std::round(t / view.snapStep) * view.snapStep);

	for (int ti = 0; ti < (int)tl.tracks.size(); ++ti)
	{
		for (int ki = 0; ki < (int)tl.tracks[ti].keys.size(); ++ki)
		{
			if (ti == excludeTrack && ki == excludeKey) continue;
			tryCandidate(tl.tracks[ti].keys[ki].time);
		}
	}
	return best;
}

/// @brief 再生ヘッドが画面外に出そうなら表示範囲をずらす(Premiereのページスクロール相当)
inline void LiveFollowPlayhead(float musicTime)
{
	LiveLaneView& view = LiveGetLaneView();
	if (!view.followHead) return;

	// ドラッグ中に勝手にスクロールすると掴んだキーが逃げるので止める
	if (view.dragTrack >= 0 || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

	const float span = view.Span();
	const float margin = span * 0.1f;

	if (musicTime > view.end - margin)
	{
		// 右端に達したら1画面ぶん送る
		view.start = musicTime - margin;
		view.end = view.start + span;
	}
	else if (musicTime < view.start + margin)
	{
		view.start = std::max(0.0f, musicTime - span + margin);
		view.end = view.start + span;
	}
}

/// @brief 曲の再生 / 一時停止 / 先頭へ(AudioSource を直接叩く)
inline void LiveDrawTransport(World& world, float musicTime)
{
	static bool playing = false;

	// シーンが作り直されて Voice が消えていたら、ボタンの表示を戻す
	{
		bool anyVoice = false;
		world.Each<AudioSourceComponent, MusicSyncComponent>(
			[&](Entity, AudioSourceComponent& src, MusicSyncComponent&)
			{
				if (src.voice) anyVoice = true;
			});
		if (!anyVoice) playing = false;
	}

	if (ImGui::Button(playing ? u8("一時停止##live") : u8("再生##live")))
	{
		playing = !playing;
		SetMusicPaused(world, !playing);

		// まだ一度も鳴らしていない音源はここで開始する
		if (playing)
		{
			world.Each<AudioSourceComponent, MusicSyncComponent>(
				[&](Entity, AudioSourceComponent& src, MusicSyncComponent&)
				{
					if (!src.voice) src.playRequested = true;
				});
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(u8("先頭へ##live")))
	{
		SeekMusic(world, 0.0f);
		LiveGetLaneView().start = 0.0f;
	}
	ImGui::SameLine();
	if (ImGui::Button(u8("停止##live")))
	{
		playing = false;
		world.Each<AudioSourceComponent, MusicSyncComponent>(
			[&](Entity, AudioSourceComponent& src, MusicSyncComponent&)
			{
				src.stopRequested = true;
			});
		SeekMusic(world, 0.0f);
	}
	ImGui::SameLine();
	ImGui::Checkbox(u8("ヘッド追従"), &LiveGetLaneView().followHead);
}

// レーンの縦グリッド(ルーラーと同じ位置に引く)
inline void LiveDrawGrid(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float w)
{
	const LiveLaneView& view = LiveGetLaneView();
	const float step = LiveNiceStep(view.Span(), w);
	const float first = std::ceil(view.start / step) * step;
	for (float t = first; t <= view.end; t += step)
	{
		const float x = view.ToX(t, p0.x, w);
		dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 18));
	}
}

/// @brief 目盛り + 再生ヘッドのつまみ。戻り値はシーク先(されなければ負)
inline float LiveDrawRuler(float musicTime, const LiveTimeline& tl, float height = 26.0f)
{
	LiveLaneView& view = LiveGetLaneView();
	ImDrawList* dl = ImGui::GetWindowDrawList();

	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const float  w = std::max(ImGui::GetContentRegionAvail().x, 50.0f);

	ImGui::InvisibleButton("##liveruler", ImVec2(w, height),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();
	const ImVec2 p1 = ImVec2(p0.x + w, p0.y + height);

	dl->AddRectFilled(p0, p1, IM_COL32(28, 28, 32, 255));

	const float step = LiveNiceStep(view.Span(), w);
	const float first = std::ceil(view.start / step) * step;
	for (float t = first; t <= view.end; t += step)
	{
		const float x = view.ToX(t, p0.x, w);
		dl->AddLine(ImVec2(x, p0.y + height * 0.55f), ImVec2(x, p1.y), IM_COL32(120, 120, 128, 255));

		char label[32];
		snprintf(label, sizeof(label), (step < 1.0f) ? "%.2f" : "%.0f", t);
		dl->AddText(ImVec2(x + 3.0f, p0.y + 3.0f), IM_COL32(195, 195, 205, 255), label);

		// 中間の細い目盛り
		const float halfX = view.ToX(t + step * 0.5f, p0.x, w);
		dl->AddLine(ImVec2(halfX, p0.y + height * 0.75f), ImVec2(halfX, p1.y), IM_COL32(90, 90, 98, 255));
	}

	LiveHandleViewInput(hovered, p0.x, w);

	// つまみ付きの再生ヘッド
	const float headX = view.ToX(musicTime, p0.x, w);
	if (headX >= p0.x - 6.0f && headX <= p1.x + 6.0f)
	{
		const ImVec2 head[5] = {
			ImVec2(headX - 6.0f, p0.y),        ImVec2(headX + 6.0f, p0.y),
			ImVec2(headX + 6.0f, p1.y - 8.0f), ImVec2(headX,        p1.y),
			ImVec2(headX - 6.0f, p1.y - 8.0f) };
		dl->AddConvexPolyFilled(head, 5, IM_COL32(235, 70, 70, 255));
	}

	// 左ドラッグでスクラブ
	float seekTo = -1.0f;
	if (active && !ImGui::GetIO().KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const float raw = std::max(0.0f, view.ToTime(ImGui::GetIO().MousePos.x, p0.x, w));
		seekTo = LiveSnapTime(raw, tl, -1, -1, raw, view.PixelsPerSecond(w));
	}
	return seekTo;
}

/// @brief 1トラック分のレーン。キーはドラッグで時刻変更、ダブルクリックで追加
/// @return シーク先(されなければ負)
inline float LiveDrawTrackLane(int trackIndex, LiveTrack& track, const LiveTimeline& tl,
	float musicTime, float height = 24.0f)
{
	LiveLaneView& view = LiveGetLaneView();
	ImDrawList* dl = ImGui::GetWindowDrawList();

	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const float  w = std::max(ImGui::GetContentRegionAvail().x, 50.0f);

	ImGui::PushID(trackIndex);
	ImGui::InvisibleButton("lane", ImVec2(w, height),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	const bool active = ImGui::IsItemActive();
	const bool hovered = ImGui::IsItemHovered();
	const bool dblClick = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	ImGui::PopID();

	const ImVec2 p1 = ImVec2(p0.x + w, p0.y + height);
	const float  cy = p0.y + height * 0.5f;

	dl->AddRectFilled(p0, p1, track.enabled ? IM_COL32(48, 48, 56, 255) : IM_COL32(38, 38, 42, 255));
	LiveDrawGrid(dl, p0, p1, w);

	// キーの区間を帯で表示(Premiereのクリップのような見え方)
	if (track.keys.size() >= 2)
	{
		const float xa = view.ToX(track.keys.front().time, p0.x, w);
		const float xb = view.ToX(track.keys.back().time, p0.x, w);
		dl->AddRectFilled(ImVec2(xa, cy - 6.0f), ImVec2(xb, cy + 6.0f),
			track.enabled ? IM_COL32(90, 120, 190, 130) : IM_COL32(90, 90, 100, 90), 3.0f);
	}

	LiveHandleViewInput(hovered, p0.x, w);

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const float  grabRadius = 7.0f;

	int hoveredKey = -1;
	for (int i = 0; i < (int)track.keys.size(); ++i)
	{
		const float x = view.ToX(track.keys[i].time, p0.x, w);
		if (hovered && std::fabs(mouse.x - x) <= grabRadius) hoveredKey = i;
	}

	// 掴む / 選ぶ
	if (active && view.dragTrack < 0 && hoveredKey >= 0 &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
	{
		view.dragTrack = trackIndex;
		view.dragKey = hoveredKey;
		view.selTrack = trackIndex;
		view.selKey = hoveredKey;
	}

	// 離したら並べ直す(添字が変わるので選択は解除)
	if (view.dragTrack == trackIndex && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		track.SortByTime();
		view.dragTrack = -1;
		view.dragKey = -1;
		view.selKey = -1;
	}

	// ドラッグ中は時刻を更新(スナップあり)
	if (view.dragTrack == trackIndex && view.dragKey >= 0 && view.dragKey < (int)track.keys.size())
	{
		const float raw = std::max(0.0f, view.ToTime(mouse.x, p0.x, w));
		track.keys[view.dragKey].time =
			LiveSnapTime(raw, tl, trackIndex, view.dragKey, musicTime, view.PixelsPerSecond(w));
	}

	// 空きをダブルクリック: その時刻の補間値でキーを追加
	if (dblClick && hoveredKey < 0)
	{
		const float raw = std::max(0.0f, view.ToTime(mouse.x, p0.x, w));
		const float t = LiveSnapTime(raw, tl, trackIndex, -1, musicTime, view.PixelsPerSecond(w));
		float4 v{};
		if (!track.Evaluate(t, v)) v = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
		track.SetKey(t, v);
	}

	// キー(ひし形)
	for (int i = 0; i < (int)track.keys.size(); ++i)
	{
		const float x = view.ToX(track.keys[i].time, p0.x, w);
		if (x < p0.x - 8.0f || x > p1.x + 8.0f) continue;

		const bool isDrag = (view.dragTrack == trackIndex && view.dragKey == i);
		const bool isSel = (view.selTrack == trackIndex && view.selKey == i);
		const ImU32 col = isDrag ? IM_COL32(255, 210, 90, 255)
			: isSel ? IM_COL32(255, 170, 60, 255)
			: (hoveredKey == i) ? IM_COL32(255, 255, 255, 255)
			: IM_COL32(225, 225, 235, 255);

		const float r = 5.5f;
		const ImVec2 pts[4] = {
			ImVec2(x, cy - r), ImVec2(x + r, cy), ImVec2(x, cy + r), ImVec2(x - r, cy) };
		dl->AddConvexPolyFilled(pts, 4, col);
		dl->AddPolyline(pts, 4, IM_COL32(18, 18, 22, 255), ImDrawFlags_Closed, 1.5f);
	}

	// 再生ヘッド
	const float headX = view.ToX(musicTime, p0.x, w);
	if (headX >= p0.x && headX <= p1.x)
		dl->AddLine(ImVec2(headX, p0.y), ImVec2(headX, p1.y), IM_COL32(235, 70, 70, 190), 1.5f);

	// キーを掴んでいない場所でのドラッグはスクラブ
	float seekTo = -1.0f;
	if (active && view.dragTrack < 0 && !ImGui::GetIO().KeyAlt &&
		ImGui::IsMouseDown(ImGuiMouseButton_Left) && hoveredKey < 0)
	{
		const float raw = std::max(0.0f, view.ToTime(mouse.x, p0.x, w));
		seekTo = LiveSnapTime(raw, tl, -1, -1, raw, view.PixelsPerSecond(w));
	}

	if (hoveredKey >= 0)
		ImGui::SetTooltip(u8("%.2f 秒"), track.keys[hoveredKey].time);

	return seekTo;
}

/// @brief ライブタイムライン編集ウィンドウの中身
/// @param world   Worldの参照
/// @param selected アウトライナーで選択中のEntity(キー打ちの対象)
inline void DrawLiveTimelineEditor(World& world, Entity selected)
{
	LiveLaneView& view = LiveGetLaneView();

	// --- 指揮者(LiveDirector)を探す ---
	Entity director = INVALID_ENTITY;
	world.Each<LiveDirectorComponent>(
		[&](Entity e, LiveDirectorComponent&)
		{
			if (director == INVALID_ENTITY) director = e;
		});

	// --- ライト作成 ---
	if (ImGui::Button(u8("スポット"))) CreateLightEntity(world, LightComponent::LightType::Spot);
	ImGui::SameLine();
	if (ImGui::Button(u8("ポイント"))) CreateLightEntity(world, LightComponent::LightType::Point);
	ImGui::SameLine();
	if (ImGui::Button(u8("レーザー"))) CreateLightEntity(world, LightComponent::LightType::Laser);
	ImGui::SameLine();
	if (ImGui::Button(u8("平行光"))) CreateLightEntity(world, LightComponent::LightType::Directional);

	if (director == INVALID_ENTITY)
	{
		ImGui::Spacing();
		ImGui::TextDisabled(u8("LiveDirector がありません"));
		if (ImGui::Button(u8("必要なEntityを作る"))) EnsureLiveRig(world);
		return;
	}

	auto& dir = world.GetComponent<LiveDirectorComponent>(director);
	LiveTimeline& tl = dir.timeline;

	// --- 曲位置 ---
	float musicTime = 0.0f;
	bool  hasMusic = false;
	world.Each<MusicSyncComponent>(
		[&](Entity, MusicSyncComponent& s)
		{
			if (!hasMusic) { musicTime = s.musicTime; hasMusic = true; }
		});

	const float duration = std::max(tl.Duration(), 1.0f);

	// 再生に合わせて表示範囲を送る
	LiveFollowPlayhead(musicTime);

	// --- ツールバー ---
	LiveDrawTransport(world, musicTime);
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::Checkbox(u8("スナップ"), &view.snap);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat(u8("グリッド"), &view.snapStep, 0.01f, 0.01f, 10.0f, u8("%.2f秒"));
	ImGui::SameLine();
	if (ImGui::Button(u8("全体表示")))
	{
		view.start = 0.0f;
		view.end = duration;
	}
	ImGui::SameLine();
	ImGui::Checkbox(u8("有効"), &dir.enabled);
	ImGui::SameLine();
	ImGui::Text(u8("%.2f / %.2f 秒"), musicTime, duration);

	if (!hasMusic)
		ImGui::TextDisabled(u8("MusicSyncComponent がありません(曲を再生するEntityに付けてください)"));

	// --- JSON ---
	{
		char buf[260]{};
		strncpy_s(buf, sizeof(buf), dir.timelinePath.c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(320.0f);
		if (ImGui::InputText(u8("JSONパス"), buf, sizeof(buf))) dir.timelinePath = buf;

		// アセットパネルからのドラッグ&ドロップ
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
			{
				dir.timelinePath = std::string((const char*)p->Data, p->DataSize - 1);
				dir.loadFailed = !tl.LoadJson(dir.timelinePath);
				dir.loadedPath = dir.timelinePath;
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("...##livejson"))
	{
		std::wstring picked;
		if (OpenFileDialog(picked, L"Timeline JSON\0*.json\0All\0*.*\0"))
		{
			// プロジェクト配下なら相対パスにして持ち回る
			namespace fs = std::filesystem;
			std::error_code ec;
			const fs::path abs = fs::absolute(WideToUtf8(picked), ec);
			const fs::path rel = fs::relative(abs, fs::current_path(), ec);
			dir.timelinePath = (!ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0)
				? rel.generic_string()
				: abs.generic_string();

			dir.loadFailed = !tl.LoadJson(dir.timelinePath);
			dir.loadedPath = dir.timelinePath;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(u8("保存")) && !dir.timelinePath.empty())
	{
		tl.SaveJson(dir.timelinePath);
		dir.loadedPath = dir.timelinePath;	// 保存直後に読み直させない
		dir.loadFailed = false;
	}
	ImGui::SameLine();
	if (ImGui::Button(u8("読み込み")) && !dir.timelinePath.empty())
	{
		dir.loadFailed = !tl.LoadJson(dir.timelinePath);
		dir.loadedPath = dir.timelinePath;
	}
	if (dir.loadFailed)
	{
		// どこを探したのか出さないと原因が分からないので絶対パスで見せる
		std::error_code ec;
		const std::string full =
			std::filesystem::absolute(dir.timelinePath, ec).generic_string();
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
			u8("読み込みに失敗: %s"), full.c_str());
	}

	// --- トラック追加 ---
	const std::string selName = LiveEntityName(world, selected);
	if (selName.empty())
	{
		ImGui::TextDisabled(u8("アウトライナーで対象のEntityを選ぶとトラックを追加できます"));
	}
	else
	{
		static int propIndex = 0;
		const char* props[] = {
			u8("位置"), u8("回転(度)"), u8("色"), u8("強度"), u8("届く距離"), u8("スポット角") };
		ImGui::SetNextItemWidth(140.0f);
		ImGui::Combo("##prop", &propIndex, props, IM_ARRAYSIZE(props));
		ImGui::SameLine();
		if (ImGui::Button(u8("トラックを追加")))
		{
			const auto prop = (LiveTrack::Property)propIndex;
			const bool exists = std::any_of(tl.tracks.begin(), tl.tracks.end(),
				[&](const LiveTrack& t) { return t.target == selName && t.property == prop; });
			if (!exists)
			{
				LiveTrack t;
				t.target = selName;
				t.property = prop;
				tl.tracks.push_back(std::move(t));
			}
		}
		ImGui::SameLine();
		ImGui::Text(u8("対象: %s"), selName.c_str());
	}

	// --- 選択キーを Delete で削除 ---
	if (view.selTrack >= 0 && view.selTrack < (int)tl.tracks.size() &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		auto& keys = tl.tracks[view.selTrack].keys;
		if (view.selKey >= 0 && view.selKey < (int)keys.size())
			keys.erase(keys.begin() + view.selKey);
		view.selTrack = -1;
		view.selKey = -1;
	}

	ImGui::Separator();

	// --- タイムライン本体(左:ヘッダ / 右:レーン) ---
	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoSavedSettings;

	if (ImGui::BeginTable("##livetimeline", 2, tableFlags))
	{
		ImGui::TableSetupColumn(u8("トラック"), ImGuiTableColumnFlags_WidthFixed, 210.0f);
		ImGui::TableSetupColumn(u8("タイム"), ImGuiTableColumnFlags_WidthStretch);

		// 目盛り行
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextDisabled(u8("ドラッグ:移動  Alt/中:パン  ホイール:ズーム"));
		ImGui::TableSetColumnIndex(1);
		{
			const float seekTo = LiveDrawRuler(musicTime, tl);
			if (seekTo >= 0.0f) SeekMusic(world, seekTo);
		}

		int removeTrack = -1;

		for (int i = 0; i < (int)tl.tracks.size(); ++i)
		{
			auto& track = tl.tracks[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// --- ヘッダ列 ---
			ImGui::TableSetColumnIndex(0);
			ImGui::Checkbox("##enabled", &track.enabled);
			ImGui::SameLine();
			ImGui::Text("%s", track.target.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", LivePropertyName(track.property));

			const Entity target = LiveFindEntityByName(world, track.target);
			const bool canKey = (target != INVALID_ENTITY);

			ImGui::BeginDisabled(!canKey);
			if (ImGui::SmallButton(u8("キーを打つ")))
			{
				float4 v{};
				if (LiveCaptureValue(world, target, track.property, v))
					track.SetKey(musicTime, v);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton(u8("削除"))) removeTrack = i;
			if (!canKey)
			{
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), u8("対象が見つかりません"));
			}

			// --- レーン列 ---
			ImGui::TableSetColumnIndex(1);
			const float seekTo = LiveDrawTrackLane(i, track, tl, musicTime);
			if (seekTo >= 0.0f) SeekMusic(world, seekTo);

			ImGui::PopID();
		}

		if (removeTrack >= 0)
		{
			tl.tracks.erase(tl.tracks.begin() + removeTrack);
			view.selTrack = view.dragTrack = -1;
			view.selKey = view.dragKey = -1;
		}

		ImGui::EndTable();
	}

	if (tl.tracks.empty()) ImGui::TextDisabled(u8("トラックがありません"));

	// --- 選択中キーの値を編集 ---
	if (view.selTrack >= 0 && view.selTrack < (int)tl.tracks.size())
	{
		auto& track = tl.tracks[view.selTrack];
		if (view.selKey >= 0 && view.selKey < (int)track.keys.size())
		{
			auto& key = track.keys[view.selKey];
			ImGui::Separator();
			ImGui::Text(u8("選択中のキー: %s / %s"),
				track.target.c_str(), LivePropertyName(track.property));

			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::DragFloat(u8("時刻"), &key.time, 0.02f, 0.0f, 100000.0f, u8("%.2f秒")))
				track.SortByTime();

			ImGui::SameLine();
			ImGui::SetNextItemWidth(240.0f);
			switch (track.property)
			{
			case LiveTrack::Property::Color:
				ImGui::ColorEdit4(u8("値"), &key.value.x,
					ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_HDR);
				break;
			case LiveTrack::Property::Intensity:
			case LiveTrack::Property::Range:
			case LiveTrack::Property::SpotAngle:
				ImGui::DragFloat(u8("値"), &key.value.x, 0.05f);
				break;
			default:
				ImGui::DragFloat3(u8("値"), &key.value.x, 0.05f);
				break;
			}
		}
	}
}
