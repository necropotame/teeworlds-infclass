/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <iostream>
#include <engine/shared/config.h>
#include "player.h"
#include <engine/shared/network.h>
#include <engine/server/roundstatistics.h>


MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	m_pCharacter = 0;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();
	
/* INFECTION MODIFICATION START ***************************************/
	m_Authed = IServer::AUTHED_NO;
	m_ScoreRound = 0;
	m_ScoreMode = PLAYERSCOREMODE_SCORE;
	m_WinAsHuman = 0;
	m_class = PLAYERCLASS_NONE;
	m_InfectionTick = -1;
	SetLanguage(Server()->GetClientLanguage(ClientID));
	for(int i=0; i<NB_PLAYERCLASS; i++)
	{
		m_knownClass[i] = false;

	int* idMap = Server()->GetIdMap(ClientID);
	for (int i = 1;i < VANILLA_MAX_CLIENTS;i++)
	{
	    idMap[i] = -1;
	}
	idMap[0] = ClientID;

	}
	m_WasHumanThisRound = false;
	
	m_MapMenu = 0;
	m_MapMenuItem = -1;
	m_MapMenuTick = -1;
	m_HookProtectionAutomatic = true;
	
	m_PrevTuningParams = *pGameServer->Tuning();
	m_NextTuningParams = m_PrevTuningParams;
	m_IsInGame = false;
	
	for(unsigned int i=0; i<sizeof(m_LastHumanClasses)/sizeof(int); i++)
		m_LastHumanClasses[i] = -1;

  m_VoodooIsSpirit = false;
/* INFECTION MODIFICATION END *****************************************/
}

CPlayer::~CPlayer()
{
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	Server()->SetClientLanguage(m_ClientID, m_aLanguage);

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(!GameServer()->m_World.m_Paused)
	{
		if(!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
			m_ViewPos -= vec2(clamp(m_ViewPos.x-m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y-m_LatestActivity.m_TargetY, -400.0f, 400.0f));

		if(!m_pCharacter && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
			m_Spawning = true;

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
			{
				m_ViewPos = m_pCharacter->m_Pos;
			}
			else
			{
				m_pCharacter->Destroy();
				delete m_pCharacter;
				m_pCharacter = 0;
			}
		}
		else if(m_Spawning && m_RespawnTick <= Server()->Tick())
			TryRespawn();
		
		if(!IsInfected()) m_HumanTime++;
	}
	else
	{
		++m_RespawnTick;
		++m_DieTick;
		++m_ScoreStartTick;
		++m_LastActionTick;
		++m_TeamChangeTick;
 	}

	if(m_MapMenu > 0)
		m_MapMenuTick++;
 	
	if(GetClass() == PLAYERCLASS_GHOUL)
	{
		if(m_GhoulLevel > 0)
		{
			m_GhoulLevelTick--;
			
			if(m_GhoulLevelTick <= 0)
			{
				m_GhoulLevelTick = (Server()->TickSpeed()*g_Config.m_InfGhoulDigestion);
				IncreaseGhoulLevel(-1);
			}
		}
		
		SetClassSkin(PLAYERCLASS_GHOUL, m_GhoulLevel);
	}
	else if (GetClass() == PLAYERCLASS_VOODOO)
	{
		if(m_VoodooIsSpirit)
		{
			SetClassSkin(PLAYERCLASS_VOODOO, 0); // 0 = spirit skin
		}
		else
		{
			SetClassSkin(PLAYERCLASS_VOODOO, 1); // 1 = normal skin
		}
  }


	
 	HandleTuningParams();
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if(m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::HandleTuningParams()
{
	if(!(m_PrevTuningParams == m_NextTuningParams))
	{
		if(m_IsReady)
		{
			CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
			int *pParams = (int *)&m_NextTuningParams;
			for(unsigned i = 0; i < sizeof(m_NextTuningParams)/sizeof(int); i++)
				Msg.AddInt(pParams[i]);
			Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());
		}
		
		m_PrevTuningParams = m_NextTuningParams;
	}
	
	m_NextTuningParams = *GameServer()->Tuning();
}

void CPlayer::HookProtection(bool Value, bool Automatic)
{
	if(m_HookProtection != Value)
	{
		m_HookProtection = Value;
		
		if(!m_HookProtectionAutomatic || !Automatic)
		{
			if(m_HookProtection)
				GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_DEFAULT, _("Hook protection enabled"), NULL);
			else
				GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_DEFAULT, _("Hook protection disabled"), NULL);
		}
	}
	
	m_HookProtectionAutomatic = Automatic;
}

