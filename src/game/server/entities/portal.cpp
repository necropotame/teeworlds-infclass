/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include "portal.h"

const float g_PortalLifeSpan = 30.0;

CPortal::CPortal(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Num)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PORTAL)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
	m_Num = Num;
	m_pLinkedPortal = 0;
	
	for(int i=0; i<MAX_PORTALBULLET; i++)
	{
		m_IDBullet[i] = Server()->SnapNewID();
	}
	
	m_LifeSpan = Server()->TickSpeed()*g_PortalLifeSpan;
}

void CPortal::Destroy()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(OwnerChar && OwnerChar->m_pPortal[m_Num] == this)
	{
		OwnerChar->m_pPortal[m_Num] = 0;
	}
	
	if(m_pLinkedPortal && m_pLinkedPortal->m_pLinkedPortal == this)
	{
		m_pLinkedPortal->m_pLinkedPortal = 0;
	}
	
	for(int i=0; i<MAX_PORTALBULLET; i++)
	{
		Server()->SnapFreeID(m_IDBullet[i]);
	}
	
	delete this;
}

void CPortal::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CPortal::Link(CPortal* pPortal)
{
	pPortal->m_pLinkedPortal = this;
	m_pLinkedPortal = pPortal;
}

void CPortal::Tick()
{
	if(!m_pLinkedPortal)
		return;
	
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
			if(Server()->Tick() - p->m_PortalTick < Server()->TickSpeed()/2.0f)
				continue;
				
			if(distance(p->m_Pos, m_Pos) < 48.0)
			{
				p->m_PortalTick = Server()->Tick();
				//p->m_Core.m_Pos = m_pLinkedPortal->m_Pos + (p->m_Pos - m_Pos);
				p->m_Core.m_Pos = m_pLinkedPortal->m_Pos;
			}
		}
	}
}

void CPortal::Snap(int SnappingClient)
{
	float Radius = 64.0f;
	float Speed = 1.0f;
	int NbBullet = MAX_PORTALBULLET;
	
	if(!m_pLinkedPortal)
	{
		CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
		if(pClient && !pClient->IsInfected())
		{
			Radius = 32.0f;
			Speed = -0.5f;
			NbBullet = MAX_PORTALBULLET/2;
		}
		else return;
	}
	
	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(Speed*time*pi/2, 2.0f*pi);
	
	for(int i=0; i<NbBullet; i++)
	{
		if(NetworkClipped(SnappingClient))
			return;
		
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDBullet[i], sizeof(CNetObj_Pickup)));
		if(!pP)
			return;
		
		float shiftedAngle = angle + 2.0*pi*static_cast<float>(i)/static_cast<float>(NbBullet);

		pP->m_X = static_cast<int>(m_Pos.x + Radius*cos(shiftedAngle));
		pP->m_Y = static_cast<int>(m_Pos.y + Radius*sin(shiftedAngle));
		//~ if(SnappingClient == m_Owner)
		//~ {
			//~ pP->m_Type = POWERUP_HEALTH;
		//~ }
		//~ else
			pP->m_Type = POWERUP_ARMOR;
		pP->m_Subtype = 0;
	}
}

void CPortal::TickPaused()
{
	++m_StartTick;
}
