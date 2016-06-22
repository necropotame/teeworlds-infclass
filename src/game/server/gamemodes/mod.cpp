/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "mod.h"

#include <game/server/player.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <time.h>
#include <iostream>

CGameControllerMOD::CGameControllerMOD(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	m_pGameType = "InfClass";
	srand (time(0));
	
	m_TotalProbInfectedClass = 0.0;
	
	m_ClassProbability[PLAYERCLASS_SMOKER] = 1.0f;
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_SMOKER];
	
	m_ClassProbability[PLAYERCLASS_HUNTER] = 0.6666f * m_ClassProbability[PLAYERCLASS_SMOKER];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_HUNTER];
	
	m_ClassProbability[PLAYERCLASS_BOOMER] = 0.6666f * m_ClassProbability[PLAYERCLASS_SMOKER];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_BOOMER];
	
	m_ClassProbability[PLAYERCLASS_GHOST] = 0.25f * m_ClassProbability[PLAYERCLASS_SMOKER];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_GHOST];
	
	m_ClassProbability[PLAYERCLASS_SPIDER] = 0.25f * m_ClassProbability[PLAYERCLASS_SPIDER];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_SPIDER];
	
	m_ClassProbability[PLAYERCLASS_WITCH] = 0.25f * m_ClassProbability[PLAYERCLASS_SMOKER];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_WITCH];
	
	m_ClassProbability[PLAYERCLASS_UNDEAD] = 0.20f * m_ClassProbability[PLAYERCLASS_SMOKER];
	m_TotalProbInfectedClass += m_ClassProbability[PLAYERCLASS_UNDEAD];
	
	m_TotalProbHumanClass = 0.0;
	
	m_ClassProbability[PLAYERCLASS_ENGINEER] = 1.0f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_ENGINEER];
	
	m_ClassProbability[PLAYERCLASS_SOLDIER] = 1.0f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_SOLDIER];
	
	m_ClassProbability[PLAYERCLASS_MERCENARY] = 1.0f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_MERCENARY];
	
	m_ClassProbability[PLAYERCLASS_SNIPER] = 1.0f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_SNIPER];
	
	m_ClassProbability[PLAYERCLASS_SCIENTIST] = 0.5f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_SCIENTIST];
	
	m_ClassProbability[PLAYERCLASS_NINJA] = 0.5f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_NINJA];
	
	m_ClassProbability[PLAYERCLASS_MEDIC] = 0.5f;
	m_TotalProbHumanClass += m_ClassProbability[PLAYERCLASS_MEDIC];
	
	m_GrowingMap = 0;
	
	m_ExplosionStarted = false;
	m_MapWidth = GameServer()->Collision()->GetWidth();
	m_MapHeight = GameServer()->Collision()->GetHeight();
	m_GrowingMap = new int[m_MapWidth*m_MapHeight];
	
	m_HumanCounter = 0;
	m_InfectedCounter = 0;
	m_InfectedStarted = false;
	
	for(int j=0; j<m_MapHeight; j++)
	{
		for(int i=0; i<m_MapWidth; i++)
		{
			vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i*32.0f, j*32.0f);
			if(GameServer()->Collision()->CheckPoint(TilePos))
			{
				m_GrowingMap[j*m_MapWidth+i] = 4;
			}
			else
			{
				m_GrowingMap[j*m_MapWidth+i] = 1;
			}
		}
	}
	
	m_SupportLimit = 5;
	m_MedicLimit = 6;
	m_SoldierLimit = 8;
	m_EngineerLimit = 8;
}

CGameControllerMOD::~CGameControllerMOD()
{
	if(m_GrowingMap) delete[] m_GrowingMap;
}

bool CGameControllerMOD::OnEntity(int Index, vec2 Pos)
{
	bool res = IGameController::OnEntity(Index, Pos);

	if(Index == ENTITY_SPAWN_RED)
	{
		int SpawnX = static_cast<int>(Pos.x)/32.0f;
		int SpawnY = static_cast<int>(Pos.y)/32.0f;
		
		if(SpawnX >= 0 && SpawnX < m_MapWidth && SpawnY >= 0 && SpawnY < m_MapHeight)
		{
			m_GrowingMap[SpawnY*m_MapWidth+SpawnX] = 6;
		}
	}

	return res;
}

