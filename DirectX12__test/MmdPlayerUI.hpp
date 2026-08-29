#pragma once
#include "Components.hpp"
#include "World.hpp"
#include "imguiinit.hpp"
#include "imgui.h"
#include <algorithm>
#include <vector>

inline constexpr float MMD_FPS = 30.0f;   // VMDは30fps

// 曲を指定秒へシーク(MusicSyncを持つAudioSource全部)
inline void SeekMusic(World& world, float time)
{
    world.Each<AudioSourceComponent, MusicSyncComponent>(
        [&](Entity, AudioSourceComponent& src, MusicSyncComponent& sync)
        {
            const float t = std::max(0.0f, time - sync.offset);
            src.seekSeconds = t;
            src.seekRequested = true;
            sync.seekBase = t;
            sync.resyncRequested = true;
        });
}

// 曲の一時停止 / 再開
inline void SetMusicPaused(World& world, bool paused)
{
    world.Each<AudioSourceComponent, MusicSyncComponent>(
        [&](Entity, AudioSourceComponent& src, MusicSyncComponent&)
        {
            if (paused) src.pauseRequested = true;
            else        src.resumeRequested = true;
        });
}

inline void DrawMmdPlayerControls(World& world, AnimatorComponent& an)
{
    if (an.clips.empty()) { ImGui::TextDisabled(u8("クリップ未読み込み")); return; }
    if (an.currentClip < 0 || an.currentClip >= (int)an.clips.size()) an.currentClip = 0;

    // シーク時に共通で呼ぶ(物理リセット + 曲も追従)
    auto seekTo = [&](float t)
        {
            an.time = t;
            an.physicsResetRequest = true;
            SeekMusic(world, t);
        };

    // --- クリップ選択 ---
    {
        std::vector<const char*> names;
        for (const auto& c : an.clips) names.push_back(c.name.c_str());
        if (ImGui::Combo(u8("クリップ"), &an.currentClip, names.data(), (int)names.size()))
            seekTo(0.0f);
    }

    const AnimationClip& clip = an.clips[an.currentClip];
    const int   totalFrame = (int)(clip.duration * MMD_FPS + 0.5f);
    const float oneFrame = 1.0f / MMD_FPS;

    // --- 再生コントロール ---
    if (ImGui::Button(an.playing ? u8("一時停止") : u8("再生")))
    {
        an.playing = !an.playing;
        SetMusicPaused(world, !an.playing);   // 曲も止める / 再開する
    }
    ImGui::SameLine();
    if (ImGui::Button(u8("最初から"))) seekTo(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("|<")) seekTo(std::max(0.0f, an.time - oneFrame));
    ImGui::SameLine();
    if (ImGui::Button(">|")) seekTo(std::min(clip.duration, an.time + oneFrame));
    ImGui::SameLine();
    ImGui::Checkbox(u8("ループ"), &an.loop);

    // --- シークスライダー(フレーム単位) ---
    int frame = (int)(an.time * MMD_FPS + 0.5f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##mmdseek", &frame, 0, std::max(totalFrame, 1), u8("%d フレーム")))
        an.time = std::clamp(frame / MMD_FPS, 0.0f, clip.duration);   // 曲はまだ動かさない

    // ドラッグ中は物理も曲も止め、離した瞬間にまとめて追従させる
    const bool active = ImGui::IsItemActive();
    if (!an.scrubbing && active)
    {
        SetMusicPaused(world, true);          // 掴んだ
    }
    else if (an.scrubbing && !active)
    {
        seekTo(an.time);                      // 離した: 物理リセット + 曲シーク
        if (an.playing) SetMusicPaused(world, false);
    }
    an.scrubbing = active;

    ImGui::Text(u8("%.2f / %.2f 秒   %d / %d フレーム"),
        an.time, clip.duration, frame, totalFrame);

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat(u8("速度"), &an.speed, 0.0f, 2.0f, "x%.2f");
}