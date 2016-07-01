/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/system.h>
#include <base/tl/array.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/mapchecker.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>
#include <game/mapitems.h>
#include <game/gamecore.h>

#include <mastersrv/mastersrv.h>

#include "register.h"
#include "server.h"

/* INFECTION MODIFICATION START ***************************************/
#include <fstream>
#include <sstream>
#include <iostream>
#include <engine/server/mapconverter.h>
/* INFECTION MODIFICATION END *****************************************/

#if defined(CONF_FAMILY_WINDOWS)
	#define _WIN32_WINNT 0x0501
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

static const char *StrLtrim(const char *pStr)
{
	while(*pStr && *pStr >= 0 && *pStr <= 32)
		pStr++;
	return pStr;
}

static void StrRtrim(char *pStr)
{
	int i = str_length(pStr);
	while(i >= 0)
	{
		if(pStr[i] < 0 || pStr[i] > 32)
			break;
		pStr[i] = 0;
		i--;
	}
}


CSnapIDPool::CSnapIDPool()
{
	Reset();
}

void CSnapIDPool::Reset()
{
	for(int i = 0; i < MAX_IDS; i++)
	{
		m_aIDs[i].m_Next = i+1;
		m_aIDs[i].m_State = 0;
	}

	m_aIDs[MAX_IDS-1].m_Next = -1;
	m_FirstFree = 0;
	m_FirstTimed = -1;
	m_LastTimed = -1;
	m_Usage = 0;
	m_InUsage = 0;
}


void CSnapIDPool::RemoveFirstTimeout()
{
	int NextTimed = m_aIDs[m_FirstTimed].m_Next;

	// add it to the free list
	m_aIDs[m_FirstTimed].m_Next = m_FirstFree;
	m_aIDs[m_FirstTimed].m_State = 0;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if(m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIDPool::NewID()
{
	int64 Now = time_get();

	// process timed ids
	while(m_FirstTimed != -1 && m_aIDs[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int ID = m_FirstFree;
	dbg_assert(ID != -1, "id error");
	if(ID == -1)
		return ID;
	m_FirstFree = m_aIDs[m_FirstFree].m_Next;
	m_aIDs[ID].m_State = 1;
	m_Usage++;
	m_InUsage++;
	return ID;
}

void CSnapIDPool::TimeoutIDs()
{
	// process timed ids
	while(m_FirstTimed != -1)
		RemoveFirstTimeout();
}

void CSnapIDPool::FreeID(int ID)
{
	if(ID < 0)
		return;
	dbg_assert(m_aIDs[ID].m_State == 1, "id is not alloced");

	m_InUsage--;
	m_aIDs[ID].m_State = 2;
	m_aIDs[ID].m_Timeout = time_get()+time_freq()*5;
	m_aIDs[ID].m_Next = -1;

	if(m_LastTimed != -1)
	{
		m_aIDs[m_LastTimed].m_Next = ID;
		m_LastTimed = ID;
	}
	else
	{
		m_FirstTimed = ID;
		m_LastTimed = ID;
	}
}


void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer* pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s?ir", CFGFLAG_SERVER|CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
}

template<class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if(Server()->m_RconClientID >= 0 && Server()->m_RconClientID < MAX_CLIENTS &&
		Server()->m_aClients[Server()->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(i == Server()->m_RconClientID || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientID == IServer::RCON_CID_VOTE)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed != CServer::AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if(Result != 0)
		return Result;

	// drop banned clients
	typename T::CDataType Data = *pData;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if(NetMatch(&Data, Server()->m_NetServer.ClientAddr(i)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(i, CLIENTDROPTYPE_BAN, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

void CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";

	if(StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes*60, pReason);
	}
	else
		ConBan(pResult, pUser);
}

/* INFECTION MODIFICATION START ***************************************/
void CServer::CClient::Reset(bool ResetScore)
{
	// reset input
	for(int i = 0; i < 200; i++)
		m_aInputs[i].m_GameTick = -1;
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_SnapRate = CClient::SNAPRATE_INIT;
	
	if(ResetScore)
	{
		m_Score = 0;
		m_NbRound = 0;
		m_NbInfection = 0;
		m_Language = LANGUAGE_EN;
		m_WaitingTime = 0;
		m_WasInfected = 0;
	}
}
/* INFECTION MODIFICATION END *****************************************/

CServer::CServer() : m_DemoRecorder(&m_SnapshotDelta)
{
	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;

	m_CurrentGameTick = 0;
	m_RunServer = 1;

	m_pCurrentMapData = 0;
	m_CurrentMapSize = 0;

	m_MapReload = 0;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	Init();
}


int CServer::TrySetClientName(int ClientID, const char *pName)
{
	char aTrimmedName[64];

	// trim the name
	str_copy(aTrimmedName, StrLtrim(pName), sizeof(aTrimmedName));
	StrRtrim(aTrimmedName);

	// check for empty names
	if(!aTrimmedName[0])
		return -1;

	// check if new and old name are the same
	if(m_aClients[ClientID].m_aName[0] && str_comp(m_aClients[ClientID].m_aName, aTrimmedName) == 0)
		return 0;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "'%s' -> '%s'", pName, aTrimmedName);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
	pName = aTrimmedName;

	// make sure that two clients doesn't have the same name
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(i != ClientID && m_aClients[i].m_State >= CClient::STATE_READY)
		{
			if(str_comp(pName, m_aClients[i].m_aName) == 0)
				return -1;
		}

	// set the client name
	str_copy(m_aClients[ClientID].m_aName, pName, MAX_NAME_LENGTH);
	return 0;
}



void CServer::SetClientName(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	if(!pName)
		return;

	char aCleanName[MAX_NAME_LENGTH];
	str_copy(aCleanName, pName, sizeof(aCleanName));

	// clear name
	//~ for(char *p = aCleanName; *p; ++p)
	//~ {
		//~ if(*p < 32)
			//~ *p = ' ';
	//~ }

	if(TrySetClientName(ClientID, aCleanName))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			char aNameTry[MAX_NAME_LENGTH];
			str_format(aNameTry, sizeof(aCleanName), "(%d)%s", i, aCleanName);
			if(TrySetClientName(ClientID, aNameTry) == 0)
				break;
		}
	}
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientID].m_aClan, pClan, MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::SetClientScore(int ClientID, int Score)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;
	m_aClients[ClientID].m_Score = Score;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
 		return;
	}
	else if(m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
 		return;
	}

	m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

int64 CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq()*Tick)/SERVER_TICK_SPEED;
}

