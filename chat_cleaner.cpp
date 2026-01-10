#include <stdio.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "chat_cleaner.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include <cstring>
#include "engine/igameeventsystem.h"
#include "usermessages.pb.h"
#include "filesystem.h"
#include "utlbuffer.h"

chat_cleaner g_chat_cleaner;
PLUGIN_EXPOSE(chat_cleaner, g_chat_cleaner);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;
IGameEventManager2* g_pGameEventManager = nullptr;
IGameEventSystem* g_pGameEventSystem = nullptr;

IUtilsApi* g_pUtils;

static const char* kConfigPath = "addons/configs/chat_cleaner/settings.ini";
static const char* kBlockedRadioPath = "addons/configs/chat_cleaner/blocked_radio.txt";
static const char* kBlockedTextPath = "addons/configs/chat_cleaner/blocked_text.txt";
static const char* kBlockedEventsPath = "addons/configs/chat_cleaner/blocked_events.txt";

static bool g_bDebugMode = false;

std::unordered_set<std::string> g_BlockedRadioMessages;
std::unordered_set<std::string> g_BlockedTextMessages;
std::unordered_set<std::string> g_BlockedEvents;

static std::string Trim(const std::string& text)
{
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return "";
	const auto last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

static void LoadList(const char* path, std::unordered_set<std::string>& out, const char* label)
{
	out.clear();
	if (!g_pFullFileSystem)
	{
		ConMsg("[chat_cleaner] No filesystem, cannot load %s\n", path);
		return;
	}
	CUtlBuffer buf;
	if (!g_pFullFileSystem->ReadFile(path, "GAME", buf))
	{
		ConMsg("[chat_cleaner] Failed to open %s, %s list empty\n", path, label);
		return;
	}
	std::string content;
	content.assign(static_cast<const char*>(buf.Base()), buf.TellPut());
	std::istringstream iss(content);
	std::string line;
	while (std::getline(iss, line))
	{
		line = Trim(line);

		if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
			line = line.substr(3);
		if (line.empty())
			continue;

		if (line.rfind("#", 0) == 0 || line.rfind("//", 0) == 0)
			continue;
		out.insert(line);
	}
	ConMsg("[chat_cleaner] Loaded %zu entries from %s\n", out.size(), path);
}

static void LoadConfig()
{
	KeyValues* hKv = new KeyValues("GameManager");

	if (!g_pFullFileSystem || !hKv->LoadFromFile(g_pFullFileSystem, kConfigPath))
	{
		ConMsg("[chat_cleaner] Failed to load %s, using defaults\n", kConfigPath);
		hKv->deleteThis();
		return;
	}

	g_bDebugMode = hKv->GetInt("DebugMode", 0) != 0;
	ConMsg("[chat_cleaner] Config loaded: DebugMode=%d\n", g_bDebugMode ? 1 : 0);
	hKv->deleteThis();
}

static void LoadBlockedRadio()
{
	LoadList(kBlockedRadioPath, g_BlockedRadioMessages, "BlockedRadio");
}

static void LoadBlockedText()
{
	LoadList(kBlockedTextPath, g_BlockedTextMessages, "BlockedText");
}

static void LoadBlockedEvents()
{
	LoadList(kBlockedEventsPath, g_BlockedEvents, "BlockedEvents");
}

static void ReloadAllConfigs()
{
	LoadConfig();
	LoadBlockedRadio();
	LoadBlockedText();
	LoadBlockedEvents();
	ConMsg("[chat_cleaner] All configs reloaded!\n");
}

bool IsBlockedRadioMessage(const char* szMsg)
{
	if (!szMsg) return false;
	
	for (const auto& blocked : g_BlockedRadioMessages)
	{
		if (strstr(szMsg, blocked.c_str()))
			return true;
	}
	return false;
}

bool IsBlockedTextMessage(const char* szMsg)
{
	if (!szMsg) return false;
	
	for (const auto& blocked : g_BlockedTextMessages)
	{
		if (strstr(szMsg, blocked.c_str()))
			return true;
	}
	return false;
}

bool IsBlockedEvent(const char* szName)
{
	if (!szName) return false;
	return g_BlockedEvents.find(szName) != g_BlockedEvents.end();
}

SH_DECL_HOOK2(IGameEventManager2, FireEvent, SH_NOATTRIB, 0, bool, IGameEvent*, bool);
SH_DECL_HOOK8_void(IGameEventSystem, PostEventAbstract, SH_NOATTRIB, 0, CSplitScreenSlot, bool, int, const uint64*, INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t);

void PostEventAbstract_Hook(CSplitScreenSlot nSlot, bool bLocalOnly, int nClientCount, const uint64* clients,
	INetworkMessageInternal* pEvent, const CNetMessage* pData, unsigned long nSize, NetChannelBufType_t bufType)
{
	if (pEvent && pData)
	{
		const char* pszName = pEvent->GetUnscopedName();
		
		if (g_bDebugMode && pszName)
		{
			const google::protobuf::Message* pDbgMsg = pData->AsMessage();
			if (pDbgMsg)
			{
				std::string dbgStr = pDbgMsg->DebugString();
				ConMsg("[DEBUG][%s] %s\n", pszName, dbgStr.c_str());
			}
		}

		if (pszName && strstr(pszName, "RadioText"))
		{
			const google::protobuf::Message* pMsg = pData->AsMessage();
			if (pMsg)
			{
				std::string debugStr = pMsg->DebugString();
				
				if (IsBlockedRadioMessage(debugStr.c_str()))
				{
					RETURN_META(MRES_SUPERCEDE);
				}
			}
		}

		if (pszName && strstr(pszName, "TextMsg"))
		{
			const google::protobuf::Message* pMsg = pData->AsMessage();
			if (pMsg)
			{
				std::string debugStr = pMsg->DebugString();
				
				if (IsBlockedTextMessage(debugStr.c_str()))
				{
					RETURN_META(MRES_SUPERCEDE);
				}
			}
		}
	}
	RETURN_META(MRES_IGNORED);
}

bool FireEvent_Hook(IGameEvent* pEvent, bool bDontBroadcast)
{
	if (pEvent)
	{
		const char* szName = pEvent->GetName();

		if (g_bDebugMode)
		{
			ConMsg("[DEBUG][Event] %s\n", szName);
		}

		if (IsBlockedEvent(szName))
		{
			RETURN_META_VALUE(MRES_SUPERCEDE, false);
		}
	}
	RETURN_META_VALUE(MRES_IGNORED, true);
}

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool chat_cleaner::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	LoadConfig();
	LoadBlockedRadio();
	LoadBlockedText();
	LoadBlockedEvents();

	SH_ADD_HOOK(IGameEventSystem, PostEventAbstract, g_pGameEventSystem, SH_STATIC(PostEventAbstract_Hook), false);

	return true;
}

