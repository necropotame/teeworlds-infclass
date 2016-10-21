/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/vmath.h>
#include <new>
#include <engine/shared/config.h>
#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>
#include <iostream>

#include "character.h"
#include "laser.h"
#include "scientist-laser.h"
#include "projectile.h"
#include "mine.h"
#include "mercenarybomb.h"
#include "hero-flag.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	m_AirJumpCounter = 0;
	m_FirstShot = true;
	
	m_pBarrier = 0;
	m_pBomb = 0;
	m_FlagID = Server()->SnapNewID();
	m_HeartID = Server()->SnapNewID();
	m_CursorID = Server()->SnapNewID();
	m_BarrierHintID = Server()->SnapNewID();
	m_AntiFireTick = 0;
	m_IsFrozen = false;
	m_FrozenTime = -1;
	m_DartLifeSpan = -1;
	m_IsInvisible = false;
	m_InvisibleTick = 0;
	m_PositionLockTick = -Server()->TickSpeed()*10;
	m_PositionLocked = false;
	m_PoisonTick = 0;
	m_HealTick = 0;
	m_InAirTick = 0;
	m_InWater = 0;
	m_WaterJumpLifeSpan = 0;
/* INFECTION MODIFICATION END *****************************************/
}

bool CCharacter::FindWitchSpawnPosition(vec2& Pos)
{
	float Angle = atan2(m_Input.m_TargetY, m_Input.m_TargetX);
	
	for(int i=0; i<32; i++)
	{
		float TestAngle;
		
		TestAngle = Angle + i * (pi / 32.0f);
		Pos = m_Pos + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;
		
		if(GameServer()->m_pController->IsSpawnable(Pos))
			return true;
		
		TestAngle = Angle - i * (pi / 32.0f);
		Pos = m_Pos + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;
		
		if(GameServer()->m_pController->IsSpawnable(Pos))
			return true;
	}
	
	return false;
}

bool CCharacter::FindPortalPosition(vec2 Pos, vec2& Res)
{
	vec2 PortalShift = Pos - m_Pos;
	vec2 PortalDir = normalize(PortalShift);
	if(length(PortalShift) > 500.0f)
		PortalShift = PortalDir * 500.0f;
	
	float Iterator = length(PortalShift);
	while(Iterator > 0.0f)
	{
		PortalShift = PortalDir * Iterator;
		vec2 PortalPos = m_Pos + PortalShift;
	
		if(GameServer()->m_pController->IsSpawnable(PortalPos))
		{
			Res = PortalPos;
			return true;
		}
		
		Iterator -= 4.0f;
	}
	
	return false;
}

void CCharacter::Reset()
{	
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

/* INFECTION MODIFICATION START ***************************************/
	m_AntiFireTick = Server()->Tick();
	m_IsFrozen = false;
	m_FrozenTime = -1;
	m_PositionLockTick = -Server()->TickSpeed()*10;
	m_PositionLocked = false;
	m_Poison = 0;

	ClassSpawnAttributes();
	DestroyChildEntities();
	if(GetClass() == PLAYERCLASS_NONE)
	{
		OpenClassChooser();
	}
/* INFECTION MODIFICATION END *****************************************/

	return true;
}

void CCharacter::Destroy()
{	
/* INFECTION MODIFICATION START ***************************************/
	DestroyChildEntities();
	
	if(m_FlagID >= 0)
	{
		Server()->SnapFreeID(m_FlagID);
		m_FlagID = -1;
	}
	if(m_HeartID >= 0)
	{
		Server()->SnapFreeID(m_HeartID);
		m_HeartID = -1;
	}
	if(m_CursorID >= 0)
	{
		Server()->SnapFreeID(m_CursorID);
		m_CursorID = -1;
	}
	if(m_BarrierHintID >= 0)
	{
		Server()->SnapFreeID(m_BarrierHintID);
		m_BarrierHintID = -1;
	}
/* INFECTION MODIFICATION END *****************************************/

	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}

void CCharacter::HandleWaterJump()
{
	if(m_InWater)
	{
		m_Core.m_Jumped &= ~2;
		m_Core.m_TriggeredEvents &= ~COREEVENT_GROUND_JUMP;
		m_Core.m_TriggeredEvents &= ~COREEVENT_AIR_JUMP;

		if(m_Input.m_Jump && m_DartLifeSpan <= 0 && m_WaterJumpLifeSpan <=0)
		{
			m_WaterJumpLifeSpan = Server()->TickSpeed()/2;
			m_DartLifeSpan =  g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
			m_DartDir = Direction;
			m_DartOldVelAmount = length(m_Core.m_Vel);
			
			m_Core.m_TriggeredEvents |= COREEVENT_AIR_JUMP;
		}
	}
	
	m_WaterJumpLifeSpan--;
	
	m_DartLifeSpan--;
	
	if(m_DartLifeSpan == 0)
	{
		//~ m_Core.m_Vel = m_DartDir * 5.0f;
		m_Core.m_Vel = m_DartDir*m_DartOldVelAmount;
	}
	
	if(m_DartLifeSpan > 0)
	{
		m_Core.m_Vel = m_DartDir * 15.0f;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);
		m_Core.m_Vel = vec2(0.f, 0.f);
	}	
}

void CCharacter::HandleNinja()
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_NINJA_HAMMER)
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_DartLifeSpan--;

	if (m_DartLifeSpan == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_DartDir*m_DartOldVelAmount;
	}

	if (m_DartLifeSpan > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_DartDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamage(vec2(0, -10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_NOINFECTION);
			}
		}
	}
}


void CCharacter::DoWeaponSwitch()
{
/* INFECTION MODIFICATION START ***************************************/
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1)
		return;
/* INFECTION MODIFICATION END *****************************************/

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(GetClass() == PLAYERCLASS_SPIDER)
	{
		int WantedHookMode = m_HookMode;
		
		if(Next < 128) // make sure we only try sane stuff
		{
			while(Next) // Next Weapon selection
			{
				WantedHookMode = (WantedHookMode+1)%2;
				Next--;
			}
		}

		if(Prev < 128) // make sure we only try sane stuff
		{
			while(Prev) // Prev Weapon selection
			{
				WantedHookMode = (WantedHookMode+2-1)%2;
				Prev--;
			}
		}

		// Direct Weapon selection
		if(m_LatestInput.m_WantedWeapon)
			WantedHookMode = m_Input.m_WantedWeapon-1;

		if(WantedHookMode >= 0 && WantedHookMode < 2)
			m_HookMode = WantedHookMode;
	}
	else
	{
		int WantedWeapon = m_ActiveWeapon;
		if(m_QueuedWeapon != -1)
			WantedWeapon = m_QueuedWeapon;
		
		if(Next < 128) // make sure we only try sane stuff
		{
			while(Next) // Next Weapon selection
			{
				WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
				if(m_aWeapons[WantedWeapon].m_Got)
					Next--;
			}
		}

		if(Prev < 128) // make sure we only try sane stuff
		{
			while(Prev) // Prev Weapon selection
			{
				WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
				if(m_aWeapons[WantedWeapon].m_Got)
					Prev--;
			}
		}

		// Direct Weapon selection
		if(m_LatestInput.m_WantedWeapon)
			WantedWeapon = m_Input.m_WantedWeapon-1;

		// check for insane values
		if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
			m_QueuedWeapon = WantedWeapon;

		DoWeaponSwitch();
	}
}