/*int CServer::TickSpeed()
{
	return SERVER_TICK_SPEED;
}*/

int CServer::Init()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClients[i].m_State = CClient::STATE_EMPTY;
		m_aClients[i].m_aName[0] = 0;
		m_aClients[i].m_aClan[0] = 0;
		m_aClients[i].m_Country = -1;
		m_aClients[i].m_Snapshots.Init();
	}

	m_CurrentGameTick = 0;
		
/* INFECTION MODIFICATION START ***************************************/
	SetFireDelay(INFWEAPON_NONE, 0);
	SetFireDelay(INFWEAPON_HAMMER, 125);
	SetFireDelay(INFWEAPON_GUN, 125);
	SetFireDelay(INFWEAPON_SHOTGUN, 500);
	SetFireDelay(INFWEAPON_GRENADE, 500);
	SetFireDelay(INFWEAPON_RIFLE, 800);
	SetFireDelay(INFWEAPON_NINJA, 800);
	SetFireDelay(INFWEAPON_ENGINEER_RIFLE, GetFireDelay(INFWEAPON_RIFLE));
	SetFireDelay(INFWEAPON_SNIPER_RIFLE, GetFireDelay(INFWEAPON_RIFLE));
	SetFireDelay(INFWEAPON_SOLDIER_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_SCIENTIST_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MEDIC_SHOTGUN, 250);
	SetFireDelay(INFWEAPON_NINJA_HAMMER, GetFireDelay(INFWEAPON_NINJA));
	SetFireDelay(INFWEAPON_NINJA_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MERCENARY_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MERCENARY_GUN, 50);
	
	SetAmmoRegenTime(INFWEAPON_NONE, 0);
	SetAmmoRegenTime(INFWEAPON_HAMMER, 0);
	SetAmmoRegenTime(INFWEAPON_GUN, 500);
	SetAmmoRegenTime(INFWEAPON_SHOTGUN, 0);
	SetAmmoRegenTime(INFWEAPON_GRENADE, 0);
	SetAmmoRegenTime(INFWEAPON_RIFLE, 0);
	SetAmmoRegenTime(INFWEAPON_NINJA, 0);
	SetAmmoRegenTime(INFWEAPON_ENGINEER_RIFLE, 6000);
	SetAmmoRegenTime(INFWEAPON_SNIPER_RIFLE, 4000);
	SetAmmoRegenTime(INFWEAPON_SOLDIER_GRENADE, 7000);
	SetAmmoRegenTime(INFWEAPON_SCIENTIST_GRENADE, 10000);
	SetAmmoRegenTime(INFWEAPON_MEDIC_SHOTGUN, 750);
	SetAmmoRegenTime(INFWEAPON_NINJA_HAMMER, 0);
	SetAmmoRegenTime(INFWEAPON_NINJA_GRENADE, 15000);
	SetAmmoRegenTime(INFWEAPON_MERCENARY_GRENADE, 5000);
	SetAmmoRegenTime(INFWEAPON_MERCENARY_GUN, 125);
	
	SetMaxAmmo(INFWEAPON_NONE, -1);
	SetMaxAmmo(INFWEAPON_HAMMER, -1);
	SetMaxAmmo(INFWEAPON_GUN, 10);
	SetMaxAmmo(INFWEAPON_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_RIFLE, 10);
	SetMaxAmmo(INFWEAPON_NINJA, 10);
	SetMaxAmmo(INFWEAPON_ENGINEER_RIFLE, 10);
	SetMaxAmmo(INFWEAPON_SNIPER_RIFLE, 10);
	SetMaxAmmo(INFWEAPON_SOLDIER_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_SCIENTIST_GRENADE, 3);
	SetMaxAmmo(INFWEAPON_MEDIC_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON_NINJA_HAMMER, -1);
	SetMaxAmmo(INFWEAPON_NINJA_GRENADE, 5);
	SetMaxAmmo(INFWEAPON_MERCENARY_GRENADE, 8);
	SetMaxAmmo(INFWEAPON_MERCENARY_GUN, 40);
	
	SetClassAvailability(PLAYERCLASS_ENGINEER, 2);
	SetClassAvailability(PLAYERCLASS_SOLDIER, 2);
	SetClassAvailability(PLAYERCLASS_MERCENARY, 2);
	SetClassAvailability(PLAYERCLASS_SNIPER, 2);
	SetClassAvailability(PLAYERCLASS_NINJA, 2);
	SetClassAvailability(PLAYERCLASS_MEDIC, 2);
	SetClassAvailability(PLAYERCLASS_SCIENTIST, 2);
	
	SetClassAvailability(PLAYERCLASS_SMOKER, 1);
	SetClassAvailability(PLAYERCLASS_HUNTER, 1);
	SetClassAvailability(PLAYERCLASS_GHOST, 1);
	SetClassAvailability(PLAYERCLASS_SPIDER, 1);
	SetClassAvailability(PLAYERCLASS_BOOMER, 1);
	SetClassAvailability(PLAYERCLASS_UNDEAD, 1);
	SetClassAvailability(PLAYERCLASS_WITCH, 1);
	
	m_InfClassChooser = 1;
/* INFECTION MODIFICATION END *****************************************/

	return 0;
}

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

bool CServer::IsAuthed(int ClientID)
{
	return m_aClients[ClientID].m_Authed;
}

int CServer::GetClientInfo(int ClientID, CClientInfo *pInfo)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(pInfo != 0, "info can not be null");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = m_aClients[ClientID].m_aName;
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		return 1;
	}
	return 0;
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}


const char *CServer::ClientName(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aName;
	else
		return "(connecting)";

}

const char *CServer::ClientClan(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID)
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

int CServer::MaxClients() const
{
	return m_NetServer.MaxClients();
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID)
{
	return SendMsgEx(pMsg, Flags, ClientID, false);
}

