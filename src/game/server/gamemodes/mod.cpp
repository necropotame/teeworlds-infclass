/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "mod.h"

/* INFECTION MODIFICATION START ***************************************/
#include <game/server/player.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <time.h>
#include <iostream>
/* INFECTION MODIFICATION END *****************************************/

CGameControllerMOD::CGameControllerMOD(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
/* INFECTION MODIFICATION START ***************************************/
	m_pGameType = "InfClass";
	srand (time(0));
	
	m_TotalProbInfectedClass = 0.0;
	
	m_ClassProbability[PLAYERCLASS_ZOMBIE] = 1.0f;
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_ZOMBIE];
	
	m_ClassProbability[PLAYERCLASS_HUNTER] = 0.6666f * m_ClassProbability[PLAYERCLASS_ZOMBIE];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_HUNTER];
	
	m_ClassProbability[PLAYERCLASS_BOOMER] = 0.6666f * m_ClassProbability[PLAYERCLASS_ZOMBIE];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_BOOMER];
	
	m_ClassProbability[PLAYERCLASS_WITCH] = 0.25 * m_ClassProbability[PLAYERCLASS_ZOMBIE];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_WITCH];
	
	m_ClassProbability[PLAYERCLASS_UNDEAD] = 0.1 * m_ClassProbability[PLAYERCLASS_ZOMBIE];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_UNDEAD];
	
	m_TotalProbHumanClass = 0.0;
	
	m_ClassProbability[PLAYERCLASS_ENGINEER] = 1.0f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_ENGINEER];
	
	m_ClassProbability[PLAYERCLASS_SOLDIER] = 1.0f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_SOLDIER];
	
	m_ClassProbability[PLAYERCLASS_SCIENTIST] = 0.5f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_SCIENTIST];
	
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
		if(pVictim->GetClass() == PLAYERCLASS_WITCH)
		{
			GameServer()->SendChatTarget(pKiller->GetCID(), "You killed a witch, +5 points");	
			pKiller->m_Score += 5;
		}
		else pKiller->m_Score += 1;
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
	std::cout << "SkinName : " << pP->m_TeeInfos.m_SkinName << std::endl;
	std::cout << "ColorBody : " << pP->m_TeeInfos.m_ColorBody << std::endl;
	std::cout << "ColorFeet : " << pP->m_TeeInfos.m_ColorFeet << std::endl;
	
	pP->SetClassSkin(pP->GetClass());
}

void CGameControllerMOD::DoWincheck()
{
	if(GameServer()->m_DebugMode)
		return;
	
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
		for(int i = 0; i < MAX_CLIENTS; i ++)
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[i];
			
			if(!pPlayer) continue;
			if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
			
			if(pPlayer->GetClass() == PLAYERCLASS_NONE)
			{
				pPlayer->SetClass(ChooseHumanClass(pPlayer));
			}
		}
			
		if(countZombie <= 0)
		{
			float InfectionProb = 1.0/static_cast<float>(countHuman);
			float random = frandom();
			
			for(int i = 0; i < MAX_CLIENTS; i ++)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[i];
				
				if(!pPlayer) continue;
				if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
				
				if(random < InfectionProb)
				{
					GameServer()->m_apPlayers[i]->StartInfection();
					
					char aBuf[512];
					str_format(aBuf, sizeof(aBuf), "%s has been infected", Server()->ClientName(GameServer()->m_apPlayers[i]->GetCID()));
					GameServer()->SendChat(-1, -2, aBuf);
					
					break;
				}
				else
				{
					random -= InfectionProb;
				}
			}
		}
	
		if(m_GameOverTick != -1)
			return;
			
		if(countHuman == 0 && countZombie > 1)
		{
			float RoundDuration = static_cast<float>((Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed()))/60.0f;
			int Minutes = static_cast<int>(RoundDuration);
			int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "Infected won this round in %i:%s%i minutes", Minutes,((Seconds < 10) ? "0" : ""), Seconds);
			GameServer()->SendChat(-1, -2, aBuf);
			
			EndRound();
		}
	}
	
	if(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60)
	{
		if(countHuman)
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "%i human%s won this round", countHuman, (countHuman>1 ? "s" : ""));
			GameServer()->SendChat(-1, -2, aBuf);
			
			for(int i = 0; i < MAX_CLIENTS; i ++)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[i];
				
				if(!pPlayer) continue;
				if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
				
				if(!pPlayer->IsInfected())
				{
					pPlayer->m_Score += 5;
					
					pPlayer->SetClass(ChooseHumanClass(pPlayer));
					GameServer()->SendChatTarget(i, "You survive, +5 points");
				}
			}
				
		}
		else
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "Infected won this round in %i minutes", g_Config.m_SvTimelimit);
			GameServer()->SendChat(-1, -2, aBuf);
		}
		EndRound();
	}
}

