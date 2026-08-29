#include "AudioEngine.hpp"
#include <filesystem>
#include <fstream>
#pragma comment(lib, "xaudio2.lib")

bool AudioEngine::Init()
{
    if (FAILED(XAudio2Create(m_XAudio2.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR)))
        return false;
    if (FAILED(m_XAudio2->CreateMasteringVoice(&m_MasterVoice)))
        return false;

    // マスターボイスのチャンネル構成を取得
    XAUDIO2_VOICE_DETAILS details{};
    m_MasterVoice->GetVoiceDetails(&details);
    m_OutputChannels = details.InputChannels;

    DWORD channelMask = 0;
    m_MasterVoice->GetChannelMask(&channelMask);
    m_ChannelMask = channelMask;

    // X3DAudio初期化
    if (FAILED(X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, m_X3DInstance)))
        return false;

    return true;
}

void AudioEngine::Shutdown()
{
    if (m_MasterVoice) { m_MasterVoice->DestroyVoice(); m_MasterVoice = nullptr; }
    m_XAudio2.Reset();
    m_Cache.clear();
}

IXAudio2SourceVoice* AudioEngine::CreateVoice(const WAVEFORMATEX& fmt)
{
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(m_XAudio2->CreateSourceVoice(&voice, &fmt)))
        return nullptr;
    return voice;
}

std::shared_ptr<AudioClip> AudioEngine::Load(const std::string& path)
{
    auto it = m_Cache.find(path);
    if (it != m_Cache.end()) return it->second;

    std::string ext = std::filesystem::path(path).extension().string();
    std::shared_ptr<AudioClip> clip;
    if (ext == ".wav" || ext == ".WAV") clip = LoadWav(path);
    else                                 clip = LoadMp3(path); 

    if (clip) m_Cache[path] = clip;
    return clip;
}

// 最小WAVパーサ（PCM/RIFF前提）
std::shared_ptr<AudioClip> AudioEngine::LoadWav(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::vector<BYTE> file((std::istreambuf_iterator<char>(f)), {});
    if (file.size() < 12 || memcmp(file.data(), "RIFF", 4) != 0) return nullptr;

    auto clip = std::make_shared<AudioClip>();
    size_t pos = 12;
    while (pos + 8 <= file.size())
    {
        char id[5] = {}; memcpy(id, &file[pos], 4);
        uint32_t size; memcpy(&size, &file[pos + 4], 4);
        pos += 8;
        if (memcmp(id, "fmt ", 4) == 0)
            memcpy(&clip->format, &file[pos], std::min<uint32_t>(size, sizeof(WAVEFORMATEX)));
        else if (memcmp(id, "data", 4) == 0)
            clip->data.assign(file.begin() + pos, file.begin() + pos + size);
        pos += size + (size & 1);   // チャンクは偶数境界
    }
    if (clip->data.empty()) return nullptr;
    return clip;
}

std::shared_ptr<AudioClip> AudioEngine::LoadMp3(const std::string&) { return nullptr; }