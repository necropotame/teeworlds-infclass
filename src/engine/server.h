/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H
#include "kernel.h"
#include "message.h"
#include <game/generated/protocol.h>
#include <engine/shared/protocol.h>
#include <string>
#include <vector>

/* INFECTION MODIFICATION START ***************************************/
enum
{
	INFWEAPON_NONE,
	INFWEAPON_HAMMER,
	INFWEAPON_GUN,
	INFWEAPON_SHOTGUN,
	INFWEAPON_GRENADE,
	INFWEAPON_RIFLE,
	INFWEAPON_NINJA,
	
	INFWEAPON_ENGINEER_RIFLE,
	
	INFWEAPON_SNIPER_RIFLE,
	
	INFWEAPON_SOLDIER_GRENADE,
	
	INFWEAPON_SCIENTIST_GRENADE,
	INFWEAPON_SCIENTIST_RIFLE,
	
	INFWEAPON_MEDIC_GRENADE,
	INFWEAPON_MEDIC_RIFLE,
	INFWEAPON_MEDIC_SHOTGUN,
	
	INFWEAPON_HERO_GRENADE,
	INFWEAPON_HERO_RIFLE,
	INFWEAPON_HERO_SHOTGUN,
	
	INFWEAPON_BIOLOGIST_SHOTGUN,
	INFWEAPON_BIOLOGIST_RIFLE,
	
	INFWEAPON_NINJA_HAMMER,
	INFWEAPON_NINJA_GRENADE,
	
	INFWEAPON_MERCENARY_GUN,
	INFWEAPON_MERCENARY_GRENADE,
	
	NB_INFWEAPON
};

enum
{
	PLAYERSCOREMODE_CLASS = 0,
	PLAYERSCOREMODE_SCORE,
	PLAYERSCOREMODE_TIME,
	NB_PLAYERSCOREMODE,
};

enum
{
	CLIENTMEMORY_LANGUAGESELECTION=0,
	CLIENTMEMORY_TOP10,
	CLIENTMEMORY_MOTD,
	CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE,
	CLIENTMEMORY_SESSION_PROCESSED,
	NUM_CLIENTMEMORIES,
};

enum
{
	MAX_ACCUSATIONS = 8,
};

enum
{
	CHATCATEGORY_DEFAULT=0,
	CHATCATEGORY_INFECTION,
	CHATCATEGORY_SCORE,
	CHATCATEGORY_PLAYER,
	CHATCATEGORY_INFECTED,
	CHATCATEGORY_HUMANS,
	CHATCATEGORY_ACCUSATION,
};

/* INFECTION MODIFICATION END *****************************************/

