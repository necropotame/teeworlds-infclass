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
	
	StartVisualEffect();
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

void CWhiteHole::StartVisualEffect()
{
	float Radius = g_Config.m_InfWhiteHoleRadius;
	float RandomRadius, RandomAngle;
	float VecX, VecY;
	vec2 tPos, tVec, tVec2;
	for(int i=0; i<CWhiteHole::NUM_PARTICLES; i++)
	{
		RandomRadius = random_float()*(Radius-4.0f);
		RandomAngle = 2.0f * pi * random_float();
		VecX = cos(RandomAngle);
		VecY = sin(RandomAngle);
		m_ParticlePos[i] = m_Pos + vec2(RandomRadius * VecX, RandomRadius * VecY);
		tPos = m_Pos + vec2(Radius * VecX, Radius * VecY);
		tVec = vec2(m_ParticleStartSpeed * -VecX, m_ParticleStartSpeed * -VecY);
		for (int k = 0; k < 500; k++) 
		{
			tPos += tVec; 
			tVec2 = m_ParticlePos[i] - tPos;
			if (dot(tVec2, tVec) <= 0)
				break;
			tVec *= m_ParticleAcceleration; 
		}
		m_ParticleVec[i] = vec2(tVec);
	}
}

void CWhiteHole::Snap(int SnappingClient)
{
	// Draw ParticleEffect
	if(!Server()->GetClientAntiPing(SnappingClient))
	{
		for(int i=0; i<CWhiteHole::NUM_PARTICLES; i++)
		{
			CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDs[i], sizeof(CNetObj_Projectile)));
			if(pObj)
			{
				pObj->m_X = (int)m_ParticlePos[i].x;
				pObj->m_Y = (int)m_ParticlePos[i].y;
				pObj->m_VelX = 0;
				pObj->m_VelY = 0;
				pObj->m_StartTick = Server()->Tick();
				pObj->m_Type = WEAPON_HAMMER;
			}
		}
	}
}

void CWhiteHole::MoveParticles()
{
	float Radius = g_Config.m_InfWhiteHoleRadius;
	float RandomAngle;
	float VecX, VecY;
	vec2 VecMid;
	for(int i=0; i<CWhiteHole::NUM_PARTICLES; i++)
	{
		m_ParticlePos[i] += m_ParticleVec[i]; 
		VecMid = m_Pos - m_ParticlePos[i];
		if (dot(VecMid, m_ParticleVec[i]) <= 0)
		{
			RandomAngle = 2.0f * pi * random_float();
			VecX = cos(RandomAngle);
			VecY = sin(RandomAngle);
			m_ParticlePos[i] = m_Pos + vec2(Radius * VecX, Radius * VecY);
			m_ParticleVec[i] = vec2(m_ParticleStartSpeed * -VecX, m_ParticleStartSpeed * -VecY);
			continue;
		}
		m_ParticleVec[i] *= m_ParticleAcceleration; 
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
		MoveParticles();

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