bool chat_cleaner::Unload(char *error, size_t maxlen)
{
	if (g_pGameEventManager)
	{
		SH_REMOVE_HOOK(IGameEventManager2, FireEvent, g_pGameEventManager, SH_STATIC(FireEvent_Hook), false);
	}
	
	if (g_pGameEventSystem)
	{
		SH_REMOVE_HOOK(IGameEventSystem, PostEventAbstract, g_pGameEventSystem, SH_STATIC(PostEventAbstract_Hook), false);
	}
	
	ConVar_Unregister();
	
	return true;
}

void chat_cleaner::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pUtils->StartupServer(g_PLID, StartupServer);
	
	g_pGameEventManager = g_pUtils->GetGameEventManager();
	if (g_pGameEventManager)
	{
		SH_ADD_HOOK(IGameEventManager2, FireEvent, g_pGameEventManager, SH_STATIC(FireEvent_Hook), false);
	}
}

///////////////////////////////////////
const char* chat_cleaner::GetLicense()
{
	return "GPL";
}

const char* chat_cleaner::GetVersion()
{
	return "1.0";
}

const char* chat_cleaner::GetDate()
{
	return __DATE__;
}

const char *chat_cleaner::GetLogTag()
{
	return "[chat_cleaner]";
}

const char* chat_cleaner::GetAuthor()
{
	return "ABKAM";
}

const char* chat_cleaner::GetDescription()
{
	return "Chat Cleaner";
}

const char* chat_cleaner::GetName()
{
	return "Chat Cleaner";
}

const char* chat_cleaner::GetURL()
{
	return "https://discord.gg/ChYfTtrtmS";
}
