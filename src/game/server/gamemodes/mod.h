/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_MOD_H
#define GAME_SERVER_GAMEMODES_MOD_H
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/entities/classchooser.h>

// you can subclass GAMECONTROLLER_CTF, GAMECONTROLLER_TDM etc if you want
// todo a modification with their base as well.
class CGameControllerMOD : public IGameController
{
public:
	CGameControllerMOD(class CGameContext *pGameServer);
	virtual ~CGameControllerMOD();
	virtual void Tick();
	// add more virtual functions here if you wish
	
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual void PostReset();
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual void OnPlayerInfoChange(class CPlayer *pP);
	virtual void DoWincheck();
	virtual bool CanSpawn(CPlayer* pPlayer, vec2 *pPos);
	virtual bool PickupAllowed(int Index);
	virtual int ChooseHumanClass(CPlayer* pPlayer);
	virtual int ChooseInfectedClass(CPlayer* pPlayer);
	
private:
	bool IsSpawnable(vec2 Pos);
	
private:
	float m_ClassProbability[NB_PLAYERCLASS];
	float m_TotalProbInfectedClass;
	float m_TotalProbHumanClass;
	
	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;
};
#endif
