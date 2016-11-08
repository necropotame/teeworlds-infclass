/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "merc-bomb.h"

CMercenaryBomb::CMercenaryBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_MERCENARY_BOMB)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
}

void CMercenaryBomb::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CMercenaryBomb::Explode()
{
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_HAMMER, false, TAKEDAMAGEMODE_NOINFECTION);
	
	GameServer()->m_World.DestroyEntity(this);
}

void CMercenaryBomb::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = POWERUP_ARMOR;
	pP->m_Subtype = 0;
}

void CMercenaryBomb::TickPaused()
{
	++m_StartTick;
}
