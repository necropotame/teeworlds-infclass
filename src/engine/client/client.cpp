/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>

#include <stdlib.h> // qsort
#include <stdarg.h>

#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <engine/shared/config.h>
#include <engine/shared/compression.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/mapchecker.h>
#include <engine/shared/ringbuffer.h>

#include "client.h"

#if defined(CONF_FAMILY_WINDOWS)
	#define _WIN32_WINNT 0x0501
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

#include "SDL.h"
#ifdef main
#undef main
#endif

CClient::CClient()
{
	m_pEditor = 0;
	m_pInput = 0;
	m_pGraphics = 0;
	m_pMap = 0;
	m_pConsole = 0;

	m_RenderFrameTime = 0.0001f;
	m_RenderFrameTimeLow = 1.0f;
	m_RenderFrameTimeHigh = 0.0f;
	m_RenderFrames = 0;
	m_LastRenderTime = time_get();

	m_WindowMustRefocus = 0;
	m_SnapCrcErrors = 0;
	m_AutoScreenshotRecycle = false;
	m_EditorActive = false;

	m_AckGameTick = -1;
	m_CurrentRecvTick = 0;
	m_RconAuthed = 0;

	m_State = IClient::STATE_OFFLINE;

	m_VersionInfo.m_State = CVersionInfo::STATE_INIT;
}

// ------ state handling -----
void CClient::SetState(int s)
{
	if(m_State == IClient::STATE_QUITING)
		return;

	m_State = s;
}

int CClient::LoadData()
{
	m_DebugFont = Graphics()->LoadTexture("debug_font.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, IGraphics::TEXLOAD_NORESAMPLE);
	return 1;
}

void CClient::Quit()
{
	SetState(IClient::STATE_QUITING);
}

void CClient::RegisterInterfaces()
{
	
}

void CClient::InitInterfaces()
{
	// fetch interfaces
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pEditor = Kernel()->RequestInterface<IEditor>();
	//m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pInput = Kernel()->RequestInterface<IEngineInput>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
}

void CClient::Run()
{
	m_LocalStartTime = time_get();
	m_SnapshotParts = 0;

	// init SDL
	{
		if(SDL_Init(0) < 0)
		{
			dbg_msg("client", "unable to init SDL base: %s", SDL_GetError());
			return;
		}

		atexit(SDL_Quit); // ignore_convention
	}

	// init graphics
	{
		if(g_Config.m_GfxThreaded)
			m_pGraphics = CreateEngineGraphicsThreaded();
		else
			m_pGraphics = CreateEngineGraphics();

		bool RegisterFail = false;
		RegisterFail = RegisterFail || !Kernel()->RegisterInterface(static_cast<IEngineGraphics*>(m_pGraphics)); // register graphics as both
		RegisterFail = RegisterFail || !Kernel()->RegisterInterface(static_cast<IGraphics*>(m_pGraphics));

		if(RegisterFail || m_pGraphics->Init() != 0)
		{
			dbg_msg("client", "couldn't init graphics");
			return;
		}
	}

	// init font rendering
	IEngineTextRender* pTextRender =  Kernel()->RequestInterface<IEngineTextRender>();
	pTextRender->Init();

	// init the input
	Input()->Init();

	// load default font
	static CFont *pDefaultFont = 0;
	char aFilename[512];
	IOHANDLE File = Storage()->OpenFile("fonts/DejaVuSans.ttf", IOFLAG_READ, IStorage::TYPE_ALL, aFilename, sizeof(aFilename));
	if(File)
	{
		io_close(File);
		pDefaultFont = pTextRender->LoadFont(aFilename);
		pTextRender->SetDefaultFont(pDefaultFont);
	}
	if(!pDefaultFont)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "font", "failed to load font. filename='fonts/DejaVuSans.ttf'");
	
	// init the editor
	m_pEditor->Init();


	// load data
	if(!LoadData())
		return;

	// connect to the server if wanted
	/*
	if(config.cl_connect[0] != 0)
		Connect(config.cl_connect);
	config.cl_connect[0] = 0;
	*/

	Input()->MouseModeRelative();

	// process pending commands
	m_pConsole->StoreCommands(false);

	while (1)
	{
		// update input
		if(Input()->Update())
			break;	// SDL_QUIT

		// release focus
		if(!m_pGraphics->WindowActive())
		{
			if(m_WindowMustRefocus == 0)
				Input()->MouseModeAbsolute();
			m_WindowMustRefocus = 1;
		}
		else if (g_Config.m_DbgFocus && Input()->KeyPressed(KEY_ESCAPE))
		{
			Input()->MouseModeAbsolute();
			m_WindowMustRefocus = 1;
		}

		// refocus
		if(m_WindowMustRefocus && m_pGraphics->WindowActive())
		{
			if(m_WindowMustRefocus < 3)
			{
				Input()->MouseModeAbsolute();
				m_WindowMustRefocus++;
			}

			if(m_WindowMustRefocus >= 3 || Input()->KeyPressed(KEY_MOUSE_1))
			{
				Input()->MouseModeRelative();
				m_WindowMustRefocus = 0;
			}
		}

		// panic quit button
		if(Input()->KeyPressed(KEY_LCTRL) && Input()->KeyPressed(KEY_LSHIFT) && Input()->KeyPressed('q'))
		{
			Quit();
			break;
		}

		if(Input()->KeyPressed(KEY_LCTRL) && Input()->KeyPressed(KEY_LSHIFT) && Input()->KeyDown('d'))
			g_Config.m_Debug ^= 1;

		if(Input()->KeyPressed(KEY_LCTRL) && Input()->KeyPressed(KEY_LSHIFT) && Input()->KeyDown('g'))
			g_Config.m_DbgGraphs ^= 1;

		/*
		if(!gfx_window_open())
			break;
		*/

		// render
		{
			if(!g_Config.m_GfxAsyncRender || m_pGraphics->IsIdle())
			{
				m_RenderFrames++;

				// update frametime
				int64 Now = time_get();
				m_RenderFrameTime = (Now - m_LastRenderTime) / (float)time_freq();
				if(m_RenderFrameTime < m_RenderFrameTimeLow)
					m_RenderFrameTimeLow = m_RenderFrameTime;
				if(m_RenderFrameTime > m_RenderFrameTimeHigh)
					m_RenderFrameTimeHigh = m_RenderFrameTime;

				m_LastRenderTime = Now;

				m_pEditor->UpdateAndRender();
				m_pGraphics->Swap();
			}
		}

		// check conditions
		if(State() == IClient::STATE_QUITING)
			break;

		// beNice
		if(g_Config.m_ClCpuThrottle)
			thread_sleep(g_Config.m_ClCpuThrottle);
		else if(g_Config.m_DbgStress || !m_pGraphics->WindowActive())
			thread_sleep(5);

		if(g_Config.m_DbgHitch)
		{
			thread_sleep(g_Config.m_DbgHitch);
			g_Config.m_DbgHitch = 0;
		}

		// update local time
		m_LocalTime = (time_get()-m_LocalStartTime)/(float)time_freq();
	}

	m_pGraphics->Shutdown();

	// shutdown SDL
	{
		SDL_Quit();
	}
}

