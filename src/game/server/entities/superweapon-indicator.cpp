/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "superweapon-indicator.h"

CSuperWeaponIndicator::CSuperWeaponIndicator(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_SUPERWEAPON_INDICATOR)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_Radius = 40.0f;
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
	m_OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	
	m_IDs.set_size(3);
	for(int i=0; i<m_IDs.size(); i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
}

CSuperWeaponIndicator::~CSuperWeaponIndicator()
{
	for(int i=0; i<m_IDs.size(); i++)
		Server()->SnapFreeID(m_IDs[i]);
}

void CSuperWeaponIndicator::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}


void CSuperWeaponIndicator::Snap(int SnappingClient)
{
	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(time*pi/2, 2.0f*pi);
	
	for(int i=0; i<m_IDs.size(); i++)
	{
		if(NetworkClipped(SnappingClient))
			return;
		
		float shiftedAngle = angle + 2.0*pi*static_cast<float>(i)/static_cast<float>(m_IDs.size());
		
		CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDs[i], sizeof(CNetObj_Projectile)));
		pProj->m_X = (int)(m_Pos.x + m_Radius*cos(shiftedAngle));
		pProj->m_Y = (int)(m_Pos.y + m_Radius*sin(shiftedAngle));
		pProj->m_VelX = (int)(0.0f);
		pProj->m_VelY = (int)(0.0f);
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Type = WEAPON_HAMMER;
	}
}

void CSuperWeaponIndicator::Tick()
{
	if (m_OwnerChar->m_HasWhiteHole == false) {
		GameServer()->m_World.DestroyEntity(this);
	} 
	
	//refresh indicator position
	m_Pos = m_OwnerChar->m_Core.m_Pos;
	
	
}
