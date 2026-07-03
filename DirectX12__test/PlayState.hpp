#pragma once

#include "EngineAPI.hpp"

#define PLAY PlayState::Get()

enum class EngineMode
{
	EDITOR,
	Play,
	PAUSE
};

class ENGINE_API PlayState
{
public:
	static PlayState& Get();

    void SetMode(EngineMode mode);
    EngineMode GetCurrentMode() const noexcept;
    void SetStandalone(bool standalone);
    bool IsStandalone() const noexcept;
    bool isPlaying() const noexcept;

private:
    EngineMode m_CurrentMode = EngineMode::EDITOR;
    bool m_Standalone = false;
};