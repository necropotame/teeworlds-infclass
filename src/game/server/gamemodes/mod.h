/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_MOD_H
#define GAME_SERVER_GAMEMODES_MOD_H
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/classes.h>
#include <game/server/entities/hero-flag.h>

// you can subclass GAMECONTROLLER_CTF, GAMECONTROLLER_TDM etc if you want
// todo a modification with their base as well.
class CGameControllerMOD : public IGameController
{
public:
	CGameControllerMOD(class CGameContext *pGameServer);
	virtual ~CGameControllerMOD();
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	// add more virtual functions here if you wish
	
	virtual bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual void OnPlayerInfoChange(class CPlayer *pP);
	virtual void DoWincheck();
	virtual void EndRound();
	virtual bool PreSpawn(CPlayer* pPlayer, vec2 *pPos);
	virtual bool PickupAllowed(int Index);
	virtual int ChooseHumanClass(CPlayer* pPlayer);
	virtual int ChooseInfectedClass(CPlayer* pPlayer);
	virtual bool IsChoosableClass(int PlayerClass);
	virtual bool CanVote();
	virtual void OnClientDrop(int ClientID, int Type);
	virtual bool IsInfectionStarted();
	
	void ResetFinalExplosion();
	
private:
	bool IsSpawnable(vec2 Pos, int TeleZoneIndex);
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected, int& NumFirstInfected);
	
private:	
	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;
	
	bool m_InfectedStarted;
	
	CHeroFlag* m_pHeroFlag;
};
#endif
