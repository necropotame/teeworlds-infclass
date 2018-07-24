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

enum
{
	RESET,
	NO_RESET
};

/* INFECTION MODIFICATION START ***************************************/

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
    m_TargetToKill = -1;
    m_TargetToKillCoolDown = 0;
	
	m_ChatResponseTargetID = -1;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	geolocation = new Geolocation("GeoLite2-Country.mmdb");
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	geolocation = new Geolocation("GeoLite2-Country.mmdb");
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < m_LaserDots.size(); i++)
		Server()->SnapFreeID(m_LaserDots[i].m_SnapID);
	for(int i = 0; i < m_HammerDots.size(); i++)
		Server()->SnapFreeID(m_HammerDots[i].m_SnapID);
	
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
	delete geolocation;
	geolocation = nullptr;
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

void CGameContext::CreateHammerDotEvent(vec2 Pos, int LifeSpan)
{
	CGameContext::HammerDotState State;
	State.m_Pos = Pos;
	State.m_LifeSpan = LifeSpan;
	State.m_SnapID = Server()->SnapNewID();
	
	m_HammerDots.add(State);
}

void CGameContext::CreateLoveEvent(vec2 Pos)
{
	CGameContext::LoveDotState State;
	State.m_Pos = Pos;
	State.m_LifeSpan = Server()->TickSpeed();
	State.m_SnapID = Server()->SnapNewID();
	
	m_LoveDots.add(State);
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

// Thanks to Stitch for the idea
void CGameContext::CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, int Weapon, int TakeDamageMode)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
	if(Damage > 0)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		int Num = m_World.FindEntities(Pos, DamageRadius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			l = 1-clamp((l-InnerRadius)/(DamageRadius-InnerRadius), 0.0f, 1.0f);
			
			if(l)
				ForceDir = normalize(Diff);
			
			float DamageToDeal = 1 + ((Damage - 1) * l);
			apEnts[i]->TakeDamage(ForceDir*Force*l, DamageToDeal, Owner, Weapon, TakeDamageMode);
		}
	}
	
	float CircleLength = 2.0*pi*max(DamageRadius-135.0f, 0.0f);
	int NumSuroundingExplosions = CircleLength/32.0f;
	float AngleStart = random_float()*pi*2.0f;
	float AngleStep = pi*2.0f/static_cast<float>(NumSuroundingExplosions);
	for(int i=0; i<NumSuroundingExplosions; i++)
	{
		CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x + (DamageRadius-135.0f) * cos(AngleStart + i*AngleStep);
			pEvent->m_Y = (int)Pos.y + (DamageRadius-135.0f) * sin(AngleStart + i*AngleStep);
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

void CGameContext::CreateSound(vec2 Pos, int Sound, int64_t Mask)
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
void CGameContext::SendChatTarget_Localization(int To, int Category, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			switch(Category)
			{
				case CHATCATEGORY_INFECTION:
					Buffer.append("☣ | ");
					break;
				case CHATCATEGORY_SCORE:
					Buffer.append("★ | ");
					break;
				case CHATCATEGORY_PLAYER:
					Buffer.append("♟ | ");
					break;
				case CHATCATEGORY_INFECTED:
					Buffer.append("⛃ | ");
					break;
				case CHATCATEGORY_HUMANS:
					Buffer.append("⛁ | ");
					break;
				case CHATCATEGORY_ACCUSATION:
					Buffer.append("☹ | ");
					break;
			}
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			
			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendChatTarget_Localization_P(int To, int Category, int Number, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			switch(Category)
			{
				case CHATCATEGORY_INFECTION:
					Buffer.append("☣ | ");
					break;
				case CHATCATEGORY_SCORE:
					Buffer.append("★ | ");
					break;
				case CHATCATEGORY_PLAYER:
					Buffer.append("♟ | ");
					break;
				case CHATCATEGORY_INFECTED:
					Buffer.append("⛃ | ");
					break;
				case CHATCATEGORY_HUMANS:
					Buffer.append("⛁ | ");
					break;
				case CHATCATEGORY_ACCUSATION:
					Buffer.append("☹ | ");
					break;
			}
			Server()->Localization()->Format_VLP(Buffer, m_apPlayers[i]->GetLanguage(), Number, pText, VarArgs);
			
			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
	
	va_end(VarArgs);
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

void CGameContext::SendMOTD_Localization(int To, const char* pText, ...)
{
	if(m_apPlayers[To])
	{
		dynamic_string Buffer;
		
		CNetMsg_Sv_Motd Msg;
		
		va_list VarArgs;
		va_start(VarArgs, pText);
		
		Server()->Localization()->Format_VL(Buffer, m_apPlayers[To]->GetLanguage(), pText, VarArgs);
	
		va_end(VarArgs);
		
		Msg.m_pMessage = Buffer.buffer();
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

void CGameContext::SendBroadcast_Localization(int To, int Priority, int LifeSpan, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendBroadcast_Localization_P(int To, int Priority, int LifeSpan, int Number, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Server()->Localization()->Format_VLP(Buffer, m_apPlayers[i]->GetLanguage(), Number, pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendBroadcast_ClassIntro(int ClientID, int Class)
{
	const char* pClassName = 0;
	
	switch(Class)
	{
		case PLAYERCLASS_ENGINEER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Engineer"));
			break;
		case PLAYERCLASS_SOLDIER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Soldier"));
			break;
		case PLAYERCLASS_MEDIC:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Medic"));
			break;
		case PLAYERCLASS_HERO:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Hero"));
			break;
		case PLAYERCLASS_NINJA:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Ninja"));
			break;
		case PLAYERCLASS_MERCENARY:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Mercenary"));
			break;
		case PLAYERCLASS_SNIPER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Sniper"));
			break;
		case PLAYERCLASS_SCIENTIST:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Scientist"));
			break;
		case PLAYERCLASS_BIOLOGIST:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Biologist"));
			break;
		case PLAYERCLASS_SMOKER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Smoker"));
			break;
		case PLAYERCLASS_HUNTER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Hunter"));
			break;
		case PLAYERCLASS_BOOMER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Boomer"));
			break;
		case PLAYERCLASS_GHOST:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Ghost"));
			break;
		case PLAYERCLASS_SPIDER:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Spider"));
			break;
		case PLAYERCLASS_GHOUL:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Ghoul"));
			break;
		case PLAYERCLASS_SLUG:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Slug"));
			break;
		case PLAYERCLASS_WITCH:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Witch"));
			break;
		case PLAYERCLASS_UNDEAD:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Undead"));
			break;
		default:
			pClassName = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), _("Unknown class"));
			break;
	}
	
	if(Class < END_HUMANCLASS)
		SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("You are a human: {str:ClassName}"), "ClassName", pClassName, NULL);
	else
		SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("You are an infected: {str:ClassName}"), "ClassName", pClassName, NULL);
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
#ifdef CONF_SQL
					Server()->ShowChallenge(i);
#endif
					Server()->SetClientMemory(i, CLIENTMEMORY_TOP10, true);
				}
			}
		}
	}
	
	//Target to kill
	if(m_TargetToKill >= 0 && (!m_apPlayers[m_TargetToKill] || !m_apPlayers[m_TargetToKill]->GetCharacter()))
	{
		m_TargetToKill = -1;
	}
	
	int LastTarget = -1;
	// Zombie is in InfecZone too long -> change target
	if(m_TargetToKill >= 0 && m_apPlayers[m_TargetToKill] && m_apPlayers[m_TargetToKill]->GetCharacter() && (m_apPlayers[m_TargetToKill]->GetCharacter()->GetInfZoneTick()*Server()->TickSpeed()) > 1000*g_Config.m_InfNinjaTargetAfkTime) 
	{
		LastTarget = m_TargetToKill;
		m_TargetToKill = -1;
	}
	
	if(m_TargetToKillCoolDown > 0)
		m_TargetToKillCoolDown--;
	
	if((m_TargetToKillCoolDown == 0 && m_TargetToKill == -1))
	{
		int m_aTargetList[MAX_CLIENTS];
		int NbTargets = 0;
		int infectedCount = 0;
		for(int i=0; i<MAX_CLIENTS; i++)
		{		
			if(m_apPlayers[i] && m_apPlayers[i]->IsInfected() && m_apPlayers[i]->GetClass() != PLAYERCLASS_UNDEAD)
			{
				if (m_apPlayers[i]->GetCharacter() && (m_apPlayers[i]->GetCharacter()->GetInfZoneTick()*Server()->TickSpeed()) < 1000*g_Config.m_InfNinjaTargetAfkTime) // Make sure zombie is not camping in InfZone
				{
					m_aTargetList[NbTargets] = i;
					NbTargets++;
				} 
				infectedCount++;
			}
		}
		
		if(NbTargets > 0)
			m_TargetToKill = m_aTargetList[random_int(0, NbTargets-1)];
			
		if(m_TargetToKill == -1)
		{
			if (LastTarget >= 0)
				m_TargetToKill = LastTarget; // Reset Target if no new targets were found
		}
		
		if (infectedCount < g_Config.m_InfNinjaMinInfected)
		{
			m_TargetToKill = -1; // disable target system
		}
	}
	
	//Check for banvote
	if(!m_VoteCloseTime)
	{
		for(int i=0; i<MAX_CLIENTS; i++)
		{
			if(Server()->ClientShouldBeBanned(i))
			{
				char aDesc[VOTE_DESC_LENGTH] = {0};
				char aCmd[VOTE_CMD_LENGTH] = {0};
				str_format(aCmd, sizeof(aCmd), "ban %d %d Banned by vote", i, g_Config.m_SvVoteKickBantime*3);
				str_format(aDesc, sizeof(aDesc), "Ban \"%s\"", Server()->ClientName(i));
				m_VoteBanClientID = i;
				StartVote(aDesc, aCmd, "");
				continue;
			}
		}
	}
	
	// check tuning
	CheckPureTuning();
	
	m_Collision.SetTime(m_pController->GetTime());

	//update hook protection in core
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetCharacter())
		{
			m_apPlayers[i]->GetCharacter()->m_Core.m_Infected = m_apPlayers[i]->IsInfected();
			m_apPlayers[i]->GetCharacter()->m_Core.m_HookProtected = m_apPlayers[i]->HookProtectionEnabled();
		}
	}
	
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
					
					str_copy(m_VoteLanguage[i], "en", sizeof(m_VoteLanguage[i]));				
					
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
	int DotIter;
	
	DotIter = 0;
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
	
	DotIter = 0;
	while(DotIter < m_HammerDots.size())
	{
		m_HammerDots[DotIter].m_LifeSpan--;
		if(m_HammerDots[DotIter].m_LifeSpan <= 0)
		{
			Server()->SnapFreeID(m_HammerDots[DotIter].m_SnapID);
			m_HammerDots.remove_index(DotIter);
		}
		else
			DotIter++;
	}
	
	DotIter = 0;
	while(DotIter < m_LoveDots.size())
	{
		m_LoveDots[DotIter].m_LifeSpan--;
		m_LoveDots[DotIter].m_Pos.y -= 5.0f;
		if(m_LoveDots[DotIter].m_LifeSpan <= 0)
		{
			Server()->SnapFreeID(m_LoveDots[DotIter].m_SnapID);
			m_LoveDots.remove_index(DotIter);
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
				Console()->ExecuteLine(m_aVoteCommand, -1, false);
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
	m_apPlayers[ClientID]->m_IsInGame = true;
	m_apPlayers[ClientID]->Respawn();
	
/* INFECTION MODIFICATION START ***************************************/
	SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} entered and joined the game"), "PlayerName", Server()->ClientName(ClientID), NULL);
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
	
	//Thanks to Stitch
	if(m_pController->IsInfectionStarted())
        m_apPlayers[ClientID]->StartInfection();
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
	m_pController->OnClientDrop(ClientID, Type);
	
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
			
			// trim right and set maximum length to 271 utf8-characters
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

				if(++Length >= 270)
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
			if(str_comp_num(pMsg->m_pMessage, "/msg ", 5) == 0)
			{
				PrivateMessage(pMsg->m_pMessage+5, ClientID, (Team != CGameContext::CHAT_ALL));
			}
			else if(str_comp_num(pMsg->m_pMessage, "/w ", 3) == 0)
			{
				PrivateMessage(pMsg->m_pMessage+3, ClientID, (Team != CGameContext::CHAT_ALL));
			}
			else if(pMsg->m_pMessage[0] == '/' || pMsg->m_pMessage[0] == '\\')
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
				
				Console()->ExecuteLineFlag(pMsg->m_pMessage + 1, ClientID, (Team != CGameContext::CHAT_ALL), CFGFLAG_CHAT);
				
				m_ChatResponseTargetID = -1;
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
			}
			else
			{
				if(Server()->GetClientSession(ClientID) && Server()->GetClientSession(ClientID)->m_MuteTick > 0)
				{
					int Time = Server()->GetClientSession(ClientID)->m_MuteTick/Server()->TickSpeed();
					SendChatTarget_Localization(ClientID, CHATCATEGORY_ACCUSATION, _("You are muted for {sec:Duration}"), "Duration", &Time, NULL);
				}
				else
				{
					//Inverse order and add ligature for arabic
					dynamic_string Buffer;
					Buffer.copy(pMsg->m_pMessage);
					Server()->Localization()->ArabicShaping(Buffer);
					SendChat(ClientID, Team, Buffer.buffer());
				}
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
				CPlayerIterator<PLAYERITER_INGAME> Iter(m_apPlayers);
				while(Iter.Next())
				{
					 if(Iter.Player()->IsInfected())
						 InfectedCount++;
				}

				if(InfectedCount <= 2)
				{
					 SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("You can't join the spectators right now"), NULL);
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
				SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} changed their name to {str:NewName}"), "PlayerName", aOldName, "NewName", Server()->ClientName(ClientID), NULL);
			}
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			// Server()->SetClientCountry(ClientID, pMsg->m_Country); // cuz geolocation
			
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

			// IP geolocation start
			std::string ip = Server()->GetClientIP(ClientID);
			Server()->SetClientCountry(ClientID, geolocation->get_country_iso_numeric_code(ip));
			// IP geolocation end

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
				
				m_VoteLanguage[ClientID][0] = 0;
				
				switch(pMsg->m_Country)
				{
					/* ar - Arabic ************************************/
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
						str_copy(m_VoteLanguage[ClientID], "ar", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* bg - Bosnian *************************************/	
					case 100: //Bulgaria
						str_copy(m_VoteLanguage[ClientID], "bg", sizeof(m_VoteLanguage[ClientID]));					
						break;
					/* bs - Bosnian *************************************/	
					case 70: //Bosnia and Hercegovina
						str_copy(m_VoteLanguage[ClientID], "bs", sizeof(m_VoteLanguage[ClientID]));					
						break;
					/* cs - Czech *************************************/	
					case 203: //Czechia
						str_copy(m_VoteLanguage[ClientID], "cs", sizeof(m_VoteLanguage[ClientID]));					
						break;
					/* de - German ************************************/	
					case 40: //Austria
					case 276: //Germany
					case 438: //Liechtenstein
					case 756: //Switzerland
						str_copy(m_VoteLanguage[ClientID], "de", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* el - Greek ***********************************/	
					case 300: //Greece
					case 196: //Cyprus
						str_copy(m_VoteLanguage[ClientID], "el", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* es - Spanish ***********************************/	
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
						str_copy(m_VoteLanguage[ClientID], "es", sizeof(m_VoteLanguage[ClientID]));
						break;	
					/* fa - Farsi ************************************/
					case 364: //Islamic Republic of Iran
					case 4: //Afghanistan
						str_copy(m_VoteLanguage[ClientID], "fa", sizeof(m_VoteLanguage[ClientID]));
						break;	
					/* fr - French ************************************/							
					case 204: //Benin
					case 854: //Burkina Faso
					case 178: //Republic of the Congo
					case 384: //Cote d’Ivoire
					case 266: //Gabon
					case 324: //Ginea
					case 466: //Mali
					case 562: //Niger
					case 686: //Senegal
					case 768: //Togo
					case 250: //France
					case 492: //Monaco
						str_copy(m_VoteLanguage[ClientID], "fr", sizeof(m_VoteLanguage[ClientID]));					
						break;
					/* hr - Croatian **********************************/	
					case 191: //Croatia
						str_copy(m_VoteLanguage[ClientID], "hr", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* hu - Hungarian *********************************/	
					case 348: //Hungary
						str_copy(m_VoteLanguage[ClientID], "hu", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* it - Italian ***********************************/	
					case 380: //Italy
						str_copy(m_VoteLanguage[ClientID], "it", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* ja - Japanese **********************************/	
					case 392: //Japan
						str_copy(m_VoteLanguage[ClientID], "ja", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* la - Latin *************************************/
					case 336: //Vatican
						str_copy(m_VoteLanguage[ClientID], "la", sizeof(m_VoteLanguage[ClientID]));				
						break;
					/* nl - Dutch *************************************/
					case 533: //Aruba
					case 531: //Curaçao
					case 534: //Sint Maarten
					case 528: //Netherland
					case 740: //Suriname
					case 56: //Belgique
						str_copy(m_VoteLanguage[ClientID], "nl", sizeof(m_VoteLanguage[ClientID]));					
						break;	
					/* pl - Polish *************************************/	
					case 616: //Poland
						str_copy(m_VoteLanguage[ClientID], "pl", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* pt - Portuguese ********************************/	
					case 24: //Angola
					case 76: //Brazil
					case 132: //Cape Verde
					//case 226: //Equatorial Guinea: official language, but not national language
					//case 446: //Macao: official language, but spoken by less than 1% of the population
					case 508: //Mozambique
					case 626: //Timor-Leste
					case 678: //São Tomé and Príncipe
						str_copy(m_VoteLanguage[ClientID], "pt", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* ru - Russian ***********************************/	
					case 112: //Belarus
					case 643: //Russia
					case 398: //Kazakhstan
						str_copy(m_VoteLanguage[ClientID], "ru", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* sk - Slovak ************************************/
					case 703: //Slovakia
						str_copy(m_VoteLanguage[ClientID], "sk", sizeof(m_VoteLanguage[ClientID]));		
						break;
					/* sr - Serbian ************************************/
					case 688: //Serbia
						str_copy(m_VoteLanguage[ClientID], "sr", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* tl - Tagalog ************************************/
					case 608: //Philippines
						str_copy(m_VoteLanguage[ClientID], "tl", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* tr - Turkish ************************************/
					case 31: //Azerbaijan
					case 792: //Turkey
						str_copy(m_VoteLanguage[ClientID], "tr", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* uk - Ukrainian **********************************/	
					case 804: //Ukraine
						str_copy(m_VoteLanguage[ClientID], "uk", sizeof(m_VoteLanguage[ClientID]));
						break;
					/* zh-Hans - Chinese (Simplified) **********************************/	
					case 156: //People’s Republic of China
					case 344: //Hong Kong
					case 446: //Macau
						str_copy(m_VoteLanguage[ClientID], "zh-Hans", sizeof(m_VoteLanguage[ClientID]));
						break;
				}
				
				if(m_VoteLanguage[ClientID][0])
				{
					Msg.m_pDescription = Server()->Localization()->Localize(m_VoteLanguage[ClientID], _("Switch language to english ?"));
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
					m_VoteLanguageTick[ClientID] = 10*Server()->TickSpeed();
				}
				else
				{
					SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You can change the language of this mod using the command /language."), NULL);
					SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("If your language is not available, you can help with translation (/help translate)."), NULL);
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
				pSelf->Console()->ExecuteLine(pOption->m_aCommand, -1, false);
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
			pSelf->Console()->ExecuteLine(aBuf, -1, false);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1, false);
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
		pSelf->Console()->ExecuteLine(aBuf, -1, false);
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
	else if(str_comp(pClassName, "biologist") == 0) pPlayer->SetClass(PLAYERCLASS_BIOLOGIST);
	else if(str_comp(pClassName, "medic") == 0) pPlayer->SetClass(PLAYERCLASS_MEDIC);
	else if(str_comp(pClassName, "hero") == 0) pPlayer->SetClass(PLAYERCLASS_HERO);
	else if(str_comp(pClassName, "ninja") == 0) pPlayer->SetClass(PLAYERCLASS_NINJA);
	else if(str_comp(pClassName, "mercenary") == 0) pPlayer->SetClass(PLAYERCLASS_MERCENARY);
	else if(str_comp(pClassName, "sniper") == 0) pPlayer->SetClass(PLAYERCLASS_SNIPER);
	else if(str_comp(pClassName, "smoker") == 0) pPlayer->SetClass(PLAYERCLASS_SMOKER);
	else if(str_comp(pClassName, "hunter") == 0) pPlayer->SetClass(PLAYERCLASS_HUNTER);
	else if(str_comp(pClassName, "boomer") == 0) pPlayer->SetClass(PLAYERCLASS_BOOMER);
	else if(str_comp(pClassName, "ghost") == 0) pPlayer->SetClass(PLAYERCLASS_GHOST);
	else if(str_comp(pClassName, "spider") == 0) pPlayer->SetClass(PLAYERCLASS_SPIDER);
	else if(str_comp(pClassName, "ghoul") == 0) pPlayer->SetClass(PLAYERCLASS_GHOUL);
	else if(str_comp(pClassName, "slug") == 0) pPlayer->SetClass(PLAYERCLASS_SLUG);
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
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	dynamic_string Buffer;
	
	const char aThanks[] = "guenstig werben, Defeater, Orangus, BlinderHeld, Warpaint, Serena, Socialdarwinist, FakeDeath, tee_to_F_U_UP!, Stitch626, Denis, NanoSlime_, tria, pinkieval…";
	const char aContributors[] = "necropotame, Stitch626";
	
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass, by necropotame (version {str:VersionCode})"), "{str:VersionCode}", "2.0", NULL); 
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Based on the concept of Infection mod by Gravity"), NULL); 
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Main contributors: {str:ListOfContributors}"), "ListOfContributors", aContributors, NULL); 
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Thanks to {str:ListOfContributors}"), "ListOfContributors", aThanks, NULL); 
	Buffer.append("\n\n");
	
	pSelf->SendMOTD(ClientID, Buffer.buffer());
	
	return true;
}

bool CGameContext::PrivateMessage(const char* pStr, int ClientID, bool TeamChat)
{	
	if(Server()->GetClientSession(ClientID) && Server()->GetClientSession(ClientID)->m_MuteTick > 0)
	{
		int Time = Server()->GetClientSession(ClientID)->m_MuteTick/Server()->TickSpeed();
		SendChatTarget_Localization(ClientID, CHATCATEGORY_ACCUSATION, _("You are muted for {sec:Duration}"), "Duration", &Time, NULL);
		return false;
	}
	
	bool ArgumentFound = false;
	const char* pArgumentIter = pStr;
	while(*pArgumentIter)
	{
		if(*pArgumentIter != ' ')
		{
			ArgumentFound = true;
			break;
		}
		
		pArgumentIter++;
	}
	
	if(!ArgumentFound)
	{
		SendChatTarget(ClientID, "Usage: /msg <username or group> <message>");
		SendChatTarget(ClientID, "Send a private message to a player or a group of players");
		SendChatTarget(ClientID, "Available groups: #near, #engineer, #soldier, ...");
		return true;
	}
	
	dynamic_string FinalMessage;
	int TextIter = 0;
	
	
	bool CheckDistance = false;
	vec2 CheckDistancePos = vec2(0.0f, 0.0f);
	
	int CheckID = -1;
	int CheckTeam = -1;
	int CheckClass = -1;
#ifdef CONF_SQL
	int CheckLevel = SQL_USERLEVEL_NORMAL;
#endif
	
	if(TeamChat && m_apPlayers[ClientID])
	{
		CheckTeam = true;
		if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
			CheckTeam = TEAM_SPECTATORS;
		if(m_apPlayers[ClientID]->IsInfected())
			CheckTeam = TEAM_RED;
		else
			CheckTeam = TEAM_BLUE;
	}
	
	char aNameFound[32];
	aNameFound[0] = 0;
	
	char aChatTitle[32];
	aChatTitle[0] = 0;
	unsigned int c = 0;
	for(; c<sizeof(aNameFound)-1; c++)
	{
		if(pStr[c] == ' ' || pStr[c] == 0)
		{
			if(str_comp(aNameFound, "!near") == 0)
			{
				if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
				{
					CheckDistance = true;
					CheckDistancePos = m_apPlayers[ClientID]->GetCharacter()->m_Pos;
					str_copy(aChatTitle, "near", sizeof(aChatTitle));
				}
			}
#ifdef CONF_SQL
			else if(str_comp(aNameFound, "!mod") == 0)
			{
				if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
				{
					CheckLevel = SQL_USERLEVEL_MOD;
					CheckDistancePos = m_apPlayers[ClientID]->GetCharacter()->m_Pos;
					str_copy(aChatTitle, "moderators", sizeof(aChatTitle));
				}
			}
#endif
			else if(str_comp(aNameFound, "!engineer") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_ENGINEER;
				str_copy(aChatTitle, "engineer", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!soldier ") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SOLDIER;
				str_copy(aChatTitle, "soldier", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!scientist") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SCIENTIST;
				str_copy(aChatTitle, "scientist", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!biologist") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_BIOLOGIST;
				str_copy(aChatTitle, "biologist", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!medic") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_MEDIC;
				str_copy(aChatTitle, "medic", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!hero") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_HERO;
				str_copy(aChatTitle, "hero", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ninja") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_NINJA;
				str_copy(aChatTitle, "ninja", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!mercenary") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_MERCENARY;
				str_copy(aChatTitle, "mercenary", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!sniper") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SNIPER;
				str_copy(aChatTitle, "sniper", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!smoker") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SMOKER;
				str_copy(aChatTitle, "smoker", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!hunter") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_HUNTER;
				str_copy(aChatTitle, "hunter", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!boomer") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_BOOMER;
				str_copy(aChatTitle, "boomer", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!spider") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SPIDER;
				str_copy(aChatTitle, "spider", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ghost") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_GHOST;
				str_copy(aChatTitle, "ghost", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ghoul") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_GHOUL;
				str_copy(aChatTitle, "ghoul", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!slug") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SLUG;
				str_copy(aChatTitle, "slug", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!undead") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_UNDEAD;
				str_copy(aChatTitle, "undead", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!witch") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_WITCH;
				str_copy(aChatTitle, "witch", sizeof(aChatTitle));
			}
			else
			{
				for(int i=0; i<MAX_CLIENTS; i++)
				{
					if(m_apPlayers[i] && str_comp(Server()->ClientName(i), aNameFound) == 0)
					{
						CheckID = i;
						str_copy(aChatTitle, "private", sizeof(aChatTitle));
						CheckTeam = -1;
						break;
					}
				}
			}
		}
		
		if(aChatTitle[0] || pStr[c] == 0)
		{
			aNameFound[c] = 0;
			break;
		}
		else
		{
			aNameFound[c] = pStr[c];
			aNameFound[c+1] = 0;
		}
	}
		
	if(!aChatTitle[0])
	{
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("No player was found with this name"));
		return true;
	}
	
	pStr += c;
	while(*pStr == ' ')
		pStr++;
	
	dynamic_string Buffer;
	Buffer.copy(pStr);
	Server()->Localization()->ArabicShaping(Buffer);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = (TeamChat ? 1 : 0);
	Msg.m_ClientID = ClientID;
	
	int NumPlayerFound = 0;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			if(i != ClientID)
			{
				if(CheckTeam >= 0)
				{
					if(CheckTeam == TEAM_SPECTATORS && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
						continue;
					else if(CheckTeam == TEAM_RED && !m_apPlayers[i]->IsInfected())
						continue;
					else if(CheckTeam == TEAM_BLUE && m_apPlayers[i]->IsInfected())
						continue;
				}
				
#ifdef CONF_SQL
				if(Server()->GetUserLevel(i) < CheckLevel)
					continue;
#endif
				
				if(CheckID >= 0 && !(i == CheckID))
					continue;
				
				if(CheckClass >= 0 && !(m_apPlayers[i]->GetClass() == CheckClass))
					continue;
				
				if(CheckDistance && !(m_apPlayers[i]->GetCharacter() && distance(m_apPlayers[i]->GetCharacter()->m_Pos, CheckDistancePos) < 1000.0f))
					continue;
			}
			
			FinalMessage.clear();
			TextIter = 0;
			if(i == ClientID)
			{
				if(str_comp(aChatTitle, "private") == 0)
				{
					TextIter = FinalMessage.append_at(TextIter, aNameFound);
					TextIter = FinalMessage.append_at(TextIter, " (");
					TextIter = FinalMessage.append_at(TextIter, aChatTitle);
					TextIter = FinalMessage.append_at(TextIter, "): ");
				}
				else
				{
					TextIter = FinalMessage.append_at(TextIter, "(");
					TextIter = FinalMessage.append_at(TextIter, aChatTitle);
					TextIter = FinalMessage.append_at(TextIter, "): ");
				}
				TextIter = FinalMessage.append_at(TextIter, Buffer.buffer());
			}
			else
			{
				TextIter = FinalMessage.append_at(TextIter, Server()->ClientName(i));
				TextIter = FinalMessage.append_at(TextIter, " (");
				TextIter = FinalMessage.append_at(TextIter, aChatTitle);
				TextIter = FinalMessage.append_at(TextIter, "): ");
				TextIter = FinalMessage.append_at(TextIter, Buffer.buffer());
			}
			Msg.m_pMessage = FinalMessage.buffer();
	
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
				
			NumPlayerFound++;
		}
	}
	
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

bool CGameContext::ConSetEmail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pEmail = pResult->GetString(0);
	
	pSelf->Server()->SetEmail(ClientID, pEmail);
	
	return true;
}

bool CGameContext::ConChallenge(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	pSelf->Server()->ShowChallenge(ClientID);
	
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
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_HERO_SCORE);
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
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SLUG_SCORE);
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
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_HERO_SCORE);
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
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SLUG_SCORE);
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
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_HERO_SCORE);
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
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

#endif

bool CGameContext::ConHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	const char *pHelpPage = (pResult->NumArguments()>0) ? pResult->GetString(0) : 0x0;

	dynamic_string Buffer;
	
	if(pHelpPage)
	{
		if(str_comp_nocase(pHelpPage, "game") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Rules of the game"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass is a team game between humans and infected."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("All players start as human."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("10 seconds later, two players become infected."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for humans is to survive until the army clean the map."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for infected is to infect all humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "translate") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("How to translate the mod"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Create an account on Crowdin and join a translation team:"), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "https://crowdin.com/project/teeuniverse", NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("For any question about the translation process, please contact us on IRC ({str:IRCAddress})"), "IRCAddress", "QuakeNet, #infclass", NULL);

			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "engineer") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Engineer"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Engineer can build walls with his hammer to block infected."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("When an infected touch the wall, he dies."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The lifespan of a wall is {sec:LifeSpan}, and walls are limited to one per player at the same time."), "LifeSpan", &g_Config.m_InfBarrierLifeSpan, NULL); 
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "soldier") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Soldier"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Soldier can pose floating bombs with his hammer."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfSoldierBombs,
				_P("Each bomb can explode one time.", "Each bomb can explode {int:NumBombs} times."),
				"NumBombs", &g_Config.m_InfSoldierBombs,
				NULL
			);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use the hammer to place the bomb and explode it multiple times."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Bombs are limited to one per player at the same time."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "scientist") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Scientist"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Scientist can pose floating mines with his hammer."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfMineLimit,
				_P("Mines are limited to one per player at the same time.", "Mines are limited to {int:NumMines} per player at the same time."),
				"NumMines", &g_Config.m_InfMineLimit,
				NULL
			);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He has also grenades that teleport him."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "biologist") == 0)
		{

			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Biologist"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The biologist has a shotgun with bouncing bullets and can create a spring laser trap by shooting with his rifle."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "medic") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Medic"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Medic can protect humans with his hammer by giving them armor."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He has also a powerful shotgun that can pullback infected."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "hero") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Hero"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Hero has a shotgun, a laser rifle and grenades."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Hero must find a flag hidden in the map."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Once taken, the flag gives 1 health point, 4 armor points, and full ammo to all humans, furthermore full health and armor to the hero."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The hero cannot be healed by a medic, but he can withstand a thrust by an infected, an his health suffice."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "ninja") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ninja"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Ninja can throw flash grenades that can freeze infected during three seconds."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfNinjaJump,
				_P("His hammer is replaced by a katana, allowing him to jump one time before touching the ground.", "His hammer is replaced by a katana, allowing him to jump {int:NinjaJump} times before touching the ground."),
				"NinjaJump", &g_Config.m_InfNinjaJump,
				NULL
			);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "mercenary") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Mercenary"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Mercenary fly in air using his machine gun."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can coil explosives with his hammer that hinder infected from harming."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfPoisonDamage,
				_P("He can also throw poison grenades that each deal one damage point.", "He can also throw poison grenades that each deal {int:NumDamagePoints} damage points."),
				"NumDamagePoints", &g_Config.m_InfPoisonDamage,
				NULL
			);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "sniper") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Sniper"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Sniper can lock his position in air for 15 seconds with his hammer."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can jump two times in air."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He has also a powerful rifle that deals 30 damage points in locked position, and 10–13 otherwise."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "smoker") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Smoker"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Smoker can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 4 damage points per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "boomer") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Boomer"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Boomer explodes when he attack."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("All humans affected by the explosion become infected."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "hunter") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Hunter"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Hunter can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can jump two times in air."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "ghost") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ghost"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Ghost can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He is invisible, except if a human is near him, if he takes a damage or if he use his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "spider") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Spider"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Spider can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("When selecting any gun, his hook enter in web mode."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Any human that touch a hook in web mode is automatically grabbed."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The hook of the spider (in both mode) deal 1 damage point per second and can grab a human during 2 seconds."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "ghoul") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ghoul"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Ghoul can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can devour all that has died close to him, which makes him stronger, faster and more resistant."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Thereupon he digests his fodder bit by bit going back to his normal state, and besides, death bereaves him of his nourishment."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "slug") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Slug"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Slug can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can make the ground and walls toxic by spreading slime with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Touching the slime inflicts three damage points in three seconds on a human."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "undead") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Undead"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Undead can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Instead of dying, he freezes during 10 seconds."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("If an infected heals him, the freeze effect disappear."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "witch") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Witch")); 
			Buffer.append(" ~~\n\n");
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Witch can infect humans and heal infected with his hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("When an infected dies, he may re-spawn near her."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("If the Witch dies, she disappears and is replaced by another class of infected."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("She can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "msg") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Targeted chat messages")); 
			Buffer.append(" ~~\n\n");
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg <PlayerName> <My Message>” to send a private message to this player."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !<ClassName> <My Message>” to send a private message to all players with a specific class."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/msg !medic I'm wounded!”"), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !near” to send a private message to all players near you."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else
			pHelpPage = 0x0;
	}
	
	if(!pHelpPage)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", pSelf->Server()->Localization()->Localize(pLanguage, _("Choose a help page with /help <page>")));
		
		dynamic_string Buffer;
		pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Available help pages: {str:PageList}"),
			"PageList", "game, translate, msg",
			NULL
		);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", Buffer.buffer());
		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "engineer, soldier, scientist, medic, hero, ninja, mercenary, sniper,");		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "smoker, hunter, boomer, ghost, spider, ghoul, undead, witch.");		
	}
	
	return true;
}

bool CGameContext::ConAntiPing(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	int Arg = pResult->GetInteger(0);

	if(Arg > 0)
		pSelf->Server()->SetClientAntiPing(ClientID, 1);
	else
		pSelf->Server()->SetClientAntiPing(ClientID, 0);
	
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
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	int Arg = pResult->GetInteger(0);

	if(Arg > 0)
	{
		pSelf->Server()->SetClientAlwaysRandom(ClientID, 1);
		const char* pTxtAlwaysRandomOn = pSelf->Server()->Localization()->Localize(pLanguage, _("A random class will be automatically attributed to you when rounds start"));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "alwaysrandom", pTxtAlwaysRandomOn);		
	}
	else
	{
		pSelf->Server()->SetClientAlwaysRandom(ClientID, 0);
		const char* pTxtAlwaysRandomOff = pSelf->Server()->Localization()->Localize(pLanguage, _("The class selector will be displayed when rounds start"));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "alwaysrandom", pTxtAlwaysRandomOff);		
	}
	
	return true;
}

bool CGameContext::ConLanguage(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	
	const char *pLanguageCode = (pResult->NumArguments()>0) ? pResult->GetString(0) : 0x0;
	char aFinalLanguageCode[8];
	aFinalLanguageCode[0] = 0;

	if(pLanguageCode)
	{
		if(str_comp_nocase(pLanguageCode, "ua") == 0)
			str_copy(aFinalLanguageCode, "uk", sizeof(aFinalLanguageCode));
		else
		{
			for(int i=0; i<pSelf->Server()->Localization()->m_pLanguages.size(); i++)
			{
				if(str_comp_nocase(pLanguageCode, pSelf->Server()->Localization()->m_pLanguages[i]->GetFilename()) == 0)
					str_copy(aFinalLanguageCode, pLanguageCode, sizeof(aFinalLanguageCode));
			}
		}
	}
	
	if(aFinalLanguageCode[0])
	{
		pSelf->Server()->SetClientLanguage(ClientID, aFinalLanguageCode);
		if(pSelf->m_apPlayers[ClientID])
			pSelf->m_apPlayers[ClientID]->SetLanguage(aFinalLanguageCode);
	}
	else
	{
		const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
		const char* pTxtUnknownLanguage = pSelf->Server()->Localization()->Localize(pLanguage, _("Unknown language"));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "language", pTxtUnknownLanguage);	
		
		dynamic_string BufferList;
		int BufferIter = 0;
		for(int i=0; i<pSelf->Server()->Localization()->m_pLanguages.size(); i++)
		{
			if(i>0)
				BufferIter = BufferList.append_at(BufferIter, ", ");
			BufferIter = BufferList.append_at(BufferIter, pSelf->Server()->Localization()->m_pLanguages[i]->GetFilename());
		}
		
		dynamic_string Buffer;
		pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Available languages: {str:ListOfLanguage}"), "ListOfLanguage", BufferList.buffer(), NULL);
		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "language", Buffer.buffer());
	}
	
	return true;
}

bool CGameContext::ConCmdList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	dynamic_string Buffer;
	
	Buffer.append("~~ ");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("List of commands")); 
	Buffer.append(" ~~\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/antiping, /alwaysrandom, /customskin, /help, /info, /language", NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/msg", NULL);
	Buffer.append("\n\n");
#ifdef CONF_SQL
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/register, /login, /logout, /setemail", NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/challenge, /top10, /rank, /goal", NULL);
	Buffer.append("\n\n");
#endif
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Press <F3> or <F4> to enable or disable hook protection"), NULL);
			
	pSelf->SendMOTD(ClientID, Buffer.buffer());
	
	return true;
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

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
	Console()->Register("setemail", "s<email>", CFGFLAG_CHAT|CFGFLAG_USER, ConSetEmail, this, "Change your email");
	
	Console()->Register("top10", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConTop10, this, "Show the top 10 on the current map");
	Console()->Register("challenge", "", CFGFLAG_CHAT|CFGFLAG_USER, ConChallenge, this, "Show the current winner of the challenge");
	Console()->Register("rank", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConRank, this, "Show your rank");
	Console()->Register("goal", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConGoal, this, "Show your goal");
#endif
	Console()->Register("help", "?s<page>", CFGFLAG_CHAT|CFGFLAG_USER, ConHelp, this, "Display help");
	Console()->Register("customskin", "s<all|me|none>", CFGFLAG_CHAT|CFGFLAG_USER, ConCustomSkin, this, "Display information about the mod");
	Console()->Register("alwaysrandom", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAlwaysRandom, this, "Display information about the mod");
	Console()->Register("antiping", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAntiPing, this, "Try to improve your ping");
	Console()->Register("language", "s<en|fr|nl|de|bg|sr-Latn|hr|cs|pl|uk|ru|el|la|it|es|pt|hu|ar|tr|sah|fa|tl|zh-Hans|ja>", CFGFLAG_CHAT|CFGFLAG_USER, ConLanguage, this, "Display information about the mod");
	Console()->Register("cmdlist", "", CFGFLAG_CHAT|CFGFLAG_USER, ConCmdList, this, "List of commands");
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
		str_copy(m_VoteLanguage[i], "en", sizeof(m_VoteLanguage[i]));				
	}

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);
	
	//Get zones
	m_ZoneHandle_Damage = m_Collision.GetZoneHandle("icDamage");
	m_ZoneHandle_Teleport = m_Collision.GetZoneHandle("icTele");
	m_ZoneHandle_Bonus = m_Collision.GetZoneHandle("icBonus");

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// select gametype
	m_pController = new CGameControllerMOD(this);

	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

	// create all entities from entity layers
	if(m_Layers.EntityGroup())
	{
		char aLayerName[12];
		
		const CMapItemGroup* pGroup = m_Layers.EntityGroup();
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = m_Layers.GetLayer(pGroup->m_StartLayer+l);
			if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;
			
				IntsToStr(pQLayer->m_aName, sizeof(aLayerName)/sizeof(int), aLayerName);
				
				const CQuad *pQuads = (const CQuad *) Kernel()->RequestInterface<IMap>()->GetDataSwapped(pQLayer->m_Data);

				for(int q = 0; q < pQLayer->m_NumQuads; q++)
				{
					vec2 P0(fx2f(pQuads[q].m_aPoints[0].x), fx2f(pQuads[q].m_aPoints[0].y));
					vec2 P1(fx2f(pQuads[q].m_aPoints[1].x), fx2f(pQuads[q].m_aPoints[1].y));
					vec2 P2(fx2f(pQuads[q].m_aPoints[2].x), fx2f(pQuads[q].m_aPoints[2].y));
					vec2 P3(fx2f(pQuads[q].m_aPoints[3].x), fx2f(pQuads[q].m_aPoints[3].y));
					vec2 Pivot(fx2f(pQuads[q].m_aPoints[4].x), fx2f(pQuads[q].m_aPoints[4].y));
					m_pController->OnEntity(aLayerName, Pivot, P0, P1, P2, P3, pQuads[q].m_PosEnv);
				}
			}
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
	//reset votes.
	EndVote();

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
		if(ClientID >= 0)
		{
			vec2 CheckPos = (m_LaserDots[i].m_Pos0 + m_LaserDots[i].m_Pos1)*0.5f;
			float dx = m_apPlayers[ClientID]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientID]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientID]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserDots[i].m_SnapID, sizeof(CNetObj_Laser)));
		if(pObj)
		{
			pObj->m_X = (int)m_LaserDots[i].m_Pos1.x;
			pObj->m_Y = (int)m_LaserDots[i].m_Pos1.y;
			pObj->m_FromX = (int)m_LaserDots[i].m_Pos0.x;
			pObj->m_FromY = (int)m_LaserDots[i].m_Pos0.y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
	for(int i=0; i < m_HammerDots.size(); i++)
	{
		if(ClientID >= 0)
		{
			vec2 CheckPos = m_HammerDots[i].m_Pos;
			float dx = m_apPlayers[ClientID]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientID]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientID]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_HammerDots[i].m_SnapID, sizeof(CNetObj_Projectile)));
		if(pObj)
		{
			pObj->m_X = (int)m_HammerDots[i].m_Pos.x;
			pObj->m_Y = (int)m_HammerDots[i].m_Pos.y;
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;
			pObj->m_StartTick = Server()->Tick();
			pObj->m_Type = WEAPON_HAMMER;
		}
	}
	for(int i=0; i < m_LoveDots.size(); i++)
	{
		if(ClientID >= 0)
		{
			vec2 CheckPos = m_LoveDots[i].m_Pos;
			float dx = m_apPlayers[ClientID]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientID]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientID]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Pickup *pObj = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_LoveDots[i].m_SnapID, sizeof(CNetObj_Pickup)));
		if(pObj)
		{
			pObj->m_X = (int)m_LoveDots[i].m_Pos.x;
			pObj->m_Y = (int)m_LoveDots[i].m_Pos.y;
			pObj->m_Type = POWERUP_HEALTH;
			pObj->m_Subtype = 0;
		}
	}
