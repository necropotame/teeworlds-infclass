/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "mod.h"

#include <game/server/player.h>
#include <engine/shared/config.h>
#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/network.h>
#include <game/mapitems.h>
#include <time.h>
#include <iostream>

CGameControllerMOD::CGameControllerMOD(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	m_pGameType = "InfClassR";
	
	m_GrowingMap = 0;
	
	m_ExplosionStarted = false;
	m_MapWidth = GameServer()->Collision()->GetWidth();
	m_MapHeight = GameServer()->Collision()->GetHeight();
	m_GrowingMap = new int[m_MapWidth*m_MapHeight];
	
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
	
	m_pHeroFlag = 0;
}

CGameControllerMOD::~CGameControllerMOD()
{
	if(m_GrowingMap) delete[] m_GrowingMap;
}

void CGameControllerMOD::OnClientDrop(int ClientID, int Type)
{
	if(Type == CLIENTDROPTYPE_BAN) return;
	if(Type == CLIENTDROPTYPE_KICK) return;
	if(Type == CLIENTDROPTYPE_SHUTDOWN) return;	
	
	CPlayer* pPlayer = GameServer()->m_apPlayers[ClientID];
	if(pPlayer && pPlayer->IsInfected() && m_InfectedStarted)
	{
		int NumHumans;
		int NumInfected;
		int NumFirstInfected;
		GetPlayerCounter(ClientID, NumHumans, NumInfected, NumFirstInfected);
		
		if(NumInfected < NumFirstInfected)
		{
			Server()->Ban(ClientID, 60*g_Config.m_InfLeaverBanTime, "Leaver");
		}
	}
}

bool CGameControllerMOD::OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv)
{
	bool res = IGameController::OnEntity(pName, Pivot, P0, P1, P2, P3, PosEnv);

	if(str_comp(pName, "icInfected") == 0)
	{
		vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;
		int SpawnX = static_cast<int>(Pos.x)/32.0f;
		int SpawnY = static_cast<int>(Pos.y)/32.0f;
		
		if(SpawnX >= 0 && SpawnX < m_MapWidth && SpawnY >= 0 && SpawnY < m_MapHeight)
		{
			m_GrowingMap[SpawnY*m_MapWidth+SpawnX] = 6;
		}
	}
	else if(str_comp(pName, "icHeroFlag") == 0)
	{
		//Add hero flag
		if(!m_pHeroFlag)
			m_pHeroFlag = new CHeroFlag(&GameServer()->m_World);
		
		vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;
		m_HeroFlagPositions.add(Pos);
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

void CGameControllerMOD::GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected, int& NumFirstInfected)
{
	NumHumans = 0;
	NumInfected = 0;
	
	//Count type of players
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.ClientID() == ClientException) continue;
		
		if(Iter.Player()->IsInfected()) NumInfected++;
		else NumHumans++;
	}
	
	if(NumHumans + NumInfected <= 1)
		NumFirstInfected = 0;
	else if(NumHumans + NumInfected <= 3)
		NumFirstInfected = 1;
	else
		NumFirstInfected = 2;
}

