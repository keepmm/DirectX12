/*****************************************************************//**
 * \file   MonoBehavior.hpp
 * \brief  
 * 
 * çÏê¨é“ 
 * çÏê¨ì˙ 2026/3/17
 * çXêVóöó
 * *********************************************************************/
#pragma once

#include "RenderContext.hpp"

class Actor;

class MonoBehavior
{
public:
	virtual ~MonoBehavior() = default;
	virtual void OnStart() {}
	virtual void OnUpdate(_In_ float deltatime) {}
	virtual void OnFixedUpdate(_In_ float deltatime) {}
	virtual void OnLateUpdate(_In_ float deltatime) {}
	virtual void OnDraw(_In_ const RenderContext& context) {}

protected:
	Actor& GetActor() const { return *m_Actor; }
private:
	friend class Actor;
	void AttachActor(Actor* actor) { m_Actor = actor; }

	Actor* m_Actor = nullptr;
};

