/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "spawnprotect.h"

CSpawProtect::CSpawProtect(CGameWorld *pGameWorld, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP)
{
	m_Owner = Owner;

	GameWorld()->InsertEntity(this);   
}

void CSpawProtect::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CSpawProtect::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);

	if(!pOwner)
	{
		Reset();
		return;
	}

	if(!pOwner->Protected())
	{
		Reset();
		return;
	}

	m_Pos = pOwner->m_Pos;
	m_Pos.y -= 32;
}


void CSpawProtect::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient, m_Pos))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = POWERUP_HEALTH;
	pP->m_Subtype = 0;


}