void CCharacter::UpdateTuningParam()
{
	CTuningParams* pTuningParams = &m_pPlayer->m_NextTuningParams;
	
	bool NoActions = false;
	bool FixedPosition = false;
	
	if(m_PositionLocked)
	{
		NoActions = true;
		FixedPosition = true;
	}
	if(m_IsFrozen)
	{
		NoActions = true;
	}
	if(m_HookMode == 1)
	{
		pTuningParams->m_HookDragSpeed = 0.0f;
		pTuningParams->m_HookDragAccel = 1.0f;
	}
	if(m_InWater == 1)
	{
		pTuningParams->m_Gravity = -0.05f;
		pTuningParams->m_GroundFriction = 0.95f;
		pTuningParams->m_GroundControlSpeed = 250.0f / Server()->TickSpeed();
		pTuningParams->m_GroundControlAccel = 1.5f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirFriction = 0.95f;
		pTuningParams->m_AirControlSpeed = 250.0f / Server()->TickSpeed();
		pTuningParams->m_AirControlAccel = 1.5f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
	}
	
	if(NoActions)
	{
		pTuningParams->m_GroundControlAccel = 0.0f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
		pTuningParams->m_AirControlAccel = 0.0f;
		pTuningParams->m_HookLength = 0.0f;
	}
	if(FixedPosition)
	{
		pTuningParams->m_Gravity = 0.0f;
	}
}

void CCharacter::FireWeapon()
{
/* INFECTION MODIFICATION START ***************************************/
	//Wait 1 second after spawning
	if(Server()->Tick() - m_AntiFireTick < Server()->TickSpeed())
		return;
	
	if(IsFrozen())
		return;
/* INFECTION MODIFICATION END *****************************************/
	
	if(m_ReloadTimer != 0)
		return;

/* INFECTION MODIFICATION START ***************************************/
	if(GetClass() == PLAYERCLASS_NONE)
		return;
/* INFECTION MODIFICATION END *****************************************/

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE || GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GUN)
		FullAuto = true;


	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GRENADE)))
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo && (GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_MERCENARY_GRENADE))
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
/* INFECTION MODIFICATION START ***************************************/
			if(GetClass() == PLAYERCLASS_ENGINEER)
			{
				if(m_pBarrier)
				{
					GameServer()->m_World.DestroyEntity(m_pBarrier);
					m_pBarrier = 0;
				}
					
				if(m_FirstShot)
				{
					m_FirstShot = false;
					m_FirstShotCoord = m_Pos;
				}
				else if(distance(m_FirstShotCoord, m_Pos) > 10.0)
				{
					//Check if the barrier is in toxic gases
					bool isAccepted = true;
					for(int i=0; i<15; i++)
					{
						if(GameServer()->Collision()->CheckZoneFlag(m_FirstShotCoord + (m_Pos - m_FirstShotCoord)*(static_cast<float>(i)/14.0f), CCollision::ZONEFLAG_INFECTION))
						{
							isAccepted = false;
						}
					}
					
					if(isAccepted)
					{
						m_FirstShot = true;
						
						CBarrier *pBarrier = new CBarrier(GameWorld(), m_FirstShotCoord, m_Pos, m_pPlayer->GetCID());
						m_pBarrier = pBarrier;
						
						GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
					}
				}
			}
			else if(GetClass() == PLAYERCLASS_SOLDIER)
			{
				if(m_pBomb)
				{
					m_pBomb->Explode();
				}
				else
				{
					CBomb *pBomb = new CBomb(GameWorld(), ProjStartPos, m_pPlayer->GetCID());
					m_pBomb = pBomb;
					
					GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
				}
			}
			else if(GetClass() == PLAYERCLASS_SNIPER)
			{
				if(m_Pos.y > -600.0)
				{
					if(m_PositionLockTick < -2*Server()->TickSpeed())
					{
						m_PositionLockTick = Server()->TickSpeed()*15;
						m_PositionLocked = true;
					}
					else if(m_PositionLockTick > Server()->TickSpeed())
					{
						m_PositionLockTick = Server()->TickSpeed();
					}
				}
			}
			else if(GetClass() == PLAYERCLASS_SCIENTIST)
			{
				bool FreeSpace = true;
				int NbMine = 0;
				
				int OlderMineTick = Server()->Tick()+1;
				CMine* pOlderMine = 0;
				CMine* pIntersectMine = 0;
				
				CMine* p = (CMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MINE);
				while(p)
				{
					float d = distance(p->m_Pos, ProjStartPos);
					
					if(p->GetOwner() == m_pPlayer->GetCID())
					{
						if(OlderMineTick > p->m_StartTick)
						{
							OlderMineTick = p->m_StartTick;
							pOlderMine = p;
						}
						NbMine++;
						
						if(d < 2.0f*g_Config.m_InfMineRadius)
						{
							if(pIntersectMine)
								FreeSpace = false;
							else
								pIntersectMine = p;
						}
					}
					else if(d < 2.0f*g_Config.m_InfMineRadius)
						FreeSpace = false;
					
					p = (CMine *)p->TypeNext();
				}
				
				if(FreeSpace)
				{
					if(pIntersectMine) //Move the mine
						GameServer()->m_World.DestroyEntity(pIntersectMine);
					else if(NbMine >= g_Config.m_InfMineLimit && pOlderMine)
						GameServer()->m_World.DestroyEntity(pOlderMine);
					
					new CMine(GameWorld(), ProjStartPos, m_pPlayer->GetCID());
					
					m_ReloadTimer = Server()->TickSpeed()/2;
				}
			}
			else if(GetClass() == PLAYERCLASS_NINJA)
			{
				if(m_DartLeft || m_InWater)
				{
					if(!m_InWater)
						m_DartLeft--;
					
					// reset Hit objects
					m_NumObjectsHit = 0;

					m_DartDir = Direction;
					m_DartLifeSpan = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
					m_DartOldVelAmount = length(m_Core.m_Vel);

					GameServer()->CreateSound(m_Pos, SOUND_NINJA_HIT);
				}
			}
			else if(GetClass() == PLAYERCLASS_BOOMER)
			{
				if(!IsFrozen())
				{
					Die(m_pPlayer->GetCID(), WEAPON_SELF);
				}
			}
			else
			{
/* INFECTION MODIFICATION END *****************************************/
				// reset objects Hit
				m_NumObjectsHit = 0;
				GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

				if(GetClass() == PLAYERCLASS_GHOST)
				{
					m_IsInvisible = false;
					m_InvisibleTick = Server()->Tick();
				}

				CCharacter *apEnts[MAX_CLIENTS];
				int Hits = 0;
				int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
															MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

				for (int i = 0; i < Num; ++i)
				{
					CCharacter *pTarget = apEnts[i];

					if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
						continue;

					// set his velocity to fast upward (for now)
					if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
						GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
					else
						GameServer()->CreateHammerHit(ProjStartPos);

					vec2 Dir;
					if (length(pTarget->m_Pos - m_Pos) > 0.0f)
						Dir = normalize(pTarget->m_Pos - m_Pos);
					else
						Dir = vec2(0.f, -1.f);
					
/* INFECTION MODIFICATION START ***************************************/
					if(IsInfected())
					{
						if(pTarget->IsInfected())
						{
							if(pTarget->IsFrozen())
							{
								pTarget->Unfreeze();
								GameServer()->ClearBroadcast(pTarget->GetPlayer()->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE);
							}
							else
							{
								pTarget->IncreaseHealth(2);
								pTarget->IncreaseArmor(2);
								pTarget->m_EmoteType = EMOTE_HAPPY;
								pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
								
								if(!pTarget->GetPlayer()->HookProtectionEnabled())
									pTarget->m_Core.m_Vel += vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
							}
						}
						else
						{
							pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_INFECTION);
						}						
					}
					else if(GetClass() == PLAYERCLASS_MEDIC && !pTarget->IsInfected())
					{
						pTarget->IncreaseArmor(4);
						if(pTarget->m_Armor == 10 && pTarget->m_NeedFullHeal)
						{
							Server()->RoundStatistics()->OnScoreEvent(GetPlayer()->GetCID(), SCOREEVENT_HUMAN_HEALING, GetClass());
							GameServer()->SendScoreSound(GetPlayer()->GetCID());
							pTarget->m_NeedFullHeal = false;
						}
						pTarget->m_EmoteType = EMOTE_HAPPY;
						pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
					}
					else
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
							m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
					}