int CServer::SendMsgEx(CMsgPacker *pMsg, int Flags, int ClientID, bool System)
{
	CNetChunk Packet;
	if(!pMsg)
		return -1;

	mem_zero(&Packet, sizeof(CNetChunk));

	Packet.m_ClientID = ClientID;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	// HACK: modify the message id in the packet and store the system flag
	*((unsigned char*)Packet.m_pData) <<= 1;
	if(System)
		*((unsigned char*)Packet.m_pData) |= 1;

	if(Flags&MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags&MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	// write message to demo recorder
	if(!(Flags&MSGFLAG_NORECORD))
		m_DemoRecorder.RecordMessage(pMsg->Data(), pMsg->Size());

	if(!(Flags&MSGFLAG_NOSEND))
	{
		if(ClientID == -1)
		{
			// broadcast
			int i;
			for(i = 0; i < MAX_CLIENTS; i++)
				if(m_aClients[i].m_State == CClient::STATE_INGAME)
				{
					Packet.m_ClientID = i;
					m_NetServer.Send(&Packet);
				}
		}
		else
			m_NetServer.Send(&Packet);
	}
	return 0;
}

void CServer::DoSnapshot()
{
	GameServer()->OnPreSnap();

	// create snapshot for demo recording
	if(m_DemoRecorder.IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];
		int SnapshotSize;

		// build snap and possibly add some messages
		m_SnapshotBuilder.Init();
		GameServer()->OnSnap(-1);
		SnapshotSize = m_SnapshotBuilder.Finish(aData);

		// write snapshot
		m_DemoRecorder.RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	// create snapshots for all clients
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// client must be ingame to recive snapshots
		if(m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick()%50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick()%10) != 0)
			continue;

		{
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot*)aData;	// Fix compiler warning for strict-aliasing
			char aDeltaData[CSnapshot::MAX_SIZE];
			char aCompData[CSnapshot::MAX_SIZE];
			int SnapshotSize;
			int Crc;
			static CSnapshot EmptySnap;
			CSnapshot *pDeltashot = &EmptySnap;
			int DeltashotSize;
			int DeltaTick = -1;
			int DeltaSize;

			m_SnapshotBuilder.Init();

			GameServer()->OnSnap(i);

			// finish snapshot
			SnapshotSize = m_SnapshotBuilder.Finish(pData);
			Crc = pData->Crc();

			// remove old snapshos
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick-SERVER_TICK_SPEED*3);

			// save it the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0);

			// find snapshot that we can preform delta against
			EmptySnap.Clear();

			{
				DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
				if(DeltashotSize >= 0)
					DeltaTick = m_aClients[i].m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_FULL)
						m_aClients[i].m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize)
			{
				// compress it
				int SnapshotSize;
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;
				int NumPackets;

				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData);
				NumPackets = (SnapshotSize+MaxSize-1)/MaxSize;

				for(int n = 0, Left = SnapshotSize; Left; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick-DeltaTick);
				SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);
			}
		}
	}

	GameServer()->OnPostSnap();
}


int CServer::NewClientCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	pThis->m_aClients[ClientID].m_State = CClient::STATE_AUTH;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	pThis->m_aClients[ClientID].m_CustomSkin = 0;
	pThis->m_aClients[ClientID].m_AlwaysRandom = 0;
	pThis->m_aClients[ClientID].m_DefaultScoreMode = PLAYERSCOREMODE_NORMAL;
	pThis->m_aClients[ClientID].m_Language = LANGUAGE_EN;
/* INFECTION MODIFICATION END *****************************************/
	
	pThis->m_aClients[ClientID].Reset();
	return 0;
}

int CServer::DelClientCallback(int ClientID, int Type, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=%s reason='%s'", ClientID, aAddrStr,	pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// notify the mod about the drop
	if(pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY && pThis->m_aClients[ClientID].m_WaitingTime <= 0)
		pThis->GameServer()->OnClientDrop(ClientID, Type, pReason);

	pThis->m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();
	pThis->m_aClients[ClientID].m_WaitingTime = 0;
	return 0;
}

void CServer::SendMap(int ClientID)
{
	m_FastDownloadLastSent[ClientID] = 0;
	m_FastDownloadLastAsk[ClientID] = 0;
	m_FastDownloadLastAskTick[ClientID] = Tick();
	
	CMsgPacker Msg(NETMSG_MAP_CHANGE);
	Msg.AddString(GetMapName(), 0);
	Msg.AddInt(m_CurrentMapCrc);
	Msg.AddInt(m_CurrentMapSize);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true);
}

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE);
	Msg.AddString(pLine, 512);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	static volatile int ReentryGuard = 0;
	int i;

	if(ReentryGuard) return;
	ReentryGuard++;

	for(i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY && pThis->m_aClients[i].m_Authed >= pThis->m_RconAuthLevel)
			pThis->SendRconLine(i, pLine);
	}

	ReentryGuard--;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);
}

