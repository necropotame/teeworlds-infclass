#ifndef GAME_SERVER_ENTITIES_LOOPER_WALL_H
#define GAME_SERVER_ENTITIES_LOOPER_WALL_H

#include <game/server/entity.h>

class CLooperWall : public CEntity
{
public:
	enum
	{
		THICKNESS = 17,
		NUM_PARTICLES = 18,
	};
public:
	CLooperWall(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner);
	virtual ~CLooperWall();
	
	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);
	int GetTick() { return m_LifeSpan; }

public:
	int m_Owner;
private:
	vec2 m_Pos2;
	int m_LifeSpan;
	array<int> m_EndPointIDs;
	const float g_BarrierMaxLength = 400.0;
	const float g_BarrierRadius = 0.0;
	int m_ParticleIDs[NUM_PARTICLES];
};

#endif
	
