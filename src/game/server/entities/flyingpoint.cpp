/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>
#include "flyingpoint.h"

CFlyingPoint::CFlyingPoint(CGameWorld *pGameWorld, vec2 Pos, int TrackedPlayer, int Points, vec2 InitialVel)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_FLYINGPOINT)
{
	m_Pos = Pos;
	m_TrackedPlayer = TrackedPlayer;
	m_InitialVel = InitialVel;
	m_Points = Points;
	m_InitialAmount = 1.0f;
	GameWorld()->InsertEntity(this);
}

void CFlyingPoint::Tick()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_TrackedPlayer);
	if(OwnerChar)
	{
		float Dist = distance(m_Pos, OwnerChar->m_Pos);
		if(Dist < 24.0f)
		{
			OwnerChar->GetPlayer()->IncreaseGhoulLevel(m_Points);
			OwnerChar->IncreaseOverallHp(4);
			GameServer()->m_World.DestroyEntity(this);
		}
		else
		{
			vec2 Dir = normalize(OwnerChar->m_Pos - m_Pos);
			m_Pos += Dir*clamp(Dist, 0.0f, 16.0f) * (1.0f - m_InitialAmount) + m_InitialVel * m_InitialAmount;
			
			m_InitialAmount *= 0.98f;
		}
	}
}

void CFlyingPoint::Snap(int SnappingClient)
{
	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(pObj)
	{
		pObj->m_X = (int)m_Pos.x;
		pObj->m_Y = (int)m_Pos.y;
		pObj->m_VelX = 0;
		pObj->m_VelY = 0;
		pObj->m_StartTick = Server()->Tick();
		pObj->m_Type = WEAPON_HAMMER;
	}
}
