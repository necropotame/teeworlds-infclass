/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>
#include <base/math.h>
#include <base/vmath.h>

#include <math.h>
#include <engine/map.h>
#include <engine/kernel.h>

#include <game/mapitems.h>
#include <game/layers.h>
#include <game/collision.h>

CCollision::CCollision()
{
	m_pPhysicsTiles = 0;
	m_PhysicsWidth = 0;
	m_PhysicsHeight = 0;
	
	m_pZoneTiles = 0;
	m_ZoneWidth = 0;
	m_ZoneHeight = 0;
	
	m_pLayers = 0;
}

CCollision::~CCollision()
{
	if(m_pPhysicsTiles)
		delete[] m_pPhysicsTiles;
	if(m_pZoneTiles)
		delete[] m_pZoneTiles;
	
	m_pPhysicsTiles = 0;
	m_pZoneTiles = 0;
}

void CCollision::Init(class CLayers *pLayers)
{
	m_pLayers = pLayers;
	
	m_PhysicsWidth = m_pLayers->PhysicsLayer()->m_Width;
	m_PhysicsHeight = m_pLayers->PhysicsLayer()->m_Height;
	CTile* pPhysicsTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->PhysicsLayer()->m_Data));
	if(m_pPhysicsTiles)
		delete[] m_pPhysicsTiles;
	m_pPhysicsTiles = new int[m_PhysicsWidth*m_PhysicsHeight];

	for(int i = 0; i < m_PhysicsWidth*m_PhysicsHeight; i++)
	{
		switch(pPhysicsTiles[i].m_Index)
		{
		case TILE_PHYSICS_WATER:
			m_pPhysicsTiles[i] = COLFLAG_WATER;
			break;
		case TILE_PHYSICS_SOLID:
			m_pPhysicsTiles[i] = COLFLAG_SOLID;
			break;
		case TILE_PHYSICS_NOHOOK:
			m_pPhysicsTiles[i] = COLFLAG_SOLID|COLFLAG_NOHOOK;
			break;
		default:
			m_pPhysicsTiles[i] = 0x0;
			break;
		}
	}
	
	m_ZoneWidth = m_pLayers->ZoneLayer()->m_Width;
	m_ZoneHeight = m_pLayers->ZoneLayer()->m_Height;
	CTile* pZoneTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->ZoneLayer()->m_Data));
	if(m_pZoneTiles)
		delete[] m_pZoneTiles;
	m_pZoneTiles = new int[m_ZoneWidth*m_ZoneHeight];

	for(int i = 0; i < m_ZoneWidth*m_ZoneHeight; i++)
	{
		switch(pZoneTiles[i].m_Index)
		{
		case TILE_ZONE_DEATH:
			m_pZoneTiles[i] = ZONEFLAG_DEATH;
			break;
		case TILE_ZONE_INFECTION:
			m_pZoneTiles[i] = ZONEFLAG_INFECTION;
			break;
		case TILE_ZONE_NOSPAWN:
			m_pZoneTiles[i] = ZONEFLAG_NOSPAWN;
			break;
		default:
			m_pZoneTiles[i] = 0x0;
			break;
		}
	}
}

int CCollision::GetTile(int x, int y)
{
	int Nx = clamp(x/32, 0, m_PhysicsWidth-1);
	int Ny = clamp(y/32, 0, m_PhysicsHeight-1);

	return m_pPhysicsTiles[Ny*m_PhysicsWidth+Nx];
}

int CCollision::GetZoneTile(int x, int y)
{
	int Nx = clamp(x/32, 0, m_ZoneWidth-1);
	int Ny = clamp(y/32, 0, m_ZoneHeight-1);

	return m_pZoneTiles[Ny*m_ZoneWidth+Nx];
}

bool CCollision::IsTileSolid(int x, int y)
{
	return GetTile(x, y)&COLFLAG_SOLID;
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision)
{
	float Distance = distance(Pos0, Pos1);
	int End(Distance+1);
	vec2 Last = Pos0;

	for(int i = 0; i < End; i++)
	{
		float a = i/Distance;
		vec2 Pos = mix(Pos0, Pos1, a);
		if(CheckPoint(Pos.x, Pos.y))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

// TODO: OPT: rewrite this smarter!
void CCollision::MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces)
{
	if(pBounces)
		*pBounces = 0;

	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

bool CCollision::TestBox(vec2 Pos, vec2 Size)
{
	Size *= 0.5f;
	if(CheckPoint(Pos.x-Size.x, Pos.y-Size.y))
		return true;
	if(CheckPoint(Pos.x+Size.x, Pos.y-Size.y))
		return true;
	if(CheckPoint(Pos.x-Size.x, Pos.y+Size.y))
		return true;
	if(CheckPoint(Pos.x+Size.x, Pos.y+Size.y))
		return true;
	return false;
}

void CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity)
{
	// do the move
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;

	float Distance = length(Vel);
	int Max = (int)Distance;

	if(Distance > 0.00001f)
	{
		//vec2 old_pos = pos;
		float Fraction = 1.0f/(float)(Max+1);
		for(int i = 0; i <= Max; i++)
		{
			//float amount = i/(float)max;
			//if(max == 0)
				//amount = 0;

			vec2 NewPos = Pos + Vel*Fraction; // TODO: this row is not nice

			if(TestBox(vec2(NewPos.x, NewPos.y), Size))
			{
				int Hits = 0;

				if(TestBox(vec2(Pos.x, NewPos.y), Size))
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					Hits++;
				}

				if(TestBox(vec2(NewPos.x, Pos.y), Size))
				{
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
					Hits++;
				}

				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}

/* INFECTION MODIFICATION START ***************************************/
bool CCollision::CheckPhysicsFlag(vec2 Pos, int Flag)
{
	return GetTile(Pos.x, Pos.y)&Flag;
}

bool CCollision::CheckZoneFlag(vec2 Pos, int Flag)
{
	return GetZoneTile(Pos.x, Pos.y)&Flag;
}
/* INFECTION MODIFICATION END *****************************************/