class IServer : public IInterface
{
	MACRO_INTERFACE("server", 0)
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	class CLocalization* m_pLocalization;

public:
	enum
	{
		AUTHED_NO=0,
		AUTHED_MOD,
		AUTHED_ADMIN,
	};
	
public:
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
		bool m_CustClt;
	};
	
	struct CClientSession
	{
		int m_RoundId;
		int m_Class;
		int m_MuteTick;
	};
	
	struct CClientAccusation
	{
		int m_Num;
		NETADDR m_Addresses[MAX_ACCUSATIONS];
	};
	
	virtual ~IServer() {};
	
	inline class CLocalization* Localization() { return m_pLocalization; }

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int MaxClients() const = 0;
	virtual const char *ClientName(int ClientID) = 0;
	virtual const char *ClientClan(int ClientID) = 0;
	virtual int ClientCountry(int ClientID) = 0;
	virtual bool ClientIngame(int ClientID) = 0;
	virtual int GetClientInfo(int ClientID, CClientInfo *pInfo) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) = 0;
	virtual std::string GetClientIP(int ClientID) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) = 0;

	template<class T>
	int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		int result = 0;
		T tmp;
		if (ClientID == -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(ClientIngame(i))
				{
					mem_copy(&tmp, pMsg, sizeof(T));
					result = SendPackMsgTranslate(&tmp, Flags, i);
				}
		} else {
			mem_copy(&tmp, pMsg, sizeof(T));
			result = SendPackMsgTranslate(&tmp, Flags, ClientID);
		}
		return result;
	}

	template<class T>
	int SendPackMsgTranslate(T *pMsg, int Flags, int ClientID)
	{
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_Emoticon *pMsg, int Flags, int ClientID)
	{
		return Translate(pMsg->m_ClientID, ClientID) && SendPackMsgOne(pMsg, Flags, ClientID);
	}

	char msgbuf[1000];

	int SendPackMsgTranslate(CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID)
	{
		if (pMsg->m_ClientID >= 0 && !Translate(pMsg->m_ClientID, ClientID))
		{
			str_format(msgbuf, sizeof(msgbuf), "%s: %s", ClientName(pMsg->m_ClientID), pMsg->m_pMessage);
			pMsg->m_pMessage = msgbuf;
			pMsg->m_ClientID = VANILLA_MAX_CLIENTS - 1;
		}
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_KillMsg *pMsg, int Flags, int ClientID)
	{
		if (!Translate(pMsg->m_Victim, ClientID)) return 0;
		if (!Translate(pMsg->m_Killer, ClientID)) pMsg->m_Killer = pMsg->m_Victim;
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	template<class T>
	int SendPackMsgOne(T *pMsg, int Flags, int ClientID)
	{
		CMsgPacker Packer(pMsg->MsgID());
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientID);
	}

	bool Translate(int& target, int client)
	{
		CClientInfo info;
		GetClientInfo(client, &info);
		if (info.m_CustClt)
			return true;
		int* map = GetIdMap(client);
		bool found = false;
		for (int i = 0; i < VANILLA_MAX_CLIENTS; i++)
		{
			if (target == map[i])
			{
				target = i;
				found = true;
				break;
			}
		}
		return found;
	}

	bool ReverseTranslate(int& target, int client)
	{
		CClientInfo info;
		GetClientInfo(client, &info);
		if (info.m_CustClt)
			return true;
		int* map = GetIdMap(client);
		if (map[target] == -1)
			return false;
		target = map[target];
		return true;
	}

	virtual void SetClientName(int ClientID, char const *pName) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;

	virtual int SnapNewID() = 0;
	virtual void SnapFreeID(int ID) = 0;
	virtual void *SnapNewItem(int Type, int ID, int Size) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	enum
	{
		RCON_CID_SERV=-1,
		RCON_CID_VOTE=-2,
	};
	virtual void SetRconCID(int ClientID) = 0;
	virtual bool IsAuthed(int ClientID) = 0;
	virtual void Kick(int ClientID, const char *pReason) = 0;

	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual bool DemoRecorder_IsRecording() = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	virtual int IsClientInfectedBefore(int ClientID) = 0;
	virtual void InfecteClient(int ClientID) = 0;
	
	virtual int GetClientNbRound(int ClientID) = 0;
	
	virtual int GetClientAntiPing(int ClientID) = 0;
	virtual void SetClientAntiPing(int ClientID, int Value) = 0;
	
	virtual int GetClientCustomSkin(int ClientID) = 0;
	virtual void SetClientCustomSkin(int ClientID, int Value) = 0;
	
	virtual int GetClientAlwaysRandom(int ClientID) = 0;
	virtual void SetClientAlwaysRandom(int ClientID, int Value) = 0;
	
	virtual int GetClientDefaultScoreMode(int ClientID) = 0;
	virtual void SetClientDefaultScoreMode(int ClientID, int Value) = 0;
	
	virtual const char* GetClientLanguage(int ClientID) = 0;
	virtual void SetClientLanguage(int ClientID, const char* pLanguage) = 0;
	
	virtual int GetFireDelay(int WID) = 0;
	virtual void SetFireDelay(int WID, int Time) = 0;
	
	virtual int GetAmmoRegenTime(int WID) = 0;
	virtual void SetAmmoRegenTime(int WID, int Time) = 0;
	
	virtual int GetMaxAmmo(int WID) = 0;
	virtual void SetMaxAmmo(int WID, int n) = 0;
	
	virtual int GetClassAvailability(int CID) = 0;
	virtual void SetClassAvailability(int CID, int n) = 0;
	
	virtual int IsClassChooserEnabled() = 0;
	
	virtual bool IsClientLogged(int ClientID) = 0;
