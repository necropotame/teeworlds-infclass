/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_WHITE_HOLE_H
#define GAME_SERVER_ENTITIES_WHITE_HOLE_H

#include <game/server/entity.h>

class CWhiteHole : public CEntity
{
public:
	enum
	{
		NUM_SIDE = 16,
		NUM_PARTICLES = 48,
		NUM_IDS = NUM_SIDE + NUM_PARTICLES,
	};
	
public:
	CWhiteHole(CGameWorld *pGameWorld, vec2 CenterPos, int OwnerClientID);
	virtual ~CWhiteHole();
	
	virtual void Snap(int SnappingClient);
	virtual void Reset();
	virtual void TickPaused();
	virtual void Tick();
	
	int GetOwner() const;
	void Pull(CCharacter *pPlayer,float intensity);
	int GetTick() { return m_LifeSpan; }
	
private:
	int m_IDs[NUM_IDS];
	
public:
	int m_StartTick;
	//float m_DetectionRadius;
	int m_Owner;
	int m_LifeSpan;
	vec2 m_InitialVel;
};

#endif