bool CGameControllerMOD::IsSpawnable(vec2 Pos)
{
	// check if the position is occupado
	CCharacter *aEnts[MAX_CLIENTS];
	int Num = GameServer()->m_World.FindEntities(Pos, 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	vec2 Positions[5] = { vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f) };	// start, left, up, right, down
	int Result = -1;
	for(int Index = 0; Index < 5 && Result == -1; ++Index)
	{
		Result = Index;
		for(int c = 0; c < Num; ++c)
			if(GameServer()->Collision()->CheckPoint(Pos+Positions[Index]) ||
				distance(aEnts[c]->m_Pos, Pos+Positions[Index]) <= aEnts[c]->m_ProximityRadius)
			{
				return false;
			}
	}
	return true;
}

bool CGameControllerMOD::CanSpawn(CPlayer* pPlayer, vec2 *pOutPos)
{
	int Team = pPlayer->GetTeam();

	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;
		
	if(pPlayer->IsInfected() && rand()%3 > 0)
	{
		for(int i = 0; i < MAX_CLIENTS; i ++)
		{
			CPlayer *pWitch = GameServer()->m_apPlayers[i];
			
			if(!pWitch) continue;
			if(pWitch->GetCID() == pPlayer->GetCID()) continue;
			if(pWitch->GetClass() != PLAYERCLASS_WITCH) continue;
			if(!pWitch->GetCharacter()) continue;
			
			vec2 spawnTile = vec2(16.0f, 16.0f) + vec2(
				static_cast<float>(static_cast<int>(round(pWitch->GetCharacter()->m_Pos.x))/32)*32.0,
				static_cast<float>(static_cast<int>(round(pWitch->GetCharacter()->m_Pos.y))/32)*32.0);
			
			for(int j=-1; j<=1; j++)
			{
				if(IsSpawnable(vec2(spawnTile.x + j*32.0, spawnTile.y-64.0)))
				{
					*pOutPos = spawnTile + vec2(j*32.0, -64.0);
					return true;
				}
				if(IsSpawnable(vec2(spawnTile.x + j*32.0, spawnTile.y+64.0)))
				{
					*pOutPos = spawnTile + vec2(j*32.0, 64.0);
					return true;
				}
				if(IsSpawnable(vec2(spawnTile.x-64.0, spawnTile.y + j*32.0)))
				{
					*pOutPos = spawnTile + vec2(-64.0, j*32.0);
					return true;
				}
				if(IsSpawnable(vec2(spawnTile.x+64.0, spawnTile.y + j*32.0)))
				{
					*pOutPos = spawnTile + vec2(64.0, j*32.0);
					return true;
				}
			}
		}
	}
			
	CSpawnEval Eval;
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
	if(Index == ENTITY_POWERUP_NINJA) return false;
	else if(Index == ENTITY_WEAPON_SHOTGUN) return false;
	else if(Index == ENTITY_WEAPON_GRENADE) return false;
	else if(Index == ENTITY_WEAPON_RIFLE) return false;
	else return true;
}

int CGameControllerMOD::ChooseHumanClass(CPlayer* pPlayer)
{
	float random = frandom();
	
	random -= m_ClassProbability[PLAYERCLASS_SCIENTIST]/m_TotalProbHumanClass;
	if(random < 0.0f)
	{
		return PLAYERCLASS_SCIENTIST;
	}
	
	random -= m_ClassProbability[PLAYERCLASS_SOLDIER]/m_TotalProbHumanClass;
	if(random < 0.0f)
	{
		return PLAYERCLASS_SOLDIER;
	}
	
	return PLAYERCLASS_ENGINEER;
}

int CGameControllerMOD::ChooseInfectedClass(CPlayer* pPlayer)
{
	float random = frandom();
	float TotalProbInfectedClass = m_TotalProbInfectedClass;
	
	//Get information about existing infected
	bool thereIsAWitch = false;
	int nbInfected = 0;
	bool thereIsAnUndead = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		
		if(pPlayer->IsInfected()) nbInfected++;
		if(pPlayer->GetClass() == PLAYERCLASS_WITCH) thereIsAWitch = true;
		if(pPlayer->GetClass() == PLAYERCLASS_UNDEAD) thereIsAnUndead = true;
	}
	
	//Check if undeads are enabled
	bool undeadEnabled = true;
	if(nbInfected < 2 || thereIsAnUndead)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_UNDEAD];
		undeadEnabled = false;
	}
	
	//Check if witches are enabled
	bool witchEnabled = true;
	if(nbInfected < 2 || thereIsAWitch)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_WITCH];
		witchEnabled = false;
	}
	
	//Find the random class
	if(undeadEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_UNDEAD]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			GameServer()->SendBroadcast("The undead is coming !", -1);
			GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
			return PLAYERCLASS_UNDEAD;
		}
	}
	
	if(witchEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_WITCH]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			GameServer()->SendBroadcast("The witch is coming !", -1);
			GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
			return PLAYERCLASS_WITCH;
		}
	}
	
	random -= m_ClassProbability[PLAYERCLASS_BOOMER]/TotalProbInfectedClass;
	if(random < 0.0f)
	{
		return PLAYERCLASS_BOOMER;
	}
	
	random -= m_ClassProbability[PLAYERCLASS_HUNTER]/TotalProbInfectedClass;
	if(random < 0.0f)
	{
		return PLAYERCLASS_HUNTER;
	}
	
	return PLAYERCLASS_ZOMBIE;
}
/* INFECTION MODIFICATION END *****************************************/
