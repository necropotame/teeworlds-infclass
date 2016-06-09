/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>
#include <iostream>

#include "character.h"
#include "laser.h"
#include "projectile.h"
#include "mercenarybomb.h"

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
	
	m_pClassChooser = 0;
	m_pBarrier = 0;
	m_pBomb = 0;
	m_pPortal[0] = 0;
	m_pPortal[1] = 0;
	m_FlagID = Server()->SnapNewID();
	m_HeartID = Server()->SnapNewID();
	m_BarrierHintID = Server()->SnapNewID();
	m_AntiFireTick = 0;
	m_PortalTick = 0;
	m_IsFrozen = false;
	m_FrozenTime = -1;
	m_IsInvisible = false;
	m_InvisibleTick = 0;
	m_HealTick = 0;
/* INFECTION MODIFICATION END *****************************************/
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
	m_PortalTick = 0;
	m_LastPortalOwner = m_pPlayer->GetCID();
	m_IsFrozen = false;
	m_FrozenTime = -1;
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


void CCharacter::HandleNinja()
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_NINJA_HAMMER)
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
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

		return;
	}

	return;
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
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

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

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
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
						if(GameServer()->Collision()->CheckPointFlag(m_FirstShotCoord + (m_Pos - m_FirstShotCoord)*(static_cast<float>(i)/14.0f), CCollision::COLFLAG_INFECTION))
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
			else if(GetClass() == PLAYERCLASS_SCIENTIST)
			{
				vec2 PortalPos = m_Pos;
							
				//Check anti portal tile
				vec2 portalTile = vec2(16.0f, 16.0f) + vec2(
					static_cast<float>(static_cast<int>(round(PortalPos.x))/32)*32.0,
					static_cast<float>(static_cast<int>(round(PortalPos.y))/32)*32.0);
				
				//Check is the tile is occuped, and if the direction is valide
				if(!GameServer()->Collision()->CheckPointFlag(portalTile, CCollision::COLFLAG_NOPORTAL) && PortalPos.y > -600.0)
				{
					if(m_pPortal[0] && m_pPortal[1])
					{
						GameServer()->m_World.DestroyEntity(m_pPortal[0]);
						GameServer()->m_World.DestroyEntity(m_pPortal[1]);
						m_pPortal[0] = 0;
						m_pPortal[1] = 0;
					}
					
					if(!m_pPortal[0])
					{
						m_pPortal[0] = new CPortal(GameWorld(), PortalPos, m_pPlayer->GetCID(), 0);
					}
					else
					{
						if(distance(m_Pos, m_pPortal[0]->m_Pos) > 64.0)
						{
							m_pPortal[1] = new CPortal(GameWorld(), PortalPos, m_pPlayer->GetCID(), 1);
							m_pPortal[1]->Link(m_pPortal[0]);
						}
					}
				}
			}
			else if(GetClass() == PLAYERCLASS_MERCENARY)
			{				
				//Find bomb
				bool BombFound = false;
				for(CMercenaryBomb *bomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARYBOMB); bomb; bomb = (CMercenaryBomb *)bomb->TypeNext())
				{
					if(bomb->m_Owner != m_pPlayer->GetCID()) continue;
					bomb->Explode();
					BombFound = true;
				}
				
				if(!BombFound)
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
			}
			else if(GetClass() == PLAYERCLASS_NINJA)
			{
				if(m_Ninja.m_NbStrike)
				{
					m_Ninja.m_NbStrike--;
					
					// reset Hit objects
					m_NumObjectsHit = 0;

					m_Ninja.m_ActivationDir = Direction;
					m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
					m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

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
							}
							else
							{
								pTarget->IncreaseHealth(2);
								pTarget->IncreaseArmor(2);
								pTarget->m_Core.m_Vel += vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
								pTarget->m_EmoteType = EMOTE_HAPPY;
								pTarget->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
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
						//pTarget->m_Core.m_Vel += vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
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
				float Speed = length(m_Core.m_Vel);
				vec2 VelDir = normalize(m_Core.m_Vel);
				if(Speed < 0.00001)
				{
					VelDir = Direction;
				}
				vec2 OrthoVelDir = vec2(-VelDir.y, VelDir.x);
				float VelDirFactor = dot(Recoil, VelDir);
				float OrthoVelDirFactor = dot(Recoil, OrthoVelDir);
				vec2 NewVel = OrthoVelDir * OrthoVelDirFactor;
				if(VelDirFactor < 0.0f)
				{
					NewVel += VelDir * (VelDirFactor + Speed);
				}
				else if(Speed < MaxSpeed)
				{
					NewVel += VelDir * min(VelDirFactor + Speed, MaxSpeed);
				}
				
				m_Core.m_Vel = NewVel;

				GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
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
					1, 0, 10.0f, -1, WEAPON_SHOTGUN);

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
		} break;

		case WEAPON_RIFLE:
		{
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
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
		int AmmoRegenTime = Server()->GetAmmoRegenTime(GetInfWeaponID(i));
		int MaxAmmo = Server()->GetMaxAmmo(GetInfWeaponID(i));
		
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
	//Check is the character is in toxic gaz
	if(m_Alive && GameServer()->Collision()->CheckPointFlag(m_Pos, CCollision::COLFLAG_INFECTION))
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
		
		//Infection after teleportation
		//~ if((Server()->Tick() - m_PortalTick) < Server()->TickSpeed() && m_LastPortalOwner != m_pPlayer->GetCID())
		//~ {
			//~ CPlayer* pBadPlayer = GameServer()->m_apPlayers[m_LastPortalOwner];
			//~ if(pBadPlayer)
			//~ {
				//~ GameServer()->SendChatTarget_Language_s(m_LastPortalOwner, TEXTID_YOU_PORTAL_INFECTION, Server()->ClientName(m_pPlayer->GetCID()));
			
				//~ pBadPlayer->IncreaseScore(-5);
			//~ }
		//~ }
	}

	--m_FrozenTime;
	if(m_IsFrozen)
	{
		if(m_FrozenTime <= 0)
		{
			Unfreeze();
			GameServer()->SendBroadcast("", m_pPlayer->GetCID());
		}
		else
		{
			int FreezeSec = 1+(m_FrozenTime/Server()->TickSpeed());
			GameServer()->SendBroadcast_Language_i(m_pPlayer->GetCID(), TEXTID_YOU_FROZEN, FreezeSec);
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
	
	if(GetClass() == PLAYERCLASS_NINJA && IsGrounded() && m_Ninja.m_CurrentMoveTime <= 0)
	{
		m_Ninja.m_ActivationTick = Server()->Tick();
		m_Ninja.m_NbStrike = 2;
	}
	
	if(m_IsFrozen)
	{
		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
	}
/* INFECTION MODIFICATION END *****************************************/

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);
	
/* INFECTION MODIFICATION START ***************************************/
	
	if(!IsInfected() && m_Core.m_HookedPlayer >= 0)
	{
		if(GameServer()->m_apPlayers[m_Core.m_HookedPlayer] && !GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->IsInfected())
		{
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_HookPos = m_Pos;
		}
	}
/* INFECTION MODIFICATION END *****************************************/
	

	// handle death-tiles and leaving gamelayer
	if(GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
/* INFECTION MODIFICATION START ***************************************/
	HandleWeapons();

	if(GetClass() == PLAYERCLASS_HUNTER)
	{
		if(IsGrounded()) m_AirJumpCounter = 0;
		if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP && m_AirJumpCounter < 1)
		{
			m_Core.m_Jumped &= ~2;
			m_AirJumpCounter++;
		}
	}
	
	if(m_pClassChooser)
	{
		if(GetClass() != PLAYERCLASS_NONE)
		{
			GameServer()->m_World.DestroyEntity(m_pClassChooser);
			m_pClassChooser = 0;
		}
		else
		{
			m_pClassChooser->m_Pos = m_Pos;
			m_pClassChooser->SetCursor(vec2(m_Input.m_TargetX, m_Input.m_TargetY));
			
			if(m_Input.m_Fire&1)
			{
				int ccRes = m_pClassChooser->SelectClass();
				if(ccRes)
				{				
					GameServer()->m_World.DestroyEntity(m_pClassChooser);
					m_pClassChooser = 0;
					
					m_pPlayer->SetClass(ccRes);
					
					m_AntiFireTick = Server()->Tick();
				}
			}
		}
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
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move();
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
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
		
/* INFECTION MODIFICATION START ***************************************/
	++m_HookDmgTick;
	++m_PortalTick;
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
		GameServer()->SendBroadcast_Language(-1, TEXTID_WITCH_DEAD);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else if(GetClass() == PLAYERCLASS_UNDEAD)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Language(-1, TEXTID_UNDEAD_DEAD);
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
	if(GetClass() != PLAYERCLASS_HUNTER || Weapon != WEAPON_SHOTGUN)
	{
		m_Core.m_Vel += Force;
	}

	CPlayer* pKillerPlayer = GameServer()->m_apPlayers[From];
	
	if(From != m_pPlayer->GetCID() && pKillerPlayer)
	{
		if(IsInfected())
		{
			if(pKillerPlayer->IsInfected()) return false;
		}
		else
		{
			//If the player is a new infected, don't infected other -> nobody knows that he is infected.
			if(!pKillerPlayer->IsInfected() || (Server()->Tick() - pKillerPlayer->m_InfectionTick)*Server()->TickSpeed() < 0.5) return false;
		}
	}
	
	//~ if(m_Ninja.m_CurrentMoveTime > 0 && GetClass() == PLAYERCLASS_NINJA)
		//~ return false;
	
	
/* INFECTION MODIFICATION END *****************************************/

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = max(1, Dmg/2);

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
	}
/* INFECTION MODIFICATION END *****************************************/

	m_DamageTakenTick = Server()->Tick();
	m_InvisibleTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}
	