void CPlayer::Snap(int SnappingClient)
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	int id = m_ClientID;
	if (!Server()->Translate(id, SnappingClient)) return;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));

	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	
	int SnapScoreMode = PLAYERSCOREMODE_SCORE;
	if(GameServer()->m_apPlayers[SnappingClient])
	{
		SnapScoreMode = GameServer()->m_apPlayers[SnappingClient]->GetScoreMode();
	}
	
/* INFECTION MODIFICATION STRAT ***************************************/
	int PlayerInfoScore = 0;
	
	if(GetTeam() == TEAM_SPECTATORS)
	{
		StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	}
	else
	{
		if(SnapScoreMode == PLAYERSCOREMODE_TIME)
		{
			float RoundDuration = static_cast<float>(m_HumanTime/((float)Server()->TickSpeed()))/60.0f;
			int Minutes = static_cast<int>(RoundDuration);
			int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes,((Seconds < 10) ? "0" : ""), Seconds);
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			
			PlayerInfoScore = m_HumanTime/Server()->TickSpeed();
		}
		else
		{
			char aClanName[12];
			switch(GetClass())
			{
				case PLAYERCLASS_ENGINEER:
					str_format(aClanName, sizeof(aClanName), "%sEngineer", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SOLDIER:
					str_format(aClanName, sizeof(aClanName), "%sSoldier", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_MERCENARY:
					str_format(aClanName, sizeof(aClanName), "%sMercenary", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SNIPER:
					str_format(aClanName, sizeof(aClanName), "%sSniper", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SCIENTIST:
					str_format(aClanName, sizeof(aClanName), "%sScientist", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_BIOLOGIST:
					str_format(aClanName, sizeof(aClanName), "%sBiologist", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_MEDIC:
					str_format(aClanName, sizeof(aClanName), "%sMedic", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_HERO:
					str_format(aClanName, sizeof(aClanName), "%sHero", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_NINJA:
					str_format(aClanName, sizeof(aClanName), "%sNinja", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SMOKER:
					str_format(aClanName, sizeof(aClanName), "%sSmoker", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_BOOMER:
					str_format(aClanName, sizeof(aClanName), "%sBoomer", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_HUNTER:
					str_format(aClanName, sizeof(aClanName), "%sHunter", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_BAT:
					str_format(aClanName, sizeof(aClanName), "%sBat", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_GHOST:
					str_format(aClanName, sizeof(aClanName), "%sGhost", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SPIDER:
					str_format(aClanName, sizeof(aClanName), "%sSpider", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_GHOUL:
					str_format(aClanName, sizeof(aClanName), "%sGhoul", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SLUG:
					str_format(aClanName, sizeof(aClanName), "%sSlug", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_VOODOO:
					str_format(aClanName, sizeof(aClanName), "%sVoodoo", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_UNDEAD:
					str_format(aClanName, sizeof(aClanName), "%sUndead", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_WITCH:
					str_format(aClanName, sizeof(aClanName), "%sWitch", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				default:
					str_format(aClanName, sizeof(aClanName), "%s?????", Server()->IsClientLogged(GetCID()) ? "@" : " ");
			}
			
			StrToInts(&pClientInfo->m_Clan0, 3, aClanName);
			
			PlayerInfoScore = Server()->RoundStatistics()->PlayerScore(m_ClientID);
		}
	}
	
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

	if(
		GameServer()->m_apPlayers[SnappingClient] && !IsInfected() &&
		(
			(Server()->GetClientCustomSkin(SnappingClient) == 1 && SnappingClient == GetCID()) ||
			(Server()->GetClientCustomSkin(SnappingClient) == 2)
		)
	)
	{
		StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_CustomSkinName);
	}
	else StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
	
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;
/* INFECTION MODIFICATION END *****************************************/

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, id, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = id;
/* INFECTION MODIFICATION START ***************************************/
	pPlayerInfo->m_Score = PlayerInfoScore;
/* INFECTION MODIFICATION END *****************************************/
	pPlayerInfo->m_Team = m_Team;

	if(m_ClientID == SnappingClient)
		pPlayerInfo->m_Local = 1;

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::FakeSnap(int SnappingClient)
{
	IServer::CClientInfo info;
	Server()->GetClientInfo(SnappingClient, &info);
	if (info.m_CustClt)
		return;

	int id = VANILLA_MAX_CLIENTS - 1;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));

	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, " ");
	StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
}

void CPlayer::OnDisconnect(int Type, const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		if(Type == CLIENTDROPTYPE_BAN)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has been banned ({str:Reason})"),
				"PlayerName", Server()->ClientName(m_ClientID),
				"Reason", pReason,
				NULL);
		}
		else if(Type == CLIENTDROPTYPE_KICK)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has been kicked ({str:Reason})"),
				"PlayerName", Server()->ClientName(m_ClientID),
				"Reason", pReason,
				NULL);
		}
		else if(pReason && *pReason)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has left the game ({str:Reason})"),
				"PlayerName", Server()->ClientName(m_ClientID),
				"Reason", pReason,
				NULL);
		}
		else
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has left the game"),
				"PlayerName", Server()->ClientName(m_ClientID),
				NULL);
		}
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags&PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags&PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
 		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire&1))
		m_Spawning = true;

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire&1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::Respawn()
{
	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	// clamp the team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;

	char aBuf[512];
	if(DoChatMsg)
	{
		if(Team == TEAM_SPECTATORS)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} joined the spectators"), "PlayerName", Server()->ClientName(m_ClientID), NULL);
			GameServer()->AddSpectatorCID(m_ClientID);
			Server()->InfecteClient(m_ClientID);
		}
		else
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} joined the game"), "PlayerName", Server()->ClientName(m_ClientID), NULL);
		}
	}

	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;

/* INFECTION MODIFICATION START ***************************************/
	if(!GameServer()->m_pController->PreSpawn(this, &SpawnPos))
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World, GameServer()->Console());
	m_pCharacter->Spawn(this, SpawnPos);
	if(GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
}

/* INFECTION MODIFICATION START ***************************************/
int CPlayer::GetClass()
{
	return m_class;
}

void CPlayer::SetClassSkin(int newClass, int State)
{
	switch(newClass)
	{
		case PLAYERCLASS_ENGINEER:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "limekitty", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_SOLDIER:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "brownbear", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_SNIPER:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "warpaint", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_MERCENARY:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "bluestripe", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_SCIENTIST:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "toptri", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_BIOLOGIST:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "twintri", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_MEDIC:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "twinbop", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_HERO:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "redstripe", sizeof(m_TeeInfos.m_SkinName));
			break;
		case PLAYERCLASS_NINJA:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "default", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 255;
			m_TeeInfos.m_ColorFeet = 0;
			break;
		case PLAYERCLASS_SMOKER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "cammostripes", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_BOOMER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "saddo", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_HUNTER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "warpaint", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_BAT:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "limekitty", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 2866368;
			m_TeeInfos.m_ColorFeet = 3866368;
			break;
		case PLAYERCLASS_GHOST:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "twintri", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_SPIDER:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "pinky", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_GHOUL:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "cammo", sizeof(m_TeeInfos.m_SkinName));
			{
				int Hue = 58 * (1.0f - clamp(State/static_cast<float>(g_Config.m_InfGhoulStomachSize), 0.0f, 1.0f));
				m_TeeInfos.m_ColorBody = (Hue<<16) + (255<<8);
			}
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_SLUG:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "coala", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3866368;
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_VOODOO:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "bluestripe", sizeof(m_TeeInfos.m_SkinName));
			if(State == 1)
			{
				m_TeeInfos.m_ColorBody = 3866368;
			}
			else
			{
				m_TeeInfos.m_ColorBody = 6183936; // grey-green
			}
			m_TeeInfos.m_ColorFeet = 65414;
			break;
		case PLAYERCLASS_UNDEAD:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "redstripe", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 3014400;
			m_TeeInfos.m_ColorFeet = 13168;
			break;
		case PLAYERCLASS_WITCH:
			m_TeeInfos.m_UseCustomColor = 1;
			str_copy(m_TeeInfos.m_SkinName, "redbopp", sizeof(m_TeeInfos.m_SkinName));
			m_TeeInfos.m_ColorBody = 16776744;
			m_TeeInfos.m_ColorFeet = 13168;
			break;
		default:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "default", sizeof(m_TeeInfos.m_SkinName));
			Server()->SetClientClan(GetCID(), "");
	}
}

