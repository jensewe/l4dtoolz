#include "l4dtoolz_mm.h"
#include "game_offsets.h"
#include "memutils.h"
#include "icommandline.h"
#include "server_class.h"
#include "sourcehook.h"
#include "matchmaking/imatchframework.h"

l4dtoolz g_l4dtoolz;
IServerGameDLL* gamedll = NULL;
IServerGameClients* gameclients = NULL;
IVEngineServer* engine = NULL;
IMatchFramework* g_pMatchFramework = NULL;
ICvar* g_pCVar = NULL;
IServer* g_pGameIServer = NULL;
void* g_pGameRules = nullptr;
int m_numGameSlots = -1;

SH_DECL_HOOK1_void(IServerGameDLL, ApplyGameSettings, SH_NOATTRIB, 0, KeyValues*);
SH_DECL_HOOK0(IMatchTitle, GetTotalNumPlayersSupported, SH_NOATTRIB, 0, int);
SH_DECL_HOOK6(IServerGameDLL, LevelInit, SH_NOATTRIB, 0, bool, char const *, char const *, char const *, char const *, bool, bool);
SH_DECL_HOOK0_void(IServerGameDLL, LevelShutdown, SH_NOATTRIB, 0);
SH_DECL_MANUALHOOK0(CTerrorGameRules_GetMaxHumanPlayers, maxhuman_idx, 0, 0, int);
SH_DECL_MANUALHOOK2_void(CBaseServer_ReplyReservationRequest, reservation_idx, 0, 0, netadr_s&, CBitRead&);

ConVar sv_maxplayers("sv_maxplayers", "-1", 0, "Max Human Players", true, -1, true, 32, l4dtoolz::OnChangeMaxplayers);
ConVar sv_force_unreserved("sv_force_unreserved", "0", 0, "Disallow lobby reservation cookie", true, 0, true, 1, l4dtoolz::OnChangeUnreserved);

void l4dtoolz::OnChangeMaxplayers ( IConVar *var, const char *pOldValue, float flOldValue )
{
	int new_value = ((ConVar*)var)->GetInt();
	int old_value = atoi(pOldValue);
	if (g_pGameIServer == NULL) {
		Msg("g_pGameIServer pointer is not available\n");
		return;
	}
	if(new_value != old_value) {
		if(new_value >= 0) {
			m_numGameSlots = new_value;
			*(int*)(((uint**)g_pGameIServer)+slots_offs) = m_numGameSlots;
		} else {
			m_numGameSlots = -1;
		}
	}
}

void l4dtoolz::OnChangeUnreserved ( IConVar *var, const char *pOldValue, float flOldValue )
{
	int new_value = ((ConVar*)var)->GetInt();
	int old_value = atoi(pOldValue);
	if (g_pGameIServer == NULL) {
		Msg("g_pGameIServer pointer is not available\n");
		return;
	}
	if(new_value != old_value) {
		if(new_value == 1) {
			engine->ServerCommand("sv_allow_lobby_connect_only 0\n");
		}
	}
}

void Hook_ApplyGameSettings(KeyValues *pKV)
{
	if (!pKV) {
		return;
	}
	m_numGameSlots = sv_maxplayers.GetInt();
	if (m_numGameSlots == -1) {
		return;
	}
	pKV->SetInt("members/numSlots", m_numGameSlots);
}

void Hook_ReplyReservationRequest(netadr_s& adr, CBitRead& inmsg)
{
	if (sv_force_unreserved.GetInt()) {
		RETURN_META(MRES_SUPERCEDE);
	}
	RETURN_META(MRES_IGNORED);
}

int Hook_GetMaxHumanPlayers()
{
	if (m_numGameSlots > 0) {
		RETURN_META_VALUE(MRES_SUPERCEDE, m_numGameSlots);
	}
	RETURN_META_VALUE(MRES_IGNORED, m_numGameSlots);
}

PLUGIN_EXPOSE(l4dtoolz, g_l4dtoolz);

bool l4dtoolz::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetServerFactory, gamedll, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_CURRENT(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pMatchFramework, IMatchFramework, IMATCHFRAMEWORK_VERSION_STRING);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);

	void* handle = NULL;
