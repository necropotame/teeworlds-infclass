#ifndef ENGINE_SERVER_MAPCONVERTER_H
#define ENGINE_SERVER_MAPCONVERTER_H

#include <base/tl/array.h>
#include <engine/storage.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/shared/datafile.h>
#include <game/mapitems.h>
#include <game/gamecore.h>

class CMapConverter
{
public:
	enum
	{
		MENUENTRY_MEDIC=0,
		MENUENTRY_NINJA,
		MENUENTRY_MERCENARY,
		MENUENTRY_SNIPER,
		MENUENTRY_RANDOM,
		MENUENTRY_ENGINEER,
		MENUENTRY_SOLDIER,
		MENUENTRY_SCIENTIST,
		NUM_MENUENTRIES,
	};
	
	enum
	{
		TIMESHIFT_GAME = 60,
		TIMESHIFT_AVAILABILITY = NUM_MENUENTRIES+1,
	};
	
	enum
	{
		MASK_DEFENDER = 2,
		MASK_MEDIC = 1,
		MASK_SUPPORT = 4,
		MASK_ALL = MASK_DEFENDER | MASK_MEDIC | MASK_SUPPORT,
	};

protected:
	IStorage *m_pStorage;
	IEngineMap *m_pMap;
	IConsole *m_pConsole;
	CDataFileWriter m_DataFile;
	
	CTile* m_pTiles;
	CTile* m_pGameLayerTiles;
	int m_Width;
	int m_Height;
	
	int m_NumGroups;
	int m_NumLayers;
	int m_NumImages;
	int m_NumEnvs;
	array<CEnvPoint> m_lEnvPoints;
	
	vec2 m_MenuPosition;

protected:	
	IEngineMap* Map() { return m_pMap; };
	IStorage* Storage() { return m_pStorage; };
	IConsole* Console() { return m_pConsole; };
	
	void InitQuad(CQuad* pQuad);
	void InitQuad(CQuad* pQuad, vec2 Pos, vec2 Size);
	
	void CreateCircle(array<CQuad>* pQuads, vec2 Pos, float Size, vec4 Color, int Env=-1, int EnvTO=0);
	
	void InitState();
	
	void CopyVersion();
	void CopyMapInfo();
	void CopyImages();
	void CopyGameLayer();
	void CopyLayers();
	
	void AddTeeLayer(const char* pName, int ImageID, vec2 Pos, float Size, int Env=-1, bool Black=false);
	int AddExternalImage(const char* pImageName, int Width, int Height);
	
	void Finalize();

public:
	CMapConverter(IStorage *pStorage, IEngineMap *pMap, IConsole* pConsole);
	~CMapConverter();
	
	bool Load();
	bool CreateLowResMap();
	bool CreateMap();
};

#endif