/* INFECTION MODIFICATION END *****************************************/
					Hits++;
				}

				// if we Hit anything, we have to wait for the reload
				if(Hits)
				{
					m_ReloadTimer = Server()->TickSpeed()/3;
				}
					
/* INFECTION MODIFICATION START ***************************************/
			}
/* INFECTION MODIFICATION END *****************************************/

		} break;

		case WEAPON_GUN:
		{
			if(GetClass() == PLAYERCLASS_MERCENARY)
			{
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					Direction,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
					1, 0, 0, -1, WEAPON_GUN);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
				Msg.AddInt(1);
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);

				Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
				
				float MaxSpeed = GameServer()->Tuning()->m_GroundControlSpeed*1.7f;
				vec2 Recoil = Direction*(-MaxSpeed/5.0f);
				SaturateVelocity(Recoil, MaxSpeed);

				GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP);
			}
			else
			{
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					Direction,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
					1, 0, 0, -1, WEAPON_GUN);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
				Msg.AddInt(1);
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);

				Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

				GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
			}
		} break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 3;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			int Damage = 1;
			if(GetClass() == PLAYERCLASS_HERO)
				Damage = 0;

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.21f, -0.14f, -0.070f, 0, 0.070f, 0.14f, 0.21f};
				float a = GetAngle(Direction);
				a += Spreading[i+3] * 2.0f*(0.25f + 0.75f*static_cast<float>(10-m_aWeapons[WEAPON_SHOTGUN].m_Ammo)/10.0f);
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				
				float LifeTime = GameServer()->Tuning()->m_ShotgunLifetime + 0.1f*static_cast<float>(m_aWeapons[WEAPON_SHOTGUN].m_Ammo)/10.0f;
				
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*LifeTime),
					Damage, 0, 10.0f, -1, WEAPON_SHOTGUN);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
			}

			Server()->SendMsg(&Msg, 0,m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			if(GetClass() == PLAYERCLASS_MERCENARY)
			{				
				//Find bomb
				bool BombFound = false;
				for(CMercenaryBomb *bomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARYBOMB); bomb; bomb = (CMercenaryBomb *)bomb->TypeNext())
				{
					if(bomb->m_Owner != m_pPlayer->GetCID()) continue;
					bomb->Explode();
					BombFound = true;
				}
				
				if(!BombFound && m_aWeapons[m_ActiveWeapon].m_Ammo)
				{
					int ShotSpread = 2;
					
					CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
					Msg.AddInt(ShotSpread*2+1);
					
					for(int i = -ShotSpread; i <= ShotSpread; ++i)
					{
						float a = GetAngle(Direction) + ((float)rand()/(float)RAND_MAX)/5.0f;
						
						CMercenaryBomb *pProj = new CMercenaryBomb(GameWorld(), m_pPlayer->GetCID(), m_Pos, vec2(cosf(a), sinf(a)));

						// pack the Projectile and send it to the client Directly
						CNetObj_Projectile p;
						pProj->FillInfo(&p);

						for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
							Msg.AddInt(((int *)&p)[i]);
						Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
					}

					GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
					
					m_ReloadTimer = Server()->TickSpeed()/4;
				}
				else
				{
					m_aWeapons[m_ActiveWeapon].m_Ammo++;
				}
			}
			else if(GetClass() == PLAYERCLASS_SCIENTIST)
			{
				vec2 PortalShift = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
				vec2 PortalDir = normalize(PortalShift);
				if(length(PortalShift) > 500.0f)
					PortalShift = PortalDir * 500.0f;
				vec2 PortalPos;
				
				if(FindPortalPosition(m_Pos + PortalShift, PortalPos))
				{
					vec2 OldPos = m_Core.m_Pos;
					m_Core.m_Pos = PortalPos;
					m_Core.m_HookedPlayer = -1;
					m_Core.m_HookState = HOOK_RETRACTED;
					m_Core.m_HookPos = m_Core.m_Pos;
					
					GameServer()->CreateDeath(OldPos, GetPlayer()->GetCID());
					GameServer()->CreateDeath(PortalPos, GetPlayer()->GetCID());
					GameServer()->CreateSound(PortalPos, SOUND_CTF_RETURN);
				}
			}
			else
			{
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
					m_pPlayer->GetCID(),
					ProjStartPos,
					Direction,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
					1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

	/* INFECTION MODIFICATION START ***************************************/
				if(GetClass() == PLAYERCLASS_NINJA)
				{
					pProj->FlashGrenade();
				}
	/* INFECTION MODIFICATION END *****************************************/

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
				Msg.AddInt(1);
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
				Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
		} break;

		case WEAPON_RIFLE:
		{
			int Damage = GameServer()->Tuning()->m_LaserDamage;
			
			if(GetClass() == PLAYERCLASS_SNIPER)
			{
				if(m_PositionLocked)
					Damage = 20;
				else
					Damage = min(10, 9 + rand()%4);
			}
			
			if(GetClass() == PLAYERCLASS_SCIENTIST)
			{
				new CScientistLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach*0.6f, m_pPlayer->GetCID(), Damage);
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
			else
			{
				new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
		} break;
	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
	{
		m_ReloadTimer = Server()->GetFireDelay(GetInfWeaponID(m_ActiveWeapon)) * Server()->TickSpeed() / 1000;
	}
}

void CCharacter::SaturateVelocity(vec2 Force, float MaxSpeed)
{
	if(length(Force) < 0.00001)
		return;
	
	float Speed = length(m_Core.m_Vel);
	vec2 VelDir = normalize(m_Core.m_Vel);
	if(Speed < 0.00001)
	{
		VelDir = normalize(Force);
	}
	vec2 OrthoVelDir = vec2(-VelDir.y, VelDir.x);
	float VelDirFactor = dot(Force, VelDir);
	float OrthoVelDirFactor = dot(Force, OrthoVelDir);
	
	vec2 NewVel = m_Core.m_Vel;
	if(Speed < MaxSpeed || VelDirFactor < 0.0f)
	{
		NewVel += VelDir*VelDirFactor;
		float NewSpeed = length(NewVel);
		if(NewSpeed > MaxSpeed)
		{
			if(VelDirFactor > 0.f)
				NewVel = VelDir*MaxSpeed;
			else
				NewVel = -VelDir*MaxSpeed;
		}
	}
	
	NewVel += OrthoVelDir * OrthoVelDirFactor;
	
	m_Core.m_Vel = NewVel;
}

