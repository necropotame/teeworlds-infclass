#ifndef GAME_GENERATED_PROTOCOL_H
#define GAME_GENERATED_PROTOCOL_H


#include <engine/message.h>

enum
{
	INPUT_STATE_MASK=0x3f
};

enum
{
	TEAM_SPECTATORS=-1,
	TEAM_RED,
	TEAM_BLUE,

	FLAG_MISSING=-3,
	FLAG_ATSTAND,
	FLAG_TAKEN,

	SPEC_FREEVIEW=-1,
};

enum
{
	EMOTE_NORMAL=0,
	EMOTE_PAIN,
	EMOTE_HAPPY,
	EMOTE_SURPRISE,
	EMOTE_ANGRY,
	EMOTE_BLINK,
	NUM_EMOTES
};

enum
{
	POWERUP_HEALTH=0,
	POWERUP_ARMOR,
	POWERUP_WEAPON,
	POWERUP_NINJA,
	NUM_POWERUPS
};

enum
{
	EMOTICON_OOP=0,
	EMOTICON_EXCLAMATION,
	EMOTICON_HEARTS,
	EMOTICON_DROP,
	EMOTICON_DOTDOT,
	EMOTICON_MUSIC,
	EMOTICON_SORRY,
	EMOTICON_GHOST,
	EMOTICON_SUSHI,
	EMOTICON_SPLATTEE,
	EMOTICON_DEVILTEE,
	EMOTICON_ZOMG,
	EMOTICON_ZZZ,
	EMOTICON_WTF,
	EMOTICON_EYES,
	EMOTICON_QUESTION,
	NUM_EMOTICONS
};

enum
{
	PLAYERFLAG_PLAYING = 1<<0,
	PLAYERFLAG_IN_MENU = 1<<1,
	PLAYERFLAG_CHATTING = 1<<2,
	PLAYERFLAG_SCOREBOARD = 1<<3,
};

enum
{
	GAMEFLAG_TEAMS = 1<<0,
	GAMEFLAG_FLAGS = 1<<1,
};

enum
{
	GAMESTATEFLAG_GAMEOVER = 1<<0,
	GAMESTATEFLAG_SUDDENDEATH = 1<<1,
	GAMESTATEFLAG_PAUSED = 1<<2,
};

enum
{
	NETOBJ_INVALID=0,
	NETOBJTYPE_PLAYERINPUT,
	NETOBJTYPE_PROJECTILE,
	NETOBJTYPE_LASER,
	NETOBJTYPE_PICKUP,
	NETOBJTYPE_FLAG,
	NETOBJTYPE_GAMEINFO,
	NETOBJTYPE_GAMEDATA,
	NETOBJTYPE_CHARACTERCORE,
	NETOBJTYPE_CHARACTER,
	NETOBJTYPE_PLAYERINFO,
	NETOBJTYPE_CLIENTINFO,
	NETOBJTYPE_SPECTATORINFO,
	NETEVENTTYPE_COMMON,
	NETEVENTTYPE_EXPLOSION,
	NETEVENTTYPE_SPAWN,
	NETEVENTTYPE_HAMMERHIT,
	NETEVENTTYPE_DEATH,
	NETEVENTTYPE_SOUNDGLOBAL,
	NETEVENTTYPE_SOUNDWORLD,
	NETEVENTTYPE_DAMAGEIND,
	NUM_NETOBJTYPES
};

enum
{
	NETMSG_INVALID=0,
	NETMSGTYPE_SV_MOTD,
	NETMSGTYPE_SV_BROADCAST,
	NETMSGTYPE_SV_CHAT,
	NETMSGTYPE_SV_KILLMSG,
	NETMSGTYPE_SV_SOUNDGLOBAL,
	NETMSGTYPE_SV_TUNEPARAMS,
	NETMSGTYPE_SV_EXTRAPROJECTILE,
	NETMSGTYPE_SV_READYTOENTER,
	NETMSGTYPE_SV_WEAPONPICKUP,
	NETMSGTYPE_SV_EMOTICON,
	NETMSGTYPE_SV_VOTECLEAROPTIONS,
	NETMSGTYPE_SV_VOTEOPTIONLISTADD,
	NETMSGTYPE_SV_VOTEOPTIONADD,
	NETMSGTYPE_SV_VOTEOPTIONREMOVE,
	NETMSGTYPE_SV_VOTESET,
	NETMSGTYPE_SV_VOTESTATUS,
	NETMSGTYPE_CL_SAY,
	NETMSGTYPE_CL_SETTEAM,
	NETMSGTYPE_CL_SETSPECTATORMODE,
	NETMSGTYPE_CL_STARTINFO,
	NETMSGTYPE_CL_CHANGEINFO,
	NETMSGTYPE_CL_KILL,
	NETMSGTYPE_CL_EMOTICON,
	NETMSGTYPE_CL_VOTE,
	NETMSGTYPE_CL_CALLVOTE,
	NUM_NETMSGTYPES
};

