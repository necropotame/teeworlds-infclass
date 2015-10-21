/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include "classchooser.h"

CClassChooser::CClassChooser(CGameWorld *pGameWorld, vec2 Pos, int pId)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_CLASSCHOOSER)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	for(int i=0; i<END_HUMANCLASS - START_HUMANCLASS - 1; i++)
	{
		m_IDClass[i] = Server()->SnapNewID();
	}
	m_PlayerID = pId;
}

void CClassChooser::SetCursor(vec2 CurPos)
{
	m_CurPos = CurPos;
}

int CClassChooser::SelectClass()
{
	if(length(m_CurPos) < 200.0)
	{
		float angle = atan2(-m_CurPos.y, -m_CurPos.x);
		
		int nbHumanClass = END_HUMANCLASS - START_HUMANCLASS - 1;
		for(int i=nbHumanClass-1; i>=0; i--)
		{
			if(angle >= static_cast<float>(i)*pi/static_cast<float>(nbHumanClass)) return START_HUMANCLASS + i + 1;
		}
	}
	return 0;
}

//~ void CClassChooser::Tick()
//~ {
	//~ float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	//~ float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	//~ vec2 PrevPos = GetPos(Pt);
	//~ vec2 CurPos = GetPos(Ct);
	//~ int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &CurPos, 0);
	//~ CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	//~ CCharacter *TargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, CurPos, 6.0f, CurPos, OwnerChar);
//~ 
	//~ m_LifeSpan--;
//~ 
	//~ if(TargetChr || Collide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
	//~ {
		//~ if(m_LifeSpan >= 0 || m_Weapon == WEAPON_GRENADE)
			//~ GameServer()->CreateSound(CurPos, m_SoundImpact);
//~ 
		//~ if(m_Explosive)
			//~ GameServer()->CreateExplosion(CurPos, m_Owner, m_Weapon, false);
//~ 
		//~ else if(TargetChr)
			//~ TargetChr->TakeDamage(m_Direction * max(0.001f, m_Force), m_Damage, m_Owner, m_Weapon);
//~ 
		//~ GameServer()->m_World.DestroyEntity(this);
	//~ }
//~ }

void CClassChooser::Snap(int SnappingClient)
{	
	if(NetworkClipped(SnappingClient) || SnappingClient != m_PlayerID)
		return;
	
	float stepAngle = pi/static_cast<float>(END_HUMANCLASS - START_HUMANCLASS - 1);
	
	for(int i=0; i<END_HUMANCLASS - START_HUMANCLASS - 1; i++)
	{
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDClass[i], sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x - 100.0*cos((static_cast<float>(i)+0.5)*stepAngle);
		pP->m_Y = (int)m_Pos.y - 100.0*sin((static_cast<float>(i)+0.5)*stepAngle);
		pP->m_Type = POWERUP_WEAPON;
		
		switch(START_HUMANCLASS + i + 1)
		{
			case PLAYERCLASS_SOLDIER:
				pP->m_Subtype = WEAPON_GRENADE;
				break;
			case PLAYERCLASS_MEDIC:
				pP->m_Subtype = WEAPON_SHOTGUN;
				break;
			case PLAYERCLASS_ENGINEER:
				pP->m_Subtype = WEAPON_RIFLE;
				break;
		}
		
	}
	
	if(length(m_CurPos) < 200.0)
	{
		float angle = atan2(-m_CurPos.y, -m_CurPos.x);
			
		int nbHumanClass = END_HUMANCLASS - START_HUMANCLASS - 1;
		int i=nbHumanClass-1;
		for(; i>=0; i--)
		{
			if(angle >= static_cast<float>(i)*pi/static_cast<float>(nbHumanClass)) break;
		}
		
		if(i>=0 && i<nbHumanClass)
		{
		
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;
				
			
			pObj->m_X = (int)m_Pos.x - 100.0*cos((static_cast<float>(i)+0.5)*stepAngle);
			pObj->m_Y = (int)m_Pos.y - 100.0*sin((static_cast<float>(i)+0.5)*stepAngle);

			pObj->m_FromX = (int)m_Pos.x;
			pObj->m_FromY = (int)m_Pos.y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
}