/* INFECTION MODIFICATION END *****************************************/
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}

int CGameContext::GetTargetToKill()
{
	return m_TargetToKill;
}
void CGameContext::TargetKilled()
{
	m_TargetToKill = -1;
	
	int PlayerCounter = 0;
	CPlayerIterator<PLAYERITER_INGAME> Iter(m_apPlayers);
	while(Iter.Next())
		PlayerCounter++;
	
	m_TargetToKillCoolDown = Server()->TickSpeed()*(10 + 3*max(0, 16 - PlayerCounter));
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

void CGameContext::List(int ClientID, const char* filter)
{
	int total = 0;
	char buf[256];
	int bufcnt = 0;
	if (filter[0])
		str_format(buf, sizeof(buf), "Listing players with \"%s\" in name:", filter);
	else
		str_format(buf, sizeof(buf), "Listing all players:", filter);
	SendChatTarget(ClientID, buf);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			total++;
			const char* name = Server()->ClientName(i);
			if (str_find_nocase(name, filter) == NULL)
				continue;
			if (bufcnt + str_length(name) + 4 > 256)
			{
				SendChatTarget(ClientID, buf);
				bufcnt = 0;
			}
			if (bufcnt != 0)
			{
				str_format(&buf[bufcnt], sizeof(buf) - bufcnt, ", %s", name);
				bufcnt += 2 + str_length(name);
			}
			else
			{
				str_format(&buf[bufcnt], sizeof(buf) - bufcnt, "%s", name);
				bufcnt += str_length(name);
			}
		}
	}
	if (bufcnt != 0)
		SendChatTarget(ClientID, buf);
	str_format(buf, sizeof(buf), "%d players online", total);
	SendChatTarget(ClientID, buf);
}