void CCharacter::HandleWeapons()
{
	if(IsFrozen())
		return;
		
	//ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
/* INFECTION MODIFICATION START ***************************************/
	for(int i=WEAPON_GUN; i<=WEAPON_RIFLE; i++)
	{
		int InfWID = GetInfWeaponID(i);
		int AmmoRegenTime = Server()->GetAmmoRegenTime(InfWID);
		int MaxAmmo = Server()->GetMaxAmmo(GetInfWeaponID(i));
		
		if(InfWID == INFWEAPON_MERCENARY_GUN)
		{
			if(m_InAirTick > Server()->TickSpeed()*4)
			{
				AmmoRegenTime = 0;
			}
		}
		
		if(AmmoRegenTime)
		{
			if(m_ReloadTimer <= 0)
			{
				if (m_aWeapons[i].m_AmmoRegenStart < 0)
					m_aWeapons[i].m_AmmoRegenStart = Server()->Tick();

				if ((Server()->Tick() - m_aWeapons[i].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
				{
					// Add some ammo
					m_aWeapons[i].m_Ammo = min(m_aWeapons[i].m_Ammo + 1, MaxAmmo);
					m_aWeapons[i].m_AmmoRegenStart = -1;
				}
			}
		}
	}
	
	if(IsInfected())
	{
		if(m_Core.m_HookedPlayer >= 0)
		{
			CCharacter *VictimChar = GameServer()->GetPlayerChar(m_Core.m_HookedPlayer);
			if(VictimChar)
			{
				if(GetClass() == PLAYERCLASS_SMOKER)
				{
					if(m_HookDmgTick + Server()->TickSpeed()*0.5 < Server()->Tick())
					{
						m_HookDmgTick = Server()->Tick();
						VictimChar->TakeDamage(vec2(0.0f,0.0f), 2, m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_NOINFECTION);
					}
				}
				else
				{
					if(m_HookDmgTick + Server()->TickSpeed() < Server()->Tick())
					{
						m_HookDmgTick = Server()->Tick();
						VictimChar->TakeDamage(vec2(0.0f,0.0f), 1, m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_NOINFECTION);
					}
				}
			}
		}
	}
/* INFECTION MODIFICATION END *****************************************/

	return;
}

/* INFECTION MODIFICATION START ***************************************/
void CCharacter::RemoveAllGun()
{
	m_aWeapons[WEAPON_GUN].m_Got = false;
	m_aWeapons[WEAPON_GUN].m_Ammo = 0;
	m_aWeapons[WEAPON_RIFLE].m_Got = false;
	m_aWeapons[WEAPON_RIFLE].m_Ammo = 0;
	m_aWeapons[WEAPON_GRENADE].m_Got = false;
	m_aWeapons[WEAPON_GRENADE].m_Ammo = 0;
	m_aWeapons[WEAPON_SHOTGUN].m_Got = false;
	m_aWeapons[WEAPON_SHOTGUN].m_Ammo = 0;
}
/* INFECTION MODIFICATION END *****************************************/

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	int MaxAmmo = Server()->GetMaxAmmo(GetInfWeaponID(Weapon));
	
	if(m_aWeapons[Weapon].m_Ammo < MaxAmmo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(MaxAmmo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
/* INFECTION MODIFICATION START ***************************************/
	if(GameServer()->Collision()->CheckPhysicsFlag(m_Core.m_Pos, CCollision::COLFLAG_WATER))
	{
		if(m_InWater == 0)
		{
			m_InWater = 1;
			m_Core.m_Vel /= 2.0f;
			m_WaterJumpLifeSpan = 0;
			
			if(length(m_Core.m_Vel) > 10.0f)
				GameServer()->CreateDeath(m_Core.m_Pos, m_pPlayer->GetCID());
		}
	}
	else
		m_InWater = 0;
	
	//Check is the character is in toxic gaz
	if(m_Alive && GameServer()->Collision()->CheckZoneFlag(m_Pos, CCollision::ZONEFLAG_INFECTION))
	{
		if(IsInfected())
		{
			if(Server()->Tick() >= m_HealTick + Server()->TickSpeed())
			{
				m_HealTick = Server()->Tick();
				if(m_Health < 10) m_Health++;
			}
		}
		else
		{
			m_pPlayer->StartInfection();
		}
	}
	
	if(m_PositionLockTick > 0)
	{
		--m_PositionLockTick;
		if(m_PositionLockTick <= 0)
			m_PositionLocked = false;
	}
	else
	{
		--m_PositionLockTick;
	}
	
	--m_FrozenTime;
	if(m_IsFrozen)
	{
		if(m_FrozenTime <= 0)
		{
			Unfreeze();
		}
		else
		{
			int FreezeSec = 1+(m_FrozenTime/Server()->TickSpeed());
			GameServer()->SendBroadcast_Language_i(m_pPlayer->GetCID(), "You are frozen: %d sec", FreezeSec, BROADCAST_PRIORITY_EFFECTSTATE, BROADCAST_DURATION_REALTIME);
		}
	}
	
	if(m_Poison > 0)
	{
		if(m_PoisonTick == 0)
		{
			m_Poison--;
			TakeDamage(vec2(0.0f, 0.0f), 1, m_PoisonFrom, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
			if(m_Poison > 0)
			{
				m_PoisonTick = Server()->TickSpeed()/2;
			}
		}
		else
		{
			m_PoisonTick--;
		}
	}
	
	//NeedHeal
	if(m_Armor >= 10)
		m_NeedFullHeal = false;
	
	if(!m_InWater && !IsGrounded() && (m_Core.m_HookState != HOOK_GRABBED || m_Core.m_HookedPlayer != -1))
	{
		m_InAirTick++;
	}
	else
	{
		m_InAirTick = 0;
	}
	
	//Ghost
	if(GetClass() == PLAYERCLASS_GHOST)
	{
		if(Server()->Tick() < m_InvisibleTick + 3*Server()->TickSpeed() || IsFrozen())
		{
			m_IsInvisible = false;
		}
		else
		{
			//Search nearest human
			int cellGhostX = static_cast<int>(round(m_Pos.x))/32;
			int cellGhostY = static_cast<int>(round(m_Pos.y))/32;
			
			vec2 SeedPos = vec2(16.0f, 16.0f) + vec2(
				static_cast<float>(static_cast<int>(round(m_Pos.x))/32)*32.0,
				static_cast<float>(static_cast<int>(round(m_Pos.y))/32)*32.0);
			
			for(int y=0; y<GHOST_SEARCHMAP_SIZE; y++)
			{
				for(int x=0; x<GHOST_SEARCHMAP_SIZE; x++)
				{
					vec2 Tile = SeedPos + vec2(32.0f*(x-GHOST_RADIUS), 32.0f*(y-GHOST_RADIUS));
					if(GameServer()->Collision()->CheckPoint(Tile))
					{
						m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] = 0x8;
					}
					else
					{
						m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] = 0x0;
					}
				}
			}
			for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
			{
				if(p->IsInfected()) continue;
				
				int cellHumanX = static_cast<int>(round(p->m_Pos.x))/32;
				int cellHumanY = static_cast<int>(round(p->m_Pos.y))/32;
				
				int cellX = cellHumanX - cellGhostX + GHOST_RADIUS;
				int cellY = cellHumanY - cellGhostY + GHOST_RADIUS;
				
				if(cellX >= 0 && cellX < GHOST_SEARCHMAP_SIZE && cellY >= 0 && cellY < GHOST_SEARCHMAP_SIZE)
				{
					m_GhostSearchMap[cellY * GHOST_SEARCHMAP_SIZE + cellX] |= 0x2;
				}
			}
			m_GhostSearchMap[GHOST_RADIUS * GHOST_SEARCHMAP_SIZE + GHOST_RADIUS] |= 0x1;
			bool HumanFound = false;
			for(int i=0; i<GHOST_RADIUS; i++)
			{
				for(int y=0; y<GHOST_SEARCHMAP_SIZE; y++)
				{
					for(int x=0; x<GHOST_SEARCHMAP_SIZE; x++)
					{
						if(!((m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x1) || (m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x8)))
						{
							if(
								(
									(x > 0 && (m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x-1] & 0x1)) ||
									(x < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x+1] & 0x1)) ||
									(y > 0 && (m_GhostSearchMap[(y-1)*GHOST_SEARCHMAP_SIZE+x] & 0x1)) ||
									(y < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[(y+1)*GHOST_SEARCHMAP_SIZE+x] & 0x1))
								) ||
								(
									(rand()%4 == 0) && (
										(x > 0 && y > 0 && (m_GhostSearchMap[(y-1)*GHOST_SEARCHMAP_SIZE+x-1] & 0x1)) ||
										(x > 0 && y < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[(y+1)*GHOST_SEARCHMAP_SIZE+x-1] & 0x1)) ||
										(x < GHOST_SEARCHMAP_SIZE-1 && y > 0 && (m_GhostSearchMap[(y-1)*GHOST_SEARCHMAP_SIZE+x+1] & 0x1)) ||
										(x < GHOST_SEARCHMAP_SIZE-1 && y < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[(y+1)*GHOST_SEARCHMAP_SIZE+x+1] & 0x1))
									)
								)
							)
							{
								m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] |= 0x4;
								//~ if((Server()->Tick()%5 == 0) && i == (Server()->Tick()/5)%GHOST_RADIUS)
								//~ {
									//~ vec2 HintPos = vec2(
										//~ 32.0f*(cellGhostX + (x - GHOST_RADIUS))+16.0f,
										//~ 32.0f*(cellGhostY + (y - GHOST_RADIUS))+16.0f);
									//~ GameServer()->CreateHammerHit(HintPos);
								//~ }
								if(m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x2)
								{
									HumanFound = true;
								}
							}
						}
					}
				}
				for(int y=0; y<GHOST_SEARCHMAP_SIZE; y++)
				{
					for(int x=0; x<GHOST_SEARCHMAP_SIZE; x++)
					{
						if(m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x4)
						{
							m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] |= 0x1;
						}
					}
				}
			}
			
			if(HumanFound)
			{				
				if(m_IsInvisible)
				{
					GameServer()->CreatePlayerSpawn(m_Pos);
					m_IsInvisible = false;
				}
				
				m_InvisibleTick = Server()->Tick();
			}
			else
			{
				m_IsInvisible = true;
			}
		}
	}
	
	if(GetClass() == PLAYERCLASS_SPIDER)
	{
		if(
			m_HookMode == 1 &&
			m_Core.m_HookState == HOOK_GRABBED &&
			distance(m_Core.m_Pos, m_Core.m_HookPos) > 48.0f &&
			m_Core.m_HookedPlayer < 0
		)
		{
			// Find other players
			for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
			{
				if(p->IsInfected()) continue;

				vec2 IntersectPos = closest_point_on_line(m_Core.m_Pos, m_Core.m_HookPos, p->m_Pos);
				float Len = distance(p->m_Pos, IntersectPos);
				if(Len < p->m_ProximityRadius)
				{				
					m_Core.m_HookState = HOOK_GRABBED;
					m_Core.m_HookPos = p->m_Pos;
					m_Core.m_HookedPlayer = p->m_pPlayer->GetCID();
					m_Core.m_HookTick = 0;
					m_HookMode = 0;
					
					break;
				}
			}
		}
	}
	
	if(GetClass() == PLAYERCLASS_NINJA && IsGrounded() && m_DartLifeSpan <= 0)
	{
		m_DartLeft = 2;
	}
	
	if(m_IsFrozen)
	{
		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
	}
	else if(GetClass() == PLAYERCLASS_SNIPER && m_PositionLocked)
	{
		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
	}
	
	UpdateTuningParam();

	m_Core.m_Input = m_Input;
	
	CCharacterCore::CParams CoreTickParams(&m_pPlayer->m_NextTuningParams);
	//~ CCharacterCore::CParams CoreTickParams(&GameWorld()->m_Core.m_Tuning);
	
	if(GetClass() == PLAYERCLASS_SPIDER)
	{
		CoreTickParams.m_HookGrabTime = 2*SERVER_TICK_SPEED;
	}
	CoreTickParams.m_HookMode = m_HookMode;
	
	vec2 PrevPos = m_Core.m_Pos;
	m_Core.Tick(true, &CoreTickParams);
	
	if(GetClass() == PLAYERCLASS_SNIPER && m_PositionLocked)
	{
		m_Core.m_Vel = vec2(0.0f, 0.0f);
		m_Core.m_Pos = PrevPos;
	}
	
	//Hook protection
	if(m_Core.m_HookedPlayer >= 0)
	{
		if(GameServer()->m_apPlayers[m_Core.m_HookedPlayer])
		{
			if(IsInfected() == GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->IsInfected() && GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->HookProtectionEnabled())
			{
				m_Core.m_HookedPlayer = -1;
				m_Core.m_HookState = HOOK_RETRACTED;
				m_Core.m_HookPos = m_Pos;
			}
		}
	}
