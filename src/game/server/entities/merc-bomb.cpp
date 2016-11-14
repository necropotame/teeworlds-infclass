/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "growingexplosion.h"
#include "merc-bomb.h"

CMercenaryBomb::CMercenaryBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Type)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_MERCENARY_BOMB)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
	m_Damage = 6;
	m_Type = Type;
}

void CMercenaryBomb::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CMercenaryBomb::IncreaseDamage()
{
	m_Damage += 2;
	if(m_Damage > g_Config.m_InfMercBombs)
		m_Damage = g_Config.m_InfMercBombs;
}

void CMercenaryBomb::Explode()
{
	float Factor = static_cast<float>(m_Damage)/g_Config.m_InfMercBombs;
	
	switch(m_Type)
	{
		case EFFECT_EXPLOSION:
			new CGrowingExplosion(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, 14.0f * Factor, GROWINGEXPLOSIONEFFECT_EXPLOSION_INFECTED);
			break;
		case EFFECT_LOVE:
			new CGrowingExplosion(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, 16.0f * Factor, GROWINGEXPLOSIONEFFECT_LOVE_INFECTED);
			break;
		case EFFECT_SHOCKWAVE:
			new CGrowingExplosion(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, 14.0f * Factor, GROWINGEXPLOSIONEFFECT_SHOCKWAVE_INFECTED);
			break;
	}
				
	GameServer()->m_World.DestroyEntity(this);
}

void CMercenaryBomb::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
	if(pClient->IsInfected())
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
