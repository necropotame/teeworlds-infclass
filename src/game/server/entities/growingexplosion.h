/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_GROWINGEXP_H
#define GAME_SERVER_ENTITIES_GROWINGEXP_H

enum
{
	GROWINGEXPLOSIONEFFECT_FREEZE_INFECTED=0,
	GROWINGEXPLOSIONEFFECT_POISON_INFECTED,
	GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED,
};
	
template<int Radius>
class CGrowingExplosion : public CEntity
{
private:
	enum
	{
		MAXGROWING = Radius,
		GROWINGMAP_LENGTH = (2*MAXGROWING+1),
		GROWINGMAP_SIZE = (GROWINGMAP_LENGTH*GROWINGMAP_LENGTH),
	};

public:
	CGrowingExplosion(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir, int Owner, int ExplosionEffect)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_GROWINGEXPLOSION)
	{
		m_Pos = Pos;
		m_StartTick = Server()->Tick();
		m_Owner = Owner;
		m_ExplosionEffect = ExplosionEffect;

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
		m_SeedY = static_cast<int>(round(m_SeedPos.y))/32;
		
		for(int j=0; j<GROWINGMAP_LENGTH; j++)
		{
			for(int i=0; i<GROWINGMAP_LENGTH; i++)
			{
				vec2 Tile = m_SeedPos + vec2(32.0f*(i-MAXGROWING), 32.0f*(j-MAXGROWING));
				if(GameServer()->Collision()->CheckPoint(Tile) || distance(Tile, m_SeedPos) > MAXGROWING*32.0f)
				{
					m_GrowingMap[j*GROWINGMAP_LENGTH+i] = -2;
				}
				else
				{
					m_GrowingMap[j*GROWINGMAP_LENGTH+i] = -1;
				}
				
				m_GrowingMapVec[j*GROWINGMAP_LENGTH+i] = vec2(0.0f, 0.0f);
			}
		}
		
		m_GrowingMap[MAXGROWING*GROWINGMAP_LENGTH+MAXGROWING] = Server()->Tick();
		
		switch(m_ExplosionEffect)
		{
			case GROWINGEXPLOSIONEFFECT_FREEZE_INFECTED:
				if(rand()%10 == 0)
				{
					GameServer()->CreateHammerHit(m_SeedPos);
				}
				break;
			case GROWINGEXPLOSIONEFFECT_POISON_INFECTED:
				if(rand()%10 == 0)
				{
					GameServer()->CreateDeath(m_SeedPos, m_Owner);
				}
				break;
			case GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED:
				{
					//~ GameServer()->CreateHammerHit(m_SeedPos);
						
					vec2 EndPoint = m_SeedPos + vec2(-16.0f + frandom()*32.0f, -16.0f + frandom()*32.0f);
					m_GrowingMapVec[MAXGROWING*GROWINGMAP_LENGTH+MAXGROWING] = EndPoint;
				}					
				break;
		}
	}
	
	virtual void Reset()
	{
		GameServer()->m_World.DestroyEntity(this);
	}
	
	virtual void Tick()
	{
		int tick = Server()->Tick();
		if((tick - m_StartTick) > Server()->TickSpeed())
		{
			GameServer()->m_World.DestroyEntity(this);
			return;
		}
		
		bool NewTile = false;
		
		for(int j=0; j<GROWINGMAP_LENGTH; j++)
		{
			for(int i=0; i<GROWINGMAP_LENGTH; i++)
			{
				if(m_GrowingMap[j*GROWINGMAP_LENGTH+i] == -1)
				{
					bool FromLeft = (i > 0 && m_GrowingMap[j*GROWINGMAP_LENGTH+i-1] < tick && m_GrowingMap[j*GROWINGMAP_LENGTH+i-1] >= 0);
					bool FromRight = (i < GROWINGMAP_LENGTH-1 && m_GrowingMap[j*GROWINGMAP_LENGTH+i+1] < tick && m_GrowingMap[j*GROWINGMAP_LENGTH+i+1] >= 0);
					bool FromTop = (j > 0 && m_GrowingMap[(j-1)*GROWINGMAP_LENGTH+i] < tick && m_GrowingMap[(j-1)*GROWINGMAP_LENGTH+i] >= 0);
					bool FromBottom = (j < GROWINGMAP_LENGTH-1 && m_GrowingMap[(j+1)*GROWINGMAP_LENGTH+i] < tick && m_GrowingMap[(j+1)*GROWINGMAP_LENGTH+i] >= 0);
					
					if(FromLeft || FromRight || FromTop || FromBottom)
					{
						m_GrowingMap[j*GROWINGMAP_LENGTH+i] = tick;
						NewTile = true;
						switch(m_ExplosionEffect)
						{
							case GROWINGEXPLOSIONEFFECT_FREEZE_INFECTED:
								if(rand()%10 == 0)
								{
									GameServer()->CreateHammerHit(m_SeedPos + vec2(32.0f*(i-MAXGROWING) - 16.0f + frandom()*32.0f, 32.0f*(j-MAXGROWING) - 16.0f + frandom()*32.0f));
								}
								break;
							case GROWINGEXPLOSIONEFFECT_POISON_INFECTED:
								if(rand()%10 == 0)
								{
									GameServer()->CreateDeath(m_SeedPos + vec2(32.0f*(i-MAXGROWING) - 16.0f + frandom()*32.0f, 32.0f*(j-MAXGROWING) - 16.0f + frandom()*32.0f), m_Owner);
								}
								break;
							case GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED:
								{
									vec2 EndPoint = m_SeedPos + vec2(32.0f*(i-MAXGROWING) - 16.0f + frandom()*32.0f, 32.0f*(j-MAXGROWING) - 16.0f + frandom()*32.0f);
									m_GrowingMapVec[j*GROWINGMAP_LENGTH+i] = EndPoint;
									
									int NumPossibleStartPoint = 0;
									vec2 PossibleStartPoint[4];
									
									if(FromLeft)
									{
										PossibleStartPoint[NumPossibleStartPoint] = m_GrowingMapVec[j*GROWINGMAP_LENGTH+i-1];
										NumPossibleStartPoint++;
									}
									if(FromRight)
									{
										PossibleStartPoint[NumPossibleStartPoint] = m_GrowingMapVec[j*GROWINGMAP_LENGTH+i+1];
										NumPossibleStartPoint++;
									}
									if(FromTop)
									{
										PossibleStartPoint[NumPossibleStartPoint] = m_GrowingMapVec[(j-1)*GROWINGMAP_LENGTH+i];
										NumPossibleStartPoint++;
									}
									if(FromBottom)
									{
										PossibleStartPoint[NumPossibleStartPoint] = m_GrowingMapVec[(j+1)*GROWINGMAP_LENGTH+i];
										NumPossibleStartPoint++;
									}
									
									if(NumPossibleStartPoint > 0)
									{
										int randNb = rand()%NumPossibleStartPoint;
										vec2 StartPoint = PossibleStartPoint[randNb];
										GameServer()->CreateLaserDotEvent(StartPoint, EndPoint, Server()->TickSpeed()/6);
									}
									
									if(rand()%10 == 0)
									{
										GameServer()->CreateSound(EndPoint, SOUND_RIFLE_BOUNCE);
									}
								}
								break;
						}
					}
				}
			}
		}
		
		if(NewTile)
		{
			switch(m_ExplosionEffect)
			{
				case GROWINGEXPLOSIONEFFECT_POISON_INFECTED:
					if(rand()%10 == 0)
					{
						GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);
					}
					break;
			}
		}
		
		// Find other players
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			int tileX = MAXGROWING + static_cast<int>(round(p->m_Pos.x))/32 - m_SeedX;
			int tileY = MAXGROWING + static_cast<int>(round(p->m_Pos.y))/32 - m_SeedY;
			
			if(!p->IsInfected())
				continue;
			
			if(tileX < 0 || tileX >= GROWINGMAP_LENGTH || tileY < 0 || tileY >= GROWINGMAP_LENGTH)
				continue;
			
			int k = tileY*GROWINGMAP_LENGTH+tileX;
			if(m_GrowingMap[k] >= 0)
			{
				if(tick - m_GrowingMap[k] < Server()->TickSpeed()/4)
				{
					switch(m_ExplosionEffect)
					{
						case GROWINGEXPLOSIONEFFECT_FREEZE_INFECTED:
							p->Freeze(3.0f, m_Owner, FREEZEREASON_FLASH);
							GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_QUESTION);
							break;
						case GROWINGEXPLOSIONEFFECT_POISON_INFECTED:
							p->Poison(7, m_Owner);
							GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_DROP);
							break;
						case GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED:
						{
							int Damage = 5+20*((float)(MAXGROWING - min(tick - m_StartTick, (int)MAXGROWING)))/(MAXGROWING);
							p->TakeDamage(normalize(p->m_Pos - m_SeedPos)*10.0f, Damage, m_Owner, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
							break;
						}
					}
				}
			}
		}
	}
	
	virtual void TickPaused()
	{
		++m_StartTick;
	}

private:
	int m_Owner;
	vec2 m_SeedPos;
	int m_SeedX;
	int m_SeedY;
	int m_StartTick;
	int m_GrowingMap[GROWINGMAP_SIZE];
	vec2 m_GrowingMapVec[GROWINGMAP_SIZE];
	int m_ExplosionEffect;
};

#endif