void CGameControllerMOD::Tick()
{
	IGameController::Tick();
	
	//Check session
	{
		CPlayerIterator<PLAYERITER_ALL> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			//Update session
			IServer::CClientSession* pSession = Server()->GetClientSession(Iter.ClientID());
			if(pSession)
			{
				if(!Server()->GetClientMemory(Iter.ClientID(), CLIENTMEMORY_SESSION_PROCESSED))
				{
					//The client already participated to this round,
					//and he exit the game as infected.
					//To avoid cheating, we assign to him the same class again.
					if(
						m_InfectedStarted &&
						pSession->m_RoundId == m_RoundId &&
						pSession->m_Class > END_HUMANCLASS
					)
					{
						Iter.Player()->SetClass(pSession->m_Class);
					}
					
					Server()->SetClientMemory(Iter.ClientID(), CLIENTMEMORY_SESSION_PROCESSED, true);
				}
				
				pSession->m_Class = Iter.Player()->GetClass();
				pSession->m_RoundId = GameServer()->m_pController->GetRoundId();
			}
		}
	}
	
	int NumHumans = 0;
	int NumInfected = 0;
	int NumFirstInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected, NumFirstInfected);
	
	m_InfectedStarted = false;
	
	//If the game can start ...
	if(m_GameOverTick == -1 && NumHumans + NumInfected >= g_Config.m_InfMinPlayers)
	{
		//If the infection started
		if(IsInfectionStarted())
		{
			bool StartInfectionTrigger = (m_RoundStartTick + Server()->TickSpeed()*10 == Server()->Tick());
			
			GameServer()->EnableTargetToKill();
			
			if(m_pHeroFlag)
				m_pHeroFlag->Show();
			
			m_InfectedStarted = true;
	
			CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
			while(Iter.Next())
			{
				if(Iter.Player()->GetClass() == PLAYERCLASS_NONE)
				{
					if(StartInfectionTrigger)
					{
						Iter.Player()->SetClass(ChooseHumanClass(Iter.Player()));
						if(Iter.Player()->GetCharacter())
							Iter.Player()->GetCharacter()->IncreaseArmor(10);
					}
					else
						Iter.Player()->StartInfection();
				}
			}
			
			int NumNeededInfection = NumFirstInfected;
			
			while(NumInfected < NumNeededInfection)
			{
				float InfectionProb = 1.0/static_cast<float>(NumHumans);
				float random = random_float();
				
				//Fair infection
				bool FairInfectionFound = false;
				
				Iter.Reset();
				while(Iter.Next())
				{
					if(Iter.Player()->IsInfected()) continue;
					
					if(!Server()->IsClientInfectedBefore(Iter.ClientID()))
					{
						Server()->InfecteClient(Iter.ClientID());
						Iter.Player()->StartInfection();
						NumInfected++;
						NumHumans--;
						
						GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION, _("{str:VictimName} has been infected"),
							"VictimName", Server()->ClientName(Iter.ClientID()),
							NULL
						);
						FairInfectionFound = true;
						break;
					}
				}
				
				//Unfair infection
				if(!FairInfectionFound)
				{
					Iter.Reset();
					while(Iter.Next())
					{
						if(Iter.Player()->IsInfected()) continue;
						
						if(random < InfectionProb)
						{
							Server()->InfecteClient(Iter.ClientID());
							Iter.Player()->StartInfection();
							NumInfected++;
							NumHumans--;
							
							GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION, _("{str:VictimName} has been infected"), "VictimName", Server()->ClientName(Iter.ClientID()), NULL);
							
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
			if(m_pHeroFlag)
				m_pHeroFlag->Show();
			
			GameServer()->DisableTargetToKill();
			
			CPlayerIterator<PLAYERITER_SPECTATORS> IterSpec(GameServer()->m_apPlayers);
			while(IterSpec.Next())
			{
				IterSpec.Player()->SetClass(PLAYERCLASS_NONE);
			}
		}
		
		//Win check
		if(m_InfectedStarted && NumHumans == 0 && NumInfected > 1)
		{			
			int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
			
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("Infected won the round in {sec:RoundDuration}"), "RoundDuration", &Seconds, NULL);
			
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "round_end winner='zombies' survivors='0' duration='%d' round='%d of %d'", Seconds, m_RoundCount+1, g_Config.m_SvRoundsPerMap);
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

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
						if(random_prob(0.1f))
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
				
				if(m_GrowingMap[tileY*m_MapWidth+tileX] & 2 && p->GetPlayer())
				{
					p->Die(p->GetPlayer()->GetCID(), WEAPON_GAME);
				}
			}
		
			//If no more explosions, game over, decide who win
			if(!NewExplosion)
			{
				if(NumHumans)
				{
					GameServer()->SendChatTarget_Localization_P(-1, CHATCATEGORY_HUMANS, NumHumans, _P("One human won the round", "{int:NumHumans} humans won the round"), "NumHumans", &NumHumans, NULL);
					
					char aBuf[512];
					int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
					str_format(aBuf, sizeof(aBuf), "round_end winner='humans' survivors='%d' duration='%d' round='%d of %d'", NumHumans, Seconds, m_RoundCount+1, g_Config.m_SvRoundsPerMap);
					GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

					CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
					while(Iter.Next())
					{
						if(!Iter.Player()->IsInfected())
						{
							//TAG_SCORE
							Server()->RoundStatistics()->OnScoreEvent(Iter.ClientID(), SCOREEVENT_HUMAN_SURVIVE, Iter.Player()->GetClass());
							Server()->RoundStatistics()->SetPlayerAsWinner(Iter.ClientID());
							GameServer()->SendScoreSound(Iter.ClientID());
							Iter.Player()->m_WinAsHuman++;
							
							GameServer()->SendChatTarget_Localization(Iter.ClientID(), CHATCATEGORY_SCORE, _("You have survived, +5 points"), NULL);

							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "survived player='%s'", Server()->ClientName(Iter.ClientID()));
							GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

						}
					}
				}
				else
				{
					int Seconds = g_Config.m_SvTimelimit*60;
					GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("Infected won the round in {sec:RoundDuration}"), "RoundDuration", &Seconds, NULL);
				}
				
				EndRound();
			}
		}
	}
	else
	{
		GameServer()->DisableTargetToKill();
		
		if(m_pHeroFlag)
			m_pHeroFlag->Show();
		
		m_RoundStartTick = Server()->Tick();
	}
}

