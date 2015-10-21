/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_BARRIER_H
#define GAME_SERVER_ENTITIES_BARRIER_H

#include <game/server/entity.h>

class CBarrier : public CEntity
{
public:
	CBarrier(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

private:
	vec2 m_Pos2;
	int m_Owner;
	int m_LifeSpan;
};

#endif
