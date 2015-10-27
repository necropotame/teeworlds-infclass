/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PORTAL_H
#define GAME_SERVER_ENTITIES_PORTAL_H

#include <game/server/entity.h>

#define MAX_PORTALBULLET 12

class CPortal : public CEntity
{
public:
	CPortal(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Num);

	virtual void Snap(int SnappingClient);
	void Destroy();
	void Reset();
	void Tick();
	void TickPaused();
	void Link(CPortal* pPortal);

private:
	int m_StartTick;
	int m_IDBullet[MAX_PORTALBULLET];
	int m_Num;
	
public:
	int m_Owner;
	CPortal* m_pLinkedPortal;
	int m_LifeSpan;
};

#endif
