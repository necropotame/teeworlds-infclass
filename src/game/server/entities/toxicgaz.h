/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_TOXICGAZ_H
#define GAME_SERVER_ENTITIES_TOXICGAZ_H

#include <game/server/entity.h>

class CToxicGaz : public CEntity
{
public:
	CToxicGaz(CGameWorld *pGameWorld, vec2 Pos);

	virtual void Reset();
	virtual void Tick();
	
	void GetIdForColor();
	
private:
	int m_AnimationTick;
	int m_ColorId;
};

#endif
