/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include "bomb.h"

CBomb::CBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_BOMB)
{
	m_Pos = Pos;
	GameWorld()->InsertEntity(this);
	m_DetectionRadius = 60.0f;
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
	m_nbBomb = 3;
	
	for(int i=0; i<MAX_BOMB; i++)
	{
		m_IDBomb[i] = Server()->SnapNewID();
	}
}

void CBomb::Destroy()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(OwnerChar && OwnerChar->m_pBomb == this)
	{
		OwnerChar->m_pBomb = 0;
	}
	
	for(int i=0; i<MAX_BOMB; i++)
	{
		Server()->SnapFreeID(m_IDBomb[i]);
	}
	delete this;
}

void CBomb::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CBomb::Explode()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(!OwnerChar)
		return;
		
	vec2 dir = normalize(OwnerChar->m_Pos - m_Pos);
	
	
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_HAMMER, false);
	for(int i=0; i<6; i++)
	{
		float angle = static_cast<float>(i)*2.0*pi/6.0;
		vec2 expPos = m_Pos + vec2(90.0*cos(angle), 90.0*sin(angle));
		GameServer()->CreateExplosion(expPos, m_Owner, WEAPON_HAMMER, false);
	}
	for(int i=0; i<12; i++)
	{
		float angle = static_cast<float>(i)*2.0*pi/12.0;
		vec2 expPos = vec2(180.0*cos(angle), 180.0*sin(angle));
		if(dot(expPos, dir) <= 0)
		{
			GameServer()->CreateExplosion(m_Pos + expPos, m_Owner, WEAPON_HAMMER, false);
		}
	}
	
	m_nbBomb--;
	
	if(m_nbBomb == 0)
	{
		GameServer()->m_World.DestroyEntity(this);
	}
}

void CBomb::Snap(int SnappingClient)
{
	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(time*pi/2, 2.0f*pi);
	
	for(int i=0; i<m_nbBomb; i++)
	{
		if(NetworkClipped(SnappingClient))
			return;
		
		float shiftedAngle = angle + 2.0*pi*static_cast<float>(i)/static_cast<float>(MAX_BOMB);
		
		CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDBomb[i], sizeof(CNetObj_Projectile)));
		pProj->m_X = (int)(m_Pos.x + m_DetectionRadius*cos(shiftedAngle));
		pProj->m_Y = (int)(m_Pos.y + m_DetectionRadius*sin(shiftedAngle));
		pProj->m_VelX = (int)(0.0f);
		pProj->m_VelY = (int)(0.0f);
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Type = WEAPON_GRENADE;
	}
}

void CBomb::TickPaused()
{
	++m_StartTick;
}

bool CBomb::AddBomb()
{
	if(m_nbBomb < MAX_BOMB)
	{
		m_nbBomb++;
		return true;
	}
	else return false;
}
