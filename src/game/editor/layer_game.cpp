/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "editor.h"


CLayerPhysics::CLayerPhysics(int w, int h)
: CLayerTiles(w, h)
{
	str_copy(m_aName, "Physics", sizeof(m_aName));
	m_GameFlags = TILESLAYERFLAG_PHYSICS;
}

CLayerPhysics::~CLayerPhysics()
{
}

int CLayerPhysics::RenderProperties(CUIRect *pToolbox)
{
	int r = CLayerTiles::RenderProperties(pToolbox);
	m_Image = -1;
	return r;
}

CLayerEntity::CLayerEntity(int w, int h)
: CLayerTiles(w, h)
{
	str_copy(m_aName, "Entities", sizeof(m_aName));
	m_GameFlags = TILESLAYERFLAG_ENTITY;
}

CLayerEntity::~CLayerEntity()
{
}

int CLayerEntity::RenderProperties(CUIRect *pToolbox)
{
	int r = CLayerTiles::RenderProperties(pToolbox);
	m_Image = -1;
	return r;
}

CLayerZone::CLayerZone(int w, int h)
: CLayerTiles(w, h)
{
	str_copy(m_aName, "Zones", sizeof(m_aName));
	m_GameFlags = TILESLAYERFLAG_ZONE;
}

CLayerZone::~CLayerZone()
{
}

int CLayerZone::RenderProperties(CUIRect *pToolbox)
{
	int r = CLayerTiles::RenderProperties(pToolbox);
	m_Image = -1;
	return r;
}

