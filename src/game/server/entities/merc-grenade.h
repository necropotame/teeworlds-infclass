/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_MERCENARYGRENADE_H
#define GAME_SERVER_ENTITIES_MERCENARYGRENADE_H

class CMercenaryGrenade : public CEntity
{
public:
	int m_Owner;
	
public:
	CMercenaryGrenade(CGameWorld *pGameWorld, int Owner, vec2 Pos, vec2 Dir);

	vec2 GetPos(float Time);
	void FillInfo(CNetObj_Projectile *pProj);

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Explode();
	virtual void Snap(int SnappingClient);

private:
	vec2 m_ActualPos;
	vec2 m_ActualDir;
	vec2 m_Direction;
	int m_StartTick;
};

#endif