/* INFECTION MODIFICATION END *****************************************/
	

	// handle death-tiles and leaving gamelayer
	if(
		GameServer()->Collision()->CheckZoneFlag(vec2(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f), CCollision::ZONEFLAG_DEATH) ||
		GameServer()->Collision()->CheckZoneFlag(vec2(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f), CCollision::ZONEFLAG_DEATH) ||
		GameServer()->Collision()->CheckZoneFlag(vec2(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f), CCollision::ZONEFLAG_DEATH) ||
		GameServer()->Collision()->CheckZoneFlag(vec2(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f), CCollision::ZONEFLAG_DEATH) ||
		GameLayerClipped(m_Pos)
	)
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
/* INFECTION MODIFICATION START ***************************************/
	HandleWaterJump();
	HandleWeapons();

	if(GetClass() == PLAYERCLASS_HUNTER || GetClass() == PLAYERCLASS_SNIPER)
	{
		if(IsGrounded()) m_AirJumpCounter = 0;
		if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP && m_AirJumpCounter < 1)
		{
			m_Core.m_Jumped &= ~2;
			m_AirJumpCounter++;
		}
	}
	
	if(m_pPlayer->InClassChooserMenu())
	{
		if(GetClass() != PLAYERCLASS_NONE)
		{
			m_AntiFireTick = Server()->Tick();
			m_pPlayer->m_InClassChooserMenu = 0;
		}
		else
		{
			vec2 CursorPos = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
			
			bool Broadcast = false;

			if(length(CursorPos) > 100.0f)
			{
				float Angle = 2.0f*pi+atan2(CursorPos.x, -CursorPos.y);
				float AngleStep = 2.0f*pi/static_cast<float>(CMapConverter::NUM_MENUCLASS);
				m_pPlayer->m_MenuClassChooserItem = ((int)((Angle+AngleStep/2.0f)/AngleStep))%CMapConverter::NUM_MENUCLASS;
				
				switch(m_pPlayer->m_MenuClassChooserItem)
				{
					case CMapConverter::MENUCLASS_RANDOM:
						GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Random choice", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
						Broadcast = true;
						break;
					case CMapConverter::MENUCLASS_ENGINEER:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_ENGINEER))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Engineer", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_SOLDIER:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SOLDIER))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Soldier", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_SCIENTIST:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SCIENTIST))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Scientist", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_MEDIC:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_MEDIC))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Medic", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_HERO:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_HERO))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Hero", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_NINJA:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_NINJA))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Ninja", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_MERCENARY:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_MERCENARY))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Mercenary", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_SNIPER:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SNIPER))
						{
							GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Sniper", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
							Broadcast = true;
						}
						break;
				}
			}
			
			if(!Broadcast)
			{
				m_pPlayer->m_MenuClassChooserItem = -1;
				GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), "Choose your class", BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME);
			}
			
			if(m_Input.m_Fire&1 && m_pPlayer->m_MenuClassChooserItem >= 0)
			{
				bool Bonus = false;
				
				int NewClass = -1;
				switch(m_pPlayer->m_MenuClassChooserItem)
				{
					case CMapConverter::MENUCLASS_MEDIC:
						NewClass = PLAYERCLASS_MEDIC;
						break;
					case CMapConverter::MENUCLASS_HERO:
						NewClass = PLAYERCLASS_HERO;
						break;
					case CMapConverter::MENUCLASS_NINJA:
						NewClass = PLAYERCLASS_NINJA;
						break;
					case CMapConverter::MENUCLASS_MERCENARY:
						NewClass = PLAYERCLASS_MERCENARY;
						break;
					case CMapConverter::MENUCLASS_SNIPER:
						NewClass = PLAYERCLASS_SNIPER;
						break;
					case CMapConverter::MENUCLASS_RANDOM:
						NewClass = GameServer()->m_pController->ChooseHumanClass(m_pPlayer);
						Bonus = true;
						break;
					case CMapConverter::MENUCLASS_ENGINEER:
						NewClass = PLAYERCLASS_ENGINEER;
						break;
					case CMapConverter::MENUCLASS_SOLDIER:
						NewClass = PLAYERCLASS_SOLDIER;
						break;
					case CMapConverter::MENUCLASS_SCIENTIST:
						NewClass = PLAYERCLASS_SCIENTIST;
						break;
				}
				
				if(NewClass >= 0 && GameServer()->m_pController->IsChoosableClass(NewClass))
				{
					m_AntiFireTick = Server()->Tick();
					m_pPlayer->m_InClassChooserMenu = 0;
					m_pPlayer->SetClass(NewClass);
					
					if(Bonus)
						IncreaseArmor(10);
				}
			}
		}
	}
		
	if(GetClass() == PLAYERCLASS_SOLDIER)
	{
		if(m_pBomb)
			GameServer()->SendBroadcast_Language_i(GetPlayer()->GetCID(), "Bombs left: %d", m_pBomb->GetNbBombs(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
	}
	else if(GetClass() == PLAYERCLASS_SCIENTIST)
	{
		int NbMines = 0;
		CMine* p = (CMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MINE);
		while(p)
		{
			if(p->GetOwner() == m_pPlayer->GetCID())
				NbMines++;
			p = (CMine *)p->TypeNext();
		}
		if(NbMines > 0)
			GameServer()->SendBroadcast_Language_i(GetPlayer()->GetCID(), "Active mines: %d", NbMines, BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
	}
	else if(GetClass() == PLAYERCLASS_HERO)
	{
		//Search for flag
		int CoolDown = 999999999;
		CHeroFlag* p = (CHeroFlag*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_HEROFLAG);
		while(p)
		{
			if(p->GetCoolDown() <= 0)
			{
				CoolDown = 0;
				break;
			}
			else
			{
				if(p->GetCoolDown() < CoolDown)
					CoolDown = p->GetCoolDown();
			}
			p = (CHeroFlag *)p->TypeNext();
		}
		if(CoolDown > 0)
			GameServer()->SendBroadcast_Language_i(GetPlayer()->GetCID(), "Next flag in: %d sec", 1+CoolDown/Server()->TickSpeed(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
	}
	else if(GetClass() == PLAYERCLASS_ENGINEER)
	{
		if(m_pBarrier)
			GameServer()->SendBroadcast_Language_i(GetPlayer()->GetCID(), "Laser wall: %d sec", 1+m_pBarrier->GetTick()/Server()->TickSpeed(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
	}
	else if(GetClass() == PLAYERCLASS_SNIPER)
	{
		if(m_PositionLocked)
			GameServer()->SendBroadcast_Language_i(GetPlayer()->GetCID(), "Position lock: %d sec", 1+m_PositionLockTick/Server()->TickSpeed(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
	}
	else if(GetClass() == PLAYERCLASS_SPIDER)
	{
		if(m_HookMode > 0)
			GameServer()->SendBroadcast_Language(GetPlayer()->GetCID(), "Web mode enabled", BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
	}
/* INFECTION MODIFICATION END *****************************************/

	// Previnput
	m_PrevInput = m_Input;
	
	return;
}

void CCharacter::TickDefered()
{
	
	// advance the dummy
	{
		CCharacterCore::CParams CoreTickParams(&GameWorld()->m_Core.m_Tuning);
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false, &CoreTickParams);
		m_ReckoningCore.Move(&CoreTickParams);
		m_ReckoningCore.Quantize();
	}

	CCharacterCore::CParams CoreTickParams(&m_pPlayer->m_NextTuningParams);
	
	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move(&CoreTickParams);
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());


	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(GetClass() != PLAYERCLASS_GHOST || !m_IsInvisible)
	{
		if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);
		if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
		if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);
	}


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
		
/* INFECTION MODIFICATION START ***************************************/
	++m_HookDmgTick;
/* INFECTION MODIFICATION END *****************************************/
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetClass() == PLAYERCLASS_UNDEAD && Killer != m_pPlayer->GetCID())
	{
		Freeze(10.0, Killer, FREEZEREASON_UNDEAD);
		return;
	}
	
	DestroyChildEntities();
/* INFECTION MODIFICATION END *****************************************/
	
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
	
	if(Killer >=0 && Killer < MAX_CLIENTS)
	{
		CPlayer* pKillerPlayer = GameServer()->m_apPlayers[Killer];
		if(pKillerPlayer && pKillerPlayer->GetClass() == PLAYERCLASS_SNIPER)
		{
			CCharacter* pKiller = GameServer()->m_apPlayers[Killer]->GetCharacter();
			if(pKiller)
				GiveWeapon(WEAPON_RIFLE, 1);
		}
	}
	
/* INFECTION MODIFICATION START ***************************************/
	if(GetClass() == PLAYERCLASS_BOOMER && !IsFrozen() && Weapon != WEAPON_GAME)
	{
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateExplosion(m_Pos, m_pPlayer->GetCID(), WEAPON_HAMMER, false, TAKEDAMAGEMODE_INFECTION);
		GameServer()->CreateExplosion(m_Pos + vec2(32, 0), m_pPlayer->GetCID(), WEAPON_HAMMER, false, TAKEDAMAGEMODE_INFECTION);
		GameServer()->CreateExplosion(m_Pos + vec2(-32, 0), m_pPlayer->GetCID(), WEAPON_HAMMER, false, TAKEDAMAGEMODE_INFECTION);
		GameServer()->CreateExplosion(m_Pos + vec2(0, 32), m_pPlayer->GetCID(), WEAPON_HAMMER, false, TAKEDAMAGEMODE_INFECTION);
		GameServer()->CreateExplosion(m_Pos + vec2(0, -32), m_pPlayer->GetCID(), WEAPON_HAMMER, false, TAKEDAMAGEMODE_INFECTION);
	}
	
	if(GetClass() == PLAYERCLASS_WITCH)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Language(-1, "The witch is dead", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else if(GetClass() == PLAYERCLASS_UNDEAD)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Language(-1, "The undead is finally dead", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else
	{
		m_pPlayer->StartInfection(false);
	}	
/* INFECTION MODIFICATION END *****************************************/
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon, int Mode)
{
/* INFECTION MODIFICATION START ***************************************/

	CPlayer* pKillerPlayer = GameServer()->m_apPlayers[From];
	
	if(pKillerPlayer->IsInfected() && Mode == TAKEDAMAGEMODE_INFECTION && GetClass() == PLAYERCLASS_HERO)
		Dmg = 12;
	
	if(GetClass() != PLAYERCLASS_HUNTER || Weapon != WEAPON_SHOTGUN)
	{
		m_Core.m_Vel += Force;
	}

	if(From != m_pPlayer->GetCID() && pKillerPlayer)
	{
		if(IsInfected())
		{
			if(pKillerPlayer->IsInfected())
			{
				//Heal and unfreeze
				if(pKillerPlayer->GetClass() == PLAYERCLASS_BOOMER && Weapon == WEAPON_HAMMER)
				{
					IncreaseHealth(4+rand()%6);
					IncreaseArmor(4+rand()%6);
					if(IsFrozen())
						Unfreeze();
						
					m_EmoteType = EMOTE_HAPPY;
					m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
				}
				return false;
			}
		}
		else
		{
			//If the player is a new infected, don't infected other -> nobody knows that he is infected.
			if(!pKillerPlayer->IsInfected() || (Server()->Tick() - pKillerPlayer->m_InfectionTick)*Server()->TickSpeed() < 0.5) return false;
		}
	}
	
/* INFECTION MODIFICATION END *****************************************/

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
	{
		if(GetClass() == PLAYERCLASS_HERO)
			return false;
		else
			Dmg = max(1, Dmg/2);
	}

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

/* INFECTION MODIFICATION START ***************************************/
	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg <= m_Armor)
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
			else
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
		}

		m_Health -= Dmg;
	
		if(From != m_pPlayer->GetCID())
			m_NeedFullHeal = true;
	}
/* INFECTION MODIFICATION END *****************************************/

	m_DamageTakenTick = Server()->Tick();
	m_InvisibleTick = Server()->Tick();

	// do damage Hit sound
	if(Dmg && From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		GameServer()->SendHitSound(From);
	}
	
/* INFECTION MODIFICATION START ***************************************/
	if(Mode == TAKEDAMAGEMODE_INFECTION && GameServer()->m_apPlayers[From]->IsInfected() && !IsInfected() && GetClass() != PLAYERCLASS_HERO)
	{
		m_pPlayer->StartInfection();
		
		GameServer()->SendChatTarget_Language_s(From, "You have infected %s, +3 points", Server()->ClientName(m_pPlayer->GetCID()));
		Server()->RoundStatistics()->OnScoreEvent(From, SCOREEVENT_INFECTION, GameServer()->m_apPlayers[From]->GetClass());
		GameServer()->SendScoreSound(From);
	
		//Search for hook
		for(CCharacter *pHook = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHook; pHook = (CCharacter *)pHook->TypeNext())
		{
			if(
				pHook->GetPlayer() &&
				pHook->m_Core.m_HookedPlayer == m_pPlayer->GetCID() &&
				pHook->GetPlayer()->GetCID() != From
			)
			{
				Server()->RoundStatistics()->OnScoreEvent(pHook->GetPlayer()->GetCID(), SCOREEVENT_HELP_HOOK_INFECTION, pHook->GetClass());
				GameServer()->SendScoreSound(pHook->GetPlayer()->GetCID());
			}
		}
		
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = From;
		Msg.m_Victim = m_pPlayer->GetCID();
		Msg.m_Weapon = WEAPON_HAMMER;
		Msg.m_ModeSpecial = 0;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
/* INFECTION MODIFICATION END *****************************************/

	// check for death
	if(m_Health <= 0)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	
	CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
	
/* INFECTION MODIFICATION START ***************************************/
	if(GetClass() == PLAYERCLASS_GHOST)
	{
		if(!pClient->IsInfected() && m_IsInvisible) return;
	}
	else if(GetClass() == PLAYERCLASS_WITCH)
	{
		CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, m_FlagID, sizeof(CNetObj_Flag));
		if(!pFlag)
			return;

		pFlag->m_X = (int)m_Pos.x;
		pFlag->m_Y = (int)m_Pos.y;
		pFlag->m_Team = TEAM_RED;
	}
	
	if(m_Armor < 10 && SnappingClient != m_pPlayer->GetCID() && !IsInfected())
	{
		
		if(pClient && pClient->GetClass() == PLAYERCLASS_MEDIC)
		{
			CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
			if(!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			if(m_Health < 10 && m_Armor == 0)
				pP->m_Type = POWERUP_HEALTH;
			else
				pP->m_Type = POWERUP_ARMOR;
			pP->m_Subtype = 0;
		}
	}
	
	if(GetClass() == PLAYERCLASS_ENGINEER && !m_pBarrier && !m_FirstShot)
	{
		if(pClient && !pClient->IsInfected())
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)m_FirstShotCoord.x;
			pObj->m_Y = (int)m_FirstShotCoord.y;
			pObj->m_FromX = (int)m_FirstShotCoord.x;
			pObj->m_FromY = (int)m_FirstShotCoord.y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
	
	if(SnappingClient == m_pPlayer->GetCID())
	{
		if(GetClass() == PLAYERCLASS_SCIENTIST && m_ActiveWeapon == WEAPON_GRENADE)
		{
			vec2 PortalShift = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
			vec2 PortalDir = normalize(PortalShift);
			if(length(PortalShift) > 500.0f)
				PortalShift = PortalDir * 500.0f;
			vec2 PortalPos;
			
			if(FindPortalPosition(m_Pos + PortalShift, PortalPos))
			{
				CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_CursorID, sizeof(CNetObj_Projectile)));
				if(!pObj)
					return;

				pObj->m_X = (int)PortalPos.x;
				pObj->m_Y = (int)PortalPos.y;
				pObj->m_VelX = 0;
				pObj->m_VelY = 0;
				pObj->m_StartTick = Server()->Tick();
				pObj->m_Type = WEAPON_HAMMER;
			}
		}
		else if(GetClass() == PLAYERCLASS_WITCH)
		{
			vec2 SpawnPos;
			if(FindWitchSpawnPosition(SpawnPos))
			{
				CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_CursorID, sizeof(CNetObj_Projectile)));
				if(!pObj)
					return;

				pObj->m_X = (int)SpawnPos.x;
				pObj->m_Y = (int)SpawnPos.y;
				pObj->m_VelX = 0;
				pObj->m_VelY = 0;
				pObj->m_StartTick = Server()->Tick();
				pObj->m_Type = WEAPON_HAMMER;
			}
		}
	}
/* INFECTION MODIFICATION END ***************************************/

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;
	int EmoteNormal = EMOTE_NORMAL;
	if(IsInfected()) EmoteNormal = EMOTE_ANGRY;
	if(m_IsInvisible) EmoteNormal = EMOTE_BLINK;
	if(IsFrozen()) EmoteNormal = EMOTE_PAIN;
	
	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EmoteNormal;
		m_EmoteStop = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_NINJA_HAMMER)
	{
		pCharacter->m_Weapon = WEAPON_NINJA;
	}
	else
	{
		pCharacter->m_Weapon = m_ActiveWeapon;
	}
	
	if(GetClass() == PLAYERCLASS_SPIDER)
	{
		pCharacter->m_HookTick -= SERVER_TICK_SPEED-SERVER_TICK_SPEED/5;
		if(pCharacter->m_HookTick < 0)
			pCharacter->m_HookTick = 0;
	}
