/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "white-hole.h"
#include "growingexplosion.h"

CWhiteHole::CWhiteHole(CGameWorld *pGameWorld, vec2 CenterPos, int OwnerClientID)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_WHITE_HOLE)
{
	m_Pos = CenterPos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	m_Owner = OwnerClientID;
	m_LifeSpan = Server()->TickSpeed()*g_Config.m_InfWhiteHoleLifeSpan;
	m_Radius = 0.0f;
	isDieing = false;
	m_PlayerPullStrength = g_Config.m_InfWhiteHolePullStrength/10.0f;

	m_NumParticles = g_Config.m_InfWhiteHoleNumParticles;
	m_IDs = new int[m_NumParticles];
	m_ParticlePos = new vec2[m_NumParticles];
	m_ParticleVec = new vec2[m_NumParticles];
	for(int i=0; i<m_NumParticles; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
	
	StartVisualEffect();
}

CWhiteHole::~CWhiteHole()
{
	for(int i=0; i<m_NumParticles; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
	delete[] m_IDs;
	delete[] m_ParticlePos;
	delete[] m_ParticleVec;
}

void CWhiteHole::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

int CWhiteHole::GetOwner() const
{
	return m_Owner;
}

void CWhiteHole::StartVisualEffect()
{
	float Radius = g_Config.m_InfWhiteHoleRadius;
	float RandomRadius, RandomAngle;
	float VecX, VecY;
	for(int i=0; i<m_NumParticles; i++)
	{
		RandomRadius = random_float()*(Radius-4.0f);
		RandomAngle = 2.0f * pi * random_float();
		VecX = cos(RandomAngle);
		VecY = sin(RandomAngle);
		m_ParticlePos[i] = m_Pos + vec2(RandomRadius * VecX, RandomRadius * VecY);
		m_ParticleVec[i] = vec2(-VecX, -VecY);
	}
	// find out how long it takes for a particle to reach the mid
	RandomRadius = random_float()*(Radius-4.0f);
	RandomAngle = 2.0f * pi * random_float();
	VecX = cos(RandomAngle);
	VecY = sin(RandomAngle);
	vec2 ParticlePos = m_Pos + vec2(Radius * VecX, Radius * VecY);
	vec2 ParticleVec = vec2(-VecX, -VecY);
	vec2 VecMid;
	float Speed;
	int i=0;
	for ( ; i<500; i++) {
		VecMid = m_Pos - ParticlePos;
		Speed = m_ParticleStartSpeed * clamp(1.0f-length(VecMid)/Radius+0.5f, 0.0f, 1.0f);
		ParticlePos += vec2(ParticleVec.x*Speed, ParticleVec.y*Speed); 
		if (dot(VecMid, ParticleVec) <= 0)
			break;
		ParticleVec *= m_ParticleAcceleration; 
	}
	//if (i > 499) dbg_msg("CWhiteHole::StartVisualEffect()", "Problem in finding out how long a particle needs to reach the mid"); // this should never happen
	m_ParticleStopTickTime = i;
}

// Draw ParticleEffect
void CWhiteHole::Snap(int SnappingClient)
{
	// Draw AntiPing white hole effect
	if (Server()->GetClientAntiPing(SnappingClient)) {	
		int NumSide = 6;
		float AngleStep = 2.0f * pi / NumSide;
		float Radius = g_Config.m_InfWhiteHoleRadius;
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
		return;
	}

	// Draw full particle effect - if anti ping is not set to true
	for(int i=0; i<m_NumParticles; i++)
	{
		if (!isDieing && distance(m_ParticlePos[i], m_Pos) > m_Radius) continue; // start animation

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

void CWhiteHole::MoveParticles()
{
	float Radius = g_Config.m_InfWhiteHoleRadius;
	float RandomAngle, Speed;
	float VecX, VecY;
	vec2 VecMid;
	for(int i=0; i<m_NumParticles; i++)
	{
		VecMid = m_Pos - m_ParticlePos[i];
		Speed = m_ParticleStartSpeed * clamp(1.0f-length(VecMid)/Radius+0.5f, 0.0f, 1.0f);
		m_ParticlePos[i] += vec2(m_ParticleVec[i].x*Speed, m_ParticleVec[i].y*Speed); 
		if (dot(VecMid, m_ParticleVec[i]) <= 0)
		{
			if (m_LifeSpan < m_ParticleStopTickTime)
			{
				// make particles disappear
				m_ParticlePos[i] = vec2(-99999.0f, -99999.0f);
				m_ParticleVec[i] = vec2(0.0f, 0.0f);
				continue;
			}
			RandomAngle = 2.0f * pi * random_float();
			VecX = cos(RandomAngle);
			VecY = sin(RandomAngle);
			m_ParticlePos[i] = m_Pos + vec2(Radius * VecX, Radius * VecY);
			m_ParticleVec[i] = vec2(-VecX, -VecY);
			continue;
		}
		m_ParticleVec[i] *= m_ParticleAcceleration; 
	}
}

void CWhiteHole::MovePlayers()
{
	vec2 Dir;
	float Distance, Intensity;
	// Find a player to pull
	for(CCharacter *pPlayer = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pPlayer; pPlayer = (CCharacter *)pPlayer->TypeNext())
	{
		if(!g_Config.m_InfWhiteHoleAffectsHumans && !pPlayer->IsInfected()) continue; // stops humans from being sucked in, if config var is set
		
		Dir = m_Pos - pPlayer->m_Pos;
		Distance = length(Dir);
		if(Distance < m_Radius)
		{
			Intensity = clamp(1.0f-Distance/m_Radius+0.5f, 0.0f, 1.0f)*m_PlayerPullStrength;
			pPlayer->m_Core.m_Vel += normalize(Dir)*Intensity;
			pPlayer->m_Core.m_Vel *= m_PlayerDrag;
		}
	}
}

void CWhiteHole::Tick()
{
	m_LifeSpan--;
	if(m_LifeSpan < 0)
	{
		new CGrowingExplosion(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, 20, GROWINGEXPLOSIONEFFECT_BOOM_INFECTED);
		GameServer()->m_World.DestroyEntity(this);
	}
	else 
	{
		if (m_LifeSpan < m_ParticleStopTickTime) // shrink radius
		{
			m_Radius = m_LifeSpan/(float)m_ParticleStopTickTime * g_Config.m_InfWhiteHoleRadius;
			isDieing = true;
		}
		else if (m_Radius < g_Config.m_InfWhiteHoleRadius) // grow radius
		{
			m_Radius += m_RadiusGrowthRate;
			if (m_Radius > g_Config.m_InfWhiteHoleRadius)
				m_Radius = g_Config.m_InfWhiteHoleRadius;
		}

		MoveParticles();
		MovePlayers();
	}
}



void CWhiteHole::TickPaused()
{
	++m_StartTick;
}