struct CNetObj_PlayerInput
{
	int m_Direction;
	int m_TargetX;
	int m_TargetY;
	int m_Jump;
	int m_Fire;
	int m_Hook;
	int m_PlayerFlags;
	int m_WantedWeapon;
	int m_NextWeapon;
	int m_PrevWeapon;
};

struct CNetObj_Projectile
{
	int m_X;
	int m_Y;
	int m_VelX;
	int m_VelY;
	int m_Type;
	int m_StartTick;
};

struct CNetObj_Laser
{
	int m_X;
	int m_Y;
	int m_FromX;
	int m_FromY;
	int m_StartTick;
};

struct CNetObj_Pickup
{
	int m_X;
	int m_Y;
	int m_Type;
	int m_Subtype;
};

struct CNetObj_Flag
{
	int m_X;
	int m_Y;
	int m_Team;
};

struct CNetObj_GameInfo
{
	int m_GameFlags;
	int m_GameStateFlags;
	int m_RoundStartTick;
	int m_WarmupTimer;
	int m_ScoreLimit;
	int m_TimeLimit;
	int m_RoundNum;
	int m_RoundCurrent;
};

struct CNetObj_GameData
{
	int m_TeamscoreRed;
	int m_TeamscoreBlue;
	int m_FlagCarrierRed;
	int m_FlagCarrierBlue;
};

struct CNetObj_CharacterCore
{
	int m_Tick;
	int m_X;
	int m_Y;
	int m_VelX;
	int m_VelY;
	int m_Angle;
	int m_Direction;
	int m_Jumped;
	int m_HookedPlayer;
	int m_HookState;
	int m_HookTick;
	int m_HookX;
	int m_HookY;
	int m_HookDx;
	int m_HookDy;
};

struct CNetObj_Character : public CNetObj_CharacterCore
{
	int m_PlayerFlags;
	int m_Health;
	int m_Armor;
	int m_AmmoCount;
	int m_Weapon;
	int m_Emote;
	int m_AttackTick;
};

struct CNetObj_PlayerInfo
{
	int m_Local;
	int m_ClientID;
	int m_Team;
	int m_Score;
	int m_Latency;
};

struct CNetObj_ClientInfo
{
	int m_Name0;
	int m_Name1;
	int m_Name2;
	int m_Name3;
	int m_Clan0;
	int m_Clan1;
	int m_Clan2;
	int m_Country;
	int m_Skin0;
	int m_Skin1;
	int m_Skin2;
	int m_Skin3;
	int m_Skin4;
	int m_Skin5;
	int m_UseCustomColor;
	int m_ColorBody;
	int m_ColorFeet;
};

struct CNetObj_SpectatorInfo
{
	int m_SpectatorID;
	int m_X;
	int m_Y;
};

struct CNetEvent_Common
{
	int m_X;
	int m_Y;
};

struct CNetEvent_Explosion : public CNetEvent_Common
{
};

struct CNetEvent_Spawn : public CNetEvent_Common
{
};

struct CNetEvent_HammerHit : public CNetEvent_Common
{
};

struct CNetEvent_Death : public CNetEvent_Common
{
	int m_ClientID;
};

struct CNetEvent_SoundGlobal : public CNetEvent_Common
{
	int m_SoundID;
};

struct CNetEvent_SoundWorld : public CNetEvent_Common
{
	int m_SoundID;
};

struct CNetEvent_DamageInd : public CNetEvent_Common
{
	int m_Angle;
};

