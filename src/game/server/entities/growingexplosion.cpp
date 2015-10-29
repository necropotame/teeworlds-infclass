/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "growingexplosion.h"

CGrowingExplosion::CGrowingExplosion(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_GROWINGEXPLOSION)
{
	m_Pos = Pos;
	m_StartTick = Server()->Tick();

	GameWorld()->InsertEntity(this);	
	
	vec2 explosionTile = vec2(16.0f, 16.0f) + vec2(
		static_cast<float>(static_cast<int>(round(m_Pos.x))/32)*32.0,
		static_cast<float>(static_cast<int>(round(m_Pos.y))/32)*32.0);
	
	//Check is the tile is occuped, and if the direction is valide
	if(GameServer()->Collision()->CheckPoint(explosionTile) && length(Dir) <= 1.1)
	{
		m_SeedPos = vec2(16.0f, 16.0f) + vec2(
		static_cast<float>(static_cast<int>(round(m_Pos.x + 32.0f*Dir.x))/32)*32.0,
		static_cast<float>(static_cast<int>(round(m_Pos.y + 32.0f*Dir.y))/32)*32.0);
	}
	else
	{
		m_SeedPos = explosionTile;
	}
	
	m_SeedX = static_cast<int>(round(m_SeedPos.x))/32;
	m_SeedY = static_cast<int>(round(m_SeedPos.x))/32;
	
	for(int j=0; j<GROWINGMAP_LENGTH; j++)
	{
		for(int i=0; i<GROWINGMAP_LENGTH; i++)
		{
			vec2 Tile = m_SeedPos + vec2(32.0f*(i-MAXGROWING), 32.0f*(j-MAXGROWING));
			if(GameServer()->Collision()->CheckPoint(Tile) || distance(Tile, m_SeedPos) > MAXGROWING*32.0f)
			{
				m_GrowingMap[j*GROWINGMAP_LENGTH+i] = -1;
			}
			else
			{
				m_GrowingMap[j*GROWINGMAP_LENGTH+i] = 0;
			}
		}
	}
	
	m_GrowingMap[MAXGROWING*GROWINGMAP_LENGTH+MAXGROWING] = 2;
	GameServer()->CreateHammerHit(m_SeedPos);
}

void CGrowingExplosion::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CGrowingExplosion::Tick()
{
	if((Server()->Tick() - m_StartTick) > Server()->TickSpeed())
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}
	
	for(int j=0; j<GROWINGMAP_LENGTH; j++)
	{
		for(int i=0; i<GROWINGMAP_LENGTH; i++)
		{
			if(m_GrowingMap[j*GROWINGMAP_LENGTH+i] == 0)
			{
				if(
					(i > 0 && m_GrowingMap[j*GROWINGMAP_LENGTH+i-1] == 2) ||
					(i < GROWINGMAP_LENGTH-1 && m_GrowingMap[j*GROWINGMAP_LENGTH+i+1] == 2) ||
					(j > 0 && m_GrowingMap[(j-1)*GROWINGMAP_LENGTH+i] == 2) ||
					(j < GROWINGMAP_LENGTH-1 && m_GrowingMap[(j+1)*GROWINGMAP_LENGTH+i] == 2)
				)
				{
					m_GrowingMap[j*GROWINGMAP_LENGTH+i] = 1;
					
					if(rand()%10 == 0)
					{
						GameServer()->CreateHammerHit(m_SeedPos + vec2(32.0f*(i-MAXGROWING) - 16.0f + frandom()*32.0f, 32.0f*(j-MAXGROWING) - 16.0f + frandom()*32.0f));
					}
				}
			}
		}
	}
	
	for(int j=0; j<GROWINGMAP_LENGTH; j++)
	{
		for(int i=0; i<GROWINGMAP_LENGTH; i++)
		{
			if(m_GrowingMap[j*GROWINGMAP_LENGTH+i] == 1)
			{
				m_GrowingMap[j*GROWINGMAP_LENGTH+i] = 2;
			}
		}
	}
	
	// Find other players
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(!p->IsInfected())
			continue;
			
		if(p->IsFrozen())
			continue;
		
		int tileX = MAXGROWING + static_cast<int>(round(p->m_Pos.x))/32 - m_SeedX;
		int tileY = MAXGROWING + static_cast<int>(round(p->m_Pos.y))/32 - m_SeedY;
		
		if(tileX < 0 || tileX >= GROWINGMAP_LENGTH || tileY < 0 || tileY >= GROWINGMAP_LENGTH)
			continue;
		
		int k = tileY*GROWINGMAP_LENGTH+tileX;
		if(m_GrowingMap[k] > 0)
		{
			p->Freeze(3.0f, FREEZEREASON_FLASH);
			GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_QUESTION);
		}
	}
}

void CGrowingExplosion::TickPaused()
{
	++m_StartTick;
}
