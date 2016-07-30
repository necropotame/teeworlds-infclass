/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/storage.h>
#include <engine/server/roundstatistics.h>
#include <engine/server/sql_server.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <iostream>
#include "gamemodes/mod.h"
#include <game/arabicinputconverter.h>

enum
{
	RESET,
	NO_RESET
};

/* INFECTION MODIFICATION START ***************************************/

bool CGameContext::s_ServerLocalizationInitialized = false;
CLocalizationDatabase CGameContext::s_ServerLocalization[NUM_TRANSLATED_LANGUAGES];

void CGameContext::InitializeServerLocatization()
{
	if(!s_ServerLocalizationInitialized)
	{
		s_ServerLocalization[LANGUAGE_FR].Load("languages/infclass/fr.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_DE].Load("languages/infclass/de.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_UK].Load("languages/infclass/uk.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_RU].Load("languages/infclass/ru.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_IT].Load("languages/infclass/it.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_ES].Load("languages/infclass/es.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_AR].Load("languages/infclass/ar.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_HU].Load("languages/infclass/hu.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_PL].Load("languages/infclass/pl.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_NL].Load("languages/infclass/nl.txt", Storage(), Console());
		s_ServerLocalization[LANGUAGE_LA].Load("languages/infclass/la.txt", Storage(), Console());
		
		s_ServerLocalizationInitialized = true;
	}
}

const char* CGameContext::ServerLocalize(const char *pStr, int Language)
{
	const char* pNewStr = 0;
	
	if(Language >= 0 && Language < NUM_TRANSLATED_LANGUAGES)
		pNewStr = s_ServerLocalization[Language].FindString(str_quickhash(pStr));
	
	if(pNewStr == 0)
		pNewStr = pStr;
	
	return pNewStr;
}

void CGameContext::OnSetAuthed(int ClientID, int Level)
{
	if(m_apPlayers[ClientID])
		m_apPlayers[ClientID]->m_Authed = Level;
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_apPlayers[i] = 0;
		m_aHitSoundState[i] = 0;
	}

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;
	
	m_ChatResponseTargetID = -1;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < m_LaserDots.size(); i++)
		Server()->SnapFreeID(m_LaserDots[i].m_SnapID);
	
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
}

void CGameContext::Clear()
{	
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
	
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		m_BroadcastStates[i].m_NoChangeTick = 0;
		m_BroadcastStates[i].m_LifeSpanTick = 0;
		m_BroadcastStates[i].m_Priority = BROADCAST_PRIORITY_LOWEST;
		m_BroadcastStates[i].m_PrevMessage[0] = 0;
		m_BroadcastStates[i].m_NextMessage[0] = 0;
	}
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateLaserDotEvent(vec2 Pos0, vec2 Pos1, int LifeSpan)
{
	CGameContext::LaserDotState State;
	State.m_Pos0 = Pos0;
	State.m_Pos1 = Pos1;
	State.m_LifeSpan = LifeSpan;
	State.m_SnapID = Server()->SnapNewID();
	
	m_LaserDots.add(State);
}

void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int TakeDamageMode)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l;
			if((int)Dmg)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon, TakeDamageMode);
		}
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}

void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
}

/* INFECTION MODIFICATION START ***************************************/
void CGameContext::SendChatTarget_Language(int To, const char* pText)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Msg.m_pMessage = ServerLocalize(pText, m_apPlayers[i]->GetLanguage());
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
}

void CGameContext::SendChatTarget_Language_s(int To, const char* pText, const char* pParam)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	char aBuf[512];
	Msg.m_pMessage = aBuf;
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), pParam);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
}

void CGameContext::SendChatTarget_Language_ss(int To, const char* pText, const char* pParam1, const char* pParam2)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	char aBuf[512];
	Msg.m_pMessage = aBuf;
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), pParam1, pParam2);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
}

void CGameContext::SendChatTarget_Language_sss(int To, const char* pText, const char* pParam1, const char* pParam2, const char* pParam3)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	char aBuf[512];
	Msg.m_pMessage = aBuf;
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), pParam1, pParam2, pParam3);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
}

void CGameContext::SendChatTarget_Language_i(int To, const char* pText, int Param)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	char aBuf[512];
	Msg.m_pMessage = aBuf;
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), Param);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
}

void CGameContext::SendChatTarget_Language_ii(int To, const char* pText, int Param1, int Param2)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	char aBuf[512];
	Msg.m_pMessage = aBuf;
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), Param1, Param2);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
}

void CGameContext::SendMOTD(int To, const char* pText)
{
	if(m_apPlayers[To])
	{
		CNetMsg_Sv_Motd Msg;
		
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendMOTD_Language(int To, const char* pText)
{
	if(m_apPlayers[To])
	{
		CNetMsg_Sv_Motd Msg;
		
		Msg.m_pMessage = ServerLocalize(pText, m_apPlayers[To]->GetLanguage());
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendMOTD_Language_s(int To, const char* pText, const char* pParam)
{
	if(m_apPlayers[To])
	{
		CNetMsg_Sv_Motd Msg;
		
		char aBuf[512];
		Msg.m_pMessage = aBuf;
		
		str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[To]->GetLanguage()), pParam);
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::AddBroadcast(int ClientID, const char* pText, int Priority, int LifeSpan)
{
	if(LifeSpan > 0)
	{
		if(m_BroadcastStates[ClientID].m_TimedPriority > Priority)
			return;
			
		str_copy(m_BroadcastStates[ClientID].m_TimedMessage, pText, sizeof(m_BroadcastStates[ClientID].m_TimedMessage));
		m_BroadcastStates[ClientID].m_LifeSpanTick = LifeSpan;
		m_BroadcastStates[ClientID].m_TimedPriority = Priority;
	}
	else
	{
		if(m_BroadcastStates[ClientID].m_Priority > Priority)
			return;
			
		str_copy(m_BroadcastStates[ClientID].m_NextMessage, pText, sizeof(m_BroadcastStates[ClientID].m_NextMessage));
		m_BroadcastStates[ClientID].m_Priority = Priority;
	}
}

void CGameContext::SendBroadcast(int To, const char *pText, int Priority, int LifeSpan)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
			AddBroadcast(i, pText, Priority, LifeSpan);
	}
}

void CGameContext::ClearBroadcast(int To, int Priority)
{
	SendBroadcast(To, "", Priority, BROADCAST_DURATION_REALTIME);
}

void CGameContext::SendBroadcast_Language(int To, const char* pText, int Priority, int LifeSpan)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			const char* pTranslatedText = ServerLocalize(pText, m_apPlayers[i]->GetLanguage());
			AddBroadcast(i, pTranslatedText, Priority, LifeSpan);
		}
	}
}

void CGameContext::SendBroadcast_Language_s(int To, const char* pText, const char* pParam, int Priority, int LifeSpan)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	char aBuf[512];
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), pParam);
			AddBroadcast(i, aBuf, Priority, LifeSpan);
		}
	}
}