/* INFECTION MODIFICATION END *****************************************/
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}
	
/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GUN)
	{
		pCharacter->m_AmmoCount /= (Server()->GetMaxAmmo(INFWEAPON_MERCENARY_GUN)/10);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(pCharacter->m_Emote == EmoteNormal)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

/* INFECTION MODIFICATION START ***************************************/
void CCharacter::OpenClassChooser()
{
	if(!Server()->IsClassChooserEnabled() || Server()->GetClientAlwaysRandom(m_pPlayer->GetCID()))
	{
		m_pPlayer->SetClass(GameServer()->m_pController->ChooseHumanClass(m_pPlayer));
	}
	else
	{
		m_pPlayer->m_InClassChooserMenu = 1;
	}
}

int CCharacter::GetClass()
{
	if(!m_pPlayer)
		return PLAYERCLASS_NONE;
	else
		return m_pPlayer->GetClass();
}

void CCharacter::ClassSpawnAttributes()
{
	switch(GetClass())
	{
		case PLAYERCLASS_ENGINEER:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_RIFLE, 10);
			m_ActiveWeapon = WEAPON_RIFLE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_ENGINEER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_ENGINEER))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "engineer");
				m_pPlayer->m_knownClass[PLAYERCLASS_ENGINEER] = true;
			}
			break;
		case PLAYERCLASS_SOLDIER:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_GRENADE, 10);
			m_ActiveWeapon = WEAPON_GRENADE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SOLDIER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SOLDIER))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "soldier");
				m_pPlayer->m_knownClass[PLAYERCLASS_SOLDIER] = true;
			}
			break;
		case PLAYERCLASS_MERCENARY:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = false;
			GiveWeapon(WEAPON_GRENADE, Server()->GetMaxAmmo(INFWEAPON_MERCENARY_GRENADE));
			GiveWeapon(WEAPON_GUN, Server()->GetMaxAmmo(INFWEAPON_MERCENARY_GUN));
			m_ActiveWeapon = WEAPON_GUN;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_MERCENARY);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_MERCENARY))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "mercenary");
				m_pPlayer->m_knownClass[PLAYERCLASS_MERCENARY] = true;
			}
			break;
		case PLAYERCLASS_SNIPER:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_RIFLE, 10);
			m_ActiveWeapon = WEAPON_RIFLE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SNIPER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SNIPER))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "sniper");
				m_pPlayer->m_knownClass[PLAYERCLASS_SNIPER] = true;
			}
			break;
		case PLAYERCLASS_SCIENTIST:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_RIFLE, 10);
			GiveWeapon(WEAPON_GRENADE, Server()->GetMaxAmmo(INFWEAPON_SCIENTIST_GRENADE));
			m_ActiveWeapon = WEAPON_GRENADE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SCIENTIST);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SCIENTIST))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "scientist");
				m_pPlayer->m_knownClass[PLAYERCLASS_SCIENTIST] = true;
			}
			break;
		case PLAYERCLASS_MEDIC:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_SHOTGUN, 10);
			m_ActiveWeapon = WEAPON_SHOTGUN;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_MEDIC);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_MEDIC))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "medic");
				m_pPlayer->m_knownClass[PLAYERCLASS_MEDIC] = true;
			}
			break;
		case PLAYERCLASS_HERO:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = false;
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_SHOTGUN, 10);
			GiveWeapon(WEAPON_RIFLE, 10);
			GiveWeapon(WEAPON_GRENADE, 10);
			m_ActiveWeapon = WEAPON_GRENADE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_HERO);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_HERO))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "hero");
				m_pPlayer->m_knownClass[PLAYERCLASS_HERO] = true;
			}
			break;
		case PLAYERCLASS_NINJA:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_GRENADE, 5);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_NINJA);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_NINJA))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "ninja");
				m_pPlayer->m_knownClass[PLAYERCLASS_NINJA] = true;
			}
			break;
		case PLAYERCLASS_NONE:
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			break;
		case PLAYERCLASS_SMOKER:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SMOKER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SMOKER))
			{   
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "smoker");
				m_pPlayer->m_knownClass[PLAYERCLASS_SMOKER] = true;
			}
			break;
		case PLAYERCLASS_BOOMER:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_BOOMER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_BOOMER))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "boomer");
				m_pPlayer->m_knownClass[PLAYERCLASS_BOOMER] = true;
			}
			break;
		case PLAYERCLASS_HUNTER:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_HUNTER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_HUNTER))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "hunter");
				m_pPlayer->m_knownClass[PLAYERCLASS_HUNTER] = true;
			}
			break;
		case PLAYERCLASS_GHOST:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_GHOST);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_GHOST))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "ghost");
				m_pPlayer->m_knownClass[PLAYERCLASS_GHOST] = true;
			}
			break;
		case PLAYERCLASS_SPIDER:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SPIDER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SPIDER))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "spider");
				m_pPlayer->m_knownClass[PLAYERCLASS_SPIDER] = true;
			}
			break;
		case PLAYERCLASS_UNDEAD:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_UNDEAD);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_UNDEAD))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "undead");
				m_pPlayer->m_knownClass[PLAYERCLASS_HUNTER] = true;
			}
			break;
		case PLAYERCLASS_WITCH:
			m_Health = 10;
			m_Armor = 10;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_WITCH);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_WITCH))
			{
				GameServer()->SendChatTarget_Language_s(m_pPlayer->GetCID(), "Type \"/help %s\" for more information about your class", "witch");
				m_pPlayer->m_knownClass[PLAYERCLASS_WITCH] = true;
			}
			break;
	}
}

