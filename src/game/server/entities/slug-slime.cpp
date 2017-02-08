/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "slug-slime.h"

CSlugSlime::CSlugSlime(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_SLUG_SLIME)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_LifeSpan = Server()->TickSpeed()*10;
	GameWorld()->InsertEntity(this);
}

void CSlugSlime::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

int CSlugSlime::GetOwner() const
{
	return m_Owner;
}

void CSlugSlime::Tick()
{
	if(m_MarkedForDestroy) return;
	
	if(m_LifeSpan <= 0)
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}
	
	// Find other players
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(p->IsInfected()) continue;

		if(distance(p->m_Pos, m_Pos) < 84.0f)
		p->Poison(3, m_Owner);
	}
	
	if(random_prob(0.2f))
	{
		GameServer()->CreateDeath(m_Pos, m_Owner);
	}
	
	m_LifeSpan--;
}
