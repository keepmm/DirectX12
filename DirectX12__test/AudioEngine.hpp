#pragma once

#include <xaudio2.h>
#include <wrl.h>
#include "Defines.hpp"
#include <x3daudio.h>

struct AudioClip
{
	std::vector<BYTE> data; // PCMデータ
	WAVEFORMATEX format;    // 音声フォーマット情報
};

class AudioEngine
{
public:
	static AudioEngine& Get()
	{
		static AudioEngine instance;
		return instance;
	}

    bool Init();
	void Shutdown();

    // クリップ取得（キャッシュ付き）
    std::shared_ptr<AudioClip> Load(const std::string& path);

    // ボイス生成（再生はAudioSource側で制御）
    IXAudio2SourceVoice* CreateVoice(const WAVEFORMATEX& fmt);

    IXAudio2* Get2() { return m_XAudio2.Get(); }

    X3DAUDIO_HANDLE& X3D() { return m_X3DInstance; }
    UINT32 ChannelMask() const { return m_ChannelMask; }
    UINT32 OutputChannels() const { return m_OutputChannels; }

private:
    AudioEngine() = default;

    ComPtr<IXAudio2> m_XAudio2;
    IXAudio2MasteringVoice* m_MasterVoice = nullptr;

    std::unordered_map<std::string, std::shared_ptr<AudioClip>> m_Cache;

    std::shared_ptr<AudioClip> LoadWav(const std::string& path);
    std::shared_ptr<AudioClip> LoadMp3(const std::string& path);
    X3DAUDIO_HANDLE m_X3DInstance{};
    UINT32 m_ChannelMask = 0;
    UINT32 m_OutputChannels = 2;
};

