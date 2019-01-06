#include <engine/server/mapconverter.h>

CMapConverter::CMapConverter(IStorage *pStorage, IEngineMap *pMap, IConsole* pConsole) :
	m_pStorage(pStorage),
	m_pMap(pMap),
	m_pConsole(pConsole),
	m_pTiles(0)
{
	
}

CMapConverter::~CMapConverter()
{
	if(m_pTiles)
		delete[] m_pTiles;
}

bool CMapConverter::Load()
{
	m_AnimationCycle = 1;
	m_MenuPosition = vec2(0.0f, 0.0f);
	
	//Find GameLayer	
	int LayersStart;
	int LayersNum;
	int GroupsStart;
	int GroupsNum;
	Map()->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
	
	CMapItemLayerTilemap *pPhysicsLayer = 0;
				
	//Find the gamelayer
	for(int g = 0; g < GroupsNum; g++)
	{
		CMapItemGroup* pGroup = (CMapItemGroup*) Map()->GetItem(GroupsStart+g, 0, 0);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = (CMapItemLayer*) Map()->GetItem(LayersStart+pGroup->m_StartLayer+l, 0, 0);
							
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTileLayer = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);
				
				if(pTileLayer->m_Flags&TILESLAYERFLAG_PHYSICS)
				{
					pPhysicsLayer = pTileLayer;
					break;
				}
			}
		}
		if(pPhysicsLayer)
			break;
	}
	
	if(!pPhysicsLayer)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "infclass", "no physics layer in loaded map");
		return false;
	}
		
	m_Width = pPhysicsLayer->m_Width;
	m_Height = pPhysicsLayer->m_Height;
	m_pPhysicsLayerTiles = (CTile*) m_pMap->GetData(pPhysicsLayer->m_Data);
	
	if(m_pTiles)
		delete[] m_pTiles;
		
	m_pTiles = new CTile[m_Width*m_Height];
	
	for(int j=0; j<m_Height; j++)
	{
		for(int i=0; i<m_Width; i++)
		{
			m_pTiles[j*m_Width+i].m_Flags = 0;
			m_pTiles[j*m_Width+i].m_Reserved = 0;
			
			int Skip = m_pPhysicsLayerTiles[j*m_Width+i].m_Skip;
			m_pTiles[j*m_Width+i].m_Skip = Skip;
			i += Skip;
		}
	}
	
	//Get the animation cycle
	CEnvPoint* pEnvPoints = NULL;
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);
		pEnvPoints = (CEnvPoint *)Map()->GetItem(Start, 0, 0);
	}
					
	if(pEnvPoints)
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		for(int i = 0; i < Num; i++)
		{
			CMapItemEnvelope *pItem = (CMapItemEnvelope *)Map()->GetItem(Start+i, 0, 0);
			if(pItem->m_NumPoints > 0)
			{
				int Duration = pEnvPoints[pItem->m_StartPoint + pItem->m_NumPoints - 1].m_Time - pEnvPoints[pItem->m_StartPoint].m_Time;
				if(Duration)
				{
					dbg_msg("DEBUG", "Duration found: %d", m_AnimationCycle);
					m_AnimationCycle *= Duration;
					dbg_msg("DEBUG", "Duration found: %d", m_AnimationCycle);
				}
			}
		}
		
		if(m_AnimationCycle)
			m_TimeShiftUnit = m_AnimationCycle*((60*1000 / m_AnimationCycle)+1);
		else
			m_TimeShiftUnit = 60*1000;
	}
	
	return true;
}

