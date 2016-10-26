/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>
#include "hero-flag.h"

CHeroFlag::CHeroFlag(CGameWorld *pGameWorld)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_HEROFLAG)
{
	m_ProximityRadius = ms_PhysSize;
	m_Hidden = true;
	GameWorld()->InsertEntity(this);
}

void CHeroFlag::Hide()
{
	m_Hidden = true;
}

void CHeroFlag::Show()
{
	if(m_Hidden)
	{
		m_Hidden = false;
		FindPosition();
		m_CoolDownTick = 0;
	}
}

void CHeroFlag::FindPosition()
{
	int NbPos = GameServer()->m_pController->HeroFlagPositions().size();
	int Index = rand()%NbPos;
	
	m_Pos = GameServer()->m_pController->HeroFlagPositions()[Index];
}

void CHeroFlag::GiveGift(CCharacter* pHero)
{
	// Find other players	
	GameServer()->SendBroadcast_Language(-1, "The Hero found the flag!", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
	
	m_CoolDownTick = Server()->TickSpeed()*15;
	
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(p->IsInfected())
			continue;
		
		p->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		
		if(p == pHero)
		{
			p->IncreaseHealth(10);
			p->IncreaseArmor(10);
		}
		else
		{
			p->IncreaseHealth(1);
			p->IncreaseArmor(4);
			
			//Special gift
			switch(p->GetClass())
			{
				case PLAYERCLASS_ENGINEER:
					p->GiveWeapon(WEAPON_RIFLE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_RIFLE)));
					break;
				case PLAYERCLASS_SOLDIER:
					p->GiveWeapon(WEAPON_GRENADE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_GRENADE)));
					break;
				case PLAYERCLASS_SCIENTIST:
					p->GiveWeapon(WEAPON_GRENADE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_GRENADE)));
					p->GiveWeapon(WEAPON_RIFLE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_RIFLE)));
					break;
				case PLAYERCLASS_MEDIC:
					p->GiveWeapon(WEAPON_SHOTGUN, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_SHOTGUN)));
					break;
				case PLAYERCLASS_HERO:
					p->GiveWeapon(WEAPON_SHOTGUN, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_SHOTGUN)));
					break;
				case PLAYERCLASS_NINJA:
					p->GiveWeapon(WEAPON_GRENADE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_GRENADE)));
					break;
				case PLAYERCLASS_SNIPER:
					p->GiveWeapon(WEAPON_RIFLE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_RIFLE)));
					break;
				case PLAYERCLASS_MERCENARY:
					p->GiveWeapon(WEAPON_GUN, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_GUN)));
					p->GiveWeapon(WEAPON_GRENADE, Server()->GetMaxAmmo(p->GetInfWeaponID(WEAPON_GRENADE)));
					break;
			}
		}
	}
}

void CHeroFlag::Tick()
{
	if(m_CoolDownTick <= 0)
	{
		// Find other players
		int NbPlayer = 0;
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			NbPlayer++;
		}
		
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(p->GetClass() != PLAYERCLASS_HERO)
				continue;

			float Len = distance(p->m_Pos, m_Pos);
			if(Len < m_ProximityRadius + p->m_ProximityRadius)
			{
				FindPosition();
				GiveGift(p);
				
				if(NbPlayer > 3)
				{
					Server()->RoundStatistics()->OnScoreEvent(p->GetPlayer()->GetCID(), SCOREEVENT_HERO_FLAG, p->GetClass());
					GameServer()->SendScoreSound(p->GetPlayer()->GetCID());
				}
				break;
			}
		}
	}
	else
		m_CoolDownTick--;
	
}

void CHeroFlag::Snap(int SnappingClient)
{
	if(m_CoolDownTick > 0)
		return;
	
	if(NetworkClipped(SnappingClient))
		return;
	
	CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
	if(pClient->GetClass() != PLAYERCLASS_HERO)
		return;

	CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, TEAM_BLUE, sizeof(CNetObj_Flag));
	if(!pFlag)
		return;

	pFlag->m_X = (int)m_Pos.x;
	pFlag->m_Y = (int)m_Pos.y+16.0f;
	pFlag->m_Team = TEAM_BLUE;
}