bool CClient::Con_Quit(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Quit();
	
	return true;
}

bool CClient::Con_Minimize(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Graphics()->Minimize();
	
	return true;
}

bool CClient::Con_Screenshot(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Graphics()->TakeScreenshot(0);
	
	return true;
}

void CClient::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	
	// register server dummy commands for tab completion
	m_pConsole->Register("quit", "", CFGFLAG_CLIENT|CFGFLAG_STORE, Con_Quit, this, "Quit Teeworlds");
	m_pConsole->Register("exit", "", CFGFLAG_CLIENT|CFGFLAG_STORE, Con_Quit, this, "Quit Teeworlds");
	m_pConsole->Register("minimize", "", CFGFLAG_CLIENT|CFGFLAG_STORE, Con_Minimize, this, "Minimize Teeworlds");
	m_pConsole->Register("screenshot", "", CFGFLAG_CLIENT, Con_Screenshot, this, "Take a screenshot");
}

static CClient *CreateClient()
{
	CClient *pClient = static_cast<CClient *>(mem_alloc(sizeof(CClient), 1));
	mem_zero(pClient, sizeof(CClient));
	return new(pClient) CClient;
}

/*
	Server Time
	Client Mirror Time
	Client Predicted Time

	Snapshot Latency
		Downstream latency

	Prediction Latency
		Upstream latency
*/

#if defined(CONF_PLATFORM_MACOSX)
extern "C" int SDL_main(int argc, char **argv_) // ignore_convention
{
	const char **argv = const_cast<const char **>(argv_);
#else
int main(int argc, const char **argv) // ignore_convention
{
#endif
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; i++) // ignore_convention
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			FreeConsole();
			break;
		}
	}
#endif

	CClient *pClient = CreateClient();
	IKernel *pKernel = IKernel::Create();
	pKernel->RegisterInterface(pClient);
	pClient->RegisterInterfaces();

	// create the components
	IEngine *pEngine = CreateEngine("Teeworlds");
	IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT);
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_CLIENT, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();
	IEngineInput *pEngineInput = CreateEngineInput();
	IEngineTextRender *pEngineTextRender = CreateEngineTextRender();
	IEngineMap *pEngineMap = CreateEngineMap();

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineInput*>(pEngineInput)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IInput*>(pEngineInput));

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineTextRender*>(pEngineTextRender)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<ITextRender*>(pEngineTextRender));

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMap*>(pEngineMap)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap*>(pEngineMap));

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(CreateEditor());
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);

		if(RegisterFail)
			return -1;
	}

	pEngine->Init();
	pConfig->Init();

	// register all console commands
	pClient->RegisterCommands();

	// init client's interfaces
	pClient->InitInterfaces();

	// execute config file
	pConsole->ExecuteFile("settings.cfg");

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	// parse the command line arguments
	if(argc > 1) // ignore_convention
		pConsole->ParseArguments(argc-1, &argv[1]); // ignore_convention

	// restore empty config strings to their defaults
	pConfig->RestoreStrings();

	pClient->Engine()->InitLogfile();

	// run the client
	dbg_msg("client", "starting...");
	pClient->Run();

	// write down the config and quit
	pConfig->Save();

	return 0;
}