void CGameContext::SendBroadcast_Language_i(int To, const char* pText, int Param, int Priority, int LifeSpan)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	char aBuf[512];
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			str_format(aBuf, sizeof(aBuf), ServerLocalize(pText, m_apPlayers[i]->GetLanguage()), Param);
			AddBroadcast(i, aBuf, Priority, LifeSpan);
		}
	}
}

void CGameContext::SendBroadcast_ClassIntro(int ClientID, int Class)
{
	const char* pClassName = 0;
	
	switch(Class)
	{
		case PLAYERCLASS_ENGINEER:
			pClassName = ServerLocalize("Engineer", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_SOLDIER:
			pClassName = ServerLocalize("Soldier", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_MEDIC:
			pClassName = ServerLocalize("Medic", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_NINJA:
			pClassName = ServerLocalize("Ninja", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_MERCENARY:
			pClassName = ServerLocalize("Mercenary", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_SNIPER:
			pClassName = ServerLocalize("Sniper", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_SCIENTIST:
			pClassName = ServerLocalize("Scientist", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_SMOKER:
			pClassName = ServerLocalize("Smoker", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_HUNTER:
			pClassName = ServerLocalize("Hunter", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_BOOMER:
			pClassName = ServerLocalize("Boomer", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_GHOST:
			pClassName = ServerLocalize("Ghost", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_SPIDER:
			pClassName = ServerLocalize("Spider", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_WITCH:
			pClassName = ServerLocalize("Witch", m_apPlayers[ClientID]->GetLanguage());
			break;
		case PLAYERCLASS_UNDEAD:
			pClassName = ServerLocalize("Undead", m_apPlayers[ClientID]->GetLanguage());
			break;
		default:
			pClassName = ServerLocalize("?????", m_apPlayers[ClientID]->GetLanguage());
			break;
	}
	
	if(Class < END_HUMANCLASS)
		SendBroadcast_Language_s(ClientID, "You are a human: %s", pClassName, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
	else
		SendBroadcast_Language_s(ClientID, "You are an infected: %s", pClassName, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CGameContext::CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
/* INFECTION MODIFICATION START ***************************************/
			if(m_apPlayers[i])
			{
				int PlayerTeam = (m_apPlayers[i]->IsInfected() ? CGameContext::CHAT_RED : CGameContext::CHAT_BLUE );
				if(m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS) PlayerTeam = CGameContext::CHAT_SPEC;
				
				if(PlayerTeam == Team)
				{
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
				}
			}
/* INFECTION MODIFICATION END *****************************************/
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	std::cout << "SendVoteStatus" << std::endl;
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
	
	if(m_VoteCloseTime && m_VoteBanClientID == ClientID)
	{
		m_VoteCloseTime = -1;
		m_VoteBanClientID = -1;
	}
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;
	
	SendChat(-1, CGameContext::CHAT_ALL, "Teams were swapped");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_apPlayers[i]->SetTeam(m_apPlayers[i]->GetTeam()^1, false);
	}

	(void)m_pController->CheckTeamBalance();
}

void CGameContext::SendHitSound(int ClientID)
{
	if(m_aHitSoundState[ClientID] < 1)
	{
		m_aHitSoundState[ClientID] = 1;
	}
}

void CGameContext::SendScoreSound(int ClientID)
{
	m_aHitSoundState[ClientID] = 2;
}

void CGameContext::OnTick()
{
	for(int i=0; i<MAX_CLIENTS; i++)
	{		
		if(m_apPlayers[i])
		{			
			//Show top10
			if(!Server()->GetClientMemory(i, CLIENTMEMORY_TOP10))
			{
				if(!g_Config.m_SvMotd[0] || Server()->GetClientMemory(i, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE))
				{
					Server()->ShowTop10(i, SQL_SCORETYPE_ROUND_SCORE);
					Server()->SetClientMemory(i, CLIENTMEMORY_TOP10, true);
				}
			}
		}
	}
	
	//Check for banvote
	if(!m_VoteCloseTime)
	{
		for(int i=0; i<MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->IsInfected())
			{
				if(Server()->ClientShouldBeBanned(i))
				{
					char aDesc[VOTE_DESC_LENGTH] = {0};
					char aCmd[VOTE_CMD_LENGTH] = {0};
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(i, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
					str_format(aDesc, sizeof(aDesc), "Ban \"%s\"", Server()->ClientName(i));
					m_VoteBanClientID = i;
					StartVote(aDesc, aCmd, "");
					continue;
				}
			}
		}
	}
	
	// check tuning
	CheckPureTuning();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	int NumActivePlayers = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			if(m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				NumActivePlayers++;
			
			Server()->RoundStatistics()->UpdatePlayer(i, m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS);
			
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
			
			if(m_VoteLanguageTick[i] > 0)
			{
				if(m_VoteLanguageTick[i] == 1)
				{
					m_VoteLanguageTick[i] = 0;
					
					CNetMsg_Sv_VoteSet Msg;
					Msg.m_Timeout = 0;
					Msg.m_pDescription = "";
					Msg.m_pReason = "";
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
					
					m_VoteLanguage[i] = LANGUAGE_EN;				
					
				}
				else
				{
					m_VoteLanguageTick[i]--;
				}
			}
		}
	}
	
	//Check for new broadcast
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			if(m_BroadcastStates[i].m_LifeSpanTick > 0 && m_BroadcastStates[i].m_TimedPriority > m_BroadcastStates[i].m_Priority)
			{
				str_copy(m_BroadcastStates[i].m_NextMessage, m_BroadcastStates[i].m_TimedMessage, sizeof(m_BroadcastStates[i].m_NextMessage));
			}
			
			//Send broadcast only if the message is different, or to fight auto-fading
			if(
				str_comp(m_BroadcastStates[i].m_PrevMessage, m_BroadcastStates[i].m_NextMessage) != 0 ||
				m_BroadcastStates[i].m_NoChangeTick > Server()->TickSpeed()
			)
			{
				CNetMsg_Sv_Broadcast Msg;
				Msg.m_pMessage = m_BroadcastStates[i].m_NextMessage;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
				
				str_copy(m_BroadcastStates[i].m_PrevMessage, m_BroadcastStates[i].m_NextMessage, sizeof(m_BroadcastStates[i].m_PrevMessage));
				
				m_BroadcastStates[i].m_NoChangeTick = 0;
			}
			else
				m_BroadcastStates[i].m_NoChangeTick++;
			
			//Update broadcast state
			if(m_BroadcastStates[i].m_LifeSpanTick > 0)
				m_BroadcastStates[i].m_LifeSpanTick--;
			
			if(m_BroadcastStates[i].m_LifeSpanTick <= 0)
			{
				m_BroadcastStates[i].m_TimedMessage[0] = 0;
				m_BroadcastStates[i].m_TimedPriority = BROADCAST_PRIORITY_LOWEST;
			}
			m_BroadcastStates[i].m_NextMessage[0] = 0;
			m_BroadcastStates[i].m_Priority = BROADCAST_PRIORITY_LOWEST;
		}
		else
		{
			m_BroadcastStates[i].m_NoChangeTick = 0;
			m_BroadcastStates[i].m_LifeSpanTick = 0;
			m_BroadcastStates[i].m_Priority = BROADCAST_PRIORITY_LOWEST;
			m_BroadcastStates[i].m_TimedPriority = BROADCAST_PRIORITY_LOWEST;
			m_BroadcastStates[i].m_PrevMessage[0] = 0;
			m_BroadcastStates[i].m_NextMessage[0] = 0;
			m_BroadcastStates[i].m_TimedMessage[0] = 0;
		}
	}
	
	//Send score and hit sound
	for(int i=0; i<MAX_CLIENTS; i++)
	{		
		if(m_apPlayers[i])
		{
			int Sound = -1;
			if(m_aHitSoundState[i] == 1)
				Sound = SOUND_HIT;
			else if(m_aHitSoundState[i] == 2)
				Sound = SOUND_CTF_GRAB_PL;
			
			if(Sound >= 0)
			{
				int Mask = CmaskOne(i);
				for(int j = 0; j < MAX_CLIENTS; j++)
				{
					if(m_apPlayers[j] && m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS && m_apPlayers[j]->m_SpectatorID == i)
						Mask |= CmaskOne(j);
				}
				CreateSound(m_apPlayers[i]->m_ViewPos, Sound, Mask);
			}
		}
		
		m_aHitSoundState[i] = 0;
	}
	
	Server()->RoundStatistics()->UpdateNumberOfPlayers(NumActivePlayers);
	
/* INFECTION MODIFICATION START ***************************************/
	//Clean old dots
	int DotIter = 0;
	while(DotIter < m_LaserDots.size())
	{
		m_LaserDots[DotIter].m_LifeSpan--;
		if(m_LaserDots[DotIter].m_LifeSpan <= 0)
		{
			Server()->SnapFreeID(m_LaserDots[DotIter].m_SnapID);
			m_LaserDots.remove_index(DotIter);
		}
		else
			DotIter++;
	}
/* INFECTION MODIFICATION END *****************************************/

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				if(m_VoteBanClientID >= 0)
				{
					Server()->RemoveAccusations(m_VoteBanClientID);
					m_VoteBanClientID = -1;
				}
				
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand, -1);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
				
				//Remove accusation if needed
				if(m_VoteBanClientID >= 0)
				{
					Server()->RemoveAccusations(m_VoteBanClientID);
					m_VoteBanClientID = -1;
				}
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientEnter(int ClientID)
{
	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->Respawn();
	
/* INFECTION MODIFICATION START ***************************************/
	SendChatTarget_Language_s(-1, "%s entered and joined the game", Server()->ClientName(ClientID));
/* INFECTION MODIFICATION END *****************************************/

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	m_VoteUpdate = true;
}

void CGameContext::OnClientConnected(int ClientID)
{
	// Check which team the player should be on
	const int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(ClientID);

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
	//players[client_id].init(client_id);
	//players[client_id].client_id = client_id;

	(void)m_pController->CheckTeamBalance();

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

/* INFECTION MODIFICATION START ***************************************/	
	Server()->RoundStatistics()->ResetPlayer(ClientID);
/* INFECTION MODIFICATION END *****************************************/	

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	if(!Server()->GetClientMemory(ClientID, CLIENTMEMORY_MOTD))
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
		
		Server()->SetClientMemory(ClientID, CLIENTMEMORY_MOTD, true);
	}
	
	m_BroadcastStates[ClientID].m_NoChangeTick = 0;
	m_BroadcastStates[ClientID].m_LifeSpanTick = 0;
	m_BroadcastStates[ClientID].m_Priority = BROADCAST_PRIORITY_LOWEST;
	m_BroadcastStates[ClientID].m_PrevMessage[0] = 0;
	m_BroadcastStates[ClientID].m_NextMessage[0] = 0;
}

void CGameContext::OnClientDrop(int ClientID, int Type, const char *pReason)
{
	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(Type, pReason);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	Server()->RoundStatistics()->ResetPlayer(ClientID);
	
	(void)m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	// update spectator modes
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
	}
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];
	
	//HACK: DDNet Client did something wrong that we can detect
	//Round and Score conditions are here only to prevent false-positif
	if(!pPlayer && Server()->GetClientNbRound(ClientID) <= 1 && Server()->GetClientNbRound(ClientID) == 0)
	{
		Server()->Kick(ClientID, "Kicked (is probably a dummy)");
		return;
	}

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
				return;

/* INFECTION MODIFICATION START ***************************************/
			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
			int Team = CGameContext::CHAT_ALL;
			if(pMsg->m_Team)
			{
				if(pPlayer->GetTeam() == TEAM_SPECTATORS) Team = CGameContext::CHAT_SPEC;
				else Team = (pPlayer->IsInfected() ? CGameContext::CHAT_RED : CGameContext::CHAT_BLUE);
			}
/* INFECTION MODIFICATION END *****************************************/
			
			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
 			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(Code > 0x20 && Code != 0xA0 && Code != 0x034F && (Code < 0x2000 || Code > 0x200F) && (Code < 0x2028 || Code > 0x202F) &&
					(Code < 0x205F || Code > 0x2064) && (Code < 0x206A || Code > 0x206F) && (Code < 0xFE00 || Code > 0xFE0F) &&
					Code != 0xFEFF && (Code < 0xFFF9 || Code > 0xFFFC))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 127)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
 			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 16 characters per second)
			if(Length == 0 || (g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed()*((15+Length)/16) > Server()->Tick()))
				return;

			pPlayer->m_LastChat = Server()->Tick();
			
			
/* INFECTION MODIFICATION START ***************************************/
			if(pMsg->m_pMessage[0] == '/' || pMsg->m_pMessage[0] == '\\')
			{
				if(
					(str_comp_nocase_num(pMsg->m_pMessage,"\\ar  ", 4) == 0) ||
					(str_comp_nocase_num(pMsg->m_pMessage,"/ar ", 4) == 0) ||
					(str_comp_nocase_num(pMsg->m_pMessage,"\\fa  ", 4) == 0) ||
					(str_comp_nocase_num(pMsg->m_pMessage,"/fa ", 4) == 0)
				)
				{
					//Inverse order and add ligature
					char aTmp[sizeof(pMsg->m_pMessage)];
					char aOutput[sizeof(pMsg->m_pMessage)];
					ConvertArabicInput(pMsg->m_pMessage+4, aTmp, aOutput);
					SendChat(ClientID, Team, aOutput);
				}
				else
				{
					switch(m_apPlayers[ClientID]->m_Authed)
					{
						case IServer::AUTHED_ADMIN:
							Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
							break;
						case IServer::AUTHED_MOD:
							Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_MOD);
							break;
						default:
							Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
					}	
					m_ChatResponseTargetID = ClientID;
					
					Console()->ExecuteLineFlag(pMsg->m_pMessage + 1, ClientID, CFGFLAG_CHAT);
					
					m_ChatResponseTargetID = -1;
					Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				}
			}
			else
			{
				SendChat(ClientID, Team, pMsg->m_pMessage);
			}
/* INFECTION MODIFICATION END *****************************************/
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
				return;
				
			int64 Now = Server()->Tick();
			pPlayer->m_LastVoteTry = Now;
			
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";
			
			if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				int KickID = str_toint(pMsg->m_Value);
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
				{
					SendChatTarget(ClientID, "Invalid client id to kick");
					return;
				}
				if(KickID == ClientID)
				{
					SendChatTarget(ClientID, "You can't kick yourself");
					return;
				}
				if(Server()->IsAuthed(KickID))
				{
					SendChatTarget(ClientID, "You can't kick admins");
					char aBufKick[128];
					str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
					SendChatTarget(KickID, aBufKick);
					return;
				}

				Server()->AddAccusation(ClientID, KickID, pReason);
			}
			else
			{
				if(pPlayer->GetTeam() == TEAM_SPECTATORS)
				{
					SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
					return;
				}

				if(m_VoteCloseTime)
				{
					SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
					return;
				}

				if(!m_pController->CanVote())
				{
					SendChatTarget(ClientID, "Votes are only allowed when the round start.");
					return;
				}

				int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
				if(pPlayer->m_LastVoteCall && Timeleft > 0)
				{
					char aChatmsg[512] = {0};
					str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote", (Timeleft/Server()->TickSpeed())+1);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}

				char aChatmsg[512] = {0};
				char aDesc[VOTE_DESC_LENGTH] = {0};
				char aCmd[VOTE_CMD_LENGTH] = {0};

				if(str_comp_nocase(pMsg->m_Type, "option") == 0)
				{
					CVoteOptionServer *pOption = m_pVoteOptionFirst;
					while(pOption)
					{
						if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
						{
							str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
										pOption->m_aDescription, pReason);
							str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
							str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
							break;
						}

						pOption = pOption->m_pNext;
					}

					if(!pOption)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
				}
				else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
				{
					if(!g_Config.m_SvVoteSpectate)
					{
						SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
						return;
					}

					int SpectateID = str_toint(pMsg->m_Value);
					if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
					{
						SendChatTarget(ClientID, "Invalid client id to move");
						return;
					}
					if(SpectateID == ClientID)
					{
						SendChatTarget(ClientID, "You can't move yourself");
						return;
					}

					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
					str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
					str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
				}

				if(aCmd[0])
				{
					SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
					StartVote(aDesc, aCmd, pReason);
					pPlayer->m_Vote = 1;
					pPlayer->m_VotePos = m_VotePos = 1;
					m_VoteCreator = ClientID;
					pPlayer->m_LastVoteCall = Now;
				}
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(m_VoteLanguageTick[ClientID] > 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				if(pMsg->m_Vote)
				{
					if(pMsg->m_Vote > 0)
					{
						Server()->SetClientLanguage(ClientID, m_VoteLanguage[ClientID]);
						if(m_apPlayers[ClientID])
						{
							m_apPlayers[ClientID]->SetLanguage(m_VoteLanguage[ClientID]);
						}
					}
					
					m_VoteLanguageTick[ClientID] = 0;
					
					CNetMsg_Sv_VoteSet Msg;
					Msg.m_Timeout = 0;
					Msg.m_pDescription = "";
					Msg.m_pReason = "";
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
				}
			}
			else if(m_VoteCloseTime && pPlayer->m_Vote == 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
			else if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;
				
				int ScoreMode = pPlayer->GetScoreMode();
				if(pMsg->m_Vote < 0) ScoreMode++;
				else ScoreMode--;
				
				if(ScoreMode < 0) ScoreMode = NB_PLAYERSCOREMODE-1;
				if(ScoreMode >= NB_PLAYERSCOREMODE) ScoreMode = 0;
				
				Server()->SetClientDefaultScoreMode(ClientID, ScoreMode);
				m_apPlayers[ClientID]->SetScoreMode(ScoreMode);
			}
			else
			{
				m_apPlayers[ClientID]->HookProtection(!m_apPlayers[ClientID]->HookProtectionEnabled(), false);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			if(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams)
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				SendBroadcast(ClientID, "Teams are locked", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
				return;
			}

			if(pPlayer->m_TeamChangeTick > Server()->Tick())
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
				SendBroadcast(ClientID, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
				return;
			}
			
/* INFECTION MODIFICATION START ***************************************/
			if(m_apPlayers[ClientID]->IsInfected() && pMsg->m_Team == TEAM_SPECTATORS) 
			{
				int InfectedCount = 0;
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					 if(m_apPlayers[i] && m_apPlayers[i]->IsInfected() && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
						 InfectedCount++;
				}

				if(InfectedCount <= 2)
				{
					 SendBroadcast_Language(ClientID, "You can't join the spectators right now", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
					 return;
				}
			}
/* INFECTION MODIFICATION END *****************************************/

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
			{
				if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
				{
					pPlayer->m_LastSetTeam = Server()->Tick();
					if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					pPlayer->SetTeam(pMsg->m_Team);
					(void)m_pController->CheckTeamBalance();
					pPlayer->m_TeamChangeTick = Server()->Tick();
				}
				else
					SendBroadcast(ClientID, "Teams must be balanced, please join other team", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-g_Config.m_SvSpectatorSlots);
				SendBroadcast(ClientID, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == pMsg->m_SpectatorID || ClientID == pMsg->m_SpectatorID ||
				(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
				SendChatTarget(ClientID, "Invalid spectator id used");
			else
				pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
		}
		else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
				return;

			CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set infos
			char aOldName[MAX_NAME_LENGTH];
			str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
			Server()->SetClientName(ClientID, pMsg->m_pName);
			if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
			{
				SendChatTarget_Language_ss(-1, "%s changed their name to %s", aOldName, Server()->ClientName(ClientID));
			}
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			
/* INFECTION MODIFICATION START ***************************************/
			str_copy(pPlayer->m_TeeInfos.m_CustomSkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_CustomSkinName));
			//~ pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			//~ pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			//~ pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
/* INFECTION MODIFICATION END *****************************************/

			m_pController->OnPlayerInfoChange(pPlayer);
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReady)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
/* INFECTION MODIFICATION START ***************************************/
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_CustomSkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_CustomSkinName));
			//~ pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			//~ pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			//~ pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);
			
			if(!Server()->GetClientMemory(ClientID, CLIENTMEMORY_LANGUAGESELECTION))
			{
				CNetMsg_Sv_VoteSet Msg;
				Msg.m_Timeout = 10;
				Msg.m_pReason = "";
				Msg.m_pDescription = 0;
				
				switch(pMsg->m_Country)
				{
					case 336: //Vatican
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_LA);
						m_VoteLanguage[ClientID] = LANGUAGE_LA;				
						break;
					case 533: //Aruba
					case 531: //Curaçao
					case 534: //Sint Maarten
					case 528: //Netherland
					case 740: //Suriname
					case 56: //Belgique
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_NL);
						m_VoteLanguage[ClientID] = LANGUAGE_NL;				
						break;									
					case 204: //Benin
					case 854: //Burkina Faso
					case 178: //Republic of the Congo
					case 384: //Cote d'Ivoire
					case 266: //Gabon
					case 324: //Ginea
					case 466: //Mali
					case 562: //Niger
					case 686: //Senegal
					case 768: //Togo
					case 250: //France
					case 492: //Monaco
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_FR);
						m_VoteLanguage[ClientID] = LANGUAGE_FR;				
						break;
					case 616: //Poland
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_PL);
						m_VoteLanguage[ClientID] = LANGUAGE_PL;				
						break;
					case 40: //Austria
					case 276: //Germany
					case 438: //Liechtenstein
					case 756: //Switzerland
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_DE);
						m_VoteLanguage[ClientID] = LANGUAGE_DE;				
						break;
					case 112: //Belarus
					case 643: //Russia
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_RU);
						m_VoteLanguage[ClientID] = LANGUAGE_RU;				
						break;
					case 804: //Ukraine
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_UK);
						m_VoteLanguage[ClientID] = LANGUAGE_UK;				
						break;
					case 380: //Italy
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_IT);
						m_VoteLanguage[ClientID] = LANGUAGE_IT;				
						break;
					case 348: //Hungary
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_HU);
						m_VoteLanguage[ClientID] = LANGUAGE_HU;				
						break;
					case 32: //Argentina
					case 68: //Bolivia
					case 152: //Chile
					case 170: //Colombia
					case 188: //Costa Rica
					case 192: //Cuba
					case 214: //Dominican Republic
					case 218: //Ecuador
					case 222: //El Salvador
					case 226: //Equatorial Guinea
					case 320: //Guatemala
					case 340: //Honduras
					case 484: //Mexico
					case 558: //Nicaragua
					case 591: //Panama
					case 600: //Paraguay
					case 604: //Peru
					case 630: //Puerto Rico
					case 724: //Spain
					case 858: //Uruguay
					case 862: //Venezuela
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_ES);
						m_VoteLanguage[ClientID] = LANGUAGE_ES;				
						break;
					case 12: //Algeria
					case 48: //Bahrain
					case 262: //Djibouti
					case 818: //Egypt
					case 368: //Iraq
					case 400: //Jordan
					case 414: //Kuwait
					case 422: //Lebanon
					case 434: //Libya
					case 478: //Mauritania
					case 504: //Morocco
					case 512: //Oman
					case 275: //Palestine
					case 634: //Qatar
					case 682: //Saudi Arabia
					case 706: //Somalia
					case 729: //Sudan
					case 760: //Syria
					case 788: //Tunisia
					case 784: //United Arab Emirates
					case 887: //Yemen
						Msg.m_pDescription = ServerLocalize("Switch language to english ?", LANGUAGE_AR);
						m_VoteLanguage[ClientID] = LANGUAGE_AR;				
						break;
				}
				
				if(Msg.m_pDescription)
				{
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
					m_VoteLanguageTick[ClientID] = 10*Server()->TickSpeed();
				}
				else
				{
					SendChatTarget_Language(ClientID, "You can change the language of this mod using the command /language.");
					SendChatTarget_Language(ClientID, "If your language is not available, you can help with translation (/help translate).");
				}
				
				Server()->SetClientMemory(ClientID, CLIENTMEMORY_LANGUAGESELECTION, true);
			}
			
