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

void CClassChooser::Destroy()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_PlayerID);
	if(OwnerChar && OwnerChar->m_pClassChooser == this)
	{
		OwnerChar->m_pClassChooser = 0;
	}
	
	for(int i=0; i<END_HUMANCLASS - START_HUMANCLASS - 1; i++)
	{
		Server()->SnapFreeID(m_IDClass[i]);
	}
	delete this;
}

void CClassChooser::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CClassChooser::SetCursor(vec2 CurPos)
{
	m_CurPos = CurPos;
}

int CClassChooser::SelectClass()
{
	if(length(m_CurPos) >= 200.0)
		return 0;

	if(length(m_CurPos) > 50.0)
	{
		int NbChoosableClass = END_HUMANCLASS-START_HUMANCLASS-1;

		float angle = atan2(-m_CurPos.y, -m_CurPos.x);
		int Selection = static_cast<int>(NbChoosableClass*(angle/pi));
		
		if(Selection < 0 || Selection >= NbChoosableClass)
			return 0;
		
		int ClassIter = 0;
		for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
		{
			if(GameServer()->m_pController->IsChoosableClass(i))
			{
				if(ClassIter == Selection) return i;
				else ClassIter++;
			}
		}
	}
	else
	{
		CPlayer* pPlayer = GameServer()->m_apPlayers[m_PlayerID];
		if(pPlayer)
		{
			return GameServer()->m_pController->ChooseHumanClass(pPlayer);
		}
	}
	
	return 0;
}

void CClassChooser::Snap(int SnappingClient)
{	
	if(NetworkClipped(SnappingClient) || SnappingClient != m_PlayerID)
		return;
	
	int NbChoosableClass = END_HUMANCLASS-START_HUMANCLASS-1;
	
	float stepAngle = pi/static_cast<float>(NbChoosableClass);
	
	int ClassIterator = 0;
	for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
	{
		if(!GameServer()->m_pController->IsChoosableClass(i))
		{
			ClassIterator++;
			continue;
		}
		
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDClass[i - START_HUMANCLASS - 1], sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x - 130.0*cos((static_cast<float>(ClassIterator)+0.5)*stepAngle);
		pP->m_Y = (int)m_Pos.y - 130.0*sin((static_cast<float>(ClassIterator)+0.5)*stepAngle);
		pP->m_Type = POWERUP_WEAPON;
		
		switch(i)
		{
			case PLAYERCLASS_SOLDIER:
				pP->m_Subtype = WEAPON_GRENADE;
				break;
			case PLAYERCLASS_MEDIC:
				pP->m_Subtype = WEAPON_SHOTGUN;
				break;
			case PLAYERCLASS_MERCENARY:
				pP->m_Subtype = WEAPON_GUN;
				break;
			case PLAYERCLASS_SCIENTIST:
				pP->m_Subtype = WEAPON_HAMMER;
				break;
			case PLAYERCLASS_ENGINEER:
				pP->m_Subtype = WEAPON_RIFLE;
				break;
			case PLAYERCLASS_NINJA:
				pP->m_Subtype = WEAPON_NINJA;
				break;
		}
		
		ClassIterator++;
	}
	
	int ClassUnderCursor = -1;
	if(length(m_CurPos) <= 200.0)
	{
		float angle = atan2(-m_CurPos.y, -m_CurPos.x);
		int Selection = static_cast<int>(NbChoosableClass*(angle/pi));
			
		if(length(m_CurPos) > 50.0)
		{
			if(Selection >= 0 && Selection < NbChoosableClass)
			{
				int ClassIter = 0;
				for(int i=START_HUMANCLASS+1; i<END_HUMANCLASS; i++)
				{
					if(GameServer()->m_pController->IsChoosableClass(i))
					{
						if(ClassIter == Selection)
						{
							ClassUnderCursor = i;
							break;
						}
						else ClassIter++;
					}
				}
					
				switch(ClassUnderCursor)
				{
					case PLAYERCLASS_SOLDIER:
						GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_SOLDIER);
						break;
					case PLAYERCLASS_MERCENARY:
						GameServer()->SendBroadcast("Mercenary", m_PlayerID);
						break;
					case PLAYERCLASS_MEDIC:
						GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_MEDIC);
						break;
					case PLAYERCLASS_SCIENTIST:
						GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_SCIENTIST);
						break;
					case PLAYERCLASS_ENGINEER:
						GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_ENGINEER);
						break;
					case PLAYERCLASS_NINJA:
						GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_NINJA);
						break;
				}
			}
		}
		else
		{
			GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_RANDOM_CHOICE);
			
			Selection = rand()%NbChoosableClass;
		}
		
		if(Selection >= 0 && Selection < NbChoosableClass)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;
				
			pObj->m_X = (int)m_Pos.x - 130.0*cos((static_cast<float>(Selection)+0.5)*stepAngle);
			pObj->m_Y = (int)m_Pos.y - 130.0*sin((static_cast<float>(Selection)+0.5)*stepAngle);

			pObj->m_FromX = (int)m_Pos.x;
			pObj->m_FromY = (int)m_Pos.y;
			pObj->m_StartTick = Server()->Tick();
		}
		
	}
	else
	{
		GameServer()->SendBroadcast_Language(m_PlayerID, TEXTID_CLASSCHOOSER_HELP);
	}
}