bool CGameControllerMOD::IsInfectionStarted()
{
	return (m_RoundStartTick + Server()->TickSpeed()*10 <= Server()->Tick());
}

void CGameControllerMOD::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameServer()->m_World.m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	pGameInfoObj->m_WarmupTimer = m_Warmup;

	pGameInfoObj->m_ScoreLimit = g_Config.m_SvScorelimit;
	pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;

	pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount+1;

	//Generate class mask
	int ClassMask = 0;
	{
		int Defender = 0;
		int Medic = 0;
		int Hero = 0;
		int Support = 0;
		
		CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			switch(Iter.Player()->GetClass())
			{
				case PLAYERCLASS_NINJA:
				case PLAYERCLASS_MERCENARY:
				case PLAYERCLASS_SNIPER:
					Support++;
					break;
				case PLAYERCLASS_ENGINEER:
				case PLAYERCLASS_SOLDIER:
				case PLAYERCLASS_SCIENTIST:
				case PLAYERCLASS_BIOLOGIST:
					Defender++;
					break;
				case PLAYERCLASS_MEDIC:
					Medic++;
					break;
				case PLAYERCLASS_HERO:
					Hero++;
					break;
				case PLAYERCLASS_LOOPER:
					Defender++;
					break;
					
			}
		}
		
		if(Defender < g_Config.m_InfDefenderLimit)
			ClassMask |= CMapConverter::MASK_DEFENDER;
		if(Medic < g_Config.m_InfMedicLimit)
			ClassMask |= CMapConverter::MASK_MEDIC;
		if(Hero < g_Config.m_InfHeroLimit)
			ClassMask |= CMapConverter::MASK_HERO;
		if(Support < g_Config.m_InfSupportLimit)
			ClassMask |= CMapConverter::MASK_SUPPORT;
	}
	
	if(GameServer()->m_apPlayers[SnappingClient])
	{
		int Page = -1;
		
		if(GameServer()->m_apPlayers[SnappingClient]->MapMenu() == 1)
		{
			int Item = GameServer()->m_apPlayers[SnappingClient]->m_MapMenuItem;
			Page = CMapConverter::TIMESHIFT_MENUCLASS + 3*((Item+1) + ClassMask*CMapConverter::TIMESHIFT_MENUCLASS_MASK) + 1;
		}
		
		if(Page >= 0)
		{
			double PageShift = static_cast<double>(Page * Server()->GetTimeShiftUnit())/1000.0f;
			double CycleShift = fmod(static_cast<double>(Server()->Tick() - pGameInfoObj->m_RoundStartTick)/Server()->TickSpeed(), Server()->GetTimeShiftUnit()/1000.0);
			int TimeShift = (PageShift + CycleShift)*Server()->TickSpeed();
			
			pGameInfoObj->m_RoundStartTick = Server()->Tick() - TimeShift;
			pGameInfoObj->m_TimeLimit += (TimeShift/Server()->TickSpeed())/60;
		}
	}

	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	//Search for witch
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.Player()->GetClass() == PLAYERCLASS_WITCH)
		{
			pGameDataObj->m_FlagCarrierRed = Iter.ClientID();
		}
	}
	
	pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
}