/* INFECTION MODIFICATION END *****************************************/

			// send vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			CNetMsg_Sv_VoteOptionListAdd OptionMsg;
			int NumOptions = 0;
			OptionMsg.m_pDescription0 = "";
			OptionMsg.m_pDescription1 = "";
			OptionMsg.m_pDescription2 = "";
			OptionMsg.m_pDescription3 = "";
			OptionMsg.m_pDescription4 = "";
			OptionMsg.m_pDescription5 = "";
			OptionMsg.m_pDescription6 = "";
			OptionMsg.m_pDescription7 = "";
			OptionMsg.m_pDescription8 = "";
			OptionMsg.m_pDescription9 = "";
			OptionMsg.m_pDescription10 = "";
			OptionMsg.m_pDescription11 = "";
			OptionMsg.m_pDescription12 = "";
			OptionMsg.m_pDescription13 = "";
			OptionMsg.m_pDescription14 = "";
			CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
			while(pCurrent)
			{
				switch(NumOptions++)
				{
				case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
				case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
				case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
				case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
				case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
				case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
				case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
				case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
				case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
				case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
				case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
				case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
				case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
				case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
				case 14:
					{
						OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
						OptionMsg.m_NumOptions = NumOptions;
						Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
						OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
						NumOptions = 0;
						OptionMsg.m_pDescription1 = "";
						OptionMsg.m_pDescription2 = "";
						OptionMsg.m_pDescription3 = "";
						OptionMsg.m_pDescription4 = "";
						OptionMsg.m_pDescription5 = "";
						OptionMsg.m_pDescription6 = "";
						OptionMsg.m_pDescription7 = "";
						OptionMsg.m_pDescription8 = "";
						OptionMsg.m_pDescription9 = "";
						OptionMsg.m_pDescription10 = "";
						OptionMsg.m_pDescription11 = "";
						OptionMsg.m_pDescription12 = "";
						OptionMsg.m_pDescription13 = "";
						OptionMsg.m_pDescription14 = "";
					}
				}
				pCurrent = pCurrent->m_pNext;
			}
			if(NumOptions > 0)
			{
				OptionMsg.m_NumOptions = NumOptions;
				Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
			}

			// send tuning parameters to client
			SendTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReady = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
		}
	}
}