void CMapConverter::InitQuad(CQuad* pQuad)
{
	for (int i=0; i<5; i++) {
		pQuad->m_aPoints[i].x = 0;
		pQuad->m_aPoints[i].y = 0;
	}
	pQuad->m_aColors[0].r = pQuad->m_aColors[1].r = 255;
	pQuad->m_aColors[0].g = pQuad->m_aColors[1].g = 255;
	pQuad->m_aColors[0].b = pQuad->m_aColors[1].b = 255;
	pQuad->m_aColors[0].a = pQuad->m_aColors[1].a = 255;
	pQuad->m_aColors[2].r = pQuad->m_aColors[3].r = 255;
	pQuad->m_aColors[2].g = pQuad->m_aColors[3].g = 255;
	pQuad->m_aColors[2].b = pQuad->m_aColors[3].b = 255;
	pQuad->m_aColors[2].a = pQuad->m_aColors[3].a = 255;
	pQuad->m_aTexcoords[0].x = 0;
	pQuad->m_aTexcoords[0].y = 0;
	pQuad->m_aTexcoords[1].x = 1<<10;
	pQuad->m_aTexcoords[1].y = 0;
	pQuad->m_aTexcoords[2].x = 0;
	pQuad->m_aTexcoords[2].y = 1<<10;
	pQuad->m_aTexcoords[3].x = 1<<10;
	pQuad->m_aTexcoords[3].y = 1<<10;
	pQuad->m_PosEnv = -1;
	pQuad->m_PosEnvOffset = 0;
	pQuad->m_ColorEnv = -1;
	pQuad->m_ColorEnvOffset = 0;
}

void CMapConverter::InitQuad(CQuad* pQuad, vec2 Pos, vec2 Size)
{
	int X0 = f2fx(Pos.x-Size.x/2.0f);
	int X1 = f2fx(Pos.x+Size.x/2.0f);
	int XC = f2fx(Pos.x);
	int Y0 = f2fx(Pos.y-Size.y/2.0f);
	int Y1 = f2fx(Pos.y+Size.y/2.0f);
	int YC = f2fx(Pos.y);
	
	InitQuad(pQuad);
	pQuad->m_aPoints[0].x = pQuad->m_aPoints[2].x = X0;
	pQuad->m_aPoints[1].x = pQuad->m_aPoints[3].x = X1;
	pQuad->m_aPoints[0].y = pQuad->m_aPoints[1].y = Y0;
	pQuad->m_aPoints[2].y = pQuad->m_aPoints[3].y = Y1;
	pQuad->m_aPoints[4].x = XC;
	pQuad->m_aPoints[4].y = YC;
}

void CMapConverter::CreateCircle(array<CQuad>* pQuads, vec2 CenterPos, float Size, vec4 Color, int Env, int EnvTO)
{
	CQuad Quad;
	InitQuad(&Quad);
	Quad.m_aPoints[0].x = f2fx(CenterPos.x);
	Quad.m_aPoints[0].y = f2fx(CenterPos.y);
	Quad.m_aColors[0].r = Quad.m_aColors[1].r = Quad.m_aColors[2].r = Quad.m_aColors[3].r = Color.r*255.0f;
	Quad.m_aColors[0].g = Quad.m_aColors[1].g = Quad.m_aColors[2].g = Quad.m_aColors[3].g = Color.g*255.0f;
	Quad.m_aColors[0].b = Quad.m_aColors[1].b = Quad.m_aColors[2].b = Quad.m_aColors[3].b = Color.b*255.0f;
	Quad.m_aColors[0].a = Quad.m_aColors[1].a = Quad.m_aColors[2].a = Quad.m_aColors[3].a = Color.a*255.0f;
	
	Quad.m_ColorEnv = Env;
	Quad.m_ColorEnvOffset = EnvTO;
	
	float AngleStep = 2.0f*pi/32.0f;
	float AngleIter = AngleStep;
	vec2 RadiusVect = vec2(Size/2.0f, 0.0f);
	vec2 LastPos = CenterPos+RadiusVect;
	for(int i=0; i<32; i++)
	{
		vec2 PosMid = CenterPos+rotate(RadiusVect, AngleIter - AngleStep/2.0f);
		vec2 Pos = CenterPos+rotate(RadiusVect, AngleIter);
		
		Quad.m_aPoints[1].x = f2fx(LastPos.x);
		Quad.m_aPoints[1].y = f2fx(LastPos.y);
		Quad.m_aPoints[2].x = f2fx(Pos.x);
		Quad.m_aPoints[2].y = f2fx(Pos.y);
		Quad.m_aPoints[3].x = f2fx(PosMid.x);
		Quad.m_aPoints[3].y = f2fx(PosMid.y);
		Quad.m_aPoints[4].x = f2fx((PosMid.x+CenterPos.x)/2.0f);
		Quad.m_aPoints[4].y = f2fx((PosMid.y+CenterPos.y)/2.0f);
		
		pQuads->add(Quad);
		
		LastPos = Pos;
		AngleIter += AngleStep;
	}
}

