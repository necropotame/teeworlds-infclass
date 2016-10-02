/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_MINE_H
#define GAME_SERVER_ENTITIES_MINE_H

#include <game/server/entity.h>

class CMine : public CEntity
{
public:
	enum
	{
		NUM_SIDE = 8,
		NUM_PARTICLES = 8,
		NUM_IDS = NUM_SIDE + NUM_PARTICLES,
	};
	
public:
	CMine(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Snap(int SnappingClient);
	void Destroy();
	void Reset();
	void TickPaused();
	void Tick();

	int GetOwner() const;
	void Explode(int DetonatedBy);

private:
	int m_IDs[NUM_IDS];
	
public:
	int m_StartTick;
	float m_DetectionRadius;
	int m_Owner;
};

#endif