void CServer::UpdateClientRconCommands()
{
	int ClientID = Tick() % MAX_CLIENTS;

	if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY && m_aClients[ClientID].m_Authed)
	{
		int ConsoleAccessLevel = m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
		for(int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientID].m_pRconCmdToSend; ++i)
		{
			SendRconCmdAdd(m_aClients[ClientID].m_pRconCmdToSend, ClientID);
			m_aClients[ClientID].m_pRconCmdToSend = m_aClients[ClientID].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
		}
	}
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientID = pPacket->m_ClientID;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);

	// unpack msgid and system flag
	int Msg = Unpacker.GetInt();
	int Sys = Msg&1;
	Msg >>= 1;

	if(Unpacker.Error())
		return;
	
	if(Sys)
	{
		// system message
		if(Msg == NETMSG_INFO)
		{
			if(m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(str_comp(pVersion, "0.6 626fce9a778df4d4") != 0 && str_comp(pVersion, GameServer()->NetVersion()) != 0)
				{
					// wrong version
					char aReason[256];
					str_format(aReason, sizeof(aReason), "Wrong version. Server is running '%s' and client '%s'", GameServer()->NetVersion(), pVersion);
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_VERSION, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(g_Config.m_Password[0] != 0 && str_comp(g_Config.m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_PASSWORD, "Wrong password");
					return;
				}

				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendMap(ClientID);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if(m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			int Chunk = Unpacker.GetInt();
			unsigned int ChunkSize = 1024-128;
			unsigned int Offset = Chunk * ChunkSize;
			int Last = 0;

			m_FastDownloadLastAsk[ClientID] = Chunk;
			m_FastDownloadLastAskTick[ClientID] = Tick();
			if(Chunk == 0)
			{
				m_FastDownloadLastSent[ClientID] = 0;
			}
			
			// drop faulty map data requests
			if(Chunk < 0 || Offset > m_CurrentMapSize)
				return;

			if(Offset+ChunkSize >= m_CurrentMapSize)
			{
				ChunkSize = m_CurrentMapSize-Offset;
				if(ChunkSize < 0)
					ChunkSize = 0;
				Last = 1;
			}
			
			if (g_Config.m_InfFastDownload && m_FastDownloadLastSent[ClientID] < Chunk+g_Config.m_InfMapWindow)
				return;

			CMsgPacker Msg(NETMSG_MAP_DATA);
			Msg.AddInt(Last);
			Msg.AddInt(m_CurrentMapCrc);
			Msg.AddInt(Chunk);
			Msg.AddInt(ChunkSize);
			Msg.AddRaw(&m_pCurrentMapData[Offset], ChunkSize);
			SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true);

			if(g_Config.m_Debug)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
		else if(Msg == NETMSG_READY)
		{
			if(m_aClients[ClientID].m_State == CClient::STATE_CONNECTING)
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%x addr=%s", ClientID, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_READY;
				m_aClients[ClientID].m_WaitingTime = TickSpeed()*g_Config.m_InfConWaitingTime;
			}
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			if(m_aClients[ClientID].m_State == CClient::STATE_READY && GameServer()->IsClientReady(ClientID))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientID=%x addr=%s", ClientID, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				
				if(m_aClients[ClientID].m_WaitingTime <= 0)
				{
					GameServer()->OnClientEnter(ClientID);
				}
			}
		}
		else if(Msg == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64 TagTime;

			m_aClients[ClientID].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size/4 > MAX_INPUT_SIZE)
				return;

			if(m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;

			if(m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientID].m_Latency = (int)(((time_get()-TagTime)*1000)/time_freq());

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick)-time_get())*1000) / time_freq();

				CMsgPacker Msg(NETMSG_INPUTTIMING);
				Msg.AddInt(IntendedTick);
				Msg.AddInt(TimeLeft);
				SendMsgEx(&Msg, 0, ClientID, true);
			}

			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[m_aClients[ClientID].m_CurrentInput];

			if(IntendedTick <= Tick())
				IntendedTick = Tick()+1;

			pInput->m_GameTick = IntendedTick;

			for(int i = 0; i < Size/4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE*sizeof(int));

			m_aClients[ClientID].m_CurrentInput++;
			m_aClients[ClientID].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
				GameServer()->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
		}
		else if(Msg == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();

			if(Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_RconClientID = ClientID;
				m_RconAuthLevel = m_aClients[ClientID].m_Authed;
				Console()->SetAccessLevel(m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD);
				Console()->ExecuteLineFlag(pCmd, CFGFLAG_SERVER);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientID = IServer::RCON_CID_SERV;
				m_RconAuthLevel = AUTHED_ADMIN;
			}
		}
		else if(Msg == NETMSG_RCON_AUTH)
		{
			const char *pPw;
			Unpacker.GetString(); // login name, not used
			pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);

			if(Unpacker.Error() == 0)
			{
				if(g_Config.m_SvRconPassword[0] == 0 && g_Config.m_SvRconModPassword[0] == 0)
				{
					SendRconLine(ClientID, "No rcon password set on server. Set sv_rcon_password and/or sv_rcon_mod_password to enable the remote console.");
				}
				else if(g_Config.m_SvRconPassword[0] && str_comp(pPw, g_Config.m_SvRconPassword) == 0)
				{
					CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
					Msg.AddInt(1);	//authed
					Msg.AddInt(1);	//cmdlist
					SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);

					m_aClients[ClientID].m_Authed = AUTHED_ADMIN;
					int SendRconCmds = Unpacker.GetInt();
					if(Unpacker.Error() == 0 && SendRconCmds)
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
					SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (admin)", ClientID);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(g_Config.m_SvRconModPassword[0] && str_comp(pPw, g_Config.m_SvRconModPassword) == 0)
				{
					CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
					Msg.AddInt(1);	//authed
					Msg.AddInt(1);	//cmdlist
					SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);

					m_aClients[ClientID].m_Authed = AUTHED_MOD;
					int SendRconCmds = Unpacker.GetInt();
					if(Unpacker.Error() == 0 && SendRconCmds)
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
					SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (moderator)", ClientID);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(g_Config.m_SvRconMaxTries)
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, g_Config.m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					if(m_aClients[ClientID].m_AuthTries >= g_Config.m_SvRconMaxTries)
					{
						if(!g_Config.m_SvRconBantime)
							m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), g_Config.m_SvRconBantime*60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}
			}
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY);
			SendMsgEx(&Msg, 0, ClientID, true);
		}
		else
		{
			if(g_Config.m_Debug)
			{
				char aHex[] = "0123456789ABCDEF";
				char aBuf[512];

				for(int b = 0; b < pPacket->m_DataSize && b < 32; b++)
				{
					aBuf[b*3] = aHex[((const unsigned char *)pPacket->m_pData)[b]>>4];
					aBuf[b*3+1] = aHex[((const unsigned char *)pPacket->m_pData)[b]&0xf];
					aBuf[b*3+2] = ' ';
					aBuf[b*3+3] = 0;
				}

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientID=%d msg=%d data_size=%d", ClientID, Msg, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if(m_aClients[ClientID].m_State >= CClient::STATE_READY)
			GameServer()->OnMessage(Msg, &Unpacker, ClientID);
	}
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token)
{
	CNetChunk Packet;
	CPacker p;
	char aBuf[128];

	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	p.Reset();

	p.AddRaw(SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO));
	str_format(aBuf, sizeof(aBuf), "%d", Token);
	p.AddString(aBuf, 6);

	p.AddString(GameServer()->Version(), 32);
	p.AddString(g_Config.m_SvName, 64);
	p.AddString(GetMapName(), 32);

	// gametype
	p.AddString(GameServer()->GameType(), 16);

	// flags
	int i = 0;
	if(g_Config.m_Password[0]) // password set
		i |= SERVER_FLAG_PASSWORD;
	str_format(aBuf, sizeof(aBuf), "%d", i);
	p.AddString(aBuf, 2);

	str_format(aBuf, sizeof(aBuf), "%d", PlayerCount); p.AddString(aBuf, 3); // num players
	str_format(aBuf, sizeof(aBuf), "%d", m_NetServer.MaxClients()-g_Config.m_SvSpectatorSlots); p.AddString(aBuf, 3); // max players
	str_format(aBuf, sizeof(aBuf), "%d", ClientCount); p.AddString(aBuf, 3); // num clients
	str_format(aBuf, sizeof(aBuf), "%d", m_NetServer.MaxClients()); p.AddString(aBuf, 3); // max clients

	for(i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			p.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
			p.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan
			str_format(aBuf, sizeof(aBuf), "%d", m_aClients[i].m_Country); p.AddString(aBuf, 6); // client country
			str_format(aBuf, sizeof(aBuf), "%d", m_aClients[i].m_Score); p.AddString(aBuf, 6); // client score
			str_format(aBuf, sizeof(aBuf), "%d", GameServer()->IsClientPlayer(i)?1:0); p.AddString(aBuf, 2); // is player?
		}
	}

	Packet.m_ClientID = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;
	Packet.m_DataSize = p.Size();
	Packet.m_pData = p.Data();
	m_NetServer.Send(&Packet);
}