void CMapConverter::AddImageQuad(const char* pName, int ImageID, int GridX, int GridY, int X, int Y, int Width, int Height, vec2 Pos, vec2 Size, vec4 Color, int Env)
{
	array<CQuad> aQuads;
	CQuad Quad;
	
	float StepX = 1.0f/GridX;
	float StepY = 1.0f/GridY;
	
	InitQuad(&Quad, Pos, Size);
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = f2fx(StepX * X);
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = f2fx(StepX * (X + Width));
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = f2fx(StepY * Y);
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = f2fx(StepY * (Y + Height));
	Quad.m_aColors[0].r = Quad.m_aColors[1].r = Quad.m_aColors[2].r = Quad.m_aColors[3].r = Color.r*255.0f;
	Quad.m_aColors[0].g = Quad.m_aColors[1].g = Quad.m_aColors[2].g = Quad.m_aColors[3].g = Color.g*255.0f;
	Quad.m_aColors[0].b = Quad.m_aColors[1].b = Quad.m_aColors[2].b = Quad.m_aColors[3].b = Color.b*255.0f;
	Quad.m_aColors[0].a = Quad.m_aColors[1].a = Quad.m_aColors[2].a = Quad.m_aColors[3].a = Color.a*255.0f;
	aQuads.add(Quad);
	
	CMapItemLayerQuads Item;
	Item.m_Version = 2;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_QUADS;
	Item.m_Image = ImageID;
	Item.m_NumQuads = aQuads.size();
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), pName);
	Item.m_Data = m_DataFile.AddDataSwapped(aQuads.size()*sizeof(CQuad), aQuads.base_ptr());
	
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapConverter::AddTeeLayer(const char* pName, int ImageID, vec2 Pos, float Size, int Env, bool Black)
{
	array<CQuad> aQuads;
	CQuad Quad;
	
	//Body, Shadow
	InitQuad(&Quad, Pos+vec2(0.0f, -4.0f), vec2(Size, Size));
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = f2fx(96.0f/256.0f);
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = f2fx(192.0f/256.0f);
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 0;
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = f2fx(96.0f/128.0f);
	aQuads.add(Quad);
	
	//BackFeet, Shadow
	InitQuad(&Quad, Pos+vec2(-7.0f, 10.0f), vec2(Size, Size/2.0f));
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*64.0f/128.0f;
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*96.0f/128.0f;
	aQuads.add(Quad);
	
	//FrontFeet, Shadow
	InitQuad(&Quad, Pos+vec2(7.0f, 10.0f), vec2(Size, Size/2.0f));
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*64.0f/128.0f;
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*96.0f/128.0f;
	aQuads.add(Quad);
	
	//BackFeet, Color
	if(!Black)
	{
		InitQuad(&Quad, Pos+vec2(-7.0f, 10.0f), vec2(Size, Size/2.0f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*32.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*64.0f/128.0f;
		aQuads.add(Quad);
		
		//Body, Color
		InitQuad(&Quad, Pos+vec2(0.0f, -4.0f), vec2(Size, Size));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 0;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f*96.0f/256.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 0;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*96.0f/128.0f;
		aQuads.add(Quad);
		
		//FrontFeet, Color
		InitQuad(&Quad, Pos+vec2(7.0f, 10.0f), vec2(Size, Size/2.0f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*32.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*64.0f/128.0f;
		aQuads.add(Quad);
		
		//Eyes
		vec2 Direction = normalize(vec2(1.0f, -0.5f));
		float EyeSeparation = (0.075f - 0.010f*absolute(Direction.x))*Size;
		vec2 Offset = vec2(Direction.x*0.125f, -0.05f+Direction.y*0.10f)*Size;
		
			//Left
		InitQuad(&Quad, Pos+vec2(Offset.x-EyeSeparation, Offset.y-4.0f), vec2(Size*0.40f, Size*0.40f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*64.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f*96.0f/256.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*96.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*128.0f/128.0f;
		aQuads.add(Quad);
		
			//Right
		InitQuad(&Quad, Pos+vec2(Offset.x+EyeSeparation, Offset.y-4.0f), vec2(-Size*0.40f, Size*0.40f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*64.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f*96.0f/256.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*96.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*128.0f/128.0f;
		aQuads.add(Quad);
	}
	
	CMapItemLayerQuads Item;
	Item.m_Version = Item.m_Layer.m_Version = 2;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_QUADS;
	Item.m_Image = ImageID;
	Item.m_NumQuads = aQuads.size();
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), pName);
	Item.m_Data = m_DataFile.AddDataSwapped(aQuads.size()*sizeof(CQuad), aQuads.base_ptr());
	
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapConverter::InitState()
{
	m_NumGroups = 0;
	m_NumLayers = 0;
	m_NumImages = 0;
	m_NumEnvs = 0;
	m_lEnvPoints.clear();
}

void CMapConverter::CopyVersion()
{
	CMapItemVersion *pItem = (CMapItemVersion *)Map()->FindItem(MAPITEMTYPE_VERSION, 0);
	m_DataFile.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(CMapItemVersion), pItem);
}

void CMapConverter::CopyMapInfo()
{
	CMapItemInfo *pItem = (CMapItemInfo *)Map()->FindItem(MAPITEMTYPE_INFO, 0);
	m_DataFile.AddItem(MAPITEMTYPE_INFO, 0, sizeof(CMapItemInfo), pItem);
}

void CMapConverter::CopyImages()
{
	int Start, Num;
	Map()->GetType(MAPITEMTYPE_IMAGE, &Start, &Num);
	for(int i = 0; i < Num; i++)
	{
		CMapItemImage *pItem = (CMapItemImage *)Map()->GetItem(Start+i, 0, 0);
		char *pName = (char *)Map()->GetData(pItem->m_ImageName);

		CMapItemImage ImageItem;
		ImageItem.m_Version = 1;
		ImageItem.m_Width = pItem->m_Width;
		ImageItem.m_Height = pItem->m_Height;
		ImageItem.m_External = pItem->m_External;
		ImageItem.m_ImageName = m_DataFile.AddData(str_length(pName)+1, pName);
		if(pItem->m_External)
			ImageItem.m_ImageData = -1;
		else
		{
			char *pData = (char *)Map()->GetData(pItem->m_ImageData);
			ImageItem.m_ImageData = m_DataFile.AddData(ImageItem.m_Width*ImageItem.m_Height*4, pData);
		}
		m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(ImageItem), &ImageItem);

		// unload image
		Map()->UnloadData(pItem->m_ImageData);
		Map()->UnloadData(pItem->m_ImageName);
	}
}

void CMapConverter::CopyAnimations()
{
	int NumEnvPoints = 0;
	CEnvPoint* pEnvPoints = NULL;
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);
		pEnvPoints = (CEnvPoint *)Map()->GetItem(Start, 0, 0);
	}
	
	if(pEnvPoints)
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		for(int i = 0; i < Num; i++)
		{
			CMapItemEnvelope *pItem = (CMapItemEnvelope *)Map()->GetItem(Start+i, 0, 0);
			CMapItemEnvelope Item = *pItem;
			m_DataFile.AddItem(MAPITEMTYPE_ENVELOPE, m_NumEnvs++, sizeof(Item), &Item);
			
			if(pItem->m_NumPoints > 0)
				NumEnvPoints = max(NumEnvPoints, pItem->m_StartPoint + pItem->m_NumPoints);
		}
		
		for(int i=0; i<NumEnvPoints; i++)
		{
			m_lEnvPoints.add(pEnvPoints[i]);
		}
	}
}

void CMapConverter::CopyGameLayer()
{
	CMapItemLayerTilemap Item;
	Item.m_Version = Item.m_Layer.m_Version = 3;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_TILES;
	Item.m_Color.r = 255;
	Item.m_Color.g = 255;
	Item.m_Color.b = 255;
	Item.m_Color.a = 255;
	Item.m_ColorEnv = -1;
	Item.m_ColorEnvOffset = 0;
	Item.m_Width = m_Width;
	Item.m_Height = m_Height;
	Item.m_Flags = 1;
	Item.m_Image = -1;
	
	//Cleanup the game layer
	//This will make maps no more usable by a server, but the original ones are in the repository
	for(int j=0; j<m_Height; j++)
	{
		for(int i=0; i<m_Width; i++)
		{
			switch(m_pPhysicsLayerTiles[j*m_Width+i].m_Index)
			{
				case TILE_PHYSICS_SOLID:
					m_pTiles[j*m_Width+i].m_Index = TILE_PHYSICS_SOLID;
					break;
				case TILE_PHYSICS_NOHOOK:
					m_pTiles[j*m_Width+i].m_Index = TILE_PHYSICS_NOHOOK;
					break;
				default:
					m_pTiles[j*m_Width+i].m_Index = TILE_PHYSICS_AIR;
			}
			i += m_pPhysicsLayerTiles[j*m_Width+i].m_Skip;
		}
	}
	
	Item.m_Data = m_DataFile.AddData(m_Width*m_Height*sizeof(CTile), m_pTiles);
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Game");
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapConverter::CopyLayers()
{
	int LayersStart, LayersNum;
	Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	int Start, Num;
	Map()->GetType(MAPITEMTYPE_GROUP, &Start, &Num);
	
	//Groups
	for(int g = 0; g < Num; g++)
	{
		CMapItemGroup *pGItem = (CMapItemGroup *)Map()->GetItem(Start+g, 0, 0);

		if(pGItem->m_Version != CMapItemGroup::CURRENT_VERSION)
			continue;

		CMapItemGroup GroupItem = *pGItem;
		GroupItem.m_StartLayer = m_NumLayers;
		
		int GroupLayers = 0;

		//Layers
		for(int l = 0; l < pGItem->m_NumLayers; l++)
		{
			CMapItemLayer *pLayerItem = (CMapItemLayer *)Map()->GetItem(LayersStart+pGItem->m_StartLayer+l, 0, 0);
			if(!pLayerItem)
				continue;

			if(pLayerItem->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTilemapItem = (CMapItemLayerTilemap *)pLayerItem;

				if(pTilemapItem->m_Flags&TILESLAYERFLAG_PHYSICS)
				{
					CopyGameLayer();
					GroupLayers++;
				}
				else if(pTilemapItem->m_Flags&TILESLAYERFLAG_ZONE)
				{
					
				}
				else if(pTilemapItem->m_Flags&TILESLAYERFLAG_ENTITY)
				{
					
				}
				else
				{
					void *pData = Map()->GetData(pTilemapItem->m_Data);

					CMapItemLayerTilemap LayerItem;
					LayerItem = *pTilemapItem;
					LayerItem.m_Data = m_DataFile.AddData(LayerItem.m_Width*LayerItem.m_Height*sizeof(CTile), pData);
					
					m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(LayerItem), &LayerItem);

					Map()->UnloadData(pTilemapItem->m_Data);
					
					GroupLayers++;
				}
			}
			else if(pLayerItem->m_Type == LAYERTYPE_QUADS)
			{
				CMapItemLayerQuads *pQuadsItem = (CMapItemLayerQuads *)pLayerItem;
				
				void *pData = Map()->GetData(pQuadsItem->m_Data);
				
				CMapItemLayerQuads LayerItem;
				LayerItem = *pQuadsItem;
				LayerItem.m_Data = m_DataFile.AddData(LayerItem.m_NumQuads*sizeof(CQuad), pData);
				
				m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(LayerItem), &LayerItem);

				Map()->UnloadData(pQuadsItem->m_Data);
				
				GroupLayers++;
			}
		}
		
		GroupItem.m_NumLayers = GroupLayers;
		
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(GroupItem), &GroupItem);
	}
}

