/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/entities/growingexplosion.h>
#include "projectile.h"

CProjectile::CProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, int Weapon)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Type = Type;
	m_Pos = Pos;
	m_Direction = Dir;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_Force = Force;
	m_Damage = Damage;
	m_SoundImpact = SoundImpact;
	m_Weapon = Weapon;
	m_StartTick = Server()->Tick();
	m_Explosive = Explosive;

	GameWorld()->InsertEntity(this);
	
/* INFECTION MODIFICATION START ***************************************/
	m_IsFlashGrenade = false;
	m_StartPos = Pos;
/* INFECTION MODIFICATION END *****************************************/
}

void CProjectile::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

vec2 CProjectile::GetPos(float Time)
{
	float Curvature = 0;
	float Speed = 0;

	switch(m_Type)
	{
		case WEAPON_GRENADE:
			Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
			Speed = GameServer()->Tuning()->m_GrenadeSpeed;
			break;

		case WEAPON_SHOTGUN:
			Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
			Speed = GameServer()->Tuning()->m_ShotgunSpeed;
			break;

		case WEAPON_GUN:
			Curvature = GameServer()->Tuning()->m_GunCurvature;
			Speed = GameServer()->Tuning()->m_GunSpeed;
			break;
	}

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}


void CProjectile::Tick()
{
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &CurPos, 0);
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *TargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, CurPos, 6.0f, CurPos, OwnerChar);

	m_LifeSpan--;

/* INFECTION MODIFICATION START ***************************************/
	if(TargetChr || Collide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
	{
		if(m_LifeSpan >= 0 || m_Weapon == WEAPON_GRENADE)
			GameServer()->CreateSound(CurPos, m_SoundImpact);

		if(m_IsFlashGrenade)
		{
			vec2 Dir = normalize(PrevPos - CurPos);
			if(length(Dir) > 1.1) Dir = normalize(m_StartPos - CurPos);
			
			new CGrowingExplosion(GameWorld(), CurPos, Dir, m_Owner);
		}
		else if(m_Explosive)
		{
			GameServer()->CreateExplosion(CurPos, m_Owner, m_Weapon, false);
		}
		else if(TargetChr)
		{
			TargetChr->TakeDamage(m_Direction * max(0.001f, m_Force), m_Damage, m_Owner, m_Weapon);
		}

		GameServer()->m_World.DestroyEntity(this);
	}
	else if(Server()->Tick() - m_PortalTick >= Server()->TickSpeed()/2.0)
	{
		// Find portals
		for(CPortal *p = (CPortal*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_PORTAL); p; p = (CPortal *)p->TypeNext())
		{
			if(!p->m_pLinkedPortal)
				continue;
			
			vec2 EndPointVec = vec2(-64.0f*sin(p->m_Angle), 64.0f*cos(p->m_Angle));
			vec2 EndPoint0 = p->m_Pos + EndPointVec;
			vec2 EndPoint1 = p->m_Pos - EndPointVec;
			
			bool startSide = ((EndPoint1.x - EndPoint0.x)*(PrevPos.y - EndPoint0.y) - (EndPoint1.y - EndPoint0.y)*(PrevPos.x - EndPoint0.x))>0;
			bool endSide = ((EndPoint1.x - EndPoint0.x)*(CurPos.y - EndPoint0.y) - (EndPoint1.y - EndPoint0.y)*(CurPos.x - EndPoint0.x))>0;
			
			if(startSide != endSide)
			{
				bool side0 = ((EndPoint0.x - PrevPos.x)*(CurPos.y - PrevPos.y) - (EndPoint0.y - PrevPos.y)*(CurPos.x - PrevPos.x))>0;
				bool side1 = ((EndPoint1.x - PrevPos.x)*(CurPos.y - PrevPos.y) - (EndPoint1.y - PrevPos.y)*(CurPos.x - PrevPos.x))>0;
				
				if(side0 != side1)
				{
					m_PortalTick = Server()->Tick();
					
					float angleDiff = p->m_pLinkedPortal->m_Angle - p->m_Angle;
					
					vec2 RelPos = CurPos - p->m_Pos;
					m_Pos = p->m_pLinkedPortal->m_Pos + vec2(
						RelPos.x*cos(angleDiff) - RelPos.y*sin(angleDiff),
						RelPos.x*sin(angleDiff) + RelPos.y*cos(angleDiff)						
					);
					
					vec2 RelDir = normalize(CurPos - PrevPos);
					m_Direction = vec2(
						RelDir.x*cos(angleDiff) - RelDir.y*sin(angleDiff),
						RelDir.x*sin(angleDiff) + RelDir.y*cos(angleDiff)	
					);
					
					m_StartTick = Server()->Tick();
				}
			}
		}
	}
	
	//~ if(m_Weapon == WEAPON_GRENADE && !m_IsFlashGrenade)
	//~ {
		//~ for(CBomb *bomb = (CBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BOMB); bomb; bomb = (CBomb *)bomb->TypeNext())
		//~ {
			//~ if(bomb->m_Owner != m_Owner) continue;
			//~ if(distance(CurPos, bomb->m_Pos) > bomb->m_DetectionRadius) continue;
//~ 
			//~ if(bomb->AddBomb()) GameServer()->m_World.DestroyEntity(this);
		//~ }
	//~ }
/* INFECTION MODIFICATION END *****************************************/
}

void CProjectile::TickPaused()
{
	++m_StartTick;
/* INFECTION MODIFICATION START ***************************************/
	++m_PortalTick;
/* INFECTION MODIFICATION END *****************************************/
}

void CProjectile::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = (int)(m_Direction.x*100.0f);
	pProj->m_VelY = (int)(m_Direction.y*100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = m_Type;
}

void CProjectile::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
}

/* INFECTION MODIFICATION START ***************************************/
void CProjectile::FlashGrenade()
{
	m_IsFlashGrenade = true;
}
/* INFECTION MODIFICATION END *****************************************/