/* DDNET MODIFICATION START *******************************************/
void CGameContext::ChatConsolePrintCallback(const char *pLine, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	int ClientID = pSelf->m_ChatResponseTargetID;

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	const char *pLineOrig = pLine;

	static volatile int ReentryGuard = 0;

	if(ReentryGuard)
		return;
	ReentryGuard++;

	if(*pLine == '[')
	do
		pLine++;
	while((pLine - 2 < pLineOrig || *(pLine - 2) != ':') && *pLine != 0); // remove the category (e.g. [Console]: No Such Command)

	pSelf->SendChatTarget(ClientID, pLine);

	ReentryGuard--;
}
/* DDNET MODIFICATION END *********************************************/

bool CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		//~ pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
	
	return true;
}

bool CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	//~ pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
	
	return true;
}

bool CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
	
	return true;
}

bool CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_pController->IsGameOver())
		return true;

	pSelf->m_World.m_Paused ^= 1;
	
	return true;
}

bool CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
	
	return true;
}

bool CGameContext::ConSkipMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->SkipMap();
	
	return true;
}

bool CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
	
	return true;
}

bool CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(-1, pResult->GetString(0), BROADCAST_PRIORITY_SERVERANNOUNCE, pSelf->Server()->TickSpeed()*3);
	
	return true;
}

