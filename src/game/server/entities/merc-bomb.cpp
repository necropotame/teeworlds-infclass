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
	float InnerRadius = 48.0f;
	
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	
	for(int i=0; i<8; i++)
	{
		float Angle = 2.0f * pi * static_cast<float>(i)/8.0f;
		GameServer()->CreateExplosion(m_Pos + vec2(cos(Angle), sin(Angle)) * InnerRadius, m_Owner, WEAPON_HAMMER, true);
	}
	
	CCharacter *apEnts[MAX_CLIENTS];
	float Radius = 135.0f;
	int Num = GameServer()->m_World.FindEntities(m_Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		vec2 Diff = apEnts[i]->m_Pos - m_Pos;
		vec2 ForceDir(0,1);
		float l = length(Diff);
		if(l)
			ForceDir = normalize(Diff);
		l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
		float Dmg = 12 * l;
		if((int)Dmg)
			apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, m_Owner, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
	}
	
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