#ifdef CONF_SQL
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword) = 0;
	virtual void Logout(int ClientID) = 0;
	virtual void SetEmail(int ClientID, const char* pEmail) = 0;
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail) = 0;
	virtual void ShowTop10(int ClientID, int ScoreType) = 0;
	virtual void ShowChallenge(int ClientID) = 0;
	virtual void ShowRank(int ClientID, int ScoreType) = 0;
	virtual void ShowGoal(int ClientID, int ScoreType) = 0;
	virtual void ShowStats(int ClientID, int UserId) = 0;
	virtual int GetUserLevel(int ClientID) = 0;
#endif
	virtual void Ban(int i, int Seconds, const char* pReason) = 0;

public:
	virtual class CRoundStatistics* RoundStatistics() = 0;
	virtual void OnRoundStart() = 0;
	virtual void OnRoundEnd() = 0;
	
	virtual void SetClientMemory(int ClientID, int Memory, bool Value = true) = 0;
	virtual void ResetClientMemoryAboutGame(int ClientID) = 0;
	virtual bool GetClientMemory(int ClientID, int Memory) = 0;
	virtual IServer::CClientSession* GetClientSession(int ClientID) = 0;
	virtual void AddAccusation(int From, int To, const char* pReason) = 0;
	virtual bool ClientShouldBeBanned(int ClientID) = 0;
	virtual void RemoveAccusations(int ClientID) = 0;
	
	virtual int GetTimeShiftUnit() const = 0; //In ms
/* INFECTION MODIFICATION END *****************************************/

	virtual int* GetIdMap(int ClientID) = 0;
	virtual void SetCustClt(int ClientID) = 0;
	// InfClassR spectators vector
	std::vector<int> spectators_id;
};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver", 0)
protected:
public:
	virtual void OnInit() = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientID) = 0;
	virtual void OnPostSnap() = 0;

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) = 0;

	virtual void OnClientConnected(int ClientID) = 0;
	virtual void OnClientEnter(int ClientID) = 0;
	virtual void OnClientDrop(int ClientID, int Type, const char *pReason) = 0;
	virtual void OnClientDirectInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientID, void *pInput) = 0;

	virtual bool IsClientReady(int ClientID) = 0;
	virtual bool IsClientPlayer(int ClientID) = 0;

	virtual const char *GameType() = 0;
	virtual const char *Version() = 0;
	virtual const char *NetVersion() = 0;
	
	virtual class CLayers *Layers() = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	virtual void ClearBroadcast(int To, int Priority) = 0;
	virtual void SendBroadcast_Localization(int To, int Priority, int LifeSpan, const char* pText, ...) = 0;
	virtual void SendBroadcast_Localization_P(int To, int Priority, int LifeSpan, int Number, const char* pText, ...) = 0;
	virtual void SendChatTarget(int To, const char* pText) = 0;
	virtual void SendChatTarget_Localization(int To, int Category, const char* pText, ...) = 0;
	virtual void SendChatTarget_Localization_P(int To, int Category, int Number, const char* pText, ...) = 0;
	virtual void SendMOTD(int To, const char* pText) = 0;
	virtual void SendMOTD_Localization(int To, const char* pText, ...) = 0;
	
	virtual void OnSetAuthed(int ClientID, int Level) = 0;
	
	virtual int GetTargetToKill() = 0;
	virtual void TargetKilled() = 0;
	virtual void EnableTargetToKill() = 0;
	virtual void DisableTargetToKill() = 0;
	virtual int GetTargetToKillCoolDown() = 0;
/* INFECTION MODIFICATION END *****************************************/
};

extern IGameServer *CreateGameServer();
#endif