bool CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
	
	return true;
}

bool CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return true;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	(void)pSelf->m_pController->CheckTeamBalance();
	
	return true;
}

bool CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);

	(void)pSelf->m_pController->CheckTeamBalance();
	
	return true;
}

bool CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SwapTeams();
	
	return true;
}

bool CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return true;

	int CounterRed = 0;
	int CounterBlue = 0;
	int PlayerTeam = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			++PlayerTeam;
	PlayerTeam = (PlayerTeam+1)/2;
	
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were shuffled");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			if(CounterRed == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
			else if(CounterBlue == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
			else
			{	
				if(rand() % 2)
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
					++CounterBlue;
				}
				else
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
					++CounterRed;
				}
			}
		}
	}

	(void)pSelf->m_pController->CheckTeamBalance();
	
	return true;
}

bool CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	if(pSelf->m_LockTeams)
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were locked");
	else
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were unlocked");
	
	return true;
}

bool CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return true;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return true;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return true;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return true;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
	
	return true;
}

bool CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return true;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
	
	return true;
}

bool CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "admin forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand, -1);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return true;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return true;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return true;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf, -1);
	}
	
	return true;
}

bool CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
	
	return true;
}

bool CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return true;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	
	return true;
}

bool CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
	
	return true;
}


/* INFECTION MODIFICATION START ***************************************/
bool CGameContext::ConSetClass(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int PlayerID = pResult->GetInteger(0);
	const char *pClassName = pResult->GetString(1);

	CPlayer* pPlayer = pSelf->m_apPlayers[PlayerID];
	
	if(!pPlayer)
		return true;

	if(str_comp(pClassName, "engineer") == 0) pPlayer->SetClass(PLAYERCLASS_ENGINEER);
	else if(str_comp(pClassName, "soldier") == 0) pPlayer->SetClass(PLAYERCLASS_SOLDIER);
	else if(str_comp(pClassName, "scientist") == 0) pPlayer->SetClass(PLAYERCLASS_SCIENTIST);
	else if(str_comp(pClassName, "medic") == 0) pPlayer->SetClass(PLAYERCLASS_MEDIC);
	else if(str_comp(pClassName, "ninja") == 0) pPlayer->SetClass(PLAYERCLASS_NINJA);
	else if(str_comp(pClassName, "mercenary") == 0) pPlayer->SetClass(PLAYERCLASS_MERCENARY);
	else if(str_comp(pClassName, "sniper") == 0) pPlayer->SetClass(PLAYERCLASS_SNIPER);
	else if(str_comp(pClassName, "smoker") == 0) pPlayer->SetClass(PLAYERCLASS_SMOKER);
	else if(str_comp(pClassName, "hunter") == 0) pPlayer->SetClass(PLAYERCLASS_HUNTER);
	else if(str_comp(pClassName, "boomer") == 0) pPlayer->SetClass(PLAYERCLASS_BOOMER);
	else if(str_comp(pClassName, "ghost") == 0) pPlayer->SetClass(PLAYERCLASS_GHOST);
	else if(str_comp(pClassName, "spider") == 0) pPlayer->SetClass(PLAYERCLASS_SPIDER);
	else if(str_comp(pClassName, "undead") == 0) pPlayer->SetClass(PLAYERCLASS_UNDEAD);
	else if(str_comp(pClassName, "witch") == 0) pPlayer->SetClass(PLAYERCLASS_WITCH);
	else if(str_comp(pClassName, "none") == 0)
	{
		pPlayer->SetClass(PLAYERCLASS_NONE);
		CCharacter* pChar = pPlayer->GetCharacter();
		if(pChar)
		{
			pChar->OpenClassChooser();
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class", "Unknown class");
		return true;
	}
	
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "The admin change the class of %s to %s", pSelf->Server()->ClientName(PlayerID), pClassName);
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	
	return true;
}

