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
	//Check if the portal is connected
	if(!m_pLinkedPortal)
		return;
	
	m_LifeSpan--;
	
	if(m_LifeSpan < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
	}
	else if(m_Num == 0)
	{
		bool Try = true;
		while(Try)
		{		
			CCharacter* Candidate1 = 0;
			CCharacter* Candidate2 = 0;
			CCharacter* Nearest1 = 0;
			CCharacter* Nearest2 = 0;
			float Candidate1Dist = 45.0f;
			float Candidate2Dist = 45.0f;
			float Nearest1Dist = 30.0f;
			float Nearest2Dist = 30.0f;
			
			//Find candidate at portal 1
			{
				CCharacter* p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
				while(p)
				{
					float d = distance(p->m_Pos, m_Pos);

					if(p->IsTeleportable())
					{
						if(d < Candidate1Dist)
						{
							Candidate1 = p;
							Candidate1Dist = d;
						}
					}
					
					p = (CCharacter *)p->TypeNext();
				}
			}
			
			//Find candidate at portal 2
			{
				CCharacter* p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
				while(p)
				{
					float d = distance(p->m_Pos, m_pLinkedPortal->m_Pos);

					if(p->IsTeleportable())
					{
						if(d < Candidate2Dist)
						{
							Candidate2 = p;
							Candidate2Dist = d;
						}
					}
					
					p = (CCharacter *)p->TypeNext();
				}
			}
			
			//Find blocking tee at portal 1
			{
				CCharacter* p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
				while(p)
				{
					float d = distance(p->m_Pos, m_Pos);

					if(p != Candidate1)
					{
						if(d < Nearest1Dist)
						{
							Nearest1 = p;
							Nearest1Dist = d;
						}
					}
					
					p = (CCharacter *)p->TypeNext();
				}
			}
			
			//Find blocking tee at portal 2
			{
				CCharacter* p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
				while(p)
				{
					float d = distance(p->m_Pos, m_pLinkedPortal->m_Pos);

					if(p != Candidate2)
					{
						if(d < Nearest2Dist)
						{
							Nearest2 = p;
							Nearest2Dist = d;
						}
					}
					
					p = (CCharacter *)p->TypeNext();
				}
			}
			
			//Teleportation
			if(Candidate1 && Candidate2)
			{
				Candidate1->m_PortalTick = Server()->Tick();
				Candidate2->m_PortalTick = Server()->Tick();
				
				vec2 OldPos = Candidate2->m_Core.m_Pos;
				if(Nearest1) //First portal occupied, swap candidates
				{
					Candidate2->m_Core.m_Pos = Candidate1->m_Core.m_Pos;
				}
				else Candidate2->m_Core.m_Pos = m_Pos;
				
				if(Nearest2) //Second portal occupied, swap candidates
				{
					Candidate1->m_Core.m_Pos = OldPos;
				}
				else Candidate1->m_Core.m_Pos = m_pLinkedPortal->m_Pos;
				
				Candidate1->m_Pos = Candidate1->m_Core.m_Pos;
				Candidate2->m_Pos = Candidate2->m_Core.m_Pos;
				
				GameServer()->CreateSound(m_Pos, SOUND_CTF_RETURN);
				GameServer()->CreateSound(m_pLinkedPortal->m_Pos, SOUND_CTF_RETURN);
				
			}
			else if(Candidate1 && !Nearest2)
			{
				Candidate1->m_PortalTick = Server()->Tick();
				Candidate1->m_Core.m_Pos = m_pLinkedPortal->m_Pos;
				Candidate1->m_Pos = Candidate1->m_Core.m_Pos;
				
				GameServer()->CreateSound(m_Pos, SOUND_CTF_RETURN);
				GameServer()->CreateSound(m_pLinkedPortal->m_Pos, SOUND_CTF_RETURN);
			}
			else if(Candidate2 && !Nearest1)
			{
				Candidate2->m_PortalTick = Server()->Tick();
				Candidate2->m_Core.m_Pos = m_Pos;
				Candidate2->m_Pos = Candidate2->m_Core.m_Pos;
				
				GameServer()->CreateSound(m_Pos, SOUND_CTF_RETURN);
				GameServer()->CreateSound(m_pLinkedPortal->m_Pos, SOUND_CTF_RETURN);
			}
			else Try = false;
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
		}
		else return;
	}
	else if(Server()->TickSpeed()*g_PortalLifeSpan - m_LifeSpan < Server()->TickSpeed()/4.0f)
	{
		Radius = 32.0f + 32.0f*static_cast<float>(Server()->TickSpeed()*g_PortalLifeSpan - m_LifeSpan)/static_cast<float>(Server()->TickSpeed()/4.0f);
	}
	
	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(Speed*time*pi/2, 2.0f*pi);
	
	float lifeSpanPercent = static_cast<float>(m_LifeSpan)/static_cast<float>(Server()->TickSpeed()*g_PortalLifeSpan);
	int NbHeart = 1 + NbBullet * lifeSpanPercent;
	
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
		if(SnappingClient == m_Owner && i<NbHeart)
		{
			pP->m_Type = POWERUP_HEALTH;
		}
		else
			pP->m_Type = POWERUP_ARMOR;
		pP->m_Subtype = 0;
	}
}

void CPortal::TickPaused()
{
	++m_StartTick;
}
