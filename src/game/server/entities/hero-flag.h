/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_HEROFLAG_H
#define GAME_SERVER_ENTITIES_HEROFLAG_H

#include <game/server/entity.h>

class CHeroFlag : public CEntity
{
private:
	bool m_Hidden;
	int m_CoolDownTick;
	
public:
	static const int ms_PhysSize = 14;

	CHeroFlag(CGameWorld *pGameWorld);

	inline int GetCoolDown() { return m_CoolDownTick; }

	virtual void Hide();
	virtual void Show();
	virtual void Tick();
	virtual void FindPosition();
	virtual void Snap(int SnappingClient);
	void GiveGift(CCharacter* pHero);
};

#endif