/* INFECTION MODIFICATION START ***************************************/
	if(Mode == TAKEDAMAGEMODE_INFECTION && GameServer()->m_apPlayers[From]->IsInfected() && !IsInfected())
	{
		m_pPlayer->StartInfection();
		
		GameServer()->SendChatTarget_Language_s(From, TEXTID_YOU_INFECTED_PLAYER, Server()->ClientName(m_pPlayer->GetCID()));
	
		GameServer()->m_apPlayers[From]->IncreaseScore(3);
		GameServer()->m_apPlayers[From]->IncreaseNbInfection(1);
	
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
		
/* INFECTION MODIFICATION START ***************************************/
	if(GetClass() == PLAYERCLASS_GHOST)
	{
		CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
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
	
	if(m_Armor == 0 && SnappingClient != m_pPlayer->GetCID() && !IsInfected())
	{
		CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
		
		if(pClient && pClient->GetClass() == PLAYERCLASS_MEDIC)
		{
			CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
			if(!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			pP->m_Type = POWERUP_HEALTH;
			pP->m_Subtype = 0;
		}
	}
	
	if(GetClass() == PLAYERCLASS_ENGINEER && !m_pBarrier && !m_FirstShot)
	{
		CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
		
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
		if(!m_pClassChooser)
		{
			m_pClassChooser = new CClassChooser(GameWorld(), m_Pos, m_pPlayer->GetCID());
		}
	}
}

int CCharacter::GetClass()
{
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_ENGINEER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_ENGINEER))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_ENGINEER_TIP);
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_SOLDIER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SOLDIER))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_SOLDIER_TIP);
				m_pPlayer->m_knownClass[PLAYERCLASS_SOLDIER] = true;
			}
			break;
		case PLAYERCLASS_MERCENARY:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, Server()->GetMaxAmmo(INFWEAPON_MERCENARY_GUN));
			m_ActiveWeapon = WEAPON_GUN;
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_SOLDIER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_MERCENARY))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_SOLDIER_TIP);
				m_pPlayer->m_knownClass[PLAYERCLASS_MERCENARY] = true;
			}
			break;
		case PLAYERCLASS_SCIENTIST:
			RemoveAllGun();
			m_pPlayer->m_InfectionTick = -1;
			m_Health = 10;
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			GiveWeapon(WEAPON_GUN, 10);
			GiveWeapon(WEAPON_SHOTGUN, 10);
			m_ActiveWeapon = WEAPON_SHOTGUN;
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_SCIENTIST);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SCIENTIST))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_SCIENTIST_TIP);
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_MEDIC);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_MEDIC))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_MEDIC_TIP);
				m_pPlayer->m_knownClass[PLAYERCLASS_MEDIC] = true;
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_NINJA);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_NINJA))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_NINJA_TIP);
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_SMOKER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_SMOKER))
			{   
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_SMOKER_TIP);
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_BOOMER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_BOOMER))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_BOOMER_TIP);
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_HUNTER);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_HUNTER))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_HUNTER_TIP);
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
			
			GameServer()->SendBroadcast("You are an infected: Ghost", m_pPlayer->GetCID());
			//~ if(!m_pPlayer->IsKownClass(PLAYERCLASS_GHOST))
			//~ {
				//~ GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_HUNTER_TIP);
				//~ m_pPlayer->m_knownClass[PLAYERCLASS_GHOST] = true;
			//~ }
			break;
		case PLAYERCLASS_UNDEAD:
			m_Health = 10;
			m_Armor = 0;
			RemoveAllGun();
			m_aWeapons[WEAPON_HAMMER].m_Got = true;
			GiveWeapon(WEAPON_HAMMER, -1);
			m_ActiveWeapon = WEAPON_HAMMER;
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_UNDEAD);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_UNDEAD))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_UNDEAD_TIP);
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
			
			GameServer()->SendBroadcast_Language(m_pPlayer->GetCID(), TEXTID_YOU_WITCH);
			if(!m_pPlayer->IsKownClass(PLAYERCLASS_WITCH))
			{
				GameServer()->SendChatTarget_Language(m_pPlayer->GetCID(), TEXTID_WITCH_TIP);
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
	if(m_pPortal[0])
	{
		GameServer()->m_World.DestroyEntity(m_pPortal[0]);
		m_pPortal[0] = 0;
	}
	if(m_pPortal[1])
	{
		GameServer()->m_World.DestroyEntity(m_pPortal[1]);
		m_pPortal[1] = 0;
	}
	if(m_pClassChooser)
	{
		GameServer()->m_World.DestroyEntity(m_pClassChooser);
		m_pClassChooser = 0;
	}
	for(CMercenaryBomb *bomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARYBOMB); bomb; bomb = (CMercenaryBomb *)bomb->TypeNext())
	{
		if(bomb->m_Owner != m_pPlayer->GetCID()) continue;
		bomb->Explode();
	}
	m_FirstShot = true;
}

void CCharacter::SetClass(int ClassChoosed)
{
	ClassSpawnAttributes();
	DestroyChildEntities();
	
	m_QueuedWeapon = -1;
	
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

bool CCharacter::IsTeleportable()
{
	return (Server()->Tick() - m_PortalTick >= Server()->TickSpeed()/2.0f);
}

int CCharacter::GetInfWeaponID(int WID)
{
	if(WID == WEAPON_HAMMER)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_MERCENARY:
				return INFWEAPON_MERCENARY_HAMMER;
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
			case PLAYERCLASS_SCIENTIST:
				return INFWEAPON_SCIENTIST_SHOTGUN;
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_SHOTGUN;
			default:
				return INFWEAPON_SHOTGUN;
		}
	}
	else if(WID == WEAPON_GRENADE)
	{
		switch(GetClass())
		{
			case PLAYERCLASS_SOLDIER:
				return INFWEAPON_SOLDIER_GRENADE;
			case PLAYERCLASS_NINJA:
				return INFWEAPON_NINJA_GRENADE;
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
