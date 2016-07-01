/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "barrier.h"

const float g_BarrierLifeSpan = 30.0;
const float g_BarrierMaxLength = 300.0;
const float g_BarrierRadius = 0.0;

CBarrier::CBarrier(CGameWorld *pGameWorld, vec2 Pos1, vec2 Pos2, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_BARRIER)
{
	m_Pos = Pos1;
	if(distance(Pos1, Pos2) > g_BarrierMaxLength)
	{
		m_Pos2 = Pos1 + normalize(Pos2 - Pos1)*g_BarrierMaxLength;
	}
	else m_Pos2 = Pos2;
	m_Owner = Owner;
	m_LifeSpan = Server()->TickSpeed()*g_BarrierLifeSpan;
	GameWorld()->InsertEntity(this);
	m_EndPointID = Server()->SnapNewID();
}

void CBarrier::Destroy()
{
	Server()->SnapFreeID(m_EndPointID);
	
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(OwnerChar && OwnerChar->m_pBarrier == this)
	{
		OwnerChar->m_pBarrier = 0;
	}
}

void CBarrier::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CBarrier::Tick()
{
	m_LifeSpan--;
	
	if(m_LifeSpan < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
	}
	else
	{
		// Find other players
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(!p->IsInfected()) continue;

			vec2 IntersectPos = closest_point_on_line(m_Pos, m_Pos2, p->m_Pos);
			float Len = distance(p->m_Pos, IntersectPos);
			if(Len < p->m_ProximityRadius+g_BarrierRadius)
			{				
				//Check for hook traps. The case when the player is frozen is in the function Die()
				if(!p->IsFrozen())
				{
					for(CCharacter *pHook = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHook; pHook = (CCharacter *)pHook->TypeNext())
					{
						if(p->GetPlayer() && p->GetPlayer()->m_InClassChooserMenu)
							continue;
						
						if(p->GetPlayer() && pHook->GetPlayer() && pHook->m_Core.m_HookedPlayer == p->GetPlayer()->GetCID() && pHook->GetPlayer()->GetCID() != m_Owner)
						{
							pHook->GetPlayer()->IncreaseScore(1);
						}
					}
				}
				
				p->Die(m_Owner, WEAPON_HAMMER);
			}
		}
	}
}

void CBarrier::TickPaused()
{
	//~ ++m_EvalTick;
}

void CBarrier::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)m_Pos.x;
		pObj->m_Y = (int)m_Pos.y;
		pObj->m_FromX = (int)m_Pos2.x;
		pObj->m_FromY = (int)m_Pos2.y;
		pObj->m_StartTick = Server()->Tick();
	}
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointID, sizeof(CNetObj_Laser)));
		if(!pObj)
			return;
		
		vec2 Pos = m_Pos2;

		pObj->m_X = (int)Pos.x;
		pObj->m_Y = (int)Pos.y;
		pObj->m_FromX = (int)Pos.x;
		pObj->m_FromY = (int)Pos.y;
		pObj->m_StartTick = Server()->Tick();
	}
}