void CCharacter::DestroyChildEntities()
{		
	if(m_pBarrier)
	{
		GameServer()->m_World.DestroyEntity(m_pBarrier);
		m_pBarrier = 0;
	}
	if(m_pBomb)
	{
		GameServer()->m_World.DestroyEntity(m_pBomb);
		m_pBomb = 0;
	}
	for(CMercenaryBomb *bomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARYBOMB); bomb; bomb = (CMercenaryBomb *)bomb->TypeNext())
	{
		if(bomb->m_Owner != m_pPlayer->GetCID()) continue;
		bomb->Explode();
	}
	{
		CMine* p = (CMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MINE);
		while(p)
		{
			if(p->GetOwner() == m_pPlayer->GetCID())
				GameServer()->m_World.DestroyEntity(p);
			
			p = (CMine *)p->TypeNext();
		}
	}		
	m_FirstShot = true;
	m_HookMode = 0;
	m_PositionLockTick = 0;
	m_PositionLocked = 0;
}

void CCharacter::SetClass(int ClassChoosed)
{
	ClassSpawnAttributes();
	DestroyChildEntities();
	
	m_QueuedWeapon = -1;
	m_NeedFullHeal = false;
	
	GameServer()->CreatePlayerSpawn(m_Pos);
}

bool CCharacter::IsInfected() const
{
	return m_pPlayer->IsInfected();
}

