/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_MERCENARY_BOMB_H
#define GAME_SERVER_ENTITIES_MERCENARY_BOMB_H

#include <game/server/entity.h>
#include <base/tl/array.h>

class CMercenaryBomb : public CEntity
{
public:
	CMercenaryBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Snap(int SnappingClient);
	void Reset();
	void Explode();
	void TickPaused();
	
public:
	int m_StartTick;
	int m_Owner;
};

#endif
