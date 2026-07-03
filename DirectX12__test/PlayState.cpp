#include "PlayState.hpp"

PlayState& PlayState::Get()
{
	static PlayState instance;
	return instance;
}

void PlayState::SetMode(EngineMode mode) { m_CurrentMode = mode; }
EngineMode PlayState::GetCurrentMode() const noexcept { return m_CurrentMode; }
void PlayState::SetStandalone(bool s) { m_Standalone = s; }
bool PlayState::IsStandalone() const noexcept { return m_Standalone; }
bool PlayState::isPlaying() const noexcept { return m_CurrentMode == EngineMode::Play; }