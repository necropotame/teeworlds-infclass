//slightly modified from engineer-wall.cpp
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include "looper-wall.h"

const float g_BarrierMaxLength = 400.0;
const float g_BarrierRadius = 0.0;

CLooperWall::CLooperWall(CGameWorld *pGameWorld, vec2 Pos1, vec2 Pos2, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LOOPER_WALL)
{
	m_Pos = Pos1;
	if(distance(Pos1, Pos2) > g_BarrierMaxLength)
	{
		m_Pos2 = Pos1 + normalize(Pos2 - Pos1)*g_BarrierMaxLength;
	}
	else m_Pos2 = Pos2;
	m_Owner = Owner;
	m_LifeSpan = Server()->TickSpeed()*g_Config.m_InfLooperBarrierLifeSpan;
	GameWorld()->InsertEntity(this);
	
	m_EndPointIDs.set_size(2);
	for(int i=0; i<2; i++)
	{
		m_EndPointIDs[i] = Server()->SnapNewID();
	}
	
	
}

CLooperWall::~CLooperWall()
{
	for(int i=0; i<2; i++)
		Server()->SnapFreeID(m_EndPointIDs[i]);
}

void CLooperWall::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CLooperWall::Tick()
{
	if(m_MarkedForDestroy) return;

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
				if(p->GetPlayer())
				{
					for(CCharacter *pHook = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHook; pHook = (CCharacter *)pHook->TypeNext())
					{
						if(
							pHook->GetPlayer() &&
							!pHook->IsInfected() &&
							pHook->m_Core.m_HookedPlayer == p->GetPlayer()->GetCID() &&
							pHook->GetPlayer()->GetCID() != m_Owner && //The engineer will get the point when the infected dies
							p->m_LastFreezer != pHook->GetPlayer()->GetCID() && //The ninja will get the point when the infected dies
							p->GetClass() != PLAYERCLASS_UNDEAD //Or exploit with score
						)
						{
							Server()->RoundStatistics()->OnScoreEvent(pHook->GetPlayer()->GetCID(), SCOREEVENT_HELP_HOOK_BARRIER, pHook->GetClass());
							GameServer()->SendScoreSound(pHook->GetPlayer()->GetCID());
						}
					}
					
					if(p->GetClass() != PLAYERCLASS_UNDEAD)
					{
						int LifeSpanReducer = ((Server()->TickSpeed()*g_Config.m_InfLooperBarrierTimeReduce)/100);
						if(!p->IsInSlowMotion()) 
						{
							if(p->GetClass() == PLAYERCLASS_GHOUL)
							{
								float Factor = p->GetPlayer()->GetGhoulPercent();
								LifeSpanReducer += Server()->TickSpeed() * 5.0f * Factor;
							}
							
							m_LifeSpan -= LifeSpanReducer;
						}
					}
				}
				
				
				//old engineer wall kills character (zombie)
				//p->Die(m_Owner, WEAPON_HAMMER);
				
				
				//Slow-Motion modification here
				if (!p->IsInSlowMotion())
				{
					p->SlowMotionEffect();
					GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_EXCLAMATION);			  
				}
			}
		}
	}
}

void CLooperWall::TickPaused()
{
	//~ ++m_EvalTick;
}

void CLooperWall::Snap(int SnappingClient)
{	
	// Laser dieing animation
	int LifeDiff = 0;
	if (m_LifeSpan < 1*Server()->TickSpeed())
		LifeDiff = 6;
	else if (m_LifeSpan < 2*Server()->TickSpeed())
		LifeDiff = random_int(4, 6);
	else if (m_LifeSpan < 5*Server()->TickSpeed())
		LifeDiff = random_int(3, 5);
	else 
		LifeDiff = 3;
	

	vec2 dirVec = normalize(vec2(m_Pos.x-m_Pos2.x, m_Pos.y-m_Pos2.y));

	for(int i=0; i<2; i++) 
	{
		if(NetworkClipped(SnappingClient))
			return;
		
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser))); //removed m_ID
			if(!pObj)
				return;
			
			if (i == 0) 
			{
				pObj->m_X = (int)m_Pos.x+dirVec.y*(THICKNESS*0.5f); 
				pObj->m_Y = (int)m_Pos.y-dirVec.x*(THICKNESS*0.5f);
				pObj->m_FromX = (int)m_Pos2.x+dirVec.y*(THICKNESS*0.5f); 
				pObj->m_FromY = (int)m_Pos2.y-dirVec.x*(THICKNESS*0.5f);
			} 
			else // i == 1
			{
				pObj->m_X = (int)m_Pos.x-dirVec.y*(THICKNESS*0.5f); 
				pObj->m_Y = (int)m_Pos.y+dirVec.x*(THICKNESS*0.5f);
				pObj->m_FromX = (int)m_Pos2.x-dirVec.y*(THICKNESS*0.5f); 
				pObj->m_FromY = (int)m_Pos2.y+dirVec.x*(THICKNESS*0.5f);
			}

			pObj->m_StartTick = Server()->Tick()-LifeDiff;
		}
		
		if(!Server()->GetClientAntiPing(SnappingClient))
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointIDs[i], sizeof(CNetObj_Laser)));
			if(!pObj)
				return;
			
			vec2 Pos = m_Pos2;

			if (i == 0) 
			{
				pObj->m_X = (int)Pos.x+dirVec.y*(THICKNESS*0.5f); 
				pObj->m_Y = (int)Pos.y-dirVec.x*(THICKNESS*0.5f);
				pObj->m_FromX = (int)Pos.x+dirVec.y*(THICKNESS*0.5f); 
				pObj->m_FromY = (int)Pos.y-dirVec.x*(THICKNESS*0.5f);
			} 
			else // i == 1
			{
				pObj->m_X = (int)Pos.x-dirVec.y*(THICKNESS*0.5f); 
				pObj->m_Y = (int)Pos.y+dirVec.x*(THICKNESS*0.5f);
				pObj->m_FromX = (int)Pos.x-dirVec.y*(THICKNESS*0.5f); 
				pObj->m_FromY = (int)Pos.y+dirVec.x*(THICKNESS*0.5f);
			}

			pObj->m_StartTick = Server()->Tick();
		}
	}
}
