/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_SERVER_H
#define ENGINE_SERVER_SERVER_H

#include <engine/server.h>
#include <engine/server/roundstatistics.h>
#include <game/server/classes.h>

/* DDNET MODIFICATION START *******************************************/
#include "sql_connector.h"
#include "sql_server.h"
/* DDNET MODIFICATION END *********************************************/

class CSnapIDPool
{
	enum
	{
		MAX_IDS = 16*1024,
	};

	class CID
	{
	public:
		short m_Next;
		short m_State; // 0 = free, 1 = alloced, 2 = timed
		int m_Timeout;
	};

	CID m_aIDs[MAX_IDS];

	int m_FirstFree;
	int m_FirstTimed;
	int m_LastTimed;
	int m_Usage;
	int m_InUsage;

public:

	CSnapIDPool();

	void Reset();
	void RemoveFirstTimeout();
	int NewID();
	void TimeoutIDs();
	void FreeID(int ID);
};


class CServerBan : public CNetBan
{
	class CServer *m_pServer;

	template<class T> int BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason);

public:
	class CServer *Server() const { return m_pServer; }

	void InitServerBan(class IConsole *pConsole, class IStorage *pStorage, class CServer* pServer);

	virtual int BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason);
	virtual int BanRange(const CNetRange *pRange, int Seconds, const char *pReason);

	static bool ConBanExt(class IConsole::IResult *pResult, void *pUser);
};


class CServer : public IServer
{
	class IGameServer *m_pGameServer;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;

/* DDNET MODIFICATION START *******************************************/
#ifdef CONF_SQL
	CSqlServer* m_apSqlReadServers[MAX_SQLSERVERS];
	CSqlServer* m_apSqlWriteServers[MAX_SQLSERVERS];
#endif
/* DDNET MODIFICATION END *********************************************/
public:
	class IGameServer *GameServer() { return m_pGameServer; }
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }

	enum
	{
		MAX_RCONCMD_SEND=16,
	};

	class CClient
	{
	public:

		enum
		{
			STATE_EMPTY = 0,
			STATE_AUTH,
			STATE_CONNECTING,
			STATE_READY,
			STATE_INGAME,

			SNAPRATE_INIT=0,
			SNAPRATE_FULL,
			SNAPRATE_RECOVER
		};

		class CInput
		{
		public:
			int m_aData[MAX_INPUT_SIZE];
			int m_GameTick; // the tick that was chosen for the input
		};

		// connection state info
		int m_State;
		int m_Latency;
		int m_SnapRate;

		int m_LastAckedSnapshot;
		int m_LastInputTick;
		CSnapshotStorage m_Snapshots;

		CInput m_LatestInput;
		CInput m_aInputs[200]; // TODO: handle input better
		int m_CurrentInput;

		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		int m_Authed;
		int m_AuthTries;

		const IConsole::CCommandInfo *m_pRconCmdToSend;

/* INFECTION MODIFICATION START ***************************************/
		void Reset(bool ResetScore=true);
		
		int m_NbRound;
		
		int m_CustomSkin;
		int m_AlwaysRandom;
		int m_DefaultScoreMode;
		int m_Language;
		int m_WaitingTime;
		int m_WasInfected;
		
		//Login
		int m_LogInstance;
		int m_UserID;
		char m_aUsername[MAX_NAME_LENGTH];
		
		bool m_Memory[NUM_CLIENTMEMORIES];
/* INFECTION MODIFICATION END *****************************************/
	};

	CClient m_aClients[MAX_CLIENTS];

	CSnapshotDelta m_SnapshotDelta;
	CSnapshotBuilder m_SnapshotBuilder;
	CSnapIDPool m_IDPool;
	CNetServer m_NetServer;
	CEcon m_Econ;
	CServerBan m_ServerBan;

	IEngineMap *m_pMap;

	int64 m_GameStartTime;
	//int m_CurrentGameTick;
	int m_RunServer;
	int m_MapReload;
	int m_RconClientID;
	int m_RconAuthLevel;
	int m_PrintCBIndex;

	int64 m_Lastheartbeat;
	//static NETADDR4 master_server;

	char m_aCurrentMap[64];
	
	unsigned m_CurrentMapCrc;
	unsigned char *m_pCurrentMapData;
	int m_CurrentMapSize;

	CDemoRecorder m_DemoRecorder;
	CRegister m_Register;
	CMapChecker m_MapChecker;

	CServer();
	virtual ~CServer();

	int TrySetClientName(int ClientID, const char *pName);

	virtual void SetClientName(int ClientID, const char *pName);
	virtual void SetClientClan(int ClientID, char const *pClan);
	virtual void SetClientCountry(int ClientID, int Country);

	void Kick(int ClientID, const char *pReason);

	void DemoRecorder_HandleAutoStart();
	bool DemoRecorder_IsRecording();

	//int Tick()
	int64 TickStartTime(int Tick);
	//int TickSpeed()

	int Init();

	void SetRconCID(int ClientID);
	bool IsAuthed(int ClientID);
	int GetClientInfo(int ClientID, CClientInfo *pInfo);
	void GetClientAddr(int ClientID, char *pAddrStr, int Size);
	const char *ClientName(int ClientID);
	const char *ClientClan(int ClientID);
	int ClientCountry(int ClientID);
	bool ClientIngame(int ClientID);
	int MaxClients() const;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID);
	int SendMsgEx(CMsgPacker *pMsg, int Flags, int ClientID, bool System);

	void DoSnapshot();

	static int NewClientCallback(int ClientID, void *pUser);
	static int DelClientCallback(int ClientID, int Type, const char *pReason, void *pUser);

	void SendMap(int ClientID);
	
	void SendConnectionReady(int ClientID);
	void SendRconLine(int ClientID, const char *pLine);
	static void SendRconLineAuthed(const char *pLine, void *pUser);

	void SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void UpdateClientRconCommands();

	void ProcessClientPacket(CNetChunk *pPacket);

	void SendServerInfo(const NETADDR *pAddr, int Token);
	void UpdateServerInfo();

	void PumpNetwork();

	char *GetMapName();
	int LoadMap(const char *pMapName);

	void InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, IConsole *pConsole);
	int Run();

	static bool ConKick(IConsole::IResult *pResult, void *pUser);
	static bool ConStatus(IConsole::IResult *pResult, void *pUser);
	static bool ConShutdown(IConsole::IResult *pResult, void *pUser);
	static bool ConRecord(IConsole::IResult *pResult, void *pUser);
	static bool ConStopRecord(IConsole::IResult *pResult, void *pUser);
	static bool ConMapReload(IConsole::IResult *pResult, void *pUser);
	static bool ConLogout(IConsole::IResult *pResult, void *pUser);
	static bool ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static bool ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static bool ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static bool ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

