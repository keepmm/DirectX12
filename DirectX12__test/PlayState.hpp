#pragma once

#define PLAY PlayState::Get()

enum class EngineMode
{
	EDITOR,
	Play,
	PAUSE
};

class PlayState
{
public:
	static PlayState& Get()
	{
		static PlayState instance;
		return instance;
	}

	void SetMode(_In_ EngineMode mode) { m_CurrentMode = mode; }
	inline EngineMode GetCurrentMode() const noexcept { return m_CurrentMode; }
	void SetStandalone(bool standalone) { m_Standalone = standalone; }
	bool IsStandalone() const noexcept { return m_Standalone; }

	bool isPlaying() const noexcept { return m_CurrentMode == EngineMode::Play; }
private:
	EngineMode m_CurrentMode = EngineMode::EDITOR;
	bool m_Standalone = false;
};
