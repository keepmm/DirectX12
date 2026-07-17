#pragma once

#include "AudioEngine.hpp"
#include <vector>

class SePlayer
{
public:
	void Play(const std::string& path, float volume = 1.0f)
	{
		auto clip = AudioEngine::Get().Load(path);
		if (!clip) return;

		Reap();   // 再生し終わったボイスを回収

		IXAudio2SourceVoice* voice = AudioEngine::Get().CreateVoice(clip->format);
		if (!voice) return;

		XAUDIO2_BUFFER buf{};
		buf.AudioBytes = static_cast<UINT32>(clip->data.size());
		buf.pAudioData = clip->data.data();
		buf.Flags = XAUDIO2_END_OF_STREAM;
		voice->SubmitSourceBuffer(&buf);
		voice->SetVolume(volume);
		voice->Start();
		m_Voices.push_back(voice);
	}

	void Shutdown()
	{
		for (auto* v : m_Voices) { v->Stop(); v->DestroyVoice(); }
		m_Voices.clear();
	}

private:
	void Reap()
	{
		for (auto it = m_Voices.begin(); it != m_Voices.end();)
		{
			XAUDIO2_VOICE_STATE st{};
			(*it)->GetState(&st);
			if (st.BuffersQueued == 0) { (*it)->DestroyVoice(); it = m_Voices.erase(it); }
			else ++it;
		}
	}

	std::vector<IXAudio2SourceVoice*> m_Voices;
};