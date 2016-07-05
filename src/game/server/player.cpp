/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <iostream>
#include <engine/shared/config.h>
#include "player.h"
#include "engine/shared/network.h"


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
	m_Score = Server()->GetClientScore(ClientID);
	m_NbRound = Server()->GetClientNbRound(ClientID);
	m_NbInfection = Server()->GetClientNbInfection(ClientID);
	m_ScoreRound = 0;
	m_ScoreMode = PLAYERSCOREMODE_SCORE;
	m_WinAsHuman = 0;
	m_class = PLAYERCLASS_NONE;
	m_InfectionTick = -1;
	m_Language = Server()->GetClientLanguage(ClientID);
	for(int i=0; i<NB_PLAYERCLASS; i++)
	{
		m_knownClass[i] = false;
	}
	m_WasHumanThisRound = false;
	
	m_InClassChooserMenu = 0;
	m_MenuClassChooserItem = -1;
	
	m_PrevTuningParams = *pGameServer->Tuning();
	m_NextTuningParams = m_PrevTuningParams;
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

	Server()->SetClientScore(m_ClientID, m_Score);
	Server()->SetClientNbRound(m_ClientID, m_NbRound);
	Server()->SetClientNbInfection(m_ClientID, m_NbInfection);
	Server()->SetClientLanguage(m_ClientID, m_Language);

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
				GameServer()->SendChatTarget_Language(GetCID(), "Hook protection enabled");
			else
				GameServer()->SendChatTarget_Language(GetCID(), "Hook protection disabled");
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

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, m_ClientID, sizeof(CNetObj_ClientInfo)));
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
		if(SnapScoreMode == PLAYERSCOREMODE_ROUNDSCORE)
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%s%i pt", (m_ScoreRound > 0 ? "+" : ""), m_ScoreRound);
			
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			
			PlayerInfoScore = m_ScoreRound;
		}
		else if(SnapScoreMode == PLAYERSCOREMODE_TIME)
		{
			float RoundDuration = static_cast<float>(m_HumanTime/((float)Server()->TickSpeed()))/60.0f;
			int Minutes = static_cast<int>(RoundDuration);
			int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes,((Seconds < 10) ? "0" : ""), Seconds);
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			
			PlayerInfoScore = m_HumanTime/Server()->TickSpeed();
		}
		//~ else if(SnapScoreMode == PLAYERSCOREMODE_NBROUND)
		//~ {
			//~ char aBuf[512];
			//~ str_format(aBuf, sizeof(aBuf), "%i round%s", m_NbRound, (m_NbRound >1 ? "s" : ""));
			//~ 
			//~ StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			//~ 
			//~ PlayerInfoScore = m_NbRound;
		//~ }
		else if(SnapScoreMode == PLAYERSCOREMODE_SCOREPERROUND)
		{
			float ScorePerRound = static_cast<float>(m_Score)/static_cast<float>(m_NbRound);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%.2f pt/rnd", ScorePerRound);
			
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			
			PlayerInfoScore = static_cast<int>(ScorePerRound*10);
		}
		else if(SnapScoreMode == PLAYERSCOREMODE_INFECTION)
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%i inf", m_NbInfection);
			
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			
			PlayerInfoScore = m_NbInfection;
		}
		else if(SnapScoreMode == PLAYERSCOREMODE_INFECTIONPERROUND)
		{
			float InfPerRound = static_cast<float>(m_NbInfection)/static_cast<float>(m_NbRound);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%.2f inf/rnd", InfPerRound);
			
			StrToInts(&pClientInfo->m_Clan0, 3, aBuf);
			
			PlayerInfoScore = static_cast<int>(InfPerRound*10);
		}
		else
		{
			char aClanName[12];
			switch(GetClass())
			{
				case PLAYERCLASS_ENGINEER:
					str_format(aClanName, sizeof(aClanName), "%s%sEngineer", m_WinAsHuman ? "*" : " ", Server()->IsClientLogged(GetCID()) ? "@" : " ");
					break;
				case PLAYERCLASS_SOLDIER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Soldier*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Soldier");
					break;
				case PLAYERCLASS_MERCENARY:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Mercenary*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Mercenary");
					break;
				case PLAYERCLASS_SNIPER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Sniper*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Sniper");
					break;
				case PLAYERCLASS_SCIENTIST:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Scientist*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Scientist");
					break;
				case PLAYERCLASS_MEDIC:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Medic*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Medic");
					break;
				case PLAYERCLASS_NINJA:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Ninja*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Ninja");
					break;
				case PLAYERCLASS_SMOKER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Smoker*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Smoker");
					break;
				case PLAYERCLASS_BOOMER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Boomer*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Boomer");
					break;
				case PLAYERCLASS_HUNTER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Hunter*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Hunter");
					break;
				case PLAYERCLASS_GHOST:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Ghost*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Ghost");
					break;
				case PLAYERCLASS_SPIDER:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Spider*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Spider");
					break;
				case PLAYERCLASS_UNDEAD:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Undead*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Undead");
					break;
				case PLAYERCLASS_WITCH:
					if(m_WinAsHuman) StrToInts(&pClientInfo->m_Clan0, 3, "Witch*");
					else StrToInts(&pClientInfo->m_Clan0, 3, "Witch");
					break;
				default:
					StrToInts(&pClientInfo->m_Clan0, 3, "");
			}
			
			if(SnapScoreMode == PLAYERSCOREMODE_CLASS)
			{
				switch(GetClass())
				{
					case PLAYERCLASS_MEDIC:
						PlayerInfoScore = 700 + GetCID();
						break;
						
					case PLAYERCLASS_ENGINEER:
						PlayerInfoScore = 600 + GetCID();
						break;
					case PLAYERCLASS_SOLDIER:
						PlayerInfoScore = 500 + GetCID();
						break;
					case PLAYERCLASS_SCIENTIST:
						PlayerInfoScore = 400 + GetCID();
						break;
						
					case PLAYERCLASS_NINJA:
						PlayerInfoScore = 300 + GetCID();
						break;
						
					case PLAYERCLASS_MERCENARY:
						PlayerInfoScore = 200 + GetCID();
						break;
					case PLAYERCLASS_SNIPER:
						PlayerInfoScore = 100 + GetCID();
						break;
						
					case PLAYERCLASS_NONE:
						PlayerInfoScore = GetCID();
						break;
						
					case PLAYERCLASS_SMOKER:
						PlayerInfoScore = -100 - GetCID();
						break;
					case PLAYERCLASS_BOOMER:
						PlayerInfoScore = -200 - GetCID();
						break;
					case PLAYERCLASS_HUNTER:
						PlayerInfoScore = -300 - GetCID();
						break;
					case PLAYERCLASS_GHOST:
						PlayerInfoScore = -400 - GetCID();
						break;
					case PLAYERCLASS_SPIDER:
						PlayerInfoScore = -500 - GetCID();
						break;
					case PLAYERCLASS_UNDEAD:
						PlayerInfoScore = -600 - GetCID();
						break;
					case PLAYERCLASS_WITCH:
						PlayerInfoScore = -700 - GetCID();
						break;
					default:
						PlayerInfoScore = -999;
				}
			}
			else
				PlayerInfoScore = m_Score;
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

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, m_ClientID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = m_ClientID;
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

void CPlayer::OnDisconnect(int Type, const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		if(Type == CLIENTDROPTYPE_BAN)
		{
			GameServer()->SendChatTarget_Language_ss(-1, "%s has been banned (%s)", Server()->ClientName(m_ClientID), pReason);
		}
		else if(Type == CLIENTDROPTYPE_KICK)
		{
			GameServer()->SendChatTarget_Language_ss(-1, "%s has been kicked (%s)", Server()->ClientName(m_ClientID), pReason);
		}
		else if(pReason && *pReason)
		{
			GameServer()->SendChatTarget_Language_ss(-1, "%s has left the game (%s)", Server()->ClientName(m_ClientID), pReason);
		}
		else
		{
			GameServer()->SendChatTarget_Language_s(-1, "%s has left the game", Server()->ClientName(m_ClientID));
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
			GameServer()->SendChatTarget_Language_s(-1, "%s joined the spectators", Server()->ClientName(m_ClientID));
		}
		else
		{
			GameServer()->SendChatTarget_Language_s(-1, "%s joined the game", Server()->ClientName(m_ClientID));
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
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);
	if(GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
}

/* INFECTION MODIFICATION START ***************************************/
int CPlayer::GetClass()
{
	return m_class;
}

void CPlayer::SetClassSkin(int newClass)
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
		case PLAYERCLASS_MEDIC:
			m_TeeInfos.m_UseCustomColor = 0;
			str_copy(m_TeeInfos.m_SkinName, "twinbop", sizeof(m_TeeInfos.m_SkinName));
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
		
	m_class = newClass;
	
	if(m_class < END_HUMANCLASS)
		HookProtection(true);
	else
		HookProtection(false);
	
	SetClassSkin(newClass);
	
	if(m_pCharacter)
	{
		m_pCharacter->SetClass(newClass);
	}
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

void CPlayer::IncreaseScore(int Points)
{
	m_Score += Points;
	m_ScoreRound += Points;
}

void CPlayer::IncreaseNbRound()
{
	m_NbRound++;
}

void CPlayer::IncreaseNbInfection(int Points)
{
	m_NbInfection++;
}

int CPlayer::GetScoreMode()
{
	return m_ScoreMode;
}

void CPlayer::SetScoreMode(int Mode)
{
	m_ScoreMode = Mode;
}

int CPlayer::GetLanguage()
{
	return m_Language;
}

void CPlayer::SetLanguage(int Language)
{
	m_Language = Language;
}
/* INFECTION MODIFICATION END *****************************************/
