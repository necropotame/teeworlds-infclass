/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "white-hole.h"

CWhiteHole::CWhiteHole(CGameWorld *pGameWorld, vec2 CenterPos, int OwnerClientID)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_WHITE_HOLE)
{
	m_Pos = CenterPos;
	GameWorld()->InsertEntity(this);
	//m_DetectionRadius = 60.0f;
	m_StartTick = Server()->Tick();
	m_Owner = OwnerClientID;
	m_InitialVel = GameServer()->GetPlayerChar(m_Owner)->m_Core.m_Vel;
	m_LifeSpan = Server()->TickSpeed()*g_Config.m_InfWhiteHoleLifeSpan;
	
	for(int i=0; i<NUM_IDS; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
	

}

CWhiteHole::~CWhiteHole()
{
	for(int i=0; i<NUM_IDS; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
}

void CWhiteHole::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

int CWhiteHole::GetOwner() const
{
	return m_Owner;
}

void CWhiteHole::Pull(CCharacter *pPlayer, float intensity)
{
	if(pPlayer) //If there is a player to pull
	{
		float Dist = distance(m_Pos, pPlayer->m_Pos);
		if(Dist < pPlayer->m_ProximityRadius+g_Config.m_InfWhiteHoleSingularitySize) //should be 50 at start
		{
			//Player remains at m_Pos
			return; // Do not pull anymore
		}
		else
		{
			//calculate direction and speed of pull
			vec2 Dir = normalize(pPlayer->m_Pos - m_Pos);
			pPlayer->m_Pos += Dir*clamp(Dist, 0.0f, 16.0f) * (1.0f - intensity) + m_InitialVel * intensity;
		}
	}
}


void CWhiteHole::Snap(int SnappingClient)
{
	//Define Radius
	float Radius = g_Config.m_InfWhiteHoleRadius;
	
	//Outer ring start
	int NumSide = CWhiteHole::NUM_SIDE;
	if(Server()->GetClientAntiPing(SnappingClient))
		NumSide = std::min(12, NumSide);
	
	float AngleStep = 2.0f * pi / NumSide;
	
	//	No outer particles planed
	for(int i=0; i<NumSide; i++)
	{
		vec2 PartPosStart = m_Pos + vec2(Radius * cos(AngleStep*i), Radius * sin(AngleStep*i));
		vec2 PartPosEnd = m_Pos + vec2(Radius * cos(AngleStep*(i+1)), Radius * sin(AngleStep*(i+1)));
		
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;
		
		pObj->m_X = (int)PartPosStart.x;
		pObj->m_Y = (int)PartPosStart.y;
		pObj->m_FromX = (int)PartPosEnd.x;
		pObj->m_FromY = (int)PartPosEnd.y;
		pObj->m_StartTick = Server()->Tick();
	}
	
//Inner particle effect start 
// contribution from duralakun
	
	if(!Server()->GetClientAntiPing(SnappingClient))
	{
		for(int i=0; i<CWhiteHole::NUM_PARTICLES; i++)
		{
			float RandomRadius = random_float()*(Radius-4.0f);
			float RandomAngle = 2.0f * pi * random_float();
			vec2 ParticlePos = m_Pos + vec2(RandomRadius * cos(RandomAngle), RandomRadius * sin(RandomAngle));
			
			CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDs[CWhiteHole::NUM_SIDE+i], sizeof(CNetObj_Projectile)));
			if(pObj)
			{
				pObj->m_X = (int)ParticlePos.x;
				pObj->m_Y = (int)ParticlePos.y;
				pObj->m_VelX = 0;
				pObj->m_VelY = 0;
				pObj->m_StartTick = Server()->Tick();
				pObj->m_Type = WEAPON_HAMMER;
			}
		}
	}
}

void CWhiteHole::Tick()
{
	
	m_LifeSpan--;
	if(m_LifeSpan < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
	}
	else 
	{
		// Find a player to pull
		for(CCharacter *pPlayer = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pPlayer; pPlayer = (CCharacter *)pPlayer->TypeNext())
		{
			//if(!pPlayer->IsInfected()) continue; All players are affected when commented
			
			
			//Calculate distance to white hole
			float Len = distance(pPlayer->m_Pos, m_Pos);
			
			//Decide the pull strength on the basis of distance to m_Pos
			if(Len < pPlayer->m_ProximityRadius+g_Config.m_InfWhiteHoleRadius)
			{
				Pull(pPlayer,0.33f); //light pull
			} 
			else if (Len < pPlayer->m_ProximityRadius+g_Config.m_InfWhiteHoleRadius * (2/3))
			{
				Pull(pPlayer,0.66f); //stronger pull
			}
			else if (Len < pPlayer->m_ProximityRadius+g_Config.m_InfWhiteHoleRadius * (1/3))
			{
				Pull(pPlayer,1.0f); //strongest pull
			}
		}
	}
	
}



void CWhiteHole::TickPaused()
{
	++m_StartTick;
}
