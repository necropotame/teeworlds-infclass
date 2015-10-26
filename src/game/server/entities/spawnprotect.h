/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_INFO_H
#define GAME_SERVER_ENTITIES_INFO_H

class CSpawProtect : public CEntity
{
public:
	CSpawProtect(CGameWorld *pGameWorld, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	
private:
	int m_Owner;
};

#endif
