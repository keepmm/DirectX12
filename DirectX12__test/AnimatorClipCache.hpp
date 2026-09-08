/*****************************************************************//**
 * \file   AnimatorClipCache.hpp
 * \brief  Play/Stop でシーンを作り直しても読み込み済みVMDを捨てないための退避場所
 *
 * 作成者 keepmm
 * 作成日 2026/9/6
 * 更新履歴
 *   2026/9/6 新規作成
 *
 * エディタの Stop はシーンをスナップショットから復元する(World を作り直す)ため、
 * AnimatorComponent の clips が消えて AsyncLoader からの読み直しになる。
 * Stop の直前にここへ退避し、AnimatorSystem がスケルトン準備後に拾い直す。
 * *********************************************************************/
#pragma once

#include "ModelData.hpp"
#include <string>
#include <unordered_map>
#include <vector>

struct AnimatorClipSnapshot
{
	std::vector<AnimationClip> clips;
	int   currentClip = 0;
	std::string currentClipName;
	float time = 0.0f;
	bool  playing = false;
};

/// @brief Entity名 -> 退避したクリップ
inline std::unordered_map<std::string, AnimatorClipSnapshot>& AnimatorClipCache()
{
	static std::unordered_map<std::string, AnimatorClipSnapshot> cache;
	return cache;
}