void CGameControllerMOD::ResetFinalExplosion()
{
	m_ExplosionStarted = false;
	
	for(int j=0; j<m_MapHeight; j++)
	{
		for(int i=0; i<m_MapWidth; i++)
		{
			if(!(m_GrowingMap[j*m_MapWidth+i] & 4))
			{
				m_GrowingMap[j*m_MapWidth+i] = 1;
			}
		}
	}
}

void CGameControllerMOD::EndRound()
{
	m_InfectedStarted = false;
	ResetFinalExplosion();
	IGameController::EndRound();
}

void CGameControllerMOD::Tick()
{
	IGameController::Tick();
	
	m_HumanCounter = 0;
	m_InfectedCounter = 0;
	
	//Count type of players
	for(int i = 0; i < MAX_CLIENTS; i ++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		
		if(pPlayer->IsInfected()) m_InfectedCounter++;
		else m_HumanCounter++;
	}
	
	m_InfectedStarted = false;
	
	//If the game can start ...
	if(m_GameOverTick == -1 && m_HumanCounter + m_InfectedCounter >= 2)
	{
		//If the infection started
		if(m_RoundStartTick + Server()->TickSpeed()*10 < Server()->Tick())
		{	
			m_InfectedStarted = true;
	
			for(int i = 0; i < MAX_CLIENTS; i ++)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[i];
				
				if(!pPlayer) continue;
				if(pPlayer->GetTeam() == TEAM_SPECTATORS)
				{
					pPlayer->StartInfection();
				}
				else if(pPlayer->GetClass() == PLAYERCLASS_NONE)
				{
					pPlayer->SetClass(ChooseHumanClass(pPlayer));
				}
			}
			
			//If needed, infect players
			int nbInfectedNeeded = 2;
			if(m_InfectedCounter + m_HumanCounter < 4)
			{
				nbInfectedNeeded = 1;
			}
			
			while(m_InfectedCounter < nbInfectedNeeded)
			{
				float InfectionProb = 1.0/static_cast<float>(m_HumanCounter);
				float random = frandom();
				
				//Fair infection
				bool FairInfectionFound = false;
				for(int i = 0; i < MAX_CLIENTS; i ++)
				{
					CPlayer *pPlayer = GameServer()->m_apPlayers[i];
					
					if(!pPlayer) continue;
					if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
					if(pPlayer->IsInfected()) continue;
					
					if(!Server()->IsClientInfectedBefore(i))
					{
						Server()->InfecteClient(i);
						GameServer()->m_apPlayers[i]->StartInfection();
						m_InfectedCounter++;
						m_HumanCounter--;
						
						GameServer()->SendChatTarget_Language_s(-1, "%s has been infected", Server()->ClientName(i));
						FairInfectionFound = true;
						break;
					}
				}
				
				//Unfair infection
				if(!FairInfectionFound)
				{
					for(int i = 0; i < MAX_CLIENTS; i ++)
					{
						CPlayer *pPlayer = GameServer()->m_apPlayers[i];
						
						if(!pPlayer) continue;
						if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
						if(pPlayer->IsInfected()) continue;
						
						if(random < InfectionProb)
						{
							Server()->InfecteClient(i);
							GameServer()->m_apPlayers[i]->StartInfection();
							m_InfectedCounter++;
							m_HumanCounter--;
							
							GameServer()->SendChatTarget_Language_s(-1, "%s has been infected", Server()->ClientName(i));
							
							break;
						}
						else
						{
							random -= InfectionProb;
						}
					}
				}
			}
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i ++)
			{
				CPlayer *pPlayer = GameServer()->m_apPlayers[i];
				
				if(!pPlayer) continue;
				if(pPlayer->GetTeam() == TEAM_SPECTATORS)
				{
					pPlayer->SetClass(PLAYERCLASS_NONE);
				}
			}
		}
		
		//Win check
		if(m_InfectedStarted && m_HumanCounter == 0 && m_InfectedCounter > 1)
		{			
			float RoundDuration = static_cast<float>((Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed()))/60.0f;
			int Minutes = static_cast<int>(RoundDuration);
			int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
			
			GameServer()->SendChatTarget_Language_ii(-1, "Infected won the round in %i:%02i minutes", Minutes, Seconds);
			
			EndRound();
		}
		
		//Start the final explosion if the time is over
		if(m_InfectedStarted && !m_ExplosionStarted && g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60)
		{
			for(CCharacter *p = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
			{
				if(p->IsInfected())
				{
					GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_GHOST);
				}
				else
				{
					GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_EYES);
				}
			}
			m_ExplosionStarted = true;
		}
		
		//Do the final explosion
		if(m_ExplosionStarted)
		{		
			bool NewExplosion = false;
			
			for(int j=0; j<m_MapHeight; j++)
			{
				for(int i=0; i<m_MapWidth; i++)
				{
					if((m_GrowingMap[j*m_MapWidth+i] & 1) && (
						(i > 0 && m_GrowingMap[j*m_MapWidth+i-1] & 2) ||
						(i < m_MapWidth-1 && m_GrowingMap[j*m_MapWidth+i+1] & 2) ||
						(j > 0 && m_GrowingMap[(j-1)*m_MapWidth+i] & 2) ||
						(j < m_MapHeight-1 && m_GrowingMap[(j+1)*m_MapWidth+i] & 2)
					))
					{
						NewExplosion = true;
						m_GrowingMap[j*m_MapWidth+i] |= 8;
						m_GrowingMap[j*m_MapWidth+i] &= ~1;
						if(rand()%10 == 0)
						{
							vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i*32.0f, j*32.0f);
							GameServer()->CreateExplosion(TilePos, -1, WEAPON_GAME, true);
							GameServer()->CreateSound(TilePos, SOUND_GRENADE_EXPLODE);
						}
					}
				}
			}
			
			for(int j=0; j<m_MapHeight; j++)
			{
				for(int i=0; i<m_MapWidth; i++)
				{
					if(m_GrowingMap[j*m_MapWidth+i] & 8)
					{
						m_GrowingMap[j*m_MapWidth+i] &= ~8;
						m_GrowingMap[j*m_MapWidth+i] |= 2;
					}
				}
			}
			
			for(CCharacter *p = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
			{
				if(!p->IsInfected())
					continue;
				
				int tileX = static_cast<int>(round(p->m_Pos.x))/32;
				int tileY = static_cast<int>(round(p->m_Pos.y))/32;
				
				if(tileX < 0) tileX = 0;
				if(tileX >= m_MapWidth) tileX = m_MapWidth-1;
				if(tileY < 0) tileY = 0;
				if(tileY >= m_MapHeight) tileY = m_MapHeight-1;
				
				if(m_GrowingMap[tileY*m_MapWidth+tileX] & 2)
				{
					p->Die(-1, WEAPON_GAME);
				}
			}
		
			//If no more explosions, game over, decide who win
			if(!NewExplosion)
			{
				if(m_HumanCounter)
				{
					if(m_HumanCounter <= 1)
					{
						GameServer()->SendChatTarget_Language(-1, "One human won the round");
					}
					else
					{
						GameServer()->SendChatTarget_Language_i(-1, "%i humans won the round", m_HumanCounter);
					}
					
					for(int i = 0; i < MAX_CLIENTS; i ++)
					{
						CPlayer *pPlayer = GameServer()->m_apPlayers[i];
						
						if(!pPlayer) continue;
						if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
						
						if(!pPlayer->IsInfected())
						{
							pPlayer->IncreaseScore(5);
							pPlayer->m_WinAsHuman++;
							
							GameServer()->SendChatTarget_Language(i, "You have survived, +5 points");
						}
					}
				}
				else
				{
					GameServer()->SendChatTarget_Language_ii(-1, "Infected won the round in %i:%02i minutes", g_Config.m_SvTimelimit, 0);
				}
				
				EndRound();
			}
		}
	}
	else
	{
		m_RoundStartTick = Server()->Tick();
	}
}