/* DDNET MODIFICATION START *******************************************/
	static bool ConAddSqlServer(IConsole::IResult *pResult, void *pUserData);
	static bool ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData);

	static void CreateTablesThread(void *pData);
/* DDNET MODIFICATION END *********************************************/
	
	void RegisterCommands();


	virtual int SnapNewID();
	virtual void SnapFreeID(int ID);
	virtual void *SnapNewItem(int Type, int ID, int Size);
	void SnapSetStaticsize(int ItemType, int Size);
	
/* INFECTION MODIFICATION START ***************************************/
protected:
	int m_FastDownloadLastSent[MAX_CLIENTS];
	int m_FastDownloadLastAsk[MAX_CLIENTS];
	int m_FastDownloadLastAskTick[MAX_CLIENTS];

public:
	int m_InfClassChooser;
	int m_InfAmmoRegenTime[NB_INFWEAPON];
	int m_InfFireDelay[NB_INFWEAPON];
	int m_InfMaxAmmo[NB_INFWEAPON];
	int m_InfClassAvailability[NB_PLAYERCLASS];

public:
	virtual int IsClientInfectedBefore(int ClientID);
	virtual void InfecteClient(int ClientID);
	
	virtual int GetClientCustomSkin(int ClientID);
	virtual void SetClientCustomSkin(int ClientID, int Value);
	
	virtual int GetClientAlwaysRandom(int ClientID);
	virtual void SetClientAlwaysRandom(int ClientID, int Value);
	
	virtual int GetClientDefaultScoreMode(int ClientID);
	virtual void SetClientDefaultScoreMode(int ClientID, int Value);
	
	virtual int GetClientLanguage(int ClientID);
	virtual void SetClientLanguage(int ClientID, int Value);
	
	virtual int GetFireDelay(int WID);
	virtual void SetFireDelay(int WID, int Time);
	
	virtual int GetAmmoRegenTime(int WID);
	virtual void SetAmmoRegenTime(int WID, int Time);
	
	virtual int GetMaxAmmo(int WID);
	virtual void SetMaxAmmo(int WID, int n);
	
	virtual int GetClassAvailability(int CID);
	virtual void SetClassAvailability(int CID, int n);
	
	virtual int GetClientNbRound(int ClientID);
	
	virtual int IsClassChooserEnabled();
	virtual bool IsClientLogged(int ClientID);
#ifdef CONF_SQL
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword);
	virtual void Logout(int ClientID);
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail);
	virtual void ShowTop10(int ClientID, int ScoreType);
	virtual void ShowRank(int ClientID, int ScoreType);
#endif
	virtual void Ban(int ClientID, int Seconds, const char* pReason);
private:
	bool InitCaptcha();
	
public:
	class CGameServerCmd
	{
	public:
		virtual ~CGameServerCmd() {};
		virtual void Execute(IGameServer* pGameServer) = 0;
	};

private:
	LOCK m_GameServerCmdLock;
	array<CGameServerCmd*> m_lGameServerCmds;
	CRoundStatistics m_RoundStatistics;

public:
	void AddGameServerCmd(CGameServerCmd* pCmd);
	
	virtual CRoundStatistics* RoundStatistics() { return &m_RoundStatistics; }
	virtual void OnRoundStart();
	virtual void OnRoundEnd();
	
	virtual void SetClientMemory(int ClientID, int Memory, bool Value = true);
	virtual void ResetClientMemoryAboutGame(int ClientID);
	virtual bool GetClientMemory(int ClientID, int Memory);
	
/* INFECTION MODIFICATION END *****************************************/
};

#endif
