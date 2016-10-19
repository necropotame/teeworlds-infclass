#ifndef ENGINE_SERVER_ROUND_STATISTICS_H
#define ENGINE_SERVER_ROUND_STATISTICS_H

#include <engine/shared/protocol.h>
#include <game/server/classes.h>

enum
{
	SCOREEVENT_HUMAN_SURVIVE=0,
	SCOREEVENT_HUMAN_SUICIDE,
	SCOREEVENT_HUMAN_HEALING,
	SCOREEVENT_INFECTION,
	SCOREEVENT_KILL_INFECTED,
	SCOREEVENT_KILL_WITCH,
	SCOREEVENT_HELP_FREEZE,
	SCOREEVENT_HELP_HOOK_BARRIER,
	SCOREEVENT_HELP_HOOK_INFECTION,
};

class CRoundStatistics
{
public:
	class CPlayer
	{
	public:
		int m_Score;
		int m_EngineerScore;
		int m_SoldierScore;
		int m_ScientistScore;
		int m_MedicScore;
		int m_NinjaScore;
		int m_MercenaryScore;
		int m_SniperScore;
		
		int m_SmokerScore;
		int m_HunterScore;
		int m_BoomerScore;
		int m_GhostScore;
		int m_SpiderScore;
		int m_UndeadScore;
		int m_WitchScore;
		
		bool m_WasSpectator;
		bool m_Won;
	
	public:
		CPlayer() { Reset(); }
		void Reset() { mem_zero(this, sizeof(CPlayer)); }
		void OnScoreEvent(int EventType, int Class);
	};

public:
	CPlayer m_aPlayers[MAX_CLIENTS];
	int m_NumPlayersMin;
	int m_NumPlayersMax;
	int m_PlayedTicks;
	
public:
	CRoundStatistics() { Reset(); }
	void Reset() { mem_zero(this, sizeof(CRoundStatistics)); }
	void ResetPlayer(int ClientID);
	void OnScoreEvent(int ClientID, int EventType, int Class);
	void SetPlayerAsWinner(int ClientID);
	
	CRoundStatistics::CPlayer* PlayerStatistics(int ClientID);
	int PlayerScore(int ClientID);
	
	int NumWinners() const;
	
	void UpdatePlayer(int ClientID, bool IsSpectator);
	void UpdateNumberOfPlayers(int Num);
	
	bool IsValidePlayer(int ClientID);
};

#endif
