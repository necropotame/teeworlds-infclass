/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>
#include "hero-flag.h"

CHeroFlag::CHeroFlag(CGameWorld *pGameWorld)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_HERO_FLAG)
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
	int Index = random_int(0, NbPos-1);
	
	m_Pos = GameServer()->m_pController->HeroFlagPositions()[Index];
}

void CHeroFlag::GiveGift(CCharacter* pHero)
{
	// Find other players	
	GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The Hero found the flag!"), NULL);
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
	m_CoolDownTick = Server()->TickSpeed()*15;
	
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(p->IsInfected())
			continue;
		
		p->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_MUSIC);
		
		if(p == pHero)
		{
			p->IncreaseHealth(10);
			p->IncreaseArmor(10);
		}
		else
		{
			p->GiveGift(GIFT_HEROFLAG);
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