int CMapConverter::AddExternalImage(const char* pImageName, int Width, int Height)
{
	CMapItemImage Item;
	Item.m_Version = 1;
	Item.m_External = 1;
	Item.m_ImageData = -1;
	Item.m_ImageName = m_DataFile.AddData(str_length((char*)pImageName)+1, (char*)pImageName);
	Item.m_Width = Width;
	Item.m_Height = Height;
	m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(Item), &Item);
	
	return m_NumImages-1;
}

void CMapConverter::Finalize()
{
	int EngineerImageID = AddExternalImage("../skins/limekitty", 256, 128);
	int SoldierImageID = AddExternalImage("../skins/brownbear", 256, 128);
	int ScientistImageID = AddExternalImage("../skins/toptri", 256, 128);
	int BiologistImageID = AddExternalImage("../skins/twintri", 256, 128);
	int LooperImageID = AddExternalImage("../skins/bluekitty", 256, 128);
	int MedicImageID = AddExternalImage("../skins/twinbop", 256, 128);
	int HeroImageID = AddExternalImage("../skins/redstripe", 256, 128);
	int NinjaImageID = AddExternalImage("../skins/x_ninja", 256, 128);
	int MercenaryImageID = AddExternalImage("../skins/bluestripe", 256, 128);
	int SniperImageID = AddExternalImage("../skins/warpaint", 256, 128);
	
	//Menu
	
	const float MenuRadius = 196.0f;
	const float MenuAngleStart = -pi/2.0f;
	
	{
		const float MenuAngleStep = 2.0f*pi/static_cast<float>(NUM_MENUCLASS);
		
			//Menu Group
		{
			CMapItemGroup Item;
			Item.m_Version = CMapItemGroup::CURRENT_VERSION;
			Item.m_ParallaxX = 0;
			Item.m_ParallaxY = 0;
			Item.m_OffsetX = 0;
			Item.m_OffsetY = 0;
			Item.m_StartLayer = m_NumLayers;
			Item.m_NumLayers = NUM_MENUCLASS+1; // not sure why +1 tho
			Item.m_UseClipping = 0;
			Item.m_ClipX = 0;
			Item.m_ClipY = 0;
			Item.m_ClipW = 0;
			Item.m_ClipH = 0;
			StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "#Generated");
			
			m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
		}
			//Menu Layers
		{
			array<CQuad> aQuads;
			
			int HiddenValues[4];
			int NormalValues[4];
			int HighlightValues[4];
			
			HiddenValues[0] = 0;
			HiddenValues[1] = 0;
			HiddenValues[2] = 0;
			HiddenValues[3] = 0;
				
			//Two passes: one for circles, one for tees
			for(int pass=0; pass<2; pass++)
			{
				if(pass == 0)
				{
					NormalValues[0] = 0;
					NormalValues[1] = 0;
					NormalValues[2] = 0;
					NormalValues[3] = 500;
					
					HighlightValues[0] = 1000;
					HighlightValues[1] = 1000;
					HighlightValues[2] = 1000;
					HighlightValues[3] = 500;
				}
				else
				{
					NormalValues[0] = 1000;
					NormalValues[1] = 1000;
					NormalValues[2] = 1000;
					NormalValues[3] = 1000;
					
					HighlightValues[0] = 1000;
					HighlightValues[1] = 1000;
					HighlightValues[2] = 1000;
					HighlightValues[3] = 1000;
				}
				
				for(int i=0; i<NUM_MENUCLASS; i++) 
				{
					int ClassMask = 0;
					
					switch(i)
					{
						case MENUCLASS_ENGINEER:
						case MENUCLASS_SOLDIER:
						case MENUCLASS_SCIENTIST:
						case MENUCLASS_BIOLOGIST:
							ClassMask = MASK_DEFENDER;
							break;
						case MENUCLASS_LOOPER:
							ClassMask = MASK_DEFENDER;
							break;
						case MENUCLASS_MEDIC:
							ClassMask = MASK_MEDIC;
							break;
						case MENUCLASS_HERO:
							ClassMask = MASK_HERO;
							break;
						default:
							ClassMask = MASK_SUPPORT;
					}
					
					//Create Animation for enable/disable simulation
					{
						int StartPoint = m_lEnvPoints.size();
						int NbPoints = 0;
						
						{
							CEnvPoint Point;
							Point.m_Time = 0;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = HiddenValues[0];
							Point.m_aValues[1] = HiddenValues[1];
							Point.m_aValues[2] = HiddenValues[2];
							Point.m_aValues[3] = HiddenValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}	
						{
							CEnvPoint Point;
							Point.m_Time = TIMESHIFT_MENUCLASS*m_TimeShiftUnit;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = NormalValues[0];
							Point.m_aValues[1] = NormalValues[1];
							Point.m_aValues[2] = NormalValues[2];
							Point.m_aValues[3] = NormalValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}
						
						//Iterate over all combinaisons of class availabilities
						for(int j=0; j<=MASK_ALL; j++)
						{
							if(pass == 0 && ((ClassMask & j) || (ClassMask == -1))) //Highlight
							{
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*((i+1)+j*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = HighlightValues[0];
									Point.m_aValues[1] = HighlightValues[1];
									Point.m_aValues[2] = HighlightValues[2];
									Point.m_aValues[3] = HighlightValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*((i+2)+j*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = NormalValues[0];
									Point.m_aValues[1] = NormalValues[1];
									Point.m_aValues[2] = NormalValues[2];
									Point.m_aValues[3] = NormalValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
							}
							else if((ClassMask != -1) && ((ClassMask & j) == 0)) //Hide
							{
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*(j*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = HiddenValues[0];
									Point.m_aValues[1] = HiddenValues[1];
									Point.m_aValues[2] = HiddenValues[2];
									Point.m_aValues[3] = HiddenValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*((j+1)*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = NormalValues[0];
									Point.m_aValues[1] = NormalValues[1];
									Point.m_aValues[2] = NormalValues[2];
									Point.m_aValues[3] = NormalValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
							}
						}
						{
							CEnvPoint Point;
							Point.m_Time = (TIMESHIFT_MENUCLASS+3*((MASK_ALL+1)*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = NormalValues[0];
							Point.m_aValues[1] = NormalValues[1];
							Point.m_aValues[2] = NormalValues[2];
							Point.m_aValues[3] = NormalValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}
						{
							CEnvPoint Point;
							Point.m_Time = (1+TIMESHIFT_MENUCLASS+3*((MASK_ALL+1)*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = HiddenValues[0];
							Point.m_aValues[1] = HiddenValues[1];
							Point.m_aValues[2] = HiddenValues[2];
							Point.m_aValues[3] = HiddenValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}
						
						CMapItemEnvelope Item;
						Item.m_Version = CMapItemEnvelope::CURRENT_VERSION;
						Item.m_Channels = 4;
						Item.m_StartPoint = StartPoint;
						Item.m_NumPoints = NbPoints;
						Item.m_Synchronized = 1;
						StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Menu Class Pages");
						m_DataFile.AddItem(MAPITEMTYPE_ENVELOPE, m_NumEnvs++, sizeof(Item), &Item);
					}
				
					//Create Circle
					if(pass == 0)
					{
						CreateCircle(&aQuads, m_MenuPosition+rotate(vec2(MenuRadius, 0.0f), MenuAngleStart+MenuAngleStep*i), 96.0f, vec4(1.0f, 1.0f, 1.0f, 0.5f), m_NumEnvs-1);
					}
					else
					{
						vec2 Pos = m_MenuPosition+rotate(vec2(MenuRadius, 0.0f), MenuAngleStart+MenuAngleStep*i);
						switch(i)
						{
							case MENUCLASS_RANDOM:
								AddTeeLayer("Random", SniperImageID, Pos, 64.0f, m_NumEnvs-1, true);
								break;
							case MENUCLASS_ENGINEER:
								AddTeeLayer("Engineer", EngineerImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_SOLDIER:
								AddTeeLayer("Soldier", SoldierImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_SCIENTIST:
								AddTeeLayer("Scientist", ScientistImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_BIOLOGIST:
								AddTeeLayer("Biologist", BiologistImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_LOOPER:
								AddTeeLayer("Looper", LooperImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_MEDIC:
								AddTeeLayer("Medic", MedicImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_HERO:
								AddTeeLayer("Hero", HeroImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_NINJA:
								AddTeeLayer("Ninja", NinjaImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_MERCENARY:
								AddTeeLayer("Mercenary", MercenaryImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
							case MENUCLASS_SNIPER:
								AddTeeLayer("Sniper", SniperImageID, Pos, 64.0f, m_NumEnvs-1);
								break;
						}
					}
				}
			
				if(pass == 0)
				{
					CMapItemLayerQuads Item;
					Item.m_Version = 2;
					Item.m_Layer.m_Flags = 0;
					Item.m_Layer.m_Type = LAYERTYPE_QUADS;
					Item.m_Image = -1;
					Item.m_NumQuads = aQuads.size();
					StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "UIQuads");
					Item.m_Data = m_DataFile.AddDataSwapped(aQuads.size()*sizeof(CQuad), aQuads.base_ptr());
					
					m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
				}
			}
		}
	}
}

bool CMapConverter::CreateMap(const char* pFilename)
{
	char aBuf[512];
	if(!m_DataFile.Open(Storage(), pFilename))
	{
		str_format(aBuf, sizeof(aBuf), "failed to open file '%s'...", pFilename);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "infclass", aBuf);
		return false;
	}
	
	InitState();
	
	CopyVersion();
	CopyMapInfo();
	CopyImages();
	CopyAnimations();
	
	//Game Group
	{
		CMapItemGroup Item;
		Item.m_Version = CMapItemGroup::CURRENT_VERSION;
		Item.m_ParallaxX = 100;
		Item.m_ParallaxY = 100;
		Item.m_OffsetX = 0;
		Item.m_OffsetY = 0;
		Item.m_StartLayer = m_NumLayers;
		Item.m_NumLayers = 1;
		Item.m_UseClipping = 0;
		Item.m_ClipX = 0;
		Item.m_ClipY = 0;
		Item.m_ClipW = 0;
		Item.m_ClipH = 0;
		StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Game");
		
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
	}
	
	CopyLayers();
	
	Finalize();
	
	m_DataFile.AddItem(MAPITEMTYPE_ENVPOINTS, 0, m_lEnvPoints.size()*sizeof(CEnvPoint), m_lEnvPoints.base_ptr());
	m_DataFile.Finish();
	
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "infclass", "highres map created");
	return true;
}
