/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_GROWINGEXP_H
#define GAME_SERVER_ENTITIES_GROWINGEXP_H

#define MAXGROWING 10
#define GROWINGMAP_LENGTH (2*MAXGROWING+1)
#define GROWINGMAP_SIZE (GROWINGMAP_LENGTH*GROWINGMAP_LENGTH)

class CGrowingExplosion : public CEntity
{
public:
	CGrowingExplosion(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir);
	
	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();

private:
	vec2 m_SeedPos;
	int m_SeedX;
	int m_SeedY;
	int m_StartTick;
	int m_GrowingMap[GROWINGMAP_SIZE];
};

#endif
