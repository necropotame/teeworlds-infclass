/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <game/server/entity.h>
#include <game/generated/server_data.h>
#include <game/generated/protocol.h>

#include <game/gamecore.h>

/* INFECTION MODIFICATION START ***************************************/
#include <game/server/entities/classchooser.h>
#include <game/server/entities/bomb.h>
#include <game/server/entities/barrier.h>
#include <game/server/entities/portal.h>
/* INFECTION MODIFICATION END *****************************************/

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

enum
{
	FREEZEREASON_FLASH = 0,
	FREEZEREASON_UNDEAD = 1
};

class CCharacter : public CEntity
{
	MACRO_ALLOC_POOL_ID()

public:
	//character's size
	static const int ms_PhysSize = 28;

	CCharacter(CGameWorld *pWorld);

	virtual void Reset();
	virtual void Destroy();
	virtual void Tick();
	virtual void TickDefered();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

	bool IsGrounded();

	void SetWeapon(int W);
	void HandleWeaponSwitch();
	void DoWeaponSwitch();

	void HandleWeapons();
	void HandleNinja();

	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void ResetInput();
	void FireWeapon();

	void Die(int Killer, int Weapon);
	bool TakeDamage(vec2 Force, int Dmg, int From, int Weapon);

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);
	bool Remove();

	bool IncreaseHealth(int Amount);
	bool IncreaseArmor(int Amount);

	bool GiveWeapon(int Weapon, int Ammo);

	void SetEmote(int Emote, int Tick);

	bool IsAlive() const { return m_Alive; }
	class CPlayer *GetPlayer() { return m_pPlayer; }

private:
	// player controlling this character
	class CPlayer *m_pPlayer;

	bool m_Alive;

	// weapon info
	CEntity *m_apHitObjects[10];
	int m_NumObjectsHit;

	struct WeaponStat
	{
		int m_AmmoRegenStart;
		int m_Ammo;
		int m_Ammocost;
		bool m_Got;

	} m_aWeapons[NUM_WEAPONS];

	int m_ActiveWeapon;
	int m_LastWeapon;
	int m_QueuedWeapon;

	int m_ReloadTimer;
	int m_AttackTick;

	int m_DamageTaken;

	int m_EmoteType;
	int m_EmoteStop;

	// last tick that the player took any action ie some input
	int m_LastAction;
	int m_LastNoAmmoSound;

	// these are non-heldback inputs
	CNetObj_PlayerInput m_LatestPrevInput;
	CNetObj_PlayerInput m_LatestInput;

	// input
	CNetObj_PlayerInput m_PrevInput;
	CNetObj_PlayerInput m_Input;
	int m_NumInputs;
	int m_Jumped;

	int m_DamageTakenTick;

	int m_Health;
	int m_Armor;

	// ninja
	struct
	{
		vec2 m_ActivationDir;
		int m_ActivationTick;
		int m_CurrentMoveTime;
		int m_OldVelAmount;
		int m_NbStrike;
	} m_Ninja;

	// the player core for the physics
/* INFECTION MODIFICATION START ***************************************/
public:
	CCharacterCore m_Core;
	
private:
/* INFECTION MODIFICATION START ***************************************/

	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core

/* INFECTION MODIFICATION START ***************************************/
private:
	int m_AirJumpCounter;
	bool m_FirstShot;
	vec2 m_FirstShotCoord;
	int m_PoisonTick;
	
	int m_FlagID;
	int m_AntiFireTick;
	
	bool m_IsFrozen;
	int m_FrozenTime;
	int m_FreezeReason;

public:
	CClassChooser* m_pClassChooser;
	CBarrier* m_pBarrier;
	CBomb* m_pBomb;
	CPortal* m_pPortal[2];
	int m_PortalTick;

public:
	void DestroyChildEntities();
	void ClassSpawnAttributes();
	void OpenClassChooser();
	int GetClass();
	void SetClass(int ClassChoosed);
	bool IsInfected() const;
	void Infection(bool v);
	void RemoveAllGun();
	void Freeze(float Time, int Reason);
	bool IsFrozen() const;
/* INFECTION MODIFICATION END *****************************************/
};

#endif
