/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "mod.h"

/* INFECTION MODIFICATION START ***************************************/
#include <game/server/player.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
/* INFECTION MODIFICATION END *****************************************/

CGameControllerMOD::CGameControllerMOD(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
/* INFECTION MODIFICATION START ***************************************/
	m_pGameType = "InfClass";
/* INFECTION MODIFICATION END *****************************************/
}

void CGameControllerMOD::Tick()
{
	// this is the main part of the gamemode, this function is run every tick

	IGameController::Tick();
}

/* INFECTION MODIFICATION START ***************************************/
int CGameControllerMOD::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
		
	if(pKiller == pVictim->GetPlayer())
	{
		if(pVictim->GetClass() != PLAYERCLASS_BOOMER)
		{
			pVictim->GetPlayer()->m_Score--; // suicide
		}
	}
	else if(!pKiller->IsInfected())
	{
		pKiller->m_Score++; // normal kill
	}
	
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*3.0f;
		
	return 0;
}

void CGameControllerMOD::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
}

void CGameControllerMOD::PostReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->SetClass(PLAYERCLASS_NONE);
		}
	}
	
	IGameController::PostReset();
}

void CGameControllerMOD::OnPlayerInfoChange(class CPlayer *pP)
{
	pP->SetClassSkin(pP->GetClass());
}

void CGameControllerMOD::DoWincheck()
{	
	int countZombie = 0;
	int countHuman = 0;
	
	for(int i = 0; i < MAX_CLIENTS; i ++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		
		if(pPlayer->IsInfected()) countZombie++;
		else countHuman++;
	}
	
	if(countZombie + countHuman < 2) return;
	
	if(m_RoundStartTick + Server()->TickSpeed()*10 < Server()->Tick())
	{
		if(countZombie <= 0)
		{
			for(int i = 0; i < MAX_CLIENTS; i ++)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[i];
				
				if(!pPlayer) continue;
				if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
				
				if(pPlayer->GetClass() == PLAYERCLASS_NONE)
				{
					pPlayer->SetClass(START_HUMANCLASS +1 + rand()%(END_HUMANCLASS - START_HUMANCLASS - 1));
				}
			}
		
			bool searchForZombie = true;
			while(searchForZombie)
			{
				int id = rand()%MAX_CLIENTS;
				if(GameServer()->m_apPlayers[id])
				{
					searchForZombie = false;
					GameServer()->m_apPlayers[id]->StartInfection();
				}
			}
		}
	
		if(m_GameOverTick != -1)
			return;
			
		if(countHuman == 0 && countZombie > 1)
		{
			GameServer()->SendBroadcast("Infected won this round", -1);
			EndRound();
		}
	}
	
	if(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60)
	{
		if(countHuman)
		{
			GameServer()->SendBroadcast("Human won this round", -1);
		}
		else
		{
			GameServer()->SendBroadcast("Infected won this round", -1);
		}
		EndRound();
	}
}

bool CGameControllerMOD::CanSpawn(CPlayer* pPlayer, vec2 *pOutPos)
{
	CSpawnEval Eval;
	int Team = pPlayer->GetTeam();

	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;

	Team = (pPlayer->IsInfected() ? TEAM_RED : TEAM_BLUE);
	
	Eval.m_FriendlyTeam = Team;

	// first try own team spawn, then normal spawn and then enemy
	EvaluateSpawnType(&Eval, 1+(Team&1));
	if(!Eval.m_Got)
	{
		EvaluateSpawnType(&Eval, 0);
		if(!Eval.m_Got)
			EvaluateSpawnType(&Eval, 1+((Team+1)&1));
	}

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

bool CGameControllerMOD::PickupAllowed(int Index)
{
	if(Index == ENTITY_POWERUP_NINJA) return g_Config.m_SvPowerups;
	else if(Index == ENTITY_WEAPON_SHOTGUN) return false;
	else if(Index == ENTITY_WEAPON_GRENADE) return false;
	else if(Index == ENTITY_WEAPON_RIFLE) return false;
	else return true;
}
/* INFECTION MODIFICATION END *****************************************/
