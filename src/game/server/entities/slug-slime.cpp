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
	m_LifeSpan = Server()->TickSpeed()*g_Config.m_InfSlimeDuration;
	GameWorld()->InsertEntity(this);
	m_HealTick = 0;
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
		if(!GameServer()->Collision()->AreConnected(p->m_Pos, m_Pos, 84.0f))
			continue; // not in reach
		
		if(p->IsInfected()) 
		{
			if(p->GetClass() != PLAYERCLASS_SLUG)
			{
				p->SetEmote(EMOTE_HAPPY, Server()->Tick());
				if(Server()->Tick() >= m_HealTick + (Server()->TickSpeed()/g_Config.m_InfSlimeHealRate))
				{
					m_HealTick = Server()->Tick();
					p->IncreaseHealth(1);
				}
			}
		} 
		else // p->IsHuman()
		{ 
			p->Poison(g_Config.m_InfSlimePoisonDuration, m_Owner); 
		}
	}
	
	if(random_prob(0.2f))
	{
		GameServer()->CreateDeath(m_Pos, m_Owner);
	}
	
	m_LifeSpan--;
}
