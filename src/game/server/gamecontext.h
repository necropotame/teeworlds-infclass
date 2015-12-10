/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <engine/server.h>
#include <engine/console.h>
#include <engine/shared/memheap.h>

#include <game/layers.h>
#include <game/voting.h>

#include "eventhandler.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "player.h"

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/

enum
{
	TEXTID_PLAYER_ENTER,
	TEXTID_PLAYER_JOIN_GAME,
	TEXTID_PLAYER_JOIN_SPEC,
	TEXTID_PLAYER_EXIT,
	TEXTID_PLAYER_EXIT_REASON,
	TEXTID_PLAYER_KICK,
	TEXTID_PLAYER_BAN,
	TEXTID_PLAYER_CHANGE_NAME,
	
	TEXTID_CMD_UNKNOWN,
	TEXTID_ALWAYSRANDOM_ON,
	TEXTID_ALWAYSRANDOM_OFF,
	TEXTID_YOU_SPEC_REJECT,
	
	TEXTID_YOU_FROZEN,
	TEXTID_YOU_INFECTED_PLAYER,
	TEXTID_YOU_KILLED_WITCH,
	TEXTID_YOU_SURVIVED,
	TEXTID_YOU_PORTAL_INFECTION,
	TEXTID_YOU_PORTAL_KILL,
	TEXTID_PLAYER_INFECTED,
	TEXTID_WITCH_SPAWN,
	TEXTID_WITCH_DEAD,
	TEXTID_UNDEAD_SPAWN,
	TEXTID_UNDEAD_DEAD,
	
	TEXTID_WIN_INFECTED,
	TEXTID_WIN_HUMAN,
	TEXTID_WIN_HUMANS,
	
	TEXTID_CLASSCHOOSER_HELP,
	TEXTID_RANDOM_CHOICE,
	
	TEXTID_ENGINEER,
	TEXTID_SOLDIER,
	TEXTID_SCIENTIST,
	TEXTID_MEDIC,
	TEXTID_NINJA,
	TEXTID_SMOKER,
	TEXTID_BOOMER,
	TEXTID_HUNTER,
	TEXTID_UNDEAD,
	TEXTID_WITCH,
	
	TEXTID_YOU_ENGINEER,
	TEXTID_YOU_SOLDIER,
	TEXTID_YOU_SCIENTIST,
	TEXTID_YOU_MEDIC,
	TEXTID_YOU_NINJA,
	TEXTID_YOU_SMOKER,
	TEXTID_YOU_BOOMER,
	TEXTID_YOU_HUNTER,
	TEXTID_YOU_UNDEAD,
	TEXTID_YOU_WITCH,
	
	TEXTID_ENGINEER_TIP,
	TEXTID_SOLDIER_TIP,
	TEXTID_SCIENTIST_TIP,
	TEXTID_MEDIC_TIP,
	TEXTID_NINJA_TIP,
	TEXTID_SMOKER_TIP,
	TEXTID_BOOMER_TIP,
	TEXTID_HUNTER_TIP,
	TEXTID_UNDEAD_TIP,
	TEXTID_WITCH_TIP,
	
	TEXTID_CMD_CMDLIST,
	TEXTID_CMD_INFO,
	TEXTID_CMD_HELP,
	TEXTID_CMD_HELP_ENGINEER,
	TEXTID_CMD_HELP_SOLDIER,
	TEXTID_CMD_HELP_SCIENTIST,
	TEXTID_CMD_HELP_MEDIC,
	TEXTID_CMD_HELP_NINJA,
	TEXTID_CMD_HELP_SMOKER,
	TEXTID_CMD_HELP_BOOMER,
	TEXTID_CMD_HELP_HUNTER,
	TEXTID_CMD_HELP_UNDEAD,
	TEXTID_CMD_HELP_WITCH
};

class CGameContext : public IGameServer
{
	IServer *m_pServer;
	class IConsole *m_pConsole;
	CLayers m_Layers;
	CCollision m_Collision;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConSwapTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConShuffleTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConLockTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	CGameContext(int Resetting);
	void Construct(int Resetting);