void CGameControllerMOD::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient);

	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	//Search for witch
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		
		if(pPlayer->GetClass() == PLAYERCLASS_WITCH)
		{
			pGameDataObj->m_FlagCarrierRed = i;
		}
	}
	pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
}

int CGameControllerMOD::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
		
	if(pKiller == pVictim->GetPlayer())
	{
		if(!pVictim->IsInfected())
		{
			CPlayer* pVictimPlayer = pVictim->GetPlayer();
			if(pVictimPlayer)
			{
				//Death by portal
				if(Weapon == WEAPON_WORLD && (Server()->Tick() - pVictim->m_PortalTick) < Server()->TickSpeed() && pVictim->m_LastPortalOwner != pVictimPlayer->GetCID())
				{
					CPlayer* pBadPlayer = GameServer()->m_apPlayers[pVictim->m_LastPortalOwner];
					if(pBadPlayer)
					{
						//~ GameServer()->SendChatTarget_Language_s(pBadPlayer->GetCID(), TEXTID_YOU_PORTAL_KILL, Server()->ClientName(pVictimPlayer->GetCID()));
						pBadPlayer->IncreaseScore(-5);
					}
				}
			}
			
			pVictim->GetPlayer()->IncreaseScore(-1); // suicide
		}
		else if(pVictim->GetClass() != PLAYERCLASS_BOOMER)
		{
			pVictim->GetPlayer()->IncreaseScore(-1); // suicide
		}
	}
	else if(pKiller->IsInfected())
	{
		CPlayer* pVictimPlayer = pVictim->GetPlayer();
		if(pVictimPlayer)
		{
			if(!pVictim->IsInfected() && Weapon == WEAPON_NINJA)
			{
				GameServer()->SendChatTarget_Language_s(pKiller->GetCID(), "You have infected %s, +3 points", Server()->ClientName(pVictimPlayer->GetCID()));
				
				pKiller->IncreaseScore(3);
				pKiller->IncreaseNbInfection(1);
			}
		}
	}
	else if(!pKiller->IsInfected())
	{
		if(pVictim->GetClass() == PLAYERCLASS_WITCH)
		{
			GameServer()->SendChatTarget_Language(pKiller->GetCID(), "You have killed a witch, +5 points");
			pKiller->IncreaseScore(5);
		}
		else pKiller->IncreaseScore(1);
		
		//Add bonus point for ninja
		if(pVictim->IsFrozen() && pVictim->m_LastFreezer >= 0)
		{
			CPlayer* pFreezer = GameServer()->m_apPlayers[pVictim->m_LastFreezer];
			if(pFreezer)
			{
				pFreezer->IncreaseScore(1);
			}
		}
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

void CGameControllerMOD::OnPlayerInfoChange(class CPlayer *pP)
{
	//~ std::cout << "SkinName : " << pP->m_TeeInfos.m_SkinName << std::endl;
	//~ std::cout << "ColorBody : " << pP->m_TeeInfos.m_ColorBody << std::endl;
	//~ std::cout << "ColorFeet : " << pP->m_TeeInfos.m_ColorFeet << std::endl;
	//~ pP->SetClassSkin(pP->GetClass());
}

void CGameControllerMOD::DoWincheck()
{
	
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
		{
			if(
				GameServer()->Collision()->CheckPoint(Pos+Positions[Index]) ||
				GameServer()->Collision()->CheckPointFlag(Pos+Positions[Index], CCollision::COLFLAG_NOSPAWN) ||
				distance(aEnts[c]->m_Pos, Pos+Positions[Index]) <= aEnts[c]->m_ProximityRadius
			)
			{
				return false;
			}
		}
	}
	return true;
}