int CGameControllerMOD::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
		
	if(pKiller->IsInfected())
	{
		CPlayer* pVictimPlayer = pVictim->GetPlayer();
		if(pVictimPlayer)
		{
			if(!pVictim->IsInfected())
			{
				GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have infected {str:VictimName}, +3 points"), "VictimName", Server()->ClientName(pVictimPlayer->GetCID()), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_INFECTION, pKiller->GetClass());
				GameServer()->SendScoreSound(pKiller->GetCID());
				
				//Search for hook
				for(CCharacter *pHook = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHook; pHook = (CCharacter *)pHook->TypeNext())
				{
					if(
						pHook->GetPlayer() &&
						pHook->m_Core.m_HookedPlayer == pVictim->GetPlayer()->GetCID() &&
						pHook->GetPlayer()->GetCID() != pKiller->GetCID()
					)
					{
						Server()->RoundStatistics()->OnScoreEvent(pHook->GetPlayer()->GetCID(), SCOREEVENT_HELP_HOOK_INFECTION, pHook->GetClass());
						GameServer()->SendScoreSound(pHook->GetPlayer()->GetCID());
					}
				}
			}
		}
	}
	else
	{
		if(pKiller == pVictim->GetPlayer())
		{
			Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_HUMAN_SUICIDE, pKiller->GetClass());
		}
		else
		{
			if(pVictim->GetClass() == PLAYERCLASS_WITCH)
			{
				GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have killed a witch, +5 points"), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_WITCH, pKiller->GetClass());
				GameServer()->SendScoreSound(pKiller->GetCID());
			}
			else if(pVictim->IsInfected())
			{
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_INFECTED, pKiller->GetClass());
				GameServer()->SendScoreSound(pKiller->GetCID());
			}
		
			if(pKiller->GetClass() == PLAYERCLASS_NINJA && pVictim->GetPlayer()->GetCID() == GameServer()->GetTargetToKill())
			{
				GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have eliminated your target, +2 points"), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_TARGET, pKiller->GetClass());
				GameServer()->TargetKilled();
				
				if(pKiller->GetCharacter())
				{
					pKiller->GetCharacter()->GiveNinjaBuf();
					pKiller->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
					GameServer()->SendEmoticon(pKiller->GetCID(), EMOTICON_MUSIC);
					pKiller->GetCharacter()->IncreaseHealth(4);
				}
			}
		}
	}
		
	//Add bonus point for ninja
	if(pVictim->IsInfected() && pVictim->IsFrozen() && pVictim->m_LastFreezer >= 0 && pVictim->m_LastFreezer != pKiller->GetCID())
	{
		CPlayer* pFreezer = GameServer()->m_apPlayers[pVictim->m_LastFreezer];
		if(pFreezer)
		{
			Server()->RoundStatistics()->OnScoreEvent(pFreezer->GetCID(), SCOREEVENT_HELP_FREEZE, pFreezer->GetClass());
			GameServer()->SendScoreSound(pFreezer->GetCID());
			
			if(pVictim->GetPlayer()->GetCID() == GameServer()->GetTargetToKill())
			{
				GameServer()->SendChatTarget_Localization(pFreezer->GetCID(), CHATCATEGORY_SCORE, _("You have eliminated your target, +2 points"), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pFreezer->GetCID(), SCOREEVENT_KILL_TARGET, pKiller->GetClass());
				GameServer()->TargetKilled();
				
				if(pFreezer->GetCharacter())
				{
					pFreezer->GetCharacter()->GiveNinjaBuf();
					pFreezer->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
					GameServer()->SendEmoticon(pFreezer->GetCID(), EMOTICON_MUSIC);
				}
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

bool CGameControllerMOD::IsSpawnable(vec2 Pos, int TeleZoneIndex)
{
	//First check if there is a tee too close
	CCharacter *aEnts[MAX_CLIENTS];
	int Num = GameServer()->m_World.FindEntities(Pos, 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	
	for(int c = 0; c < Num; ++c)
	{
		if(distance(aEnts[c]->m_Pos, Pos) <= 60)
			return false;
	}
	
	//Check the center
	int TeleIndex = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Teleport, Pos);
	if(GameServer()->Collision()->CheckPoint(Pos))
		return false;
	if(TeleZoneIndex && TeleIndex == TeleZoneIndex)
		return false;
	
	//Check the border of the tee. Kind of extrem, but more precise
	for(int i=0; i<16; i++)
	{
		float Angle = i * (2.0f * pi / 16.0f);
		vec2 CheckPos = Pos + vec2(cos(Angle), sin(Angle)) * 30.0f;
		TeleIndex = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Teleport, CheckPos);
		if(GameServer()->Collision()->CheckPoint(CheckPos))
			return false;
		if(TeleZoneIndex && TeleIndex == TeleZoneIndex)
			return false;
	}
	
	return true;
}

bool CGameControllerMOD::PreSpawn(CPlayer* pPlayer, vec2 *pOutPos)
{
	// spectators can't spawn
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		return false;
	
	if(m_InfectedStarted)
		pPlayer->StartInfection();
	else
		pPlayer->m_WasHumanThisRound = true;
		
	if(pPlayer->IsInfected() && m_ExplosionStarted)
		return false;
		
	if(m_InfectedStarted && pPlayer->IsInfected() && random_prob(0.66f))
	{
		CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if(Iter.Player()->GetCID() == pPlayer->GetCID()) continue;
			if(Iter.Player()->GetClass() != PLAYERCLASS_WITCH) continue;
			if(!Iter.Player()->GetCharacter()) continue;
			
			vec2 SpawnPos;
			if(Iter.Player()->GetCharacter()->FindWitchSpawnPosition(SpawnPos))
			{
				*pOutPos = SpawnPos;
				return true;
			}
		}
	}
			
	int Type = (pPlayer->IsInfected() ? 0 : 1);

	// get spawn point
	int RandomShift = random_int(0, m_SpawnPoints[Type].size()-1);
	for(int i = 0; i < m_SpawnPoints[Type].size(); i++)
	{
		int I = (i + RandomShift)%m_SpawnPoints[Type].size();
		if(IsSpawnable(m_SpawnPoints[Type][I], 0))
		{
			*pOutPos = m_SpawnPoints[Type][I];
			return true;
		}
	}
	
	return false;
}

bool CGameControllerMOD::PickupAllowed(int Index)
{
	if(Index == TILE_ENTITY_POWERUP_NINJA) return false;
	else if(Index == TILE_ENTITY_WEAPON_SHOTGUN) return false;
	else if(Index == TILE_ENTITY_WEAPON_GRENADE) return false;
	else if(Index == TILE_ENTITY_WEAPON_RIFLE) return false;
	else return true;
}

int CGameControllerMOD::ChooseHumanClass(CPlayer* pPlayer)
{
	//Get information about existing humans
	int nbSupport = 0;
	int nbHero = 0;
	int nbMedic = 0;
	int nbDefender = 0;
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);	
	
	while(Iter.Next())
	{
		switch(Iter.Player()->GetClass())
		{
			case PLAYERCLASS_NINJA:
			case PLAYERCLASS_MERCENARY:
			case PLAYERCLASS_SNIPER:
				nbSupport++;
				break;
			case PLAYERCLASS_MEDIC:
				nbMedic++;
				break;
			case PLAYERCLASS_HERO:
				nbHero++;
				break;
			case PLAYERCLASS_ENGINEER:
			case PLAYERCLASS_SOLDIER:
			case PLAYERCLASS_SCIENTIST:
			case PLAYERCLASS_BIOLOGIST:
				nbDefender++;
				break;
			case PLAYERCLASS_LOOPER:
				nbDefender++;
				break;
		}
	}
	
	double Probability[NB_HUMANCLASS];
	
	Probability[PLAYERCLASS_ENGINEER - START_HUMANCLASS - 1] =
		(nbDefender < g_Config.m_InfDefenderLimit && g_Config.m_InfEnableEngineer) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_SOLDIER - START_HUMANCLASS - 1] =
		(nbDefender < g_Config.m_InfDefenderLimit && g_Config.m_InfEnableSoldier) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_SCIENTIST - START_HUMANCLASS - 1] =
		(nbDefender < g_Config.m_InfDefenderLimit && g_Config.m_InfEnableScientist) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_BIOLOGIST - START_HUMANCLASS - 1] =
		(nbDefender < g_Config.m_InfDefenderLimit && g_Config.m_InfEnableBiologist) ?
		1.0f : 0.0f;

	Probability[PLAYERCLASS_MERCENARY - START_HUMANCLASS - 1] =
		(nbSupport < g_Config.m_InfSupportLimit && g_Config.m_InfEnableMercenary) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_SNIPER - START_HUMANCLASS - 1] =
		(nbSupport < g_Config.m_InfSupportLimit && g_Config.m_InfEnableSniper) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_NINJA - START_HUMANCLASS - 1] =
		(nbSupport < g_Config.m_InfSupportLimit && g_Config.m_InfEnableNinja) ?
		1.0f : 0.0f;

	Probability[PLAYERCLASS_MEDIC - START_HUMANCLASS - 1] =
		(nbMedic < g_Config.m_InfMedicLimit && g_Config.m_InfEnableMedic) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_HERO - START_HUMANCLASS - 1] =
		(nbHero < g_Config.m_InfHeroLimit && g_Config.m_InfEnableHero) ?
		1.0f : 0.0f;
	Probability[PLAYERCLASS_LOOPER - START_HUMANCLASS - 1] =
		(nbDefender < g_Config.m_InfDefenderLimit && g_Config.m_InfEnableLooper) ?
		1.0f : 0.0f;
	
	/* commented because it breaks newly added "Fun Rounds"
	//Random is not fair enough. We keep the last two classes took by the player, and avoid to give him those again
	
	for(unsigned int i=0; i<sizeof(pPlayer->m_LastHumanClasses)/sizeof(int); i++)
	{
		if(pPlayer->m_LastHumanClasses[i] > START_HUMANCLASS && pPlayer->m_LastHumanClasses[i] < END_HUMANCLASS)
		{
			Probability[pPlayer->m_LastHumanClasses[i] - 1 - START_HUMANCLASS] = 0.0f;
		}
	}*/
	
	return START_HUMANCLASS + 1 + random_distribution(Probability, Probability + NB_HUMANCLASS);
}

