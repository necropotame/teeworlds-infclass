#ifndef SERVER_CONTENT_HEADER
#define SERVER_CONTENT_HEADER
struct CDataSound
{
	int m_Id;
	const char* m_pFilename;
};
struct CDataSoundset
{
	const char* m_pName;
	int m_NumSounds;
	CDataSound *m_aSounds;
	int m_Last;
};
struct CDataImage
{
	const char* m_pName;
	const char* m_pFilename;
	int m_Id;
};
struct CDataSpriteset
{
	CDataImage* m_pImage;
	int m_Gridx;
	int m_Gridy;
};
struct CDataSprite
{
	const char* m_pName;
	CDataSpriteset* m_pSet;
	int m_X;
	int m_Y;
	int m_W;
	int m_H;
};
struct CDataPickupspec
{
	const char* m_pName;
	int m_Respawntime;
	int m_Spawndelay;
};
struct CAnimKeyframe
{
	float m_Time;
	float m_X;
	float m_Y;
	float m_Angle;
};
struct CAnimSequence
{
	int m_NumFrames;
	CAnimKeyframe *m_aFrames;
};
struct CAnimation
{
	const char* m_pName;
	CAnimSequence m_Body;
	CAnimSequence m_BackFoot;
	CAnimSequence m_FrontFoot;
	CAnimSequence m_Attach;
};
struct CDataWeaponspec
{
	const char* m_pName;
	CDataSprite* m_pSpriteBody;
	CDataSprite* m_pSpriteCursor;
	CDataSprite* m_pSpriteProj;
	int m_NumSpriteMuzzles;
	CDataSprite* *m_aSpriteMuzzles;
	int m_VisualSize;
	int m_Firedelay;
	int m_Maxammo;
	int m_Ammoregentime;
	int m_Damage;
	float m_Offsetx;
	float m_Offsety;
	float m_Muzzleoffsetx;
	float m_Muzzleoffsety;
	float m_Muzzleduration;
};
struct CDataWeaponspecHammer
{
	CDataWeaponspec* m_pBase;
};
struct CDataWeaponspecGun
{
	CDataWeaponspec* m_pBase;
	float m_Curvature;
	float m_Speed;
	float m_Lifetime;
};
struct CDataWeaponspecShotgun
{
	CDataWeaponspec* m_pBase;
	float m_Curvature;
	float m_Speed;
	float m_Speeddiff;
	float m_Lifetime;
};
struct CDataWeaponspecGrenade
{
	CDataWeaponspec* m_pBase;
	float m_Curvature;
	float m_Speed;
	float m_Lifetime;
};
struct CDataWeaponspecRifle
{
	CDataWeaponspec* m_pBase;
	float m_Reach;
	int m_BounceDelay;
	int m_BounceNum;
	float m_BounceCost;
};
struct CDataWeaponspecNinja
{
	CDataWeaponspec* m_pBase;
	int m_Duration;
	int m_Movetime;
	int m_Velocity;
};
struct CDataWeaponspecs
{
	CDataWeaponspecHammer m_Hammer;
	CDataWeaponspecGun m_Gun;
	CDataWeaponspecShotgun m_Shotgun;
	CDataWeaponspecGrenade m_Grenade;
	CDataWeaponspecRifle m_Rifle;
	CDataWeaponspecNinja m_Ninja;
	int m_NumId;
	CDataWeaponspec *m_aId;
};
struct CDataContainer
{
	int m_NumSounds;
	CDataSoundset *m_aSounds;
	int m_NumImages;
	CDataImage *m_aImages;
	int m_NumPickups;
	CDataPickupspec *m_aPickups;
	int m_NumSpritesets;
	CDataSpriteset *m_aSpritesets;
	int m_NumSprites;
	CDataSprite *m_aSprites;
	int m_NumAnimations;
	CAnimation *m_aAnimations;
	CDataWeaponspecs m_Weapons;
};
extern CDataContainer *g_pData;
enum
{
	IMAGE_NULL=0,
	IMAGE_GAME,
	IMAGE_PARTICLES,
	IMAGE_CURSOR,
	IMAGE_BANNER,
	IMAGE_EMOTICONS,
	IMAGE_BROWSEICONS,
	IMAGE_CONSOLE_BG,
	IMAGE_CONSOLE_BAR,
	IMAGE_DEMOBUTTONS,
	IMAGE_FILEICONS,
	IMAGE_GUIBUTTONS,
	IMAGE_GUIICONS,
	NUM_IMAGES
};
enum
{
	ANIM_BASE=0,
	ANIM_IDLE,
	ANIM_INAIR,
	ANIM_WALK,
	ANIM_HAMMER_SWING,
	ANIM_NINJA_SWING,
	NUM_ANIMS
};
enum
{
	SPRITE_PART_SLICE=0,
	SPRITE_PART_BALL,
	SPRITE_PART_SPLAT01,
	SPRITE_PART_SPLAT02,
	SPRITE_PART_SPLAT03,
	SPRITE_PART_SMOKE,
	SPRITE_PART_SHELL,
	SPRITE_PART_EXPL01,
	SPRITE_PART_AIRJUMP,
	SPRITE_PART_HIT01,
	SPRITE_HEALTH_FULL,
	SPRITE_HEALTH_EMPTY,
	SPRITE_ARMOR_FULL,
	SPRITE_ARMOR_EMPTY,
	SPRITE_STAR1,
	SPRITE_STAR2,
	SPRITE_STAR3,
	SPRITE_PART1,
	SPRITE_PART2,
	SPRITE_PART3,
	SPRITE_PART4,
	SPRITE_PART5,
	SPRITE_PART6,
	SPRITE_PART7,
	SPRITE_PART8,
	SPRITE_PART9,
	SPRITE_WEAPON_GUN_BODY,
	SPRITE_WEAPON_GUN_CURSOR,
	SPRITE_WEAPON_GUN_PROJ,
	SPRITE_WEAPON_GUN_MUZZLE1,
	SPRITE_WEAPON_GUN_MUZZLE2,
	SPRITE_WEAPON_GUN_MUZZLE3,
	SPRITE_WEAPON_SHOTGUN_BODY,
	SPRITE_WEAPON_SHOTGUN_CURSOR,
	SPRITE_WEAPON_SHOTGUN_PROJ,
	SPRITE_WEAPON_SHOTGUN_MUZZLE1,
	SPRITE_WEAPON_SHOTGUN_MUZZLE2,
	SPRITE_WEAPON_SHOTGUN_MUZZLE3,
	SPRITE_WEAPON_GRENADE_BODY,
	SPRITE_WEAPON_GRENADE_CURSOR,
	SPRITE_WEAPON_GRENADE_PROJ,
	SPRITE_WEAPON_HAMMER_BODY,
	SPRITE_WEAPON_HAMMER_CURSOR,
	SPRITE_WEAPON_HAMMER_PROJ,
	SPRITE_WEAPON_NINJA_BODY,
	SPRITE_WEAPON_NINJA_CURSOR,
	SPRITE_WEAPON_NINJA_PROJ,
	SPRITE_WEAPON_RIFLE_BODY,
	SPRITE_WEAPON_RIFLE_CURSOR,
	SPRITE_WEAPON_RIFLE_PROJ,
	SPRITE_HOOK_CHAIN,
	SPRITE_HOOK_HEAD,
	SPRITE_WEAPON_NINJA_MUZZLE1,
	SPRITE_WEAPON_NINJA_MUZZLE2,
	SPRITE_WEAPON_NINJA_MUZZLE3,
	SPRITE_PICKUP_HEALTH,
	SPRITE_PICKUP_ARMOR,
	SPRITE_PICKUP_WEAPON,
	SPRITE_PICKUP_NINJA,
	SPRITE_FLAG_BLUE,
	SPRITE_FLAG_RED,
	SPRITE_TEE_BODY,
	SPRITE_TEE_BODY_OUTLINE,
	SPRITE_TEE_FOOT,
	SPRITE_TEE_FOOT_OUTLINE,
	SPRITE_TEE_HAND,
	SPRITE_TEE_HAND_OUTLINE,
	SPRITE_TEE_EYE_NORMAL,
	SPRITE_TEE_EYE_ANGRY,
	SPRITE_TEE_EYE_PAIN,
	SPRITE_TEE_EYE_HAPPY,
	SPRITE_TEE_EYE_DEAD,
	SPRITE_TEE_EYE_SURPRISE,
	SPRITE_OOP,
	SPRITE_EXCLAMATION,
	SPRITE_HEARTS,
	SPRITE_DROP,
	SPRITE_DOTDOT,
	SPRITE_MUSIC,
	SPRITE_SORRY,
	SPRITE_GHOST,
	SPRITE_SUSHI,
	SPRITE_SPLATTEE,
	SPRITE_DEVILTEE,
	SPRITE_ZOMG,
	SPRITE_ZZZ,
	SPRITE_WTF,
	SPRITE_EYES,
	SPRITE_QUESTION,
	SPRITE_BROWSE_LOCK,
	SPRITE_BROWSE_HEART,
	SPRITE_BROWSE_UNPURE,
	SPRITE_DEMOBUTTON_PLAY,
	SPRITE_DEMOBUTTON_PAUSE,
	SPRITE_DEMOBUTTON_STOP,
	SPRITE_DEMOBUTTON_SLOWER,
	SPRITE_DEMOBUTTON_FASTER,
	SPRITE_FILE_DEMO1,
	SPRITE_FILE_DEMO2,
	SPRITE_FILE_FOLDER,
	SPRITE_FILE_MAP1,
	SPRITE_FILE_MAP2,
	SPRITE_GUIBUTTON_OFF,
	SPRITE_GUIBUTTON_ON,
	SPRITE_GUIBUTTON_HOVER,
	SPRITE_GUIICON_MUTE,
	SPRITE_GUIICON_FRIEND,
	NUM_SPRITES
};
#endif