void CServer::UpdateServerInfo()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			SendServerInfo(m_NetServer.ClientAddr(i), -1);
	}
}


void CServer::PumpNetwork()
{
	CNetChunk Packet;

	m_NetServer.Update();

	// process packets
	while(m_NetServer.Recv(&Packet))
	{
		if(Packet.m_ClientID == -1)
		{
			// stateless
			if(!m_Register.RegisterProcessPacket(&Packet))
			{
				if(Packet.m_DataSize == sizeof(SERVERBROWSE_GETINFO)+1 &&
					mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
				{
					SendServerInfo(&Packet.m_Address, ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)]);
				}
			}
		}
		else
			ProcessClientPacket(&Packet);
	}
	
	//Thanks, DDNet <3
	if(g_Config.m_InfFastDownload)
	{
		for (int i=0;i<MAX_CLIENTS;i++)
		{
			if(m_aClients[i].m_State != CClient::STATE_CONNECTING)
				continue;
				
			if(m_FastDownloadLastAskTick[i] < Tick()-TickSpeed())
			{
				m_FastDownloadLastSent[i] = m_FastDownloadLastAsk[i];
				m_FastDownloadLastAskTick[i] = Tick();
			}
			if (m_FastDownloadLastAsk[i] < m_FastDownloadLastSent[i]-g_Config.m_InfMapWindow)
				continue;

			int Chunk = m_FastDownloadLastSent[i]++;
			unsigned int ChunkSize = 1024-128;
			unsigned int Offset = Chunk * ChunkSize;
			int Last = 0;

			// drop faulty map data requests
			if(Chunk < 0 || Offset > m_CurrentMapSize)
				continue;
			if(Offset+ChunkSize >= m_CurrentMapSize)
			{
				ChunkSize = m_CurrentMapSize-Offset;
				Last = 1;
			}

			CMsgPacker Msg(NETMSG_MAP_DATA);
			Msg.AddInt(Last);
			Msg.AddInt(m_CurrentMapCrc);
			Msg.AddInt(Chunk);
			Msg.AddInt(ChunkSize);
			Msg.AddRaw(&m_pCurrentMapData[Offset], ChunkSize);
			SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);

			if(g_Config.m_Debug)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}

	m_ServerBan.Update();
	m_Econ.Update();
}

char *CServer::GetMapName()
{
	// get the name of the map without his path
	char *pMapShortName = &g_Config.m_SvMap[0];
	for(int i = 0; i < str_length(g_Config.m_SvMap)-1; i++)
	{
		if(g_Config.m_SvMap[i] == '/' || g_Config.m_SvMap[i] == '\\')
			pMapShortName = &g_Config.m_SvMap[i+1];
	}
	return pMapShortName;
}

int CServer::LoadMap(const char *pMapName)
{
	//DATAFILE *df;
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);

	/*df = datafile_load(buf);
	if(!df)
		return 0;*/

	// check for valid standard map
	if(!m_MapChecker.ReadAndValidateMap(Storage(), aBuf, IStorage::TYPE_ALL))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mapchecker", "invalid standard map");
		return 0;
	}

	if(!m_pMap->Load(aBuf))
		return 0;

/* INFECTION MODIFICATION START ***************************************/
	{
		CMapConverter MapConverter(Storage(), m_pMap, Console());
		if(!MapConverter.Load())
			return 0;
			
		if(!MapConverter.CreateMap())
			return 0;
	}

	// stop recording when we change map
	m_DemoRecorder.Stop();
	
	// reinit snapshot ids
	m_IDPool.TimeoutIDs();

	str_copy(m_aCurrentMap, pMapName, sizeof(m_aCurrentMap));
	//map_set(df);
	
	//Get Crc
	{
		CDataFileReader df;
		df.Open(Storage(), "maps/infc_x_current.map", IStorage::TYPE_ALL);
		
		//Crc
		m_CurrentMapCrc = df.Crc();
		char aBufMsg[256];
		str_format(aBufMsg, sizeof(aBufMsg), "generated map crc is %08x", aBuf, m_CurrentMapCrc);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);
		
		df.Close();
	}
	
	//Data for download
	{	
		IOHANDLE File = Storage()->OpenFile("maps/infc_x_current.map", IOFLAG_READ, IStorage::TYPE_ALL);
		m_CurrentMapSize = (int)io_length(File);
		if(m_pCurrentMapData)
			mem_free(m_pCurrentMapData);
		m_pCurrentMapData = (unsigned char *)mem_alloc(m_CurrentMapSize, 1);
		io_read(File, m_pCurrentMapData, m_CurrentMapSize);
		io_close(File);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", "maps/infc_x_current.map loaded in memory");
	}
	
	{
		g_Config.m_SvTimelimit = 5;
		
		//Open file
		char MapInfoFilename[512];
		str_format(MapInfoFilename, sizeof(MapInfoFilename), "maps/%s.mapinfo", pMapName);
		IOHANDLE File = Storage()->OpenFile(MapInfoFilename, IOFLAG_READ, IStorage::TYPE_ALL);
		if(File)
		{
			char MapInfoLine[512];
			bool isEndOfFile = false;
			while(!isEndOfFile)
			{
				isEndOfFile = true;
				
				//Load one line
				int MapInfoLineLenth = 0;
				char c;
				while(io_read(File, &c, 1))
				{
					isEndOfFile = false;
					
					if(c == '\n') break;
					else
					{
						MapInfoLine[MapInfoLineLenth] = c;
						MapInfoLineLenth++;
					}
				}
				
				MapInfoLine[MapInfoLineLenth] = 0;
				
				//Get the key
				if(str_comp_nocase_num(MapInfoLine, "timelimit ", 10) == 0)
				{
					g_Config.m_SvTimelimit = str_toint(MapInfoLine+10);
				}
			}
		
			io_close(File);
		}
		else
			g_Config.m_SvTimelimit = 5;
	}