int CGameControllerMOD::ChooseInfectedClass(CPlayer* pPlayer)
{
	//Get information about existing infected
	int nbInfected = 0;
	bool thereIsAWitch = false;
	bool thereIsAnUndead = false;

	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.Player()->IsInfected()) nbInfected++;
		if(Iter.Player()->GetClass() == PLAYERCLASS_WITCH) thereIsAWitch = true;
		if(Iter.Player()->GetClass() == PLAYERCLASS_UNDEAD) thereIsAnUndead = true;
	}
	
	double Probability[NB_INFECTEDCLASS];
	
	Probability[PLAYERCLASS_SMOKER - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_SMOKER)) ?
		(double) g_Config.m_InfProbaSmoker : 0.0f;
	Probability[PLAYERCLASS_HUNTER - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_HUNTER)) ?
		(double) g_Config.m_InfProbaHunter : 0.0f;
	Probability[PLAYERCLASS_BAT - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_BAT)) ?
		(double) g_Config.m_InfProbaBat : 0.0f;
	Probability[PLAYERCLASS_BOOMER - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_BOOMER)) ?
		(double) g_Config.m_InfProbaBoomer : 0.0f;
	
	Probability[PLAYERCLASS_GHOST - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_GHOST)) ?
		(double) g_Config.m_InfProbaGhost : 0.0f;
	Probability[PLAYERCLASS_SPIDER - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_SPIDER)) ?
		(double) g_Config.m_InfProbaSpider : 0.0f;
	Probability[PLAYERCLASS_GHOUL - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_GHOUL) && nbInfected >= g_Config.m_InfGhoulThreshold) ?
		(double) g_Config.m_InfProbaGhoul : 0.0f;
	Probability[PLAYERCLASS_SLUG - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_SLUG)) ?
		(double) g_Config.m_InfProbaSlug : 0.0f;
	
	Probability[PLAYERCLASS_WITCH - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_WITCH) && nbInfected > 2 && !thereIsAWitch) ?
		(double) g_Config.m_InfProbaWitch : 0.0f;
	Probability[PLAYERCLASS_UNDEAD - START_INFECTEDCLASS - 1] =
		(Server()->GetClassAvailability(PLAYERCLASS_UNDEAD) && nbInfected > 2 && !thereIsAnUndead) ?
		(double) g_Config.m_InfProbaUndead : 0.0f;
	
	int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "infected victim='%s' duration='%d'", 
		Server()->ClientName(pPlayer->GetCID()), Seconds);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	return START_INFECTEDCLASS + 1 + random_distribution(Probability, Probability + NB_INFECTEDCLASS);
}