bool CGameContext::ConChatInfo(IConsole::IResult *pResult, void *pUserData)
{
	char aBuf[512];
	char aBuf2[512];
	
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	int Language = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	const char* pLine1 = pSelf->ServerLocalize("InfectionClass, by necropotame (version %s)", Language); 
	const char* pLine2 = pSelf->ServerLocalize("Based on Infection mod by Gravity", Language); 
	const char* pLine3 = pSelf->ServerLocalize("Thanks to %s", Language);
	
	str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s", pLine1, pLine2, pLine3);
	str_format(aBuf2, sizeof(aBuf2), aBuf, "2.0", "guenstig werben, Defeater, Orangus, BlinderHeld, Warpaint, Serena, socialdarwinist, FakeDeath, tee_to_F_U_UP!, ...");
	
	pSelf->SendMOTD(ClientID, aBuf2);
	
	return true;
}

#ifdef CONF_SQL

bool CGameContext::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pLogin = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);
	const char *pEmail = 0;
	
	if(pResult->NumArguments()>2)
		pEmail = pResult->GetString(2);
	
	pSelf->Server()->Register(ClientID, pLogin, pPassword, pEmail);
	
	return true;
}

bool CGameContext::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pLogin = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);
	pSelf->Server()->Login(ClientID, pLogin, pPassword);
	
	return true;
}

bool CGameContext::ConLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	pSelf->Server()->Logout(ClientID);
	
	return true;
}

