/*****************************************************************//**
 * \file   LiveTimeline.cpp
 * \brief  LiveTimeline の補間と JSON 入出力
 *
 * 作成者 keepmm
 * 作成日 2026/9/4
 * 更新履歴
 *   2026/9/4 新規作成
 *   2026/9/4 キーフレーム補間トラック(LiveTrack)を追加
 * *********************************************************************/
#include "LiveTimeline.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

// ---------------- LiveTrack ---------------- //

void LiveTrack::SortByTime()
{
	std::stable_sort(keys.begin(), keys.end(),
		[](const LiveKey& a, const LiveKey& b) { return a.time < b.time; });
}

bool LiveTrack::Evaluate(float time, float4& out) const
{
	if (keys.empty()) return false;

	// 端は最初/最後のキーで固定する
	if (time <= keys.front().time) { out = keys.front().value; return true; }
	if (time >= keys.back().time) { out = keys.back().value; return true; }

	// time 以上の最初のキーを二分探索
	const auto it = std::lower_bound(keys.begin(), keys.end(), time,
		[](const LiveKey& k, float t) { return k.time < t; });

	const LiveKey& b = *it;
	const LiveKey& a = *(it - 1);
	const float span = b.time - a.time;
	const float s = (span > 1e-6f) ? (time - a.time) / span : 0.0f;

	out.x = a.value.x + (b.value.x - a.value.x) * s;
	out.y = a.value.y + (b.value.y - a.value.y) * s;
	out.z = a.value.z + (b.value.z - a.value.z) * s;
	out.w = a.value.w + (b.value.w - a.value.w) * s;
	return true;
}

int LiveTrack::FindKey(float time, float tolerance) const
{
	int best = -1;
	float bestDiff = tolerance;
	for (int i = 0; i < (int)keys.size(); ++i)
	{
		const float diff = std::fabs(keys[i].time - time);
		if (diff <= bestDiff) { bestDiff = diff; best = i; }
	}
	return best;
}

void LiveTrack::SetKey(float time, const float4& value)
{
	const int i = FindKey(time);
	if (i >= 0)
	{
		keys[i].value = value;	// 同じ時刻のキーは上書き
		return;
	}
	keys.push_back(LiveKey{ time, value });
	SortByTime();
}

// ---------------- LiveTimeline ---------------- //

void LiveTimeline::SortByTime()
{
	std::stable_sort(cues.begin(), cues.end(),
		[](const LiveCue& a, const LiveCue& b) { return a.time < b.time; });
	for (auto& t : tracks) t.SortByTime();
}

void LiveTimeline::ResetFired(float fromTime)
{
	for (auto& c : cues)
	{
		c.fired = (c.time < fromTime);
	}
}

float LiveTimeline::Duration() const
{
	float d = 0.0f;
	for (const auto& c : cues) d = std::max(d, c.time);
	for (const auto& t : tracks)
		if (!t.keys.empty()) d = std::max(d, t.keys.back().time);
	return d;
}

bool LiveTimeline::LoadJson(const std::string& path)
{
	std::ifstream ifs(path);
	if (!ifs) return false;

	json j;
	ifs >> j;

	cues.clear();
	if (j.contains("cues"))
	{
		for (const auto& e : j["cues"])
		{
			LiveCue c;
			c.type = (LiveCue::Type)e.value("type", 1);
			c.time = e.value("time", 0.0f);
			c.fade = e.value("fade", 0.0f);
			c.target = e.value("target", std::string{});
			c.value = e.value("value", 1.0f);
			if (e.contains("color"))
			{
				const auto& a = e["color"];
				c.color = COLOR{ a[0], a[1], a[2], a.size() > 3 ? (float)a[3] : 1.0f };
			}
			cues.push_back(std::move(c));
		}
	}

	tracks.clear();
	if (j.contains("tracks"))
	{
		for (const auto& e : j["tracks"])
		{
			LiveTrack t;
			t.property = (LiveTrack::Property)e.value("property", 0);
			t.target = e.value("target", std::string{});
			t.enabled = e.value("enabled", true);
			if (e.contains("keys"))
			{
				for (const auto& k : e["keys"])
				{
					LiveKey key;
					key.time = k.value("time", 0.0f);
					const auto& v = k.at("value");
					key.value = float4{
						v[0],
						v.size() > 1 ? (float)v[1] : 0.0f,
						v.size() > 2 ? (float)v[2] : 0.0f,
						v.size() > 3 ? (float)v[3] : 0.0f };
					t.keys.push_back(key);
				}
			}
			tracks.push_back(std::move(t));
		}
	}

	SortByTime();
	ResetFired(0.0f);
	return true;
}

bool LiveTimeline::SaveJson(const std::string& path) const
{
	json j;

	j["cues"] = json::array();
	for (const auto& c : cues)
	{
		json e;
		e["type"] = (uint8_t)c.type;
		e["time"] = c.time;
		e["fade"] = c.fade;
		e["target"] = c.target;
		e["value"] = c.value;
		e["color"] = { c.color.x, c.color.y, c.color.z, c.color.w };
		j["cues"].push_back(std::move(e));
	}

	j["tracks"] = json::array();
	for (const auto& t : tracks)
	{
		json e;
		e["property"] = (uint8_t)t.property;
		e["target"] = t.target;
		e["enabled"] = t.enabled;
		e["keys"] = json::array();
		for (const auto& k : t.keys)
		{
			e["keys"].push_back(json{
				{ "time",  k.time },
				{ "value", { k.value.x, k.value.y, k.value.z, k.value.w } } });
		}
		j["tracks"].push_back(std::move(e));
	}

	std::ofstream ofs(path);
	if (!ofs) return false;
	ofs << j.dump(4);
	return true;
}