bool CGameControllerMOD::IsEnabledClass(int PlayerClass) {
	switch(PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
			return g_Config.m_InfEnableEngineer;
		case PLAYERCLASS_SOLDIER:
			return g_Config.m_InfEnableSoldier;
		case PLAYERCLASS_SCIENTIST:
			return g_Config.m_InfEnableScientist;
		case PLAYERCLASS_BIOLOGIST:
			return g_Config.m_InfEnableBiologist;
		case PLAYERCLASS_MEDIC:
			return g_Config.m_InfEnableMedic;
		case PLAYERCLASS_HERO:
			return g_Config.m_InfEnableHero;
		case PLAYERCLASS_NINJA:
			return g_Config.m_InfEnableNinja;
		case PLAYERCLASS_MERCENARY:
			return g_Config.m_InfEnableMercenary;
		case PLAYERCLASS_SNIPER:
			return g_Config.m_InfEnableSniper;
		case PLAYERCLASS_LOOPER:
			return g_Config.m_InfEnableLooper;
		default:
			return false;
	}
}

bool CGameControllerMOD::IsChoosableClass(int PlayerClass)
{
	if (!IsEnabledClass(PlayerClass))
		return false;

	int nbDefender = 0;
	int nbMedic = 0;
	int nbHero = 0;
	int nbSupport = 0;

	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		switch(Iter.Player()->GetClass())
		{
			case PLAYERCLASS_NINJA:
			case PLAYERCLASS_MERCENARY:
			case PLAYERCLASS_SNIPER:
				nbSupport++;
				break;
			case PLAYERCLASS_MEDIC:
				nbMedic++;
				break;
			case PLAYERCLASS_HERO:
				nbHero++;
				break;
			case PLAYERCLASS_ENGINEER:
			case PLAYERCLASS_SOLDIER:
			case PLAYERCLASS_SCIENTIST:
			case PLAYERCLASS_BIOLOGIST:
				nbDefender++;
				break;
			case PLAYERCLASS_LOOPER:
				nbDefender++;
				break;
		}
	}
	
	switch(PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
		case PLAYERCLASS_SOLDIER:
		case PLAYERCLASS_SCIENTIST:
		case PLAYERCLASS_BIOLOGIST:
			return (nbDefender < g_Config.m_InfDefenderLimit);
		case PLAYERCLASS_MEDIC:
			return (nbMedic < g_Config.m_InfMedicLimit);
		case PLAYERCLASS_HERO:
			return (nbHero < g_Config.m_InfHeroLimit);
		case PLAYERCLASS_NINJA:
		case PLAYERCLASS_MERCENARY:
		case PLAYERCLASS_SNIPER:
			return (nbSupport < g_Config.m_InfSupportLimit);
		case PLAYERCLASS_LOOPER:
			return (nbDefender < g_Config.m_InfDefenderLimit);
	}
	
	return false;
}

bool CGameControllerMOD::CanVote()
{
	return !m_InfectedStarted;
}