/* INFECTION MODIFICATION END *****************************************/
	
	return 1;
}

void CServer::InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, IConsole *pConsole)
{
	m_Register.Init(pNetServer, pMasterServer, pConsole);
}

int CServer::Run()
{
	//
	m_PrintCBIndex = Console()->RegisterPrintCallback(g_Config.m_ConsoleOutputLevel, SendRconLineAuthed, this);

	// load map
	if(!LoadMap(g_Config.m_SvMap))
	{
		dbg_msg("server", "failed to load map. mapname='%s'", g_Config.m_SvMap);
		return -1;
	}

	// start server
	NETADDR BindAddr;
	if(g_Config.m_Bindaddr[0] && net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
	{
		// sweet!
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}
	else
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}

	if(!m_NetServer.Open(BindAddr, &m_ServerBan, g_Config.m_SvMaxClients, g_Config.m_SvMaxClientsPerIP, 0))
	{
		dbg_msg("server", "couldn't open socket. port %d might already be in use", g_Config.m_SvPort);
		return -1;
	}

	m_NetServer.SetCallbacks(NewClientCallback, DelClientCallback, this);

	m_Econ.Init(Console(), &m_ServerBan);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", g_Config.m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	GameServer()->OnInit();
	str_format(aBuf, sizeof(aBuf), "version %s", GameServer()->NetVersion());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// process pending commands
	m_pConsole->StoreCommands(false);

	// start game
	{
		int64 ReportTime = time_get();
		int ReportInterval = 3;

		m_Lastheartbeat = 0;
		m_GameStartTime = time_get();

		if(g_Config.m_Debug)
		{
			str_format(aBuf, sizeof(aBuf), "baseline memory usage %dk", mem_stats()->allocated/1024);
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}

		while(m_RunServer)
		{
			int64 t = time_get();
			int NewTicks = 0;

			// load new map TODO: don't poll this
			if(str_comp(g_Config.m_SvMap, m_aCurrentMap) != 0 || m_MapReload)
			{
				m_MapReload = 0;

				// load map
				if(LoadMap(g_Config.m_SvMap))
				{
					// new map loaded
					GameServer()->OnShutdown();

					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						if(m_aClients[c].m_State <= CClient::STATE_AUTH)
							continue;

						SendMap(c);
/* INFECTION MODIFICATION START ***************************************/
						m_aClients[c].Reset(false);
/* INFECTION MODIFICATION END *****************************************/
						m_aClients[c].m_State = CClient::STATE_CONNECTING;
					}

					m_GameStartTime = time_get();
					m_CurrentGameTick = 0;
					Kernel()->ReregisterInterface(GameServer());
					GameServer()->OnInit();
					UpdateServerInfo();
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), "failed to load map. mapname='%s'", g_Config.m_SvMap);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
					str_copy(g_Config.m_SvMap, m_aCurrentMap, sizeof(g_Config.m_SvMap));
				}
			}

			while(t > TickStartTime(m_CurrentGameTick+1))
			{
				m_CurrentGameTick++;
				NewTicks++;

				for(int i=0; i<MAX_CLIENTS; i++)
				{
					if(m_aClients[i].m_WaitingTime > 0)
					{
						m_aClients[i].m_WaitingTime--;
						if(m_aClients[i].m_WaitingTime <= 0)
						{
							if(m_aClients[i].m_State == CClient::STATE_READY)
							{
								GameServer()->OnClientConnected(i);	
								SendConnectionReady(i);
							}
							else if(m_aClients[i].m_State == CClient::STATE_INGAME)
							{
								GameServer()->OnClientEnter(i);
							}
						}
					}
				}
				
				// apply new input
				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					if(m_aClients[c].m_State == CClient::STATE_EMPTY)
						continue;
					for(int i = 0; i < 200; i++)
					{
						if(m_aClients[c].m_aInputs[i].m_GameTick == Tick())
						{
							if(m_aClients[c].m_State == CClient::STATE_INGAME)
								GameServer()->OnClientPredictedInput(c, m_aClients[c].m_aInputs[i].m_aData);
							break;
						}
					}
				}

				GameServer()->OnTick();
			}

			// snap game
			if(NewTicks)
			{
				if(g_Config.m_SvHighBandwidth || (m_CurrentGameTick%2) == 0)
					DoSnapshot();

				UpdateClientRconCommands();
			}

			// master server stuff
			m_Register.RegisterUpdate(m_NetServer.NetType());

			PumpNetwork();

			if(ReportTime < time_get())
			{
				if(g_Config.m_Debug)
				{
					/*
					static NETSTATS prev_stats;
					NETSTATS stats;
					netserver_stats(net, &stats);

					perf_next();

					if(config.dbg_pref)
						perf_dump(&rootscope);

					dbg_msg("server", "send=%8d recv=%8d",
						(stats.send_bytes - prev_stats.send_bytes)/reportinterval,
						(stats.recv_bytes - prev_stats.recv_bytes)/reportinterval);

					prev_stats = stats;
					*/
				}

				ReportTime += time_freq()*ReportInterval;
			}

			// wait for incomming data
			net_socket_read_wait(m_NetServer.Socket(), 5);
		}
	}
	// disconnect all clients on shutdown
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			m_NetServer.Drop(i, CLIENTDROPTYPE_SHUTDOWN, "Server shutdown");

		m_Econ.Shutdown();
	}

	GameServer()->OnShutdown();
	m_pMap->Unload();

	if(m_pCurrentMapData)
		mem_free(m_pCurrentMapData);
	return 0;
}

void CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	if(pResult->NumArguments() > 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pResult->GetString(1));
		((CServer *)pUser)->Kick(pResult->GetInteger(0), aBuf);
	}
	else
		((CServer *)pUser)->Kick(pResult->GetInteger(0), "Kicked by console");
}

void CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer* pThis = static_cast<CServer *>(pUser);

/* INFECTION MODIFICATION START ***************************************/
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			net_addr_str(pThis->m_NetServer.ClientAddr(i), aAddrStr, sizeof(aAddrStr), true);
			if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
			{
				char aLangBuf[8];
				
				switch(pThis->m_aClients[i].m_Language)
				{
					case LANGUAGE_EN:
						str_copy(aLangBuf, "en", sizeof(aLangBuf));
						break;
					case LANGUAGE_FR:
						str_copy(aLangBuf, "fr", sizeof(aLangBuf));
						break;
					case LANGUAGE_DE:
						str_copy(aLangBuf, "de", sizeof(aLangBuf));
						break;
					case LANGUAGE_UK:
						str_copy(aLangBuf, "uk", sizeof(aLangBuf));
						break;
					case LANGUAGE_RU:
						str_copy(aLangBuf, "ru", sizeof(aLangBuf));
						break;
					case LANGUAGE_IT:
						str_copy(aLangBuf, "it", sizeof(aLangBuf));
						break;
					case LANGUAGE_ES:
						str_copy(aLangBuf, "es", sizeof(aLangBuf));
						break;
					//~ case LANGUAGE_AR:
						//~ str_copy(aLangBuf, "ar", sizeof(aLangBuf));
						//~ break;
				}
				
				const char *pAuthStr = pThis->m_aClients[i].m_Authed == CServer::AUTHED_ADMIN ? "Admin" :
										pThis->m_aClients[i].m_Authed == CServer::AUTHED_MOD ? "Mod" : "";
				str_format(aBuf, sizeof(aBuf), "#%02i (%s) %s, [%s], cs:%i | ar:%i | language:%s",
					i,
					aAddrStr,
					pThis->m_aClients[i].m_aName,
					pAuthStr,
					pThis->m_aClients[i].m_CustomSkin,
					pThis->m_aClients[i].m_AlwaysRandom,
					aLangBuf
				);
			}
			else
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", i, aAddrStr);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
/* INFECTION MODIFICATION END *****************************************/
}

void CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_RunServer = 0;
}

void CServer::DemoRecorder_HandleAutoStart()
{
	if(g_Config.m_SvAutoDemoRecord)
	{
		m_DemoRecorder.Stop();
		char aFilename[128];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_DemoRecorder.Start(Storage(), m_pConsole, aFilename, GameServer()->NetVersion(), m_aCurrentMap, m_CurrentMapCrc, "server");
		if(g_Config.m_SvAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/server", "autorecord", ".demo", g_Config.m_SvAutoDemoMax);
		}
	}
}

bool CServer::DemoRecorder_IsRecording()
{
	return m_DemoRecorder.IsRecording();
}

void CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	CServer* pServer = (CServer *)pUser;
	char aFilename[128];

	if(pResult->NumArguments())
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pResult->GetString(0));
	else
	{
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/demo_%s.demo", aDate);
	}
	pServer->m_DemoRecorder.Start(pServer->Storage(), pServer->Console(), aFilename, pServer->GameServer()->NetVersion(), pServer->m_aCurrentMap, pServer->m_CurrentMapCrc, "server");
}

void CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_DemoRecorder.Stop();
}

void CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_MapReload = 1;
}

void CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
		Msg.AddInt(0);	//authed
		Msg.AddInt(0);	//cmdlist
		pServer->SendMsgEx(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID, true);

		pServer->m_aClients[pServer->m_RconClientID].m_Authed = AUTHED_NO;
		pServer->m_aClients[pServer->m_RconClientID].m_AuthTries = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_pRconCmdToSend = 0;
		pServer->SendRconLine(pServer->m_RconClientID, "Logout successful.");
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out", pServer->m_RconClientID);
		pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->UpdateServerInfo();
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
}

void CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if(pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if(pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(pThis->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY || pThis->m_aClients[i].m_Authed != CServer::AUTHED_MOD ||
					(pThis->m_aClients[i].m_pRconCmdToSend && str_comp(pResult->GetString(0), pThis->m_aClients[i].m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if(OldAccessLevel == IConsole::ACCESS_LEVEL_ADMIN)
					pThis->SendRconCmdAdd(pInfo, i);
				else
					pThis->SendRconCmdRem(pInfo, i);
			}
		}
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
}

/* INFECTION MODIFICATION START ***************************************/
void CServer::ConSetClassAvailability(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pServer = (CServer*) pUserData;
	
	const char *pClassName = pResult->GetString(0);
	int NewValue = pResult->GetInteger(1);
	int ClassId = 0;
	
	if(NewValue < 0) NewValue = 0;
	
	if(str_comp(pClassName, "engineer") == 0) ClassId = PLAYERCLASS_ENGINEER;
	else if(str_comp(pClassName, "soldier") == 0) ClassId = PLAYERCLASS_SOLDIER;
	else if(str_comp(pClassName, "mercenary") == 0) ClassId = PLAYERCLASS_MERCENARY;
	else if(str_comp(pClassName, "sniper") == 0) ClassId = PLAYERCLASS_SNIPER;
	else if(str_comp(pClassName, "scientist") == 0) ClassId = PLAYERCLASS_SCIENTIST;
	else if(str_comp(pClassName, "medic") == 0) ClassId = PLAYERCLASS_MEDIC;
	else if(str_comp(pClassName, "ninja") == 0) ClassId = PLAYERCLASS_NINJA;
	else if(str_comp(pClassName, "smoker") == 0) ClassId = PLAYERCLASS_SMOKER;
	else if(str_comp(pClassName, "hunter") == 0) ClassId = PLAYERCLASS_HUNTER;
	else if(str_comp(pClassName, "ghost") == 0) ClassId = PLAYERCLASS_GHOST;
	else if(str_comp(pClassName, "spider") == 0) ClassId = PLAYERCLASS_SPIDER;
	else if(str_comp(pClassName, "boomer") == 0) ClassId = PLAYERCLASS_BOOMER;
	else if(str_comp(pClassName, "undead") == 0) ClassId = PLAYERCLASS_UNDEAD;
	else if(str_comp(pClassName, "witch") == 0) ClassId = PLAYERCLASS_WITCH;
	else
	{
		pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class_availability", "No such class");
	}
	
	if(NewValue > 2) NewValue = 2;
	
	pServer->SetClassAvailability(ClassId, NewValue);
}

void CServer::ConClassChooser(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pServer = (CServer*) pUserData;
	
	int NewValue = pResult->GetInteger(0);
	
	pServer->m_InfClassChooser = (NewValue > 0 ? 1 : 0);
}

/* INFECTION MODIFICATION END *****************************************/

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	// register console commands
	Console()->Register("kick", "i?r", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "", CFGFLAG_SERVER, ConStatus, this, "List players");
	Console()->Register("shutdown", "", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");

	Console()->Register("record", "?s", CFGFLAG_SERVER|CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);

/* INFECTION MODIFICATION START ***************************************/
	Console()->Register("inf_set_class_availability", "si", CFGFLAG_SERVER, ConSetClassAvailability, this, "Enable/Disable a class");
	Console()->Register("inf_classchooser", "i", CFGFLAG_SERVER, ConClassChooser, this, "Enable/Disable class chooser");
/* INFECTION MODIFICATION END *****************************************/

	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	m_pGameServer->OnConsoleInit();
}


int CServer::SnapNewID()
{
	return m_IDPool.NewID();
}

void CServer::SnapFreeID(int ID)
{
	m_IDPool.FreeID(ID);
}


void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	dbg_assert(Type >= 0 && Type <=0xffff, "incorrect type");
	dbg_assert(ID >= 0 && ID <=0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

int main(int argc, const char **argv) // ignore_convention
{
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; i++) // ignore_convention
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
			break;
		}
	}
#endif

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine("Teeworlds");
	IEngineMap *pEngineMap = CreateEngineMap();
	IGameServer *pGameServer = CreateGameServer();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER|CFGFLAG_ECON);
	IEngineMasterServer *pEngineMasterServer = CreateEngineMasterServer();
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();

	pServer->InitRegister(&pServer->m_NetServer, pEngineMasterServer, pConsole);

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMap*>(pEngineMap)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap*>(pEngineMap));
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pGameServer);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMasterServer*>(pEngineMasterServer)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMasterServer*>(pEngineMasterServer));

		if(RegisterFail)
			return -1;
	}

	pEngine->Init();
	pConfig->Init();
	pEngineMasterServer->Init();
	pEngineMasterServer->Load();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	// parse the command line arguments
	if(argc > 1) // ignore_convention
		pConsole->ParseArguments(argc-1, &argv[1]); // ignore_convention

	// restore empty config strings to their defaults
	pConfig->RestoreStrings();

	pEngine->InitLogfile();

	// run the server
	dbg_msg("server", "starting...");
	pServer->Run();

	// free
	delete pServer;
	delete pKernel;
	delete pEngineMap;
	delete pGameServer;
	delete pConsole;
	delete pEngineMasterServer;
	delete pStorage;
	delete pConfig;
	return 0;
}

/* INFECTION MODIFICATION START ***************************************/
int CServer::IsClientInfectedBefore(int ClientID)
{
	return m_aClients[ClientID].m_WasInfected;
}

void CServer::InfecteClient(int ClientID)
{
	m_aClients[ClientID].m_WasInfected = 1;
	bool NonInfectedFound = false;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CServer::CClient::STATE_INGAME && m_aClients[i].m_WasInfected == 0)
		{
			NonInfectedFound = true;
			break;
		}
	}
	
	if(!NonInfectedFound)
	{
		for(int i=0; i<MAX_CLIENTS; i++)
		{
			m_aClients[i].m_WasInfected = 0;
		}
	}
}

int CServer::GetClientCustomSkin(int ClientID)
{
	return m_aClients[ClientID].m_CustomSkin;
}

void CServer::SetClientCustomSkin(int ClientID, int Value)
{
	m_aClients[ClientID].m_CustomSkin = Value;
}

int CServer::GetClientAlwaysRandom(int ClientID)
{
	return m_aClients[ClientID].m_AlwaysRandom;
}

void CServer::SetClientAlwaysRandom(int ClientID, int Value)
{
	m_aClients[ClientID].m_AlwaysRandom = Value;
}

int CServer::GetClientDefaultScoreMode(int ClientID)
{
	return m_aClients[ClientID].m_DefaultScoreMode;
}

void CServer::SetClientDefaultScoreMode(int ClientID, int Value)
{
	m_aClients[ClientID].m_DefaultScoreMode = Value;
}

int CServer::GetClientLanguage(int ClientID)
{
	return m_aClients[ClientID].m_Language;
}

void CServer::SetClientLanguage(int ClientID, int Value)
{
	m_aClients[ClientID].m_Language = Value;
}
	
int CServer::GetFireDelay(int WID)
{
	return m_InfFireDelay[WID];
}

void CServer::SetFireDelay(int WID, int Time)
{
	m_InfFireDelay[WID] = Time;
}

int CServer::GetAmmoRegenTime(int WID)
{
	return m_InfAmmoRegenTime[WID];
}

void CServer::SetAmmoRegenTime(int WID, int Time)
{
	m_InfAmmoRegenTime[WID] = Time;
}

int CServer::GetMaxAmmo(int WID)
{
	return m_InfMaxAmmo[WID];
}

void CServer::SetMaxAmmo(int WID, int n)
{
	m_InfMaxAmmo[WID] = n;
}

int CServer::GetClassAvailability(int CID)
{
	return m_InfClassAvailability[CID];
}

void CServer::SetClassAvailability(int CID, int n)
{
	m_InfClassAvailability[CID] = n;
}

int CServer::GetClientNbInfection(int ClientID)
{
	return m_aClients[ClientID].m_NbInfection;
}

void CServer::SetClientNbInfection(int ClientID, int Score)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;
	m_aClients[ClientID].m_NbInfection = Score;
}

int CServer::GetClientNbRound(int ClientID)
{
	return m_aClients[ClientID].m_NbRound;
}

void CServer::SetClientNbRound(int ClientID, int Score)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;
	m_aClients[ClientID].m_NbRound = Score;
}

int CServer::GetClientScore(int ClientID)
{
	return m_aClients[ClientID].m_Score;
}

int CServer::IsClassChooserEnabled()
{
	return m_InfClassChooser;
}

/* INFECTION MODIFICATION END *****************************************/