bool CGameControllerMOD::PreSpawn(CPlayer* pPlayer, vec2 *pOutPos)
{
	// spectators can't spawn
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		return false;
	
	if(m_InfectedStarted)
	{
		pPlayer->StartInfection();
	}
	else
	{
		pPlayer->m_WasHumanThisRound = true;
	}
		
	if(pPlayer->IsInfected() && m_ExplosionStarted)
		return false;
		
	if(m_InfectedStarted && pPlayer->IsInfected() && rand()%3 > 0)
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
	int Team = (pPlayer->IsInfected() ? TEAM_RED : TEAM_BLUE);
	Eval.m_FriendlyTeam = Team;

	// first try own team spawn, then normal spawn and then enemy
	EvaluateSpawnType(&Eval, 1+Team);

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
	float TotalProbHumanClass = m_TotalProbHumanClass;
	
	//Get information about existing infected
	int nbSupport = 0;
	int nbMedic = 0;
	int nbSoldier = 0;
	int nbEngineer = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		
		switch(pPlayer->GetClass())
		{
			case PLAYERCLASS_NINJA:
			case PLAYERCLASS_MERCENARY:
			case PLAYERCLASS_SNIPER:
			case PLAYERCLASS_SCIENTIST:
				nbSupport++;
				break;
			case PLAYERCLASS_MEDIC:
				nbMedic++;
				break;
			case PLAYERCLASS_SOLDIER:
				nbSoldier++;
				break;
			case PLAYERCLASS_ENGINEER:
				nbEngineer++;
				break;
		}
	}
	
	bool scientistEnabled = true;
	if(nbSupport >= m_SupportLimit || Server()->GetClassAvailability(PLAYERCLASS_SCIENTIST) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_SCIENTIST];
		scientistEnabled = false;
	}
	
	bool ninjaEnabled = true;
	if(nbSupport >= m_SupportLimit || Server()->GetClassAvailability(PLAYERCLASS_NINJA) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_NINJA];
		ninjaEnabled = false;
	}
	
	bool sniperEnabled = true;
	if(nbSupport >= m_SupportLimit || Server()->GetClassAvailability(PLAYERCLASS_SNIPER) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_SNIPER];
		sniperEnabled = false;
	}
	
	bool mercenaryEnabled = true;
	if(nbSupport >= m_SupportLimit || Server()->GetClassAvailability(PLAYERCLASS_MERCENARY) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_MERCENARY];
		mercenaryEnabled = false;
	}
	
	bool medicEnabled = true;
	if(nbMedic >= m_MedicLimit || Server()->GetClassAvailability(PLAYERCLASS_MEDIC) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_MEDIC];
		medicEnabled = false;
	}
	
	bool soldierEnabled = true;
	if(nbSoldier >= m_SoldierLimit || Server()->GetClassAvailability(PLAYERCLASS_SOLDIER) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_SOLDIER];
		soldierEnabled = false;
	}
	
	bool engineerEnabled = true;
	if(nbEngineer >= m_EngineerLimit || Server()->GetClassAvailability(PLAYERCLASS_ENGINEER) == 0)
	{
		TotalProbHumanClass -= m_ClassProbability[PLAYERCLASS_ENGINEER];
		engineerEnabled = false;
	}
	
	if(scientistEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_SCIENTIST]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_SCIENTIST;
		}
	}
	
	if(ninjaEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_NINJA]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_NINJA;
		}
	}
	
	if(medicEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_MEDIC]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_MEDIC;
		}
	}
	
	if(sniperEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_SNIPER]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_SNIPER;
		}
	}
	
	if(mercenaryEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_MERCENARY]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_MERCENARY;
		}
	}
	
	if(soldierEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_SOLDIER]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_SOLDIER;
		}
	}
	
	if(scientistEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_ENGINEER]/TotalProbHumanClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_ENGINEER;
		}
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
	
	//Check if hunters are enabled
	bool hunterEnabled = true;
	if(Server()->GetClassAvailability(PLAYERCLASS_HUNTER) == 0)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_HUNTER];
		hunterEnabled = false;
	}
	
	//Check if ghost are enabled
	bool ghostEnabled = true;
	if(Server()->GetClassAvailability(PLAYERCLASS_GHOST) == 0)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_GHOST];
		ghostEnabled = false;
	}
	
	//Check if spider are enabled
	bool spiderEnabled = true;
	if(Server()->GetClassAvailability(PLAYERCLASS_SPIDER) == 0)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_SPIDER];
		spiderEnabled = false;
	}
	
	//Check if boomers are enabled
	bool boomerEnabled = true;
	if(Server()->GetClassAvailability(PLAYERCLASS_BOOMER) == 0)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_BOOMER];
		boomerEnabled = false;
	}
	
	//Check if undeads are enabled
	bool undeadEnabled = true;
	if(nbInfected < 2 || thereIsAnUndead || (Server()->GetClassAvailability(PLAYERCLASS_UNDEAD) == 0) || !pPlayer->m_WasHumanThisRound)
	{
		TotalProbInfectedClass -= m_ClassProbability[PLAYERCLASS_UNDEAD];
		undeadEnabled = false;
	}
	
	//Check if witches are enabled
	bool witchEnabled = true;
	if(nbInfected < 2 || thereIsAWitch || (Server()->GetClassAvailability(PLAYERCLASS_WITCH) == 0) || !pPlayer->m_WasHumanThisRound)
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
			GameServer()->SendBroadcast_Language(-1, "The undead is coming!");
			GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
			return PLAYERCLASS_UNDEAD;
		}
	}
	
	if(witchEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_WITCH]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			GameServer()->SendBroadcast_Language(-1, "The witch is coming!");
			GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
			return PLAYERCLASS_WITCH;
		}
	}
	
	if(boomerEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_BOOMER]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_BOOMER;
		}
	}
	
	if(ghostEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_GHOST]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_GHOST;
		}
	}
	
	if(spiderEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_SPIDER]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_SPIDER;
		}
	}
	
	if(hunterEnabled)
	{
		random -= m_ClassProbability[PLAYERCLASS_HUNTER]/TotalProbInfectedClass;
		if(random < 0.0f)
		{
			return PLAYERCLASS_HUNTER;
		}
	}
	
	return PLAYERCLASS_SMOKER;
}