bool CGameContext::ConTop10(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

bool CGameContext::ConRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

bool CGameContext::ConGoal(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

#endif

bool CGameContext::ConHelp(IConsole::IResult *pResult, void *pUserData)
{
	char aBuf[512];
	
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	int Language = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	const char *pHelpPage = (pResult->NumArguments()>0) ? pResult->GetString(0) : 0x0;

	if(pHelpPage)
	{
		if(str_comp_nocase(pHelpPage, "game") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Rules of the game:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("InfectionClass is a team game between humans and infected.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("All players start as human.", Language);
			const char* pLine4 = pSelf->ServerLocalize("10 seconds later, two players become infected.", Language);
			const char* pLine5 = pSelf->ServerLocalize("The goal for humans is to survive until the army clean the map.", Language);
			const char* pLine6 = pSelf->ServerLocalize("The goal for infected is to infect all humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4, pLine5, pLine6);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "translate") == 0)
		{
			const char* pLine1 = "How to translate the mod:"; 
			const char* pLine2 = "Download this file:";
			const char* pLine3 = "https://raw.githubusercontent.com";
			const char* pLine4 = "   /necropotame/teeworlds-infclass/master";
			const char* pLine5 = "   /data/languages/infclass/fr.txt";
			const char* pLine6 = "Keep the english line and replace the french line with your translation";
			const char* pLine7 = "Send the new file in github or at necropotame@gmail.com";

			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4, pLine5, pLine6, pLine7);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "engineer") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Engineer:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Engineer can build walls with his hammer to block infected.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("When an infected touch the wall, he dies.", Language);
			const char* pLine4 = pSelf->ServerLocalize("The lifespan of a wall is 30 seconds, and walls are limited to one per player at the same time.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "soldier") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Soldier:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Soldier can pose floating bombs with his hammer.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("Each bomb can explode three times.", Language);
			const char* pLine4 = pSelf->ServerLocalize("Use the hammer to place the bomb and explode it multiple times.", Language);
			const char* pLine5 = pSelf->ServerLocalize("Bombs are limited to one per player at the same time.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4, pLine5);
				
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "scientist") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Scientist:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Scientist can pose floating mines with his hammer.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("Mines are limited to two per player at the same time.", Language);
			const char* pLine4 = pSelf->ServerLocalize("He has also grenades that teleport him.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);
					
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "medic") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Medic:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Medic can protect humans with his hammer by giving them armor.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("He has also a powerful shotgun that can pullback infected.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s", pLine1, pLine2, pLine3);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "ninja") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Ninja:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Ninja can throw flash grenades that can freeze infected during three seconds.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("His hammer is replaced by a katana, allowing him to jump two times before touching the ground.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s", pLine1, pLine2, pLine3);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "mercenary") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Mercenary:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Mercenary fly in air using his machine gun.", Language);
			const char* pLine3 = pSelf->ServerLocalize("He can also throw poison grenades that deals 8 damage points.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s", pLine1, pLine2, pLine3);
	
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "sniper") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Sniper:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Sniper can lock his position in air for 15 seconds with his hammer.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("He can jump two times in air.", Language); 
			const char* pLine4 = pSelf->ServerLocalize("He has also a powerful rifle that deals 19-20 damage points in locked position, and 9-10 otherwise.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);

			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "smoker") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Smoker:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Smoker can infect humans and heal infected with his hammer.", Language);
			const char* pLine3 = pSelf->ServerLocalize("He can also inflict 4 damage points per seconds by hooking humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s", pLine1, pLine2, pLine3);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "boomer") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Boomer:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Boomer explodes when he attack.", Language);
			const char* pLine3 = pSelf->ServerLocalize("All humans affected by the explosion become infected.", Language);
			const char* pLine4 = pSelf->ServerLocalize("He can also inflict 1 damage point per seconds by hooking humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "hunter") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Hunter:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Hunter can infect humans and heal infected with his hammer.", Language);
			const char* pLine3 = pSelf->ServerLocalize("He can jump two times in air.", Language);
			const char* pLine4 = pSelf->ServerLocalize("He can also inflict 1 damage point per seconds by hooking humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "ghost") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Ghost:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Ghost can infect humans and heal infected with his hammer.", Language);
			const char* pLine3 = pSelf->ServerLocalize("He is invisible, except if a human is near him, if he takes a damage or if he use his hammer.", Language);
			const char* pLine4 = pSelf->ServerLocalize("He can also inflict 1 damage point per seconds by hooking humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "spider") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Spider:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Spider can infect humans and heal infected with his hammer.", Language); 
			const char* pLine3 = pSelf->ServerLocalize("When selecting any gun, his hook enter in web mode.", Language); 
			const char* pLine4 = pSelf->ServerLocalize("Any human that touch a hook in web mode is automatically grabbed.", Language);
			const char* pLine5 = pSelf->ServerLocalize("The hook of the spider (in both mode) deal 1 damage point per seconds and can grab a human during 2 seconds.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4, pLine5);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "undead") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Undead:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Undead can infect humans and heal infected with his hammer.", Language);
			const char* pLine3 = pSelf->ServerLocalize("Instead of dying, he freezes during 10 seconds.", Language);
			const char* pLine4 = pSelf->ServerLocalize("If an infected heals him, the freeze effect disappear.", Language);
			const char* pLine5 = pSelf->ServerLocalize("He can also inflict 1 damage point per seconds by hooking humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4, pLine5);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else if(str_comp_nocase(pHelpPage, "witch") == 0)
		{
			const char* pLine1 = pSelf->ServerLocalize("Witch:", Language); 
			const char* pLine2 = pSelf->ServerLocalize("The Witch can infect humans and heal infected with his hammer.", Language);
			const char* pLine3 = pSelf->ServerLocalize("When an infected dies, he may re-spawn near her.", Language);
			const char* pLine4 = pSelf->ServerLocalize("If the Witch dies, she disappear and is replaced by an another class of infected.", Language);
			const char* pLine5 = pSelf->ServerLocalize("She can also inflict 1 damage point per seconds by hooking humans.", Language);
			
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4, pLine5);
			
			pSelf->SendMOTD(ClientID, aBuf);
		}
		else
			pHelpPage = 0x0;
	}
	
	if(!pHelpPage)
	{
		const char* pTxtChooseHelp = pSelf->ServerLocalize("Choose a help page with /help <page>", Language);
		const char* pTxtAvailableHelpPage = pSelf->ServerLocalize("Available help pages: %s", Language);
		str_format(aBuf, sizeof(aBuf), pTxtAvailableHelpPage, "game, translate, engineer, soldier, scientist");
		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", pTxtChooseHelp);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", aBuf);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "ghost, spider, undead, witch, medic, ninja, mercenary, sniper, smoker, hunter, boomer");
	}
	
	return true;
}

bool CGameContext::ConCustomSkin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pArg = pResult->GetString(0);

	if(pArg)
	{
		if(str_comp_nocase(pArg, "all") == 0)
			pSelf->Server()->SetClientCustomSkin(ClientID, 2);
		else if(str_comp_nocase(pArg, "me") == 0)
			pSelf->Server()->SetClientCustomSkin(ClientID, 1);
		else if(str_comp_nocase(pArg, "none") == 0)
			pSelf->Server()->SetClientCustomSkin(ClientID, 0);
	}
	
	return true;
}

bool CGameContext::ConAlwaysRandom(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	int Language = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	int Arg = pResult->GetInteger(0);

	if(Arg > 0)
	{
		pSelf->Server()->SetClientAlwaysRandom(ClientID, 1);
		const char* pTxtAlwaysRandomOn = pSelf->ServerLocalize("A random class will be automatically attributed to you when rounds start", Language);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "alwaysrandom", pTxtAlwaysRandomOn);		
	}
	else
	{
		pSelf->Server()->SetClientAlwaysRandom(ClientID, 0);
		const char* pTxtAlwaysRandomOff = pSelf->ServerLocalize("The class selector will be displayed when rounds start", Language);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "alwaysrandom", pTxtAlwaysRandomOff);		
	}
	
	return true;
}

