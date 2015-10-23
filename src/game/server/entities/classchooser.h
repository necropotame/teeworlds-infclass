/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_CLASSCHOOSER_H
#define GAME_SERVER_ENTITIES_CLASSCHOOSER_H

#include <game/server/entity.h>

enum
{
	PLAYERCLASS_NONE = 0,
	
	START_HUMANCLASS,
	PLAYERCLASS_SOLDIER,
	PLAYERCLASS_ENGINEER,
	END_HUMANCLASS,
	
	PLAYERCLASS_ZOMBIE,
	PLAYERCLASS_BOOMER,
	PLAYERCLASS_HUNTER,
	PLAYERCLASS_WITCH,
	
	PLAYERCLASS_MEDIC,
	NB_PLAYERCLASS,
};

class CClassChooser : public CEntity
{
public:
	CClassChooser(CGameWorld *pGameWorld, vec2 Pos, int pId);

	virtual void Destroy();
	virtual void Snap(int SnappingClient);
	
	void SetCursor(vec2 CurPos);
	int SelectClass();

private:
	int m_StartTick;
	int m_IDClass[END_HUMANCLASS - START_HUMANCLASS - 1];
	vec2 m_CurPos;
	int m_PlayerID;
};

#endif