#ifdef PLATFORM_WINDOWS
	if (!(handle=SH_GET_ORIG_VFNPTR_ENTRY(engine, &IVEngineServer::CreateFakeClient))) {
		Warning("Failed to get address 'IVEngineServer::CreateFakeClient'\n");
	} else {
		g_pGameIServer = *reinterpret_cast<IServer **>(reinterpret_cast<unsigned char *>(handle)+sv_offs);
#else
	if (!(handle=dlopen(engine_dll, RTLD_LAZY))) {
		Warning("Could't open library '%s'\n", engine_dll);
	} else {
		g_pGameIServer = (IServer *)g_MemUtils.ResolveSymbol(handle, "sv");
		dlclose(handle);
#endif
		int* m_nMaxClientsLimit = (int*)(((uint**)g_pGameIServer)+maxplayers_offs);
		if (*m_nMaxClientsLimit != 0x12) {
			Warning("Couldn't patch maxplayers\n");
			g_pGameIServer = NULL;
		} else {
			*m_nMaxClientsLimit = 0x20;
			const char *pszCmdLineMax;
			if(CommandLine()->CheckParm("-maxplayers", &pszCmdLineMax) || CommandLine()->CheckParm("+maxplayers", &pszCmdLineMax)) {
				char command[32];
				UTIL_Format(command, sizeof(command), "maxplayers %d\n", clamp(atoi(pszCmdLineMax), 1, 32));
				engine->ServerCommand(command);
			} else {
				engine->ServerCommand("maxplayers 31\n");
			}
			engine->ServerExecute();
		}
	}

	SH_ADD_HOOK(IServerGameDLL, ApplyGameSettings, gamedll, SH_STATIC(Hook_ApplyGameSettings), true);
	SH_ADD_HOOK(IMatchTitle, GetTotalNumPlayersSupported, g_pMatchFramework->GetMatchTitle(), SH_STATIC(Hook_GetMaxHumanPlayers), false);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, LevelInit, gamedll, this, &l4dtoolz::LevelInit, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, LevelShutdown, gamedll, this, &l4dtoolz::LevelShutdown, false);

	if (g_pGameIServer) {
		SH_ADD_MANUALHOOK(CBaseServer_ReplyReservationRequest, g_pGameIServer, SH_STATIC(Hook_ReplyReservationRequest), false);
	} else {
		Warning("g_pGameIServer pointer is not available\n");
	}

	ConVar_Register(0, this);

	return true;
}

bool l4dtoolz::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, ApplyGameSettings, gamedll, SH_STATIC(Hook_ApplyGameSettings), true);
	SH_REMOVE_HOOK(IMatchTitle, GetTotalNumPlayersSupported, g_pMatchFramework->GetMatchTitle(), SH_STATIC(Hook_GetMaxHumanPlayers), false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, LevelInit, gamedll, this, &l4dtoolz::LevelInit, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, LevelShutdown, gamedll, this, &l4dtoolz::LevelShutdown, false);

	if (g_pGameIServer) {
		SH_REMOVE_MANUALHOOK(CBaseServer_ReplyReservationRequest, g_pGameIServer, SH_STATIC(Hook_ReplyReservationRequest), false);
	}

	LevelShutdown();
	ConVar_Unregister();

	return true;
}

bool l4dtoolz::LevelInit(const char *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame, bool background)
{
	g_pGameRules = nullptr;

	ServerClass *pServerClass = UTIL_FindServerClass("CTerrorGameRulesProxy");
	if (pServerClass) {
		int i, iCount;
		iCount = pServerClass->m_pTable->GetNumProps();
		for (i = 0; i < iCount; i++) {
			if (stricmp(pServerClass->m_pTable->GetProp(i)->GetName(), "terror_gamerules_data") == 0) {
				g_pGameRules = (*pServerClass->m_pTable->GetProp(i)->GetDataTableProxyFn())(NULL, NULL, NULL, NULL, 0);
				break;
			}
		}	
	}

	if (g_pGameRules) {
		SH_ADD_MANUALHOOK(CTerrorGameRules_GetMaxHumanPlayers, g_pGameRules, SH_STATIC(Hook_GetMaxHumanPlayers), false);
	} else {
		Warning("g_pGameRules pointer is not available\n");
	}

	return true;
}

void l4dtoolz::LevelShutdown()
{
	if (g_pGameRules) {
		SH_REMOVE_MANUALHOOK(CTerrorGameRules_GetMaxHumanPlayers, g_pGameRules, SH_STATIC(Hook_GetMaxHumanPlayers), false);
		g_pGameRules = nullptr;
	}
}

size_t UTIL_Format(char *buffer, size_t maxlength, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	size_t len = vsnprintf(buffer, maxlength, fmt, ap);
	va_end(ap);

	if (len >= maxlength)
	{
		len = maxlength - 1;
		buffer[len] = '\0';
	}

	return len;
}

ServerClass *UTIL_FindServerClass(const char *classname)
{
	ServerClass *sc = gamedll->GetAllServerClasses();
	while (sc)
	{
		if (strcmp(classname, sc->GetName()) == 0)
		{
			return sc;
		}
		sc = sc->m_pNext;
	}

	return NULL;
}

const char *l4dtoolz::GetLicense()
{
	return "GPLv3";
}

const char *l4dtoolz::GetVersion()
{
	return "2.0.0";
}

const char *l4dtoolz::GetDate()
{
	return __DATE__;
}

const char *l4dtoolz::GetLogTag()
{
	return "L4DToolZ";
}

const char *l4dtoolz::GetAuthor()
{
	return "Accelerator, Ivailosp";
}

const char *l4dtoolz::GetDescription()
{
	return "Unlock the max player limit on L4D and L4D2";
}

const char *l4dtoolz::GetName()
{
	return "L4DToolZ";
}

const char *l4dtoolz::GetURL()
{
	return "https://github.com/Accelerator74/l4dtoolz";
}

bool l4dtoolz::RegisterConCommandBase(ConCommandBase *pVar)
{
	return META_REGCVAR(pVar);
}
