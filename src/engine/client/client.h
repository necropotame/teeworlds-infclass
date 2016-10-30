/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_CLIENT_H
#define ENGINE_CLIENT_CLIENT_H

class CClient : public IClient
{
	// needed interfaces
	IEngine *m_pEngine;
	IEditor *m_pEditor;
	IEngineInput *m_pInput;
	IEngineGraphics *m_pGraphics;
	IEngineMap *m_pMap;
	IConsole *m_pConsole;
	IStorage *m_pStorage;

	enum
	{
		NUM_SNAPSHOT_TYPES=2,
		PREDICTION_MARGIN=1000/50/2, // magic network prediction value
	};

	class CMapChecker m_MapChecker;

	char m_aServerAddressStr[256];

	unsigned m_SnapshotParts;
	int64 m_LocalStartTime;

	int m_DebugFont;
	
	int64 m_LastRenderTime;
	float m_RenderFrameTimeLow;
	float m_RenderFrameTimeHigh;
	int m_RenderFrames;

	NETADDR m_ServerAddress;
	int m_WindowMustRefocus;
	int m_SnapCrcErrors;
	bool m_AutoScreenshotRecycle;
	bool m_EditorActive;
	bool m_SoundInitFailed;
	bool m_ResortServerBrowser;

	int m_AckGameTick;
	int m_CurrentRecvTick;
	int m_RconAuthed;
	int m_UseTempRconCommands;

	// version info
	struct CVersionInfo
	{
		enum
		{
			STATE_INIT=0,
			STATE_START,
			STATE_READY,
		};

		int m_State;
		class CHostLookup m_VersionServeraddr;
	} m_VersionInfo;

	volatile int m_GfxState;
	static void GraphicsThreadProxy(void *pThis) { ((CClient*)pThis)->GraphicsThread(); }
	void GraphicsThread();

public:
	IEngine *Engine() { return m_pEngine; }
	IEngineGraphics *Graphics() { return m_pGraphics; }
	IEngineInput *Input() { return m_pInput; }
	IStorage *Storage() { return m_pStorage; }

	CClient();

	virtual int GetDebugFont() { return m_DebugFont; }

	// ------ state handling -----
	void SetState(int s);

	int LoadData();

	// ---

	virtual void Quit();

	void RegisterInterfaces();
	void InitInterfaces();

	void Run();

	static bool Con_Quit(IConsole::IResult *pResult, void *pUserData);
	static bool Con_Minimize(IConsole::IResult *pResult, void *pUserData);
	static bool Con_Screenshot(IConsole::IResult *pResult, void *pUserData);
	
	void RegisterCommands();
};
#endif