void CPlayer::SetClass(int newClass)
{	
	if(m_class == newClass)
		return;
	
	if(newClass > START_HUMANCLASS && newClass < END_HUMANCLASS)
	{
		bool ClassFound = false;
		for(unsigned int i=0; i<sizeof(m_LastHumanClasses)/sizeof(int); i++)
		{
			if(m_LastHumanClasses[i] == newClass)
				ClassFound = true;
		}
		if(!ClassFound)
		{
			for(unsigned int i=0; i<sizeof(m_LastHumanClasses)/sizeof(int)-1; i++)
			{
				m_LastHumanClasses[i] = m_LastHumanClasses[i+1];
			}
			m_LastHumanClasses[sizeof(m_LastHumanClasses)/sizeof(int)-1] = newClass;
		}
	}
	
	m_GhoulLevel = 0;
	m_GhoulLevelTick = 0;
	
	m_class = newClass;
	
	if(m_class < END_HUMANCLASS)
		HookProtection(true);
	else
		HookProtection(true); // true = hook protection for zombies by default
	
	SetClassSkin(newClass);
	
	if(m_pCharacter)
	{
		m_pCharacter->SetClass(newClass);
	}
}

int CPlayer::GetOldClass()
{
	bool hasOldClass = false;
	for (int i = START_HUMANCLASS+1; i < END_HUMANCLASS; i++) {
		if (m_classOld == i) {
			hasOldClass = true;
			break;
		}
	}
	if (!hasOldClass)
		return PLAYERCLASS_MEDIC; // if old class was not set, it defaults to medic
	else
		return m_classOld;
}