void CCharacter::Freeze(float Time, int Player, int Reason)
{
	if(m_IsFrozen)
		return;
	
	m_IsFrozen = true;
	m_FrozenTime = Server()->TickSpeed()*Time;
	m_FreezeReason = Reason;
	
	m_LastFreezer = Player;
}

void CCharacter::Unfreeze()
{
	m_IsFrozen = false;
	m_FrozenTime = -1;
	
	if(m_FreezeReason == FREEZEREASON_UNDEAD)
	{
		m_Health = 10.0;
	}
	
	GameServer()->CreatePlayerSpawn(m_Pos);
}

void CCharacter::Poison(int Count, int From)
{
	if(m_Poison <= 0)
	{
		m_PoisonTick = 0;
		m_Poison = Count;
		m_PoisonFrom = From;
	}
}

bool CCharacter::IsFrozen() const
{
	return m_IsFrozen;
}

int CCharacter::GetInfWeaponID(int WID)
{
	if(WID == WEAPON_HAMMER)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_NINJA:
				return INFWEAPON_NINJA_HAMMER;
			default:
				return INFWEAPON_HAMMER;
		}
	}
	else if(WID == WEAPON_GUN)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_MERCENARY:
				return INFWEAPON_MERCENARY_GUN;
			default:
				return INFWEAPON_GUN;
		}
		return INFWEAPON_GUN;
	}
	else if(WID == WEAPON_SHOTGUN)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_SHOTGUN;
			case PLAYERCLASS_HERO:
				return INFWEAPON_HERO_SHOTGUN;
			default:
				return INFWEAPON_SHOTGUN;
		}
	}
	else if(WID == WEAPON_GRENADE)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_MERCENARY:
				return INFWEAPON_MERCENARY_GRENADE;
			case PLAYERCLASS_SOLDIER:
				return INFWEAPON_SOLDIER_GRENADE;
			case PLAYERCLASS_NINJA:
				return INFWEAPON_NINJA_GRENADE;
			case PLAYERCLASS_SCIENTIST:
				return INFWEAPON_SCIENTIST_GRENADE;
			case PLAYERCLASS_HERO:
				return INFWEAPON_HERO_GRENADE;
			default:
				return INFWEAPON_GRENADE;
		}
	}
	else if(WID == WEAPON_RIFLE)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_ENGINEER:
				return INFWEAPON_ENGINEER_RIFLE;
			case PLAYERCLASS_SCIENTIST:
				return INFWEAPON_SCIENTIST_RIFLE;
			case PLAYERCLASS_SNIPER:
				return INFWEAPON_SNIPER_RIFLE;
			case PLAYERCLASS_HERO:
				return INFWEAPON_HERO_RIFLE;
			default:
				return INFWEAPON_RIFLE;
		}
	}
	else if(WID == WEAPON_NINJA)
	{
		return INFWEAPON_NINJA;
	}
	else return INFWEAPON_NONE;
}
/* INFECTION MODIFICATION END *****************************************/
