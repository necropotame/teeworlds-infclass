/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H
#include "kernel.h"
#include "message.h"

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
	
	INFWEAPON_MEDIC_SHOTGUN,
	
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
	PLAYERSCOREMODE_ROUNDSCORE,
	PLAYERSCOREMODE_SCOREPERROUND,
	PLAYERSCOREMODE_INFECTION,
	PLAYERSCOREMODE_INFECTIONPERROUND,
	PLAYERSCOREMODE_TIME,
	NB_PLAYERSCOREMODE,
};

enum
{
	LANGUAGE_FR=0,
	LANGUAGE_DE,
	LANGUAGE_RU,
	LANGUAGE_UK,
	LANGUAGE_IT,
	LANGUAGE_ES,
	LANGUAGE_AR,
	LANGUAGE_HU,
	LANGUAGE_EN, //Must be the last
	NUM_LANGUAGES,
	NUM_TRANSLATED_LANGUAGES = NUM_LANGUAGES-1,
};

/* INFECTION MODIFICATION END *****************************************/

class IServer : public IInterface
{
	MACRO_INTERFACE("server", 0)
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
	};

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int MaxClients() const = 0;
	virtual const char *ClientName(int ClientID) = 0;
	virtual const char *ClientClan(int ClientID) = 0;
	virtual int ClientCountry(int ClientID) = 0;
	virtual bool ClientIngame(int ClientID) = 0;
	virtual int GetClientInfo(int ClientID, CClientInfo *pInfo) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) = 0;

	template<class T>
	int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		CMsgPacker Packer(pMsg->MsgID());
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientID);
	}

	virtual void SetClientName(int ClientID, char const *pName) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;
	virtual void SetClientScore(int ClientID, int Score) = 0;

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
	
	virtual int GetClientScore(int ClientID) = 0;
	
	virtual int GetClientNbRound(int ClientID) = 0;
	virtual void SetClientNbRound(int ClientID, int Score) = 0;
	
	virtual int GetClientNbInfection(int ClientID) = 0;
	virtual void SetClientNbInfection(int ClientID, int Score) = 0;
	
	virtual int GetClientCustomSkin(int ClientID) = 0;
	virtual void SetClientCustomSkin(int ClientID, int Value) = 0;
	
	virtual int GetClientAlwaysRandom(int ClientID) = 0;
	virtual void SetClientAlwaysRandom(int ClientID, int Value) = 0;
	
	virtual int GetClientDefaultScoreMode(int ClientID) = 0;
	virtual void SetClientDefaultScoreMode(int ClientID, int Value) = 0;
	
	virtual int GetClientLanguage(int ClientID) = 0;
	virtual void SetClientLanguage(int ClientID, int Value) = 0;
	
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
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword) = 0;
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword) = 0;
/* INFECTION MODIFICATION END *****************************************/
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
	
/* INFECTION MODIFICATION START ***************************************/
	virtual void SendBroadcast_Language(int To, const char* pText, bool LowPriority = false) = 0;
	virtual void SendBroadcast_Language_i(int To, const char* pText, int Param, bool LowPriority = false) = 0;
	virtual void SendChatTarget_Language(int To, const char* pText) = 0;
	virtual void SendChatTarget_Language_s(int To, const char* pText, const char* pParam) = 0;
	virtual void SendChatTarget_Language_ss(int To, const char* pText, const char* pParam1, const char* pParam2) = 0;
	virtual void SendChatTarget_Language_i(int To, const char* pText, int Param) = 0;
	virtual void SendChatTarget_Language_ii(int To, const char* pText, int Param1, int Param2) = 0;
	virtual void SendMODT_Language(int To, const char* pText) = 0;
	virtual void SendMODT_Language_s(int To, const char* pText, const char* pParam) = 0;
/* INFECTION MODIFICATION END *****************************************/
};

extern IGameServer *CreateGameServer();
#endif
