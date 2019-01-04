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
#include "projectile.h"
#include "laser.h"

#include "flyingpoint.h"

#include "engineer-wall.h"
#include "soldier-bomb.h"
#include "scientist-laser.h"
#include "scientist-mine.h"
#include "biologist-mine.h"
#include "bouncing-bullet.h"
#include "merc-grenade.h"
#include "merc-bomb.h"
#include "medic-grenade.h"
#include "hero-flag.h"
#include "slug-slime.h"

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
CCharacter::CCharacter(CGameWorld *pWorld, IConsole *pConsole)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER),
m_pConsole(pConsole)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	m_AirJumpCounter = 0;
	m_FirstShot = true;
	
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
	m_PositionLockAvailable = false;
	m_PoisonTick = 0;
	m_HealTick = 0;
	m_InfZoneTick = -1;
	m_InAirTick = 0;
	m_InWater = 0;
	m_BonusTick = 0;
	m_WaterJumpLifeSpan = 0;
	m_NinjaVelocityBuff = 0;
	m_NinjaStrengthBuff = 0;
	m_NinjaAmmoBuff = 0;
/* INFECTION MODIFICATION END *****************************************/
}

bool CCharacter::
FindWitchSpawnPosition(vec2& Pos)
{
	float Angle = atan2f(m_Input.m_TargetY, m_Input.m_TargetX);//atan2f instead of atan2
	
	for(int i=0; i<32; i++)
	{
		float TestAngle;
		
		TestAngle = Angle + i * (pi / 32.0f);
		Pos = m_Pos + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;
		
		if(GameServer()->m_pController->IsSpawnable(Pos, ZONE_TELE_NOWITCH))
			return true;
		
		TestAngle = Angle - i * (pi / 32.0f);
		Pos = m_Pos + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;
		
		if(GameServer()->m_pController->IsSpawnable(Pos, ZONE_TELE_NOWITCH))
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
	
		if(GameServer()->m_pController->IsSpawnable(PortalPos, ZONE_TELE_NOSCIENTIST))
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
	m_LoveTick = -1;
	m_HallucinationTick = -1;
	m_SlipperyTick = -1;
	m_PositionLockTick = -Server()->TickSpeed()*10;
	m_PositionLocked = false;
	m_PositionLockAvailable = false;
	m_Poison = 0;

	ClassSpawnAttributes();
	DestroyChildEntities();
	GiveArmorIfLonely();
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
		float VelocityBuff = min(1.0f + static_cast<float>(m_NinjaVelocityBuff)/10.0f, 2.0f);
		m_Core.m_Vel = m_DartDir * g_pData->m_Weapons.m_Ninja.m_Velocity * VelocityBuff;
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

				aEnts[i]->TakeDamage(vec2(0, -10.0f), min(g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage + m_NinjaStrengthBuff, 20), m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_NOINFECTION);
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
	if(m_SlipperyTick > 0)
	{
		pTuningParams->m_GroundFriction = 1.0f;
	}
	
	if(NoActions)
	{
		pTuningParams->m_GroundControlAccel = 0.0f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
		pTuningParams->m_AirControlAccel = 0.0f;
		pTuningParams->m_HookLength = 0.0f;
	}
	if(FixedPosition || m_Core.m_IsPassenger)
	{
		pTuningParams->m_Gravity = 0.0f;
	}
	
	if(GetClass() == PLAYERCLASS_GHOUL)
	{
		float Factor = m_pPlayer->GetGhoulPercent();
		pTuningParams->m_GroundControlSpeed = pTuningParams->m_GroundControlSpeed * (1.0f + 0.5f*Factor);
		pTuningParams->m_GroundControlAccel = pTuningParams->m_GroundControlAccel * (1.0f + 0.5f*Factor);
		pTuningParams->m_GroundJumpImpulse = pTuningParams->m_GroundJumpImpulse * (1.0f + 0.35f*Factor);
		pTuningParams->m_AirJumpImpulse = pTuningParams->m_AirJumpImpulse * (1.0f + 0.35f*Factor);
		pTuningParams->m_AirControlSpeed = pTuningParams->m_AirControlSpeed * (1.0f + 0.5f*Factor);
		pTuningParams->m_AirControlAccel = pTuningParams->m_AirControlAccel * (1.0f + 0.5f*Factor);
		pTuningParams->m_HookDragAccel = pTuningParams->m_HookDragAccel * (1.0f + 0.5f*Factor);
		pTuningParams->m_HookDragSpeed = pTuningParams->m_HookDragSpeed * (1.0f + 0.5f*Factor);
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

	bool AutoFire = false;
	bool FullAuto = false;
	
	if(m_ActiveWeapon == WEAPON_GUN || m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;

	if(GetClass() == PLAYERCLASS_SLUG && m_ActiveWeapon == WEAPON_HAMMER)
		FullAuto = true;
	
	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GRENADE)
																					   || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MEDIC_GRENADE)))
	{
		AutoFire = true;
		WillFire = true;
	}

	if(!WillFire || m_pPlayer->MapMenu() > 0)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo && (GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_MERCENARY_GRENADE)
										  && (GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_MEDIC_GRENADE))
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
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_BIOLOGIST_RIFLE && m_aWeapons[m_ActiveWeapon].m_Ammo < 10)
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
				for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
				{
					if(pWall->m_Owner == m_pPlayer->GetCID())
						GameServer()->m_World.DestroyEntity(pWall);
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
						vec2 TestPos = m_FirstShotCoord + (m_Pos - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
						if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, TestPos) == ZONE_DAMAGE_INFECTION)
						{
							isAccepted = false;
						}
					}
					
					if(isAccepted)
					{
						m_FirstShot = true;
						
						new CEngineerWall(GameWorld(), m_FirstShotCoord, m_Pos, m_pPlayer->GetCID());
						
						GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
					}
				}
			}
			else if(GetClass() == PLAYERCLASS_SOLDIER)
			{
				bool BombFound = false;
				for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
				{
					if(pBomb->m_Owner == m_pPlayer->GetCID())
					{
						pBomb->Explode();
						BombFound = true;
					}
				}
				
				if(!BombFound)
				{
					new CSoldierBomb(GameWorld(), ProjStartPos, m_pPlayer->GetCID());
					GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
				}
			}
			else if(GetClass() == PLAYERCLASS_SNIPER)
			{
				if(m_Pos.y > -600.0)
				{
					if(m_PositionLockTick <= 0 && m_PositionLockAvailable)
					{
						m_PositionLockTick = Server()->TickSpeed()*15;
						m_PositionLocked = true;
						m_PositionLockAvailable = false;
					}
					else
					{
						m_PositionLockTick = 0;
						m_PositionLocked = false;
					}
				}
			}
			else if(GetClass() == PLAYERCLASS_MERCENARY && g_Config.m_InfMercLove && !GameServer()->m_FunRound)
			{
				CMercenaryBomb* pCurrentBomb = NULL;
				for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
				{
					if(pBomb->m_Owner == m_pPlayer->GetCID())
					{
						pCurrentBomb = pBomb;
						break;
					}
				}
				
				if(pCurrentBomb)
				{
					if(pCurrentBomb->ReadyToExplode() || distance(pCurrentBomb->m_Pos, m_Pos) > 80.0f)
						pCurrentBomb->Explode();
					else
					{
						pCurrentBomb->IncreaseDamage();
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
					}
				}
				else
				{
					new CMercenaryBomb(GameWorld(), m_Pos, m_pPlayer->GetCID());
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
				}
				
				m_ReloadTimer = Server()->TickSpeed()/4;
			}
			else if(GetClass() == PLAYERCLASS_SCIENTIST)
			{
				bool FreeSpace = true;
				int NbMine = 0;
				
				int OlderMineTick = Server()->Tick()+1;
				CScientistMine* pOlderMine = 0;
				CScientistMine* pIntersectMine = 0;
				
				CScientistMine* p = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE);
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
					
					p = (CScientistMine *)p->TypeNext();
				}
				
				if(FreeSpace)
				{
					if(pIntersectMine) //Move the mine
						GameServer()->m_World.DestroyEntity(pIntersectMine);
					else if(NbMine >= g_Config.m_InfMineLimit && pOlderMine)
						GameServer()->m_World.DestroyEntity(pOlderMine);
					
					new CScientistMine(GameWorld(), ProjStartPos, m_pPlayer->GetCID());
					
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
				if(!IsFrozen() && !IsInLove())
				{
					Die(m_pPlayer->GetCID(), WEAPON_SELF);
				}
			}
			else
			{
/* INFECTION MODIFICATION END *****************************************/
				// reset objects Hit
				int Hits = 0;
				bool ShowAttackAnimation = false;
				
				// make sure that the slug will not auto-fire to attack
				if(!AutoFire)
				{
					ShowAttackAnimation = true;
					
					m_NumObjectsHit = 0;
					GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

					if(GetClass() == PLAYERCLASS_GHOST)
					{
						m_IsInvisible = false;
						m_InvisibleTick = Server()->Tick();
					}

					CCharacter *apEnts[MAX_CLIENTS];
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
									if(pTarget->IncreaseOverallHp(4))
									{
										IncreaseOverallHp(1);

										pTarget->m_EmoteType = EMOTE_HAPPY;
										pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
									}
									
									if(!pTarget->GetPlayer()->HookProtectionEnabled())
										pTarget->m_Core.m_Vel += vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
								}
							}
							else if(GetClass() == PLAYERCLASS_BAT) {
								pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_Config.m_InfBatDamage,
									m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
							}
							else
							{
								pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
									m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_INFECTION);
							}
						}
						else if(GetClass() == PLAYERCLASS_BIOLOGIST || GetClass() == PLAYERCLASS_MERCENARY)
						{
							/* affects mercenary only if love bombs are disabled. */
							if (pTarget->IsInfected())
							{
								pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
										m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
							}
						}
						else if(GetClass() == PLAYERCLASS_MEDIC)
						{
							if (pTarget->IsInfected())
							{
								pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
										m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
							}
							else
							{
								if(pTarget->GetClass() != PLAYERCLASS_HERO)
								{
									pTarget->IncreaseArmor(4);
									if(pTarget->m_Armor == 10 && pTarget->m_NeedFullHeal)
									{
										Server()->RoundStatistics()->OnScoreEvent(GetPlayer()->GetCID(), SCOREEVENT_HUMAN_HEALING, GetClass());
										GameServer()->SendScoreSound(GetPlayer()->GetCID());
										pTarget->m_NeedFullHeal = false;
										m_aWeapons[WEAPON_GRENADE].m_Ammo++;
									}
									pTarget->m_EmoteType = EMOTE_HAPPY;
									pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
								}
							}
						}
						else
						{
							pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
						}
	/* INFECTION MODIFICATION END *****************************************/
						Hits++;
					}
				}
				
				// if we Hit anything, we have to wait for the reload
				if(Hits)
				{
					m_ReloadTimer = Server()->TickSpeed()/3;
				}
				else if(GetClass() == PLAYERCLASS_SLUG)
				{
					vec2 CheckPos = m_Pos + Direction * 64.0f;
					if(GameServer()->Collision()->IntersectLine(m_Pos, CheckPos, 0x0, &CheckPos))
					{
						float Distance = 99999999.0f;
						for(CSlugSlime* pSlime = (CSlugSlime*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SLUG_SLIME); pSlime; pSlime = (CSlugSlime*) pSlime->TypeNext())
						{
							if(distance(pSlime->m_Pos, m_Pos) < Distance)
							{
								Distance = distance(pSlime->m_Pos, m_Pos);
							}
						}
						
						if(Distance > 84.0f)
						{
							ShowAttackAnimation = true;
							new CSlugSlime(GameWorld(), CheckPos, m_pPlayer->GetCID());
						}
					}
				}
				
				if(!ShowAttackAnimation)
					return;
					
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

				Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
				
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

				Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

				GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
			}
		} break;

		case WEAPON_SHOTGUN:
		{
			int ShotSpread = 3;
			if(GetClass() == PLAYERCLASS_BIOLOGIST)
				ShotSpread = 1;
			
			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			float Force = 2.0f;
			if(GetClass() == PLAYERCLASS_MEDIC)
				Force = 10.0f;
				
			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.21f, -0.14f, -0.070f, 0, 0.070f, 0.14f, 0.21f};
				float a = GetAngle(Direction);
				a += Spreading[i+3] * 2.0f*(0.25f + 0.75f*static_cast<float>(10-m_aWeapons[WEAPON_SHOTGUN].m_Ammo)/10.0f);
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				
				float LifeTime = GameServer()->Tuning()->m_ShotgunLifetime + 0.1f*static_cast<float>(m_aWeapons[WEAPON_SHOTGUN].m_Ammo)/10.0f;
				
				if(GetClass() == PLAYERCLASS_BIOLOGIST)
				{
					CBouncingBullet *pProj = new CBouncingBullet(GameWorld(), m_pPlayer->GetCID(), ProjStartPos, vec2(cosf(a), sinf(a))*Speed);

					// pack the Projectile and send it to the client Directly
					CNetObj_Projectile p;
					pProj->FillInfo(&p);
					for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
						Msg.AddInt(((int *)&p)[i]);
				}
				else
				{
					CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
						m_pPlayer->GetCID(),
						ProjStartPos,
						vec2(cosf(a), sinf(a))*Speed,
						(int)(Server()->TickSpeed()*LifeTime),
						1, 0, Force, -1, WEAPON_SHOTGUN);

					// pack the Projectile and send it to the client Directly
					CNetObj_Projectile p;
					pProj->FillInfo(&p);
					for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
						Msg.AddInt(((int *)&p)[i]);
				}
				
			}

			Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			if(GetClass() == PLAYERCLASS_MERCENARY)
			{				
				//Find bomb
				bool BombFound = false;
				for(CMercenaryGrenade *pGrenade = (CMercenaryGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_GRENADE); pGrenade; pGrenade = (CMercenaryGrenade*) pGrenade->TypeNext())
				{
					if(pGrenade->m_Owner != m_pPlayer->GetCID()) continue;
					pGrenade->Explode();
					BombFound = true;
				}
				
				if(!BombFound && m_aWeapons[m_ActiveWeapon].m_Ammo)
				{
					int ShotSpread = 2;
					
					CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
					Msg.AddInt(ShotSpread*2+1);
					
					for(int i = -ShotSpread; i <= ShotSpread; ++i)
					{
						float a = GetAngle(Direction) + random_float()/5.0f;
						
						CMercenaryGrenade *pProj = new CMercenaryGrenade(GameWorld(), m_pPlayer->GetCID(), m_Pos, vec2(cosf(a), sinf(a)));

						// pack the Projectile and send it to the client Directly
						CNetObj_Projectile p;
						pProj->FillInfo(&p);

						for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
							Msg.AddInt(((int *)&p)[i]);
						Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
					}

					GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
					
					m_ReloadTimer = Server()->TickSpeed()/4;
				}
				else
				{
					m_aWeapons[m_ActiveWeapon].m_Ammo++;
				}
			}
			else if(GetClass() == PLAYERCLASS_MEDIC)
			{
				//Find bomb
				bool BombFound = false;
				for(CMedicGrenade *pGrenade = (CMedicGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MEDIC_GRENADE); pGrenade; pGrenade = (CMedicGrenade*) pGrenade->TypeNext())
				{
					if(pGrenade->m_Owner != m_pPlayer->GetCID()) continue;
					pGrenade->Explode();
					BombFound = true;
				}
				
				if(!BombFound && m_aWeapons[m_ActiveWeapon].m_Ammo)
				{
					int ShotSpread = 0;
					
					CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
					Msg.AddInt(ShotSpread*2+1);
					
					for(int i = -ShotSpread; i <= ShotSpread; ++i)
					{
						float a = GetAngle(Direction) + random_float()/5.0f;
						
						CMedicGrenade *pProj = new CMedicGrenade(GameWorld(), m_pPlayer->GetCID(), m_Pos, vec2(cosf(a), sinf(a)));

						// pack the Projectile and send it to the client Directly
						CNetObj_Projectile p;
						pProj->FillInfo(&p);

						for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
							Msg.AddInt(((int *)&p)[i]);
						Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
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
					if(g_Config.m_InfScientistTpSelfharm > 0) {
						auto pScientist = GetPlayer()->GetCharacter();
						pScientist->TakeDamage(vec2(0.0f, 0.0f), g_Config.m_InfScientistTpSelfharm * 2, GetPlayer()->GetCID(), WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
					}
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
				Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
		} break;

		case WEAPON_RIFLE:
		{
			if(GetClass() == PLAYERCLASS_BIOLOGIST)
			{
				for(CBiologistMine* pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
				{
					if(pMine->m_Owner != m_pPlayer->GetCID()) continue;
						GameServer()->m_World.DestroyEntity(pMine);
				}
				
				vec2 To = m_Pos + Direction*400.0f;
				if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
				{
					new CBiologistMine(GameWorld(), m_Pos, To, m_pPlayer->GetCID());
					GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
					m_aWeapons[WEAPON_RIFLE].m_Ammo = 0;
				}
				else
					return;
			}
			else
			{
				int Damage = GameServer()->Tuning()->m_LaserDamage;
				
				if(GetClass() == PLAYERCLASS_SNIPER)
				{
					if(m_PositionLocked)
						Damage = 30;
					else
						Damage = random_int(10, 13);
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
		
		if(InfWID == INFWEAPON_NINJA_GRENADE)
			MaxAmmo = min(MaxAmmo + m_NinjaAmmoBuff, 10);
		
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
				float Rate = 1.0f;
				int Damage = 1;
				
				if(GetClass() == PLAYERCLASS_SMOKER)
				{
					Rate = 0.5f;
					Damage = g_Config.m_InfSmokerHookDamage;
				}
				else if(GetClass() == PLAYERCLASS_GHOUL)
				{
					Rate = 0.33f + 0.66f * (1.0f-m_pPlayer->GetGhoulPercent());
				}
				
				if(m_HookDmgTick + Server()->TickSpeed()*Rate < Server()->Tick())
				{
					m_HookDmgTick = Server()->Tick();
					VictimChar->TakeDamage(vec2(0.0f,0.0f), Damage, m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_NOINFECTION);
					if((GetClass() == PLAYERCLASS_SMOKER || GetClass() == PLAYERCLASS_BAT) && !VictimChar->IsInfected())
						IncreaseOverallHp(2);
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
	int InfWID = GetInfWeaponID(Weapon);
	int MaxAmmo = Server()->GetMaxAmmo(InfWID);
	
	if(InfWID == INFWEAPON_NINJA_GRENADE)
		MaxAmmo = min(MaxAmmo + m_NinjaAmmoBuff, 10);
	
	if(Ammo < 0)
		Ammo = MaxAmmo;
	
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
	//~ if(GameServer()->Collision()->CheckPhysicsFlag(m_Core.m_Pos, CCollision::COLFLAG_WATER))
	//~ {
		//~ if(m_InWater == 0)
		//~ {
			//~ m_InWater = 1;
			//~ m_Core.m_Vel /= 2.0f;
			//~ m_WaterJumpLifeSpan = 0;
		//~ }
	//~ }
	//~ else
		//~ m_InWater = 0;
	if(GetClass() == PLAYERCLASS_SNIPER && m_PositionLocked)
	{
		if(m_Input.m_Jump && !m_PrevInput.m_Jump)
		{
			m_PositionLocked = false;
		}
	}
	
	if(!IsInfected() && IsAlive() && GameServer()->m_pController->IsInfectionStarted())
	{
		int Index = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Bonus, m_Pos.x, m_Pos.y);
		if(Index == ZONE_BONUS_BONUS)
		{
			m_BonusTick++;
			if(m_BonusTick > Server()->TickSpeed()*60)
			{
				m_BonusTick = 0;
				
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("You have held a bonus area for one minute, +5 points"), NULL);
				GameServer()->SendEmoticon(m_pPlayer->GetCID(), EMOTICON_MUSIC);
				SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
				GiveGift(GIFT_HEROFLAG);
				Server()->RoundStatistics()->OnScoreEvent(m_pPlayer->GetCID(), SCOREEVENT_BONUS, GetClass());
				GameServer()->SendScoreSound(m_pPlayer->GetCID());
			}
		}
	}
	else
		m_BonusTick = 0;
	
	{
		int Index0 = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f);
		int Index1 = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f);
		int Index2 = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f);
		int Index3 = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f);
		
		if(Index0 == ZONE_DAMAGE_DEATH || Index1 == ZONE_DAMAGE_DEATH || Index2 == ZONE_DAMAGE_DEATH || Index3 == ZONE_DAMAGE_DEATH)
		{
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		}
		else if(GetClass() != PLAYERCLASS_UNDEAD && (Index0 == ZONE_DAMAGE_DEATH_NOUNDEAD || Index1 == ZONE_DAMAGE_DEATH_NOUNDEAD || Index2 == ZONE_DAMAGE_DEATH_NOUNDEAD || Index3 == ZONE_DAMAGE_DEATH_NOUNDEAD))
		{
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		}
		else if(IsInfected() && (Index0 == ZONE_DAMAGE_DEATH_INFECTED || Index1 == ZONE_DAMAGE_DEATH_INFECTED || Index2 == ZONE_DAMAGE_DEATH_INFECTED || Index3 == ZONE_DAMAGE_DEATH_INFECTED))
		{
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		}
		else if(m_Alive && (Index0 == ZONE_DAMAGE_INFECTION || Index1 == ZONE_DAMAGE_INFECTION || Index2 == ZONE_DAMAGE_INFECTION || Index3 == ZONE_DAMAGE_INFECTION))
		{
			if(IsInfected())
			{
				if(Server()->Tick() >= m_HealTick + (Server()->TickSpeed()/g_Config.m_InfInfzoneHealRate))
				{
					m_HealTick = Server()->Tick();
					IncreaseHealth(1);
				}
				if (m_InfZoneTick < 0) m_InfZoneTick = Server()->Tick(); // Save Tick when zombie enters infection zone
			}
			else
			{
				m_pPlayer->StartInfection();
			}
		}
		if(m_Alive && (Index0 != ZONE_DAMAGE_INFECTION && Index1 != ZONE_DAMAGE_INFECTION && Index2 != ZONE_DAMAGE_INFECTION && Index3 != ZONE_DAMAGE_INFECTION))
		{
			m_InfZoneTick = -1;// Reset Tick when zombie is not in infection zone
		}
	}
	
	if(m_PositionLockTick > 0)
	{
		--m_PositionLockTick;
		if(m_PositionLockTick <= 0)
			m_PositionLocked = false;
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
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE, BROADCAST_DURATION_REALTIME, _("You are frozen: {sec:EffectDuration}"), "EffectDuration", &FreezeSec, NULL);
		}
	}
	
	if(m_HallucinationTick > 0)
		--m_HallucinationTick;
	
	if(m_LoveTick > 0)
		--m_LoveTick;
	
	if(m_SlipperyTick > 0)
		--m_SlipperyTick;
	
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
									(random_prob(0.25f)) && (
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
			(m_HookMode == 1 || g_Config.m_InfSpiderCatchHumans) &&
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
		m_DartLeft = g_Config.m_InfNinjaJump;
	}
	if(GetClass() == PLAYERCLASS_SNIPER && m_InAirTick <= Server()->TickSpeed())
	{
		m_PositionLockAvailable = true;
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
	if(GetClass() == PLAYERCLASS_BAT)
	{
		CoreTickParams.m_HookGrabTime = g_Config.m_InfBatHookTime*SERVER_TICK_SPEED;
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

	if(GetClass() == PLAYERCLASS_BAT) {
		if(IsGrounded() || g_Config.m_InfBatAirjumpLimit == 0) m_AirJumpCounter = 0;
		else if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP && m_AirJumpCounter < g_Config.m_InfBatAirjumpLimit) {
			m_Core.m_Jumped &= ~2;
			m_AirJumpCounter++;
		}
	}
	
	if(m_pPlayer->MapMenu() == 1)
	{
		if(GetClass() != PLAYERCLASS_NONE)
		{
			m_AntiFireTick = Server()->Tick();
			m_pPlayer->CloseMapMenu();
		}
		else
		{
			vec2 CursorPos = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
			
			bool Broadcast = false;

			if(length(CursorPos) > 100.0f)
			{
				float Angle = 2.0f*pi+atan2(CursorPos.x, -CursorPos.y);
				float AngleStep = 2.0f*pi/static_cast<float>(CMapConverter::NUM_MENUCLASS);
				m_pPlayer->m_MapMenuItem = ((int)((Angle+AngleStep/2.0f)/AngleStep))%CMapConverter::NUM_MENUCLASS;
				
				switch(m_pPlayer->m_MapMenuItem)
				{
					case CMapConverter::MENUCLASS_RANDOM:
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), NULL);
						Broadcast = true;
						break;
					case CMapConverter::MENUCLASS_ENGINEER:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_ENGINEER))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Engineer"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_SOLDIER:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SOLDIER))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Soldier"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_SCIENTIST:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SCIENTIST))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Scientist"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_BIOLOGIST:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_BIOLOGIST))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Biologist"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_MEDIC:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_MEDIC))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Medic"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_HERO:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_HERO))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Hero"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_NINJA:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_NINJA))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Ninja"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_MERCENARY:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_MERCENARY))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Mercenary"), NULL);
							Broadcast = true;
						}
						break;
					case CMapConverter::MENUCLASS_SNIPER:
						if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SNIPER))
						{
							GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Sniper"), NULL);
							Broadcast = true;
						}
						break;
				}
			}
			
			if(!Broadcast)
			{
				m_pPlayer->m_MapMenuItem = -1;
				GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Choose your class"), NULL);
			}
			
			if(m_Input.m_Fire&1 && m_pPlayer->m_MapMenuItem >= 0)
			{
				bool Bonus = false;
				
				int NewClass = -1;
				switch(m_pPlayer->m_MapMenuItem)
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
					case CMapConverter::MENUCLASS_BIOLOGIST:
						NewClass = PLAYERCLASS_BIOLOGIST;
						break;
				}
				
				if(NewClass >= 0 && GameServer()->m_pController->IsChoosableClass(NewClass))
				{
					m_AntiFireTick = Server()->Tick();
					m_pPlayer->m_MapMenuItem = 0;
					m_pPlayer->SetClass(NewClass);
					m_pPlayer->SetOldClass(NewClass);
					
					// class '11' counts as picking "Random"
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%d'", Server()->ClientName(m_pPlayer->GetCID()), Bonus ? 11 : NewClass);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
					
					if(Bonus)
						IncreaseArmor(10);
				}
			}
		}
	}
		
	if(GetClass() == PLAYERCLASS_ENGINEER)
	{
		CEngineerWall* pCurrentWall = NULL;
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->m_Owner == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}
		
		if(pCurrentWall)
		{
			int Seconds = 1+pCurrentWall->GetTick()/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Laser wall: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_SOLDIER)
	{
		int NumBombs = 0;
		for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
		{
			if(pBomb->m_Owner == m_pPlayer->GetCID())
				NumBombs += pBomb->GetNbBombs();
		}
		
		if(NumBombs)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, NumBombs,
				_P("One bomb left", "{int:NumBombs} bombs left"),
				"NumBombs", &NumBombs,
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_SCIENTIST)
	{
		int NumMines = 0;
		for(CScientistMine *pMine = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE); pMine; pMine = (CScientistMine*) pMine->TypeNext())
		{
			if(pMine->m_Owner == m_pPlayer->GetCID())
				NumMines++;
		}
		
		if(NumMines > 0)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, NumMines,
				_P("One mine is active", "{int:NumMines} mines are active"),
				"NumMines", &NumMines,
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_BIOLOGIST)
	{
		int NumMines = 0;
		for(CBiologistMine *pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
		{
			if(pMine->m_Owner == m_pPlayer->GetCID())
				NumMines++;
		}
		
		if(NumMines > 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Mine activated"),
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_NINJA)
	{
		int TargetID = GameServer()->GetTargetToKill();
		int CoolDown = GameServer()->GetTargetToKillCoolDown();
		
		if(CoolDown > 0)
		{
			int Seconds = 1+CoolDown/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Next target in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
		else if(TargetID >= 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Target to eliminate: {str:PlayerName}"),
				"PlayerName", Server()->ClientName(TargetID),
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_SNIPER)
	{
		if(m_PositionLocked)
		{
			int Seconds = 1+m_PositionLockTick/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Position lock: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_MERCENARY)
	{
		CMercenaryBomb* pCurrentBomb = NULL;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
		{
			if(pBomb->m_Owner == m_pPlayer->GetCID())
			{
				pCurrentBomb = pBomb;
				break;
			}
		}
		
		if(pCurrentBomb)
		{
			float BombLevel = pCurrentBomb->m_Damage/static_cast<float>(g_Config.m_InfMercBombs);
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Explosive yield: {percent:BombLevel}"),
				"BombLevel", &BombLevel,
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_HERO)
	{
		//Search for flag
		int CoolDown = 999999999;
		for(CHeroFlag *pFlag = (CHeroFlag*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_HERO_FLAG); pFlag; pFlag = (CHeroFlag*) pFlag->TypeNext())
		{
			if(pFlag->GetCoolDown() <= 0)
			{
				CoolDown = 0;
				break;
			}
			else if(pFlag->GetCoolDown() < CoolDown)
				CoolDown = pFlag->GetCoolDown();
		}
		
		if(CoolDown > 0)
		{
			int Seconds = 1+CoolDown/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Next flag in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetClass() == PLAYERCLASS_SPIDER)
	{
		if(m_HookMode > 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, _("Web mode enabled"), NULL);
		}
	}
	else if(GetClass() == PLAYERCLASS_GHOUL)
	{
		if(m_pPlayer->GetGhoulLevel())
		{
			float FodderInStomach = m_pPlayer->GetGhoulPercent();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Stomach filled by {percent:FodderInStomach}"),
				"FodderInStomach", &FodderInStomach,
				NULL
			);
		}
	}
/* INFECTION MODIFICATION END *****************************************/

	// Previnput
	m_PrevInput = m_Input;
	
	return;
}

void CCharacter::GiveGift(int GiftType)
{
	IncreaseHealth(1);
	IncreaseArmor(4);
	
	switch(GetClass())
	{
		case PLAYERCLASS_ENGINEER:
			GiveWeapon(WEAPON_RIFLE, -1);
			break;
		case PLAYERCLASS_SOLDIER:
			GiveWeapon(WEAPON_GRENADE, -1);
			break;
		case PLAYERCLASS_SCIENTIST:
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			break;
		case PLAYERCLASS_BIOLOGIST:
			GiveWeapon(WEAPON_RIFLE, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			break;
		case PLAYERCLASS_MEDIC:
			GiveWeapon(WEAPON_SHOTGUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			break;
		case PLAYERCLASS_HERO:
			GiveWeapon(WEAPON_SHOTGUN, -1);
			break;
		case PLAYERCLASS_NINJA:
			GiveWeapon(WEAPON_GRENADE, -1);
			break;
		case PLAYERCLASS_SNIPER:
			GiveWeapon(WEAPON_RIFLE, -1);
			break;
		case PLAYERCLASS_MERCENARY:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			break;
	}
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

bool CCharacter::IncreaseOverallHp(int Amount)
{
	bool success = false;
	if(m_Health < 10)
	{
		int healthDiff = 10-m_Health;
		IncreaseHealth(Amount);
		success = true;
		Amount = Amount - healthDiff;
	}
	if(Amount > 0)
	{
		if (IncreaseArmor(Amount)) 
			success = true;
	}
	return success;
}

void CCharacter::SetHealthArmor(int HealthAmount, int ArmorAmount)
{
	m_Health = HealthAmount;
	m_Armor = ArmorAmount;
}

int CCharacter::GetHealthArmorSum()
{
	return m_Health + m_Armor;
}

void CCharacter::Die(int Killer, int Weapon)
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetClass() == PLAYERCLASS_UNDEAD && Killer != m_pPlayer->GetCID())
	{
		Freeze(10.0, Killer, FREEZEREASON_UNDEAD);
		return;
	}
	
	//Find the nearest ghoul
	{
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(p->GetClass() != PLAYERCLASS_GHOUL || p == this) continue;
			if(p->GetPlayer() && p->GetPlayer()->GetGhoulPercent() >= 1.0f) continue;

			float Len = distance(p->m_Pos, m_Pos);
			
			if(p && Len < 800.0f)
			{
				int Points = (IsInfected() ? 8 : 14);
				new CFlyingPoint(GameWorld(), m_Pos, p->GetPlayer()->GetCID(), Points, m_Core.m_Vel);
			}
		}
	}
	if(GetClass() == PLAYERCLASS_GHOUL)
	{
		m_pPlayer->IncreaseGhoulLevel(-20);
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
	if(GetClass() == PLAYERCLASS_BOOMER && !IsFrozen() && Weapon != WEAPON_GAME && !(IsInLove() && Weapon == WEAPON_SELF) )
	{
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateExplosionDisk(m_Pos, 80.0f, 107.5f, 14, 52.0f, m_pPlayer->GetCID(), WEAPON_HAMMER, TAKEDAMAGEMODE_INFECTION);
	}
	
	if(GetClass() == PLAYERCLASS_WITCH)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The witch is dead"), NULL);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else if(GetClass() == PLAYERCLASS_UNDEAD)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The undead is finally dead"), NULL);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else
	{
		m_pPlayer->StartInfection(false);
	}	
	if (m_Core.m_Passenger) {
		m_Core.m_Passenger->m_IsPassenger = false; // InfClassR taxi mode
	}
/* INFECTION MODIFICATION END *****************************************/
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon, int Mode)
{
/* INFECTION MODIFICATION START ***************************************/

	CPlayer* pKillerPlayer = GameServer()->m_apPlayers[From];
	
	if(GetClass() == PLAYERCLASS_HERO && Mode == TAKEDAMAGEMODE_INFECTION && pKillerPlayer && pKillerPlayer->IsInfected())
		Dmg = 12;
	
	if(pKillerPlayer && pKillerPlayer->GetCharacter() && pKillerPlayer->GetCharacter()->IsInLove())
	{
		Dmg = 0;
		Mode = TAKEDAMAGEMODE_NOINFECTION;
	}
	
	if(GetClass() != PLAYERCLASS_HUNTER || Weapon != WEAPON_SHOTGUN)
	{
		m_Core.m_Vel += Force;
	}
	
	if(GetClass() == PLAYERCLASS_GHOUL)
	{
		int DamageAccepted = 0;
		for(int i=0; i<Dmg; i++)
		{
			if(random_prob(1.0f - m_pPlayer->GetGhoulPercent()/2.0f))
				DamageAccepted++;
		}
		Dmg = DamageAccepted;
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
					IncreaseOverallHp(8+random_int(0, 10));
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
		if(GetClass() == PLAYERCLASS_HERO || (GetClass() == PLAYERCLASS_SOLDIER && m_ActiveWeapon == WEAPON_GRENADE)
										  || (GetClass() == PLAYERCLASS_SCIENTIST && m_ActiveWeapon == WEAPON_RIFLE))
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
			
		if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
			GameServer()->SendHitSound(From);
	}
/* INFECTION MODIFICATION END *****************************************/

	m_DamageTakenTick = Server()->Tick();
	m_InvisibleTick = Server()->Tick();

	// do damage Hit sound
	
/* INFECTION MODIFICATION START ***************************************/
	if(Mode == TAKEDAMAGEMODE_INFECTION && GameServer()->m_apPlayers[From]->IsInfected() && !IsInfected() && GetClass() != PLAYERCLASS_HERO)
	{
		m_pPlayer->StartInfection();
		
		GameServer()->SendChatTarget_Localization(From, CHATCATEGORY_SCORE, _("You have infected {str:VictimName}, +3 points"), "VictimName", Server()->ClientName(m_pPlayer->GetCID()), NULL);
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
	int id = m_pPlayer->GetCID();

	if (!Server()->Translate(id, SnappingClient))
		return;

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
	
	if(m_Armor < 10 && SnappingClient != m_pPlayer->GetCID() && !IsInfected() && GetClass() != PLAYERCLASS_HERO)
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
	else if((m_Armor + m_Health) < 10 && SnappingClient != m_pPlayer->GetCID() && IsInfected() && pClient->IsInfected())
	{
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x;
		pP->m_Y = (int)m_Pos.y - 60.0;
		pP->m_Type = POWERUP_HEALTH;
		pP->m_Subtype = 0;
	}
	
	if(pClient && !pClient->IsInfected() && GetClass() == PLAYERCLASS_ENGINEER && !m_FirstShot)
	{
		CEngineerWall* pCurrentWall = NULL;
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->m_Owner == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}
		
		if(!pCurrentWall)
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
		else if(GetClass() == PLAYERCLASS_HERO) 
		{
			CHeroFlag *pFlag = (CHeroFlag*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_HERO_FLAG);
			
			// Guide hero to flag
			if(pFlag->GetCoolDown() <= 0)
			{
				CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_CursorID, sizeof(CNetObj_Projectile)));
				if(!pObj)
					return;

				float Angle = atan2f(pFlag->m_Pos.y-m_Pos.y, pFlag->m_Pos.x-m_Pos.x);
				vec2 Indicator = m_Pos + vec2(cos(Angle), sin(Angle)) * 84.0f; 

				pObj->m_X = (int)Indicator.x;
				pObj->m_Y = (int)Indicator.y;
				pObj->m_VelX = 0;
				pObj->m_VelY = 0;
				pObj->m_StartTick = Server()->Tick();
			}
		}
	}
/* INFECTION MODIFICATION END ***************************************/

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, id, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;
	int EmoteNormal = EMOTE_NORMAL;
	if(IsInfected()) EmoteNormal = EMOTE_ANGRY;
	if(m_IsInvisible) EmoteNormal = EMOTE_BLINK;
	if(m_LoveTick > 0 || m_HallucinationTick > 0) EmoteNormal = EMOTE_SURPRISE;
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

	if (pCharacter->m_HookedPlayer != -1)
	{
		if (!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
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
	if(GetClass() == PLAYERCLASS_BAT)
	{
		pCharacter->m_HookTick -= (g_Config.m_InfBatHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED/5;
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
		if(Server()->IsClassChooserEnabled())
			IncreaseArmor(10);
	}
	else
	{
		m_pPlayer->OpenMapMenu(1);
	}
}

int CCharacter::GetClass()
{
	if(!m_pPlayer)
		return PLAYERCLASS_NONE;
	else
		return m_pPlayer->GetClass();
}

void CCharacter::GiveNinjaBuf()
{
	if(GetClass() != PLAYERCLASS_NINJA)
		return;
	
	switch(random_int(0, 2))
	{
		case 0: //Velocity Buff
			m_NinjaVelocityBuff++;
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("Sword velocity increased"), NULL);
			break;
		case 1: //Strength Buff
			m_NinjaStrengthBuff++;
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("Sword strength increased"), NULL);
			break;
		case 2: //Ammo Buff
			m_NinjaAmmoBuff++;
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("Grenade limit increased"), NULL);
			break;
	}
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
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			m_ActiveWeapon = WEAPON_RIFLE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_ENGINEER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_ENGINEER))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "engineer", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_ENGINEER] = true;
			}
			break;
		case PLAYERCLASS_SOLDIER:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			m_ActiveWeapon = WEAPON_GRENADE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SOLDIER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SOLDIER))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "soldier", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_SOLDIER] = true;
			}
			break;
		case PLAYERCLASS_MERCENARY:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_GUN, -1);
			m_ActiveWeapon = WEAPON_GUN;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_MERCENARY);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_MERCENARY))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "mercenary", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_MERCENARY] = true;
			}
			break;
		case PLAYERCLASS_SNIPER:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			m_ActiveWeapon = WEAPON_RIFLE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SNIPER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SNIPER))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "sniper", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_SNIPER] = true;
			}
			break;
		case PLAYERCLASS_SCIENTIST:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			m_ActiveWeapon = WEAPON_GRENADE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SCIENTIST);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SCIENTIST))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "scientist", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_SCIENTIST] = true;
			}
			break;
		case PLAYERCLASS_BIOLOGIST:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			m_ActiveWeapon = WEAPON_SHOTGUN;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_BIOLOGIST);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_BIOLOGIST))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "biologist", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_BIOLOGIST] = true;
			}
			break;
		case PLAYERCLASS_MEDIC:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			m_ActiveWeapon = WEAPON_SHOTGUN;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_MEDIC);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_MEDIC))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "medic", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_MEDIC] = true;
			}
			break;
		case PLAYERCLASS_HERO:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = false;
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			GiveWeapon(WEAPON_RIFLE, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			m_ActiveWeapon = WEAPON_GRENADE;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_HERO);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_HERO))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "hero", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_HERO] = true;
			}
			break;
		case PLAYERCLASS_NINJA:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_NINJA);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_NINJA))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "ninja", NULL);
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "smoker", NULL);
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "boomer", NULL);
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "hunter", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_HUNTER] = true;
			}
			break;
		case PLAYERCLASS_BAT:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_BAT);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_BAT))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "bat", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_BAT] = true;
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "ghost", NULL);
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "spider", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_SPIDER] = true;
			}
			break;
		case PLAYERCLASS_GHOUL:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_GHOUL);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_GHOUL))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "ghoul", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_GHOUL] = true;
			}
			break;
		case PLAYERCLASS_SLUG:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PLAYERCLASS_SLUG);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SLUG))
			{
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "slug", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_SLUG] = true;
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "undead", NULL);
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
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", "witch", NULL);
				m_pPlayer->m_knownClass[PLAYERCLASS_WITCH] = true;
			}
			break;
	}
}

