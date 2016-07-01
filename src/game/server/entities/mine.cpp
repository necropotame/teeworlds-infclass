/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "mine.h"
#include "growingexplosion.h"

CMine::CMine(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_MINE)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_DetectionRadius = 60.0f;
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
	
	for(int i=0; i<NUM_IDS; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
}

void CMine::Destroy()
{
	for(int i=0; i<NUM_IDS; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
	delete this;
}

void CMine::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

int CMine::GetOwner() const
{
	return m_Owner;
}

void CMine::Explode()
{
	new CGrowingExplosion<8>(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
	GameServer()->m_World.DestroyEntity(this);
	
	//Self damage
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(OwnerChar)
	{
		float Dist = distance(m_Pos, OwnerChar->m_Pos);
		if(Dist < OwnerChar->m_ProximityRadius+g_Config.m_InfMineRadius)
			OwnerChar->TakeDamage(vec2(0.0f, 0.0f), 6, m_Owner, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
		else if(Dist < OwnerChar->m_ProximityRadius+2*g_Config.m_InfMineRadius)
		{
			float Alpha = (Dist - g_Config.m_InfMineRadius-OwnerChar->m_ProximityRadius)/g_Config.m_InfMineRadius;
			OwnerChar->TakeDamage(vec2(0.0f, 0.0f), 6*Alpha, m_Owner, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
		}
	}
}

void CMine::Snap(int SnappingClient)
{
	float AngleStep = 2.0f * pi / CMine::NUM_SIDE;
	float Radius = g_Config.m_InfMineRadius;
	for(int i=0; i<CMine::NUM_SIDE; i++)
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
	for(int i=0; i<CMine::NUM_PARTICLES; i++)
	{
		float RandomRadius = sqrt((float)frandom())*(Radius-8.0f);
		float RandomAngle = 2.0f * pi * frandom();
		vec2 ParticlePos = m_Pos + vec2(RandomRadius * cos(RandomAngle), RandomRadius * sin(RandomAngle));
		
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[CMine::NUM_SIDE+i], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)ParticlePos.x;
		pObj->m_Y = (int)ParticlePos.y;
		pObj->m_FromX = (int)ParticlePos.x;
		pObj->m_FromY = (int)ParticlePos.y;
		pObj->m_StartTick = Server()->Tick();
	}
}

void CMine::Tick()
{
	// Find other players
	bool MustExplode = false;
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(!p->IsInfected()) continue;

		float Len = distance(p->m_Pos, m_Pos);
		if(Len < p->m_ProximityRadius+g_Config.m_InfMineRadius)
		{
			MustExplode = true;
			break;
		}
	}
	
	if(MustExplode)
		Explode();
}

void CMine::TickPaused()
{
	++m_StartTick;
}
