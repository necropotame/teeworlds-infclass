/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include "toxicgaz.h"

CToxicGaz::CToxicGaz(CGameWorld *pGameWorld, vec2 Pos)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_TOXICGAZ)
{
	m_Pos = Pos;
	m_AnimationTick = rand()%Server()->TickSpeed()/2;
	m_ColorId = -1;
	
	GameWorld()->InsertEntity(this);
}

void CToxicGaz::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CToxicGaz::GetIdForColor()
{
	m_ColorId = -1;
	for(int i = 0; i < MAX_CLIENTS; i ++)
	{		
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(!pPlayer->IsInfected()) continue;
		
		if(pPlayer->GetClass() == PLAYERCLASS_WITCH && m_ColorId < 0)
		{
			m_ColorId = i;
		}
		else if(pPlayer->GetClass() == PLAYERCLASS_UNDEAD && m_ColorId < 0)
		{
			m_ColorId = i;
		}
		else
		{
			m_ColorId = i;
			return;
		}
	}
}

void CToxicGaz::Tick()
{
	m_AnimationTick--;
	
	if(m_AnimationTick <= 0)
	{
		m_AnimationTick = Server()->TickSpeed()/2;
		
		GetIdForColor();
		
		if(m_ColorId >= 0)
		{
			GameServer()->CreateDeath(m_Pos, m_ColorId);
		}
		
		// Find other players
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{		
			if(distance(p->m_Pos, m_Pos) < 100.0)
			{
				p->GetPlayer()->StartInfection();
			}
		}
	}
}