void CCharacter::GiveArmorIfLonely() {
	if (this->IsInfected()) {
		unsigned int nbZombies=0;
		CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if (Iter.Player()->IsInfected())
				nbZombies++;
		}
		if (nbZombies <= 1) /* Lonely zombie */
			m_Armor = 10;
	}
}

void CCharacter::DestroyChildEntities()
{
	m_NinjaVelocityBuff = 0;
	m_NinjaStrengthBuff = 0;
	m_NinjaAmmoBuff = 0;
	
	for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
	{
		if(pWall->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pWall);
	}
	for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
	{
		if(pBomb->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pBomb);
	}
	for(CMercenaryGrenade* pGrenade = (CMercenaryGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_GRENADE); pGrenade; pGrenade = (CMercenaryGrenade*) pGrenade->TypeNext())
	{
		if(pGrenade->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pGrenade);
	}
	for(CMedicGrenade* pGrenade = (CMedicGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MEDIC_GRENADE); pGrenade; pGrenade = (CMedicGrenade*) pGrenade->TypeNext())
	{
		if(pGrenade->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pGrenade);
	}
	for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
	{
		if(pBomb->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pBomb);
	}
	for(CScientistMine* pMine = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE); pMine; pMine = (CScientistMine*) pMine->TypeNext())
	{
		if(pMine->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pMine);
	}
	for(CBiologistMine* pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
	{
		if(pMine->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pMine);
	}
	for(CSlugSlime* pSlime = (CSlugSlime*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SLUG_SLIME); pSlime; pSlime = (CSlugSlime*) pSlime->TypeNext())
	{
		if(pSlime->GetOwner() != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pSlime);
	}
			
	m_FirstShot = true;
	m_HookMode = 0;
	m_PositionLockTick = 0;
	m_PositionLocked = false;
	m_PositionLockAvailable = false;
}

void CCharacter::SetClass(int ClassChoosed)
{
	ClassSpawnAttributes();
	DestroyChildEntities();
	GiveArmorIfLonely();
	
	m_QueuedWeapon = -1;
	m_NeedFullHeal = false;
	
	GameServer()->CreatePlayerSpawn(m_Pos);
}

bool CCharacter::IsInfected() const
{
	return m_pPlayer->IsInfected();
}

bool CCharacter::IsInLove() const
{
    return m_LoveTick > 0;
}

void CCharacter::LoveEffect()
{
	if(m_LoveTick <= 0)
		m_LoveTick = Server()->TickSpeed()*5;
}

void CCharacter::HallucinationEffect()
{
	if(m_HallucinationTick <= 0)
		m_HallucinationTick = Server()->TickSpeed()*5;
}

void CCharacter::SlipperyEffect()
{
	if(m_SlipperyTick <= 0)
		m_SlipperyTick = Server()->TickSpeed()/2;
}

void CCharacter::Freeze(float Time, int Player, int Reason)
{
	if(m_IsFrozen && m_FreezeReason == FREEZEREASON_UNDEAD)
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
			case PLAYERCLASS_BIOLOGIST:
				return INFWEAPON_BIOLOGIST_SHOTGUN;
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
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_GRENADE;
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
			case PLAYERCLASS_BIOLOGIST:
				return INFWEAPON_BIOLOGIST_RIFLE;
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_RIFLE;
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

int CCharacter::GetInfZoneTick() // returns how many ticks long a player is already in InfZone
{
	if (m_InfZoneTick < 0) return 0;
	return Server()->Tick()-m_InfZoneTick;
}
/* INFECTION MODIFICATION END *****************************************/
