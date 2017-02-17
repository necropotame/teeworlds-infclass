/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SLUG_SLIME_H
#define GAME_SERVER_ENTITIES_SLUG_SLIME_H

#include <game/server/entity.h>

class CSlugSlime : public CEntity
{
public:
	CSlugSlime(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Reset();
	virtual void Tick();

	int GetOwner() const;
	
public:
	int m_Owner;
	int m_LifeSpan;
	int m_HealTick;
};

#endif