bool CGameContext::ConLanguage(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	
	const char *pLanguageCode = (pResult->NumArguments()>0) ? pResult->GetString(0) : 0x0;

	int Language = -1;

	if(pLanguageCode)
	{
		if(str_comp_nocase(pLanguageCode, "fr") == 0)
			Language = LANGUAGE_FR;
		else if(str_comp_nocase(pLanguageCode, "de") == 0)
			Language = LANGUAGE_DE;
		else if(str_comp_nocase(pLanguageCode, "uk") == 0)
			Language = LANGUAGE_UK;
		else if(str_comp_nocase(pLanguageCode, "ru") == 0)
			Language = LANGUAGE_RU;
		else if(str_comp_nocase(pLanguageCode, "it") == 0)
			Language = LANGUAGE_IT;
		else if(str_comp_nocase(pLanguageCode, "es") == 0)
			Language = LANGUAGE_ES;
		else if(str_comp_nocase(pLanguageCode, "ar") == 0)
			Language = LANGUAGE_AR;
		else if(str_comp_nocase(pLanguageCode, "hu") == 0)
			Language = LANGUAGE_HU;
		else if(str_comp_nocase(pLanguageCode, "pl") == 0)
			Language = LANGUAGE_PL;
		else if(str_comp_nocase(pLanguageCode, "nl") == 0)
			Language = LANGUAGE_NL;
		else if(str_comp_nocase(pLanguageCode, "la") == 0)
			Language = LANGUAGE_LA;
		else if(str_comp_nocase(pLanguageCode, "en") == 0)
			Language = LANGUAGE_EN;
	}
	
	if(Language >= 0)
	{
		pSelf->Server()->SetClientLanguage(ClientID, Language);
		if(pSelf->m_apPlayers[ClientID])
			pSelf->m_apPlayers[ClientID]->SetLanguage(Language);
	}
	else
	{
		const char* pTxtUnknownLanguage = pSelf->ServerLocalize("Unknown language", Language);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "language", pTxtUnknownLanguage);		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "language", "Help: /language <en|fr|de|uk|ru|it|es|ar|hu|pl|nl|la>");
	}
	
	return true;
}

bool CGameContext::ConCmdList(IConsole::IResult *pResult, void *pUserData)
{
	char aBuf[512];
	
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	int Language = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	const char* pLine1 = pSelf->ServerLocalize("List of commands", Language);
	const char* pLine2 = "/ar, /alwaysrandom, /customskin, /fa, /help, /info, /language"; 
#ifdef CONF_SQL
	const char* pLineSql = "/register, /login, /logout, /top10, /rank, /goal"; 
#endif
	const char* pLine3 = pSelf->ServerLocalize("Press <F3> or <F4> to enable or disable hook protection", Language);
	const char* pLine4 = pSelf->ServerLocalize("Press <F3> or <F4> while holding <TAB> to switch the score system", Language);

#ifdef CONF_SQL
	str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLineSql, pLine3, pLine4);
#else
	str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s", pLine1, pLine2, pLine3, pLine4);
#endif

	pSelf->SendMOTD(ClientID, aBuf);
	
	return true;
}

bool CGameContext::ConFriendlyBan(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	const char *pPlayername = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";
	
	bool PlayerBanned = false;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(pSelf->m_apPlayers[i] && str_comp(pSelf->Server()->ClientName(i), pPlayername) == 0)
		{
			pSelf->Server()->Ban(i, Minutes*60, pReason);
			PlayerBanned = true;
		}
	}
	
	if(!PlayerBanned)
	{
		int ClientID = pResult->GetClientID();
		int Language = pSelf->m_apPlayers[ClientID]->GetLanguage();
		
		const char* pTxt = pSelf->ServerLocalize("No player was found with this name", Language);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "fban", pTxt);
	}
	
	return true;
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	InitializeServerLocatization();

	m_ChatPrintCBIndex = Console()->RegisterPrintCallback(0, ChatConsolePrintCallback, this);

	Console()->Register("tune", "s<param> i<value>", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("skip_map", "", CFGFLAG_SERVER|CFGFLAG_STORE, ConSkipMap, this, "Change map to the next in the rotation");
	Console()->Register("restart", "?i<sec>", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r<message>", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");
	
/* INFECTION MODIFICATION START ***************************************/
	Console()->Register("inf_set_class", "is", CFGFLAG_SERVER, ConSetClass, this, "Set the class of a player");
	
	//Chat Command
	Console()->Register("info", "", CFGFLAG_CHAT|CFGFLAG_USER, ConChatInfo, this, "Display information about the mod");
#ifdef CONF_SQL
	Console()->Register("register", "s<username> s<password> ?s<email>", CFGFLAG_CHAT|CFGFLAG_USER, ConRegister, this, "Create an account");
	Console()->Register("login", "s<username> s<password>", CFGFLAG_CHAT|CFGFLAG_USER, ConLogin, this, "Login to an account");
	Console()->Register("logout", "", CFGFLAG_CHAT|CFGFLAG_USER, ConLogout, this, "Logout");
	Console()->Register("top10", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConTop10, this, "Show the top 10 on the current map");
	Console()->Register("rank", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConRank, this, "Show your rank");
	Console()->Register("goal", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConGoal, this, "Show your goal");
#endif
	Console()->Register("help", "?s<page>", CFGFLAG_CHAT|CFGFLAG_USER, ConHelp, this, "Display help");
	Console()->Register("customskin", "s<all|me|none>", CFGFLAG_CHAT|CFGFLAG_USER, ConCustomSkin, this, "Display information about the mod");
	Console()->Register("alwaysrandom", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAlwaysRandom, this, "Display information about the mod");
	Console()->Register("language", "s<fr|de|uk|ru|it|es|ar|hu|pl|nl|la>", CFGFLAG_CHAT|CFGFLAG_USER, ConLanguage, this, "Display information about the mod");
	Console()->Register("cmdlist", "", CFGFLAG_CHAT|CFGFLAG_USER, ConCmdList, this, "List of commands");
	
	Console()->Register("fban", "s<playername> ?i<minutes> ?r<reason>", CFGFLAG_CHAT, ConFriendlyBan, this, "Friendly version of ban cmd");
/* INFECTION MODIFICATION END *****************************************/

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		Server()->ResetClientMemoryAboutGame(i);
	}

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_VoteLanguageTick[i] = 0;
		m_VoteLanguage[i] = LANGUAGE_EN;				
	}

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// select gametype
	m_pController = new CGameControllerMOD(this);

	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.EntityLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);
	
	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
			m_pController->OnEntity(pTiles[y*pTileMap->m_Width+x].m_Index, Pos);
		}
	}

	//game.world.insert_entity(game.Controller);

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

/* INFECTION MODIFICATION START ***************************************/
	//Snap laser dots
	for(int i=0; i < m_LaserDots.size(); i++)
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserDots[i].m_SnapID, sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)m_LaserDots[i].m_Pos1.x;
		pObj->m_Y = (int)m_LaserDots[i].m_Pos1.y;
		pObj->m_FromX = (int)m_LaserDots[i].m_Pos0.x;
		pObj->m_FromY = (int)m_LaserDots[i].m_Pos0.y;
		pObj->m_StartTick = Server()->Tick();
	}
/* INFECTION MODIFICATION END *****************************************/
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }
