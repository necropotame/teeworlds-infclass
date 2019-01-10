//slightly modified from engineer-wall.cpp
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include "looper-wall.h"

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
	for(int i=0; i<NUM_PARTICLES; i++)
	{
		m_ParticleIDs[i] = Server()->SnapNewID();
	}
}

CLooperWall::~CLooperWall()
{
	for(int i=0; i<2; i++)
	{
		Server()->SnapFreeID(m_EndPointIDs[i]);
	}
	for(int i=0; i<NUM_PARTICLES; i++)
	{
		Server()->SnapFreeID(m_ParticleIDs[i]);
	}
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

				//Slow-Motion modification here
				if (!p->IsInSlowMotion())
				{
					p->SlowMotionEffect(g_Config.m_InfSlowMotionWallDuration);
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
	
	vec2 dirVec = vec2(m_Pos.x-m_Pos2.x, m_Pos.y-m_Pos2.y);
	vec2 dirVecN = normalize(dirVec);
	vec2 dirVecT = vec2(dirVecN.y*THICKNESS*0.5f, -dirVecN.x*THICKNESS*0.5f);

	for(int i=0; i<2; i++) 
	{
		if(NetworkClipped(SnappingClient))
			return;

		if (i == 1)
		{
			dirVecT.x = -dirVecT.x;
			dirVecT.y = -dirVecT.y;
		}
		
		// draws the first two dots + the lasers
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser))); //removed m_ID
			if(!pObj)
				return;

			pObj->m_X = (int)m_Pos.x+dirVecT.x; 
			pObj->m_Y = (int)m_Pos.y+dirVecT.y;
			pObj->m_FromX = (int)m_Pos2.x+dirVecT.x; 
			pObj->m_FromY = (int)m_Pos2.y+dirVecT.y;

			pObj->m_StartTick = Server()->Tick()-LifeDiff;
		}
		
		// draws one dot at the end of each laser
		if(!Server()->GetClientAntiPing(SnappingClient))
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointIDs[i], sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)m_Pos2.x+dirVecT.x; 
			pObj->m_Y = (int)m_Pos2.y+dirVecT.y;
			pObj->m_FromX = (int)m_Pos2.x+dirVecT.x; 
			pObj->m_FromY = (int)m_Pos2.y+dirVecT.y;

			pObj->m_StartTick = Server()->Tick();
		}
	}

	// draw particles inside wall
	if(!Server()->GetClientAntiPing(SnappingClient))
	{
		vec2 startPos = vec2(m_Pos2.x+dirVecT.x, m_Pos2.y+dirVecT.y);
		dirVecT.x = -dirVecT.x*2.0f;
		dirVecT.y = -dirVecT.y*2.0f;
		
		int particleCount = length(dirVec)/g_BarrierMaxLength*NUM_PARTICLES;
		for(int i=0; i<particleCount; i++)
		{
			CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ParticleIDs[i], sizeof(CNetObj_Projectile)));
			if(pObj)
			{
				float fRandom1 = random_float();
				float fRandom2 = random_float();
				pObj->m_X = (int)startPos.x + fRandom1*dirVec.x + fRandom2*dirVecT.x;
				pObj->m_Y = (int)startPos.y + fRandom1*dirVec.y + fRandom2*dirVecT.y;
				pObj->m_VelX = 0;
				pObj->m_VelY = 0;
				pObj->m_StartTick = Server()->Tick();
				pObj->m_Type = WEAPON_HAMMER;
			}
		}
	}
}
