/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_BOMB_H
#define GAME_SERVER_ENTITIES_BOMB_H

#include <game/server/entity.h>

#define MAX_BOMB 3

class CBomb : public CEntity
{
public:
	CBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Snap(int SnappingClient);
	void Destroy();
	void Reset();
	void Explode();
	bool AddBomb();
	void TickPaused();
	int GetNbBombs() { return m_nbBomb; }

private:
	int m_StartTick;
	int m_IDBomb[MAX_BOMB];
	int m_nbBomb;
	
public:
	float m_DetectionRadius;
	int m_Owner;
};

#endif
