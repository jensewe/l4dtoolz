#ifndef _INCLUDE_GAME_OFFSETS_
#define _INCLUDE_GAME_OFFSETS_

#if SH_SYS == SH_SYS_WIN32
int maxplayers_offs = 138; // m_nMaxClientsLimit (in CGameServer::SetMaxClients)
#if SOURCE_ENGINE == SE_LEFT4DEAD
#include "l4d1_offsets_win32.h"
#else
#include "l4d2_offsets_win32.h"
#endif
#else
int maxplayers_offs = 136; // m_nMaxClientsLimit (in CGameServer::SetMaxClients)
#if SOURCE_ENGINE == SE_LEFT4DEAD
#include "l4d1_offsets_linux.h"
#else
#include "l4d2_offsets_linux.h"
#endif
#endif

#endif //_INCLUDE_GAME_OFFSETS_