void CPlayer::SetOldClass(int oldClass)
{
	m_classOld = oldClass;
}

void CPlayer::StartInfection(bool force)
{
	if(!force && IsInfected())
		return;
	
	
	if(!IsInfected())
	{
		m_InfectionTick = Server()->Tick();
	}
	
	int c = GameServer()->m_pController->ChooseInfectedClass(this);
	
	SetClass(c);
}

bool CPlayer::IsInfected() const
{
	return (m_class > END_HUMANCLASS);
}

bool CPlayer::IsKownClass(int c)
{
	return m_knownClass[c];
}

int CPlayer::GetScoreMode()
{
	return m_ScoreMode;
}

void CPlayer::SetScoreMode(int Mode)
{
	m_ScoreMode = Mode;
}

const char* CPlayer::GetLanguage()
{
	return m_aLanguage;
}

void CPlayer::SetLanguage(const char* pLanguage)
{
	str_copy(m_aLanguage, pLanguage, sizeof(m_aLanguage));
}
void CPlayer::OpenMapMenu(int Menu)
{
	m_MapMenu = Menu;
	m_MapMenuTick = 0;
}

void CPlayer::CloseMapMenu()
{
	m_MapMenu = 0;
	m_MapMenuTick = -1;
}

bool CPlayer::MapMenuClickable()
{
	return (m_MapMenu > 0 && (m_MapMenuTick > Server()->TickSpeed()/2));
}

float CPlayer::GetGhoulPercent()
{
	return clamp(m_GhoulLevel/static_cast<float>(g_Config.m_InfGhoulStomachSize), 0.0f, 1.0f);
}

void CPlayer::IncreaseGhoulLevel(int Diff)
{
	int NewGhoulLevel = m_GhoulLevel + Diff;
	if(NewGhoulLevel < 0)
		NewGhoulLevel = 0;
	if(NewGhoulLevel > g_Config.m_InfGhoulStomachSize)
		NewGhoulLevel = g_Config.m_InfGhoulStomachSize;
	
	m_GhoulLevel = NewGhoulLevel;
}

void CPlayer::SetToSpirit(bool IsSpirit)
{
	m_VoodooIsSpirit = IsSpirit;
}

/* INFECTION MODIFICATION END *****************************************/