	bool m_Resetting;
public:
	IServer *Server() const { return m_pServer; }
	class IConsole *Console() { return m_pConsole; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }

	CGameContext();
	~CGameContext();

	void Clear();

	CEventHandler m_Events;
	CPlayer *m_apPlayers[MAX_CLIENTS];

	IGameController *m_pController;
	CGameWorld m_World;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);

	int m_LockTeams;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote();
	void SendVoteSet(int ClientID);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No);
	void AbortVoteKickOnDisconnect(int ClientID);

	int m_VoteCreator;
	int64 m_VoteCloseTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_NumVoteOptions;
	int m_VoteEnforce;
	enum
	{
		VOTE_ENFORCE_UNKNOWN=0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// helper functions
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount);
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage);
	void CreateHammerHit(vec2 Pos);
	void CreatePlayerSpawn(vec2 Pos);
	void CreateDeath(vec2 Pos, int Who);
	void CreateSound(vec2 Pos, int Sound, int Mask=-1);
	void CreateSoundGlobal(int Sound, int Target=-1);
	
/* INFECTION MODIFICATION START ***************************************/
	void CreateDeadlyPortalWarning(vec2 Pos, int Owner);
/* INFECTION MODIFICATION END *****************************************/


	enum
	{
		CHAT_ALL=-2,
		CHAT_SPEC=-1,
		CHAT_RED=0,
		CHAT_BLUE=1
	};

	// network
	void SendChatTarget(int To, const char *pText);
	void SendChat(int ClientID, int Team, const char *pText);
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendBroadcast(const char *pText, int ClientID);


	//
	void CheckPureTuning();
	void SendTuningParams(int ClientID);

	//
	void SwapTeams();

	// engine events
	virtual void OnInit();
	virtual void OnConsoleInit();
	virtual void OnShutdown();

	virtual void OnTick();
	virtual void OnPreSnap();
	virtual void OnSnap(int ClientID);
	virtual void OnPostSnap();

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID);

	virtual void OnClientConnected(int ClientID);
	virtual void OnClientEnter(int ClientID);
	virtual void OnClientDrop(int ClientID, int Type, const char *pReason);
	virtual void OnClientDirectInput(int ClientID, void *pInput);
	virtual void OnClientPredictedInput(int ClientID, void *pInput);

	virtual bool IsClientReady(int ClientID);
	virtual bool IsClientPlayer(int ClientID);

	virtual const char *GameType();
	virtual const char *Version();
	virtual const char *NetVersion();
	
/* INFECTION MODIFICATION START ***************************************/
private:
	static void ConSetClass(IConsole::IResult *pResult, void *pUserData);
	
	static const char* ms_TextEn[];
	static const char* ms_TextFr[];
	static const char* ms_TextDe[];
	
public:
	virtual void SendBroadcast_Language(int To, int TextId);
	virtual void SendBroadcast_Language_i(int To, int TextId, int Value);
	
	virtual void SendChatTarget_Language(int To, int TextId);
	virtual void SendChatTarget_Language_s(int To, int TextId, const char* Text);
	virtual void SendChatTarget_Language_ss(int To, int TextId, const char* Text, const char* Text2);
	virtual void SendChatTarget_Language_i(int To, int TextId, int Value);
	virtual void SendChatTarget_Language_ii(int To, int TextId, int Value, int Value2);
	virtual void SendMODT_Language(int To, int TextId);
	virtual void SendMODT_Language_s(int To, int TextId, const char* Text);
	
private:
	virtual const char* GetTextTranslation(int TextId, int Language);

/* INFECTION MODIFICATION END *****************************************/
};

inline int CmaskAll() { return -1; }
inline int CmaskOne(int ClientID) { return 1<<ClientID; }
inline int CmaskAllExceptOne(int ClientID) { return 0x7fffffff^CmaskOne(ClientID); }
inline bool CmaskIsSet(int Mask, int ClientID) { return (Mask&CmaskOne(ClientID)) != 0; }
#endif