struct CNetMsg_Sv_Motd
{
	const char *m_pMessage;
	int MsgID() const { return NETMSGTYPE_SV_MOTD; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_pMessage, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_Broadcast
{
	const char *m_pMessage;
	int MsgID() const { return NETMSGTYPE_SV_BROADCAST; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_pMessage, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_Chat
{
	int m_Team;
	int m_ClientID;
	const char *m_pMessage;
	int MsgID() const { return NETMSGTYPE_SV_CHAT; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Team);
		pPacker->AddInt(m_ClientID);
		pPacker->AddString(m_pMessage, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_KillMsg
{
	int m_Killer;
	int m_Victim;
	int m_Weapon;
	int m_ModeSpecial;
	int MsgID() const { return NETMSGTYPE_SV_KILLMSG; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Killer);
		pPacker->AddInt(m_Victim);
		pPacker->AddInt(m_Weapon);
		pPacker->AddInt(m_ModeSpecial);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_SoundGlobal
{
	int m_SoundID;
	int MsgID() const { return NETMSGTYPE_SV_SOUNDGLOBAL; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_SoundID);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_TuneParams
{
	int MsgID() const { return NETMSGTYPE_SV_TUNEPARAMS; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_ExtraProjectile
{
	int MsgID() const { return NETMSGTYPE_SV_EXTRAPROJECTILE; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_ReadyToEnter
{
	int MsgID() const { return NETMSGTYPE_SV_READYTOENTER; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_WeaponPickup
{
	int m_Weapon;
	int MsgID() const { return NETMSGTYPE_SV_WEAPONPICKUP; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Weapon);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_Emoticon
{
	int m_ClientID;
	int m_Emoticon;
	int MsgID() const { return NETMSGTYPE_SV_EMOTICON; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_ClientID);
		pPacker->AddInt(m_Emoticon);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_VoteClearOptions
{
	int MsgID() const { return NETMSGTYPE_SV_VOTECLEAROPTIONS; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_VoteOptionListAdd
{
	int m_NumOptions;
	const char *m_pDescription0;
	const char *m_pDescription1;
	const char *m_pDescription2;
	const char *m_pDescription3;
	const char *m_pDescription4;
	const char *m_pDescription5;
	const char *m_pDescription6;
	const char *m_pDescription7;
	const char *m_pDescription8;
	const char *m_pDescription9;
	const char *m_pDescription10;
	const char *m_pDescription11;
	const char *m_pDescription12;
	const char *m_pDescription13;
	const char *m_pDescription14;
	int MsgID() const { return NETMSGTYPE_SV_VOTEOPTIONLISTADD; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_NumOptions);
		pPacker->AddString(m_pDescription0, -1);
		pPacker->AddString(m_pDescription1, -1);
		pPacker->AddString(m_pDescription2, -1);
		pPacker->AddString(m_pDescription3, -1);
		pPacker->AddString(m_pDescription4, -1);
		pPacker->AddString(m_pDescription5, -1);
		pPacker->AddString(m_pDescription6, -1);
		pPacker->AddString(m_pDescription7, -1);
		pPacker->AddString(m_pDescription8, -1);
		pPacker->AddString(m_pDescription9, -1);
		pPacker->AddString(m_pDescription10, -1);
		pPacker->AddString(m_pDescription11, -1);
		pPacker->AddString(m_pDescription12, -1);
		pPacker->AddString(m_pDescription13, -1);
		pPacker->AddString(m_pDescription14, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_VoteOptionAdd
{
	const char *m_pDescription;
	int MsgID() const { return NETMSGTYPE_SV_VOTEOPTIONADD; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_pDescription, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_VoteOptionRemove
{
	const char *m_pDescription;
	int MsgID() const { return NETMSGTYPE_SV_VOTEOPTIONREMOVE; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_pDescription, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_VoteSet
{
	int m_Timeout;
	const char *m_pDescription;
	const char *m_pReason;
	int MsgID() const { return NETMSGTYPE_SV_VOTESET; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Timeout);
		pPacker->AddString(m_pDescription, -1);
		pPacker->AddString(m_pReason, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Sv_VoteStatus
{
	int m_Yes;
	int m_No;
	int m_Pass;
	int m_Total;
	int MsgID() const { return NETMSGTYPE_SV_VOTESTATUS; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Yes);
		pPacker->AddInt(m_No);
		pPacker->AddInt(m_Pass);
		pPacker->AddInt(m_Total);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_Say
{
	int m_Team;
	const char *m_pMessage;
	int MsgID() const { return NETMSGTYPE_CL_SAY; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Team);
		pPacker->AddString(m_pMessage, -1);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_SetTeam
{
	int m_Team;
	int MsgID() const { return NETMSGTYPE_CL_SETTEAM; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Team);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_SetSpectatorMode
{
	int m_SpectatorID;
	int MsgID() const { return NETMSGTYPE_CL_SETSPECTATORMODE; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_SpectatorID);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_StartInfo
{
	const char *m_pName;
	const char *m_pClan;
	int m_Country;
	const char *m_pSkin;
	int m_UseCustomColor;
	int m_ColorBody;
	int m_ColorFeet;
	int MsgID() const { return NETMSGTYPE_CL_STARTINFO; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_pName, -1);
		pPacker->AddString(m_pClan, -1);
		pPacker->AddInt(m_Country);
		pPacker->AddString(m_pSkin, -1);
		pPacker->AddInt(m_UseCustomColor);
		pPacker->AddInt(m_ColorBody);
		pPacker->AddInt(m_ColorFeet);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_ChangeInfo
{
	const char *m_pName;
	const char *m_pClan;
	int m_Country;
	const char *m_pSkin;
	int m_UseCustomColor;
	int m_ColorBody;
	int m_ColorFeet;
	int MsgID() const { return NETMSGTYPE_CL_CHANGEINFO; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_pName, -1);
		pPacker->AddString(m_pClan, -1);
		pPacker->AddInt(m_Country);
		pPacker->AddString(m_pSkin, -1);
		pPacker->AddInt(m_UseCustomColor);
		pPacker->AddInt(m_ColorBody);
		pPacker->AddInt(m_ColorFeet);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_Kill
{
	int MsgID() const { return NETMSGTYPE_CL_KILL; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_Emoticon
{
	int m_Emoticon;
	int MsgID() const { return NETMSGTYPE_CL_EMOTICON; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Emoticon);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_Vote
{
	int m_Vote;
	int MsgID() const { return NETMSGTYPE_CL_VOTE; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddInt(m_Vote);
		return pPacker->Error() != 0;
	}
};

struct CNetMsg_Cl_CallVote
{
	const char *m_Type;
	const char *m_Value;
	const char *m_Reason;
	int MsgID() const { return NETMSGTYPE_CL_CALLVOTE; }
	
	bool Pack(CMsgPacker *pPacker)
	{
		pPacker->AddString(m_Type, -1);
		pPacker->AddString(m_Value, -1);
		pPacker->AddString(m_Reason, -1);
		return pPacker->Error() != 0;
	}
};

enum
{
	SOUND_GUN_FIRE=0,
	SOUND_SHOTGUN_FIRE,
	SOUND_GRENADE_FIRE,
	SOUND_HAMMER_FIRE,
	SOUND_HAMMER_HIT,
	SOUND_NINJA_FIRE,
	SOUND_GRENADE_EXPLODE,
	SOUND_NINJA_HIT,
	SOUND_RIFLE_FIRE,
	SOUND_RIFLE_BOUNCE,
	SOUND_WEAPON_SWITCH,
	SOUND_PLAYER_PAIN_SHORT,
	SOUND_PLAYER_PAIN_LONG,
	SOUND_BODY_LAND,
	SOUND_PLAYER_AIRJUMP,
	SOUND_PLAYER_JUMP,
	SOUND_PLAYER_DIE,
	SOUND_PLAYER_SPAWN,
	SOUND_PLAYER_SKID,
	SOUND_TEE_CRY,
	SOUND_HOOK_LOOP,
	SOUND_HOOK_ATTACH_GROUND,
	SOUND_HOOK_ATTACH_PLAYER,
	SOUND_HOOK_NOATTACH,
	SOUND_PICKUP_HEALTH,
	SOUND_PICKUP_ARMOR,
	SOUND_PICKUP_GRENADE,
	SOUND_PICKUP_SHOTGUN,
	SOUND_PICKUP_NINJA,
	SOUND_WEAPON_SPAWN,
	SOUND_WEAPON_NOAMMO,
	SOUND_HIT,
	SOUND_CHAT_SERVER,
	SOUND_CHAT_CLIENT,
	SOUND_CHAT_HIGHLIGHT,
	SOUND_CTF_DROP,
	SOUND_CTF_RETURN,
	SOUND_CTF_GRAB_PL,
	SOUND_CTF_GRAB_EN,
	SOUND_CTF_CAPTURE,
	SOUND_MENU,
	NUM_SOUNDS
};
enum
{
	WEAPON_HAMMER=0,
	WEAPON_GUN,
	WEAPON_SHOTGUN,
	WEAPON_GRENADE,
	WEAPON_RIFLE,
	WEAPON_NINJA,
	NUM_WEAPONS
};


class CNetObjHandler
{
	const char *m_pMsgFailedOn;
	const char *m_pObjCorrectedOn;
	char m_aMsgData[1024];
	int m_NumObjCorrections;
	int ClampInt(const char *pErrorMsg, int Value, int Min, int Max);

	static const char *ms_apObjNames[];
	static int ms_aObjSizes[];
	static const char *ms_apMsgNames[];

public:
	CNetObjHandler();

	int ValidateObj(int Type, void *pData, int Size);
	const char *GetObjName(int Type);
	int GetObjSize(int Type);
	int NumObjCorrections();
	const char *CorrectedObjOn();

	const char *GetMsgName(int Type);
	void *SecureUnpackMsg(int Type, CUnpacker *pUnpacker);
	const char *FailedMsgOn();
};


#endif // GAME_GENERATED_PROTOCOL_H