bool CGameControllerMOD::IsChoosableClass(int PlayerClass)
{
	int nbClass = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		
		if(!pPlayer) continue;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS) continue;
		
		switch(PlayerClass)
		{
			case PLAYERCLASS_ENGINEER:
				if(pPlayer->GetClass() == PLAYERCLASS_ENGINEER) nbClass++;
				break;
			case PLAYERCLASS_SOLDIER:
				if(pPlayer->GetClass() == PLAYERCLASS_SOLDIER) nbClass++;
				break;
			case PLAYERCLASS_MEDIC:
				if(pPlayer->GetClass() == PLAYERCLASS_MEDIC) nbClass++;
				break;
			case PLAYERCLASS_NINJA:
			case PLAYERCLASS_MERCENARY:
			case PLAYERCLASS_SNIPER:
			case PLAYERCLASS_SCIENTIST:
				if(pPlayer->GetClass() == PLAYERCLASS_NINJA) nbClass++;
				if(pPlayer->GetClass() == PLAYERCLASS_MERCENARY) nbClass++;
				if(pPlayer->GetClass() == PLAYERCLASS_SNIPER) nbClass++;
				if(pPlayer->GetClass() == PLAYERCLASS_SCIENTIST) nbClass++;
				break;
		}
	}
	
	if(PlayerClass == PLAYERCLASS_ENGINEER) return (nbClass < m_EngineerLimit && Server()->GetClassAvailability(PLAYERCLASS_ENGINEER) > 1);
	else if(PlayerClass == PLAYERCLASS_SOLDIER) return (nbClass < m_SoldierLimit && Server()->GetClassAvailability(PLAYERCLASS_SOLDIER) > 1);
	else if(PlayerClass == PLAYERCLASS_MEDIC) return (nbClass < m_MedicLimit && Server()->GetClassAvailability(PLAYERCLASS_MEDIC) > 1);
	else if(PlayerClass == PLAYERCLASS_NINJA) return (nbClass < m_SupportLimit && Server()->GetClassAvailability(PLAYERCLASS_NINJA) > 1);
	else if(PlayerClass == PLAYERCLASS_MERCENARY) return (nbClass < m_SupportLimit && Server()->GetClassAvailability(PLAYERCLASS_MERCENARY) > 1);
	else if(PlayerClass == PLAYERCLASS_SNIPER) return (nbClass < m_SupportLimit && Server()->GetClassAvailability(PLAYERCLASS_SNIPER) > 1);
	else if(PlayerClass == PLAYERCLASS_SCIENTIST) return (nbClass < m_SupportLimit && Server()->GetClassAvailability(PLAYERCLASS_SCIENTIST) > 1);
	else return false;
}

bool CGameControllerMOD::CanVote()
{
	return !m_InfectedStarted;
}
