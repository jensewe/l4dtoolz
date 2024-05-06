#ifndef _INCLUDE_L4D1_OFFSETS_LINUX_
#define _INCLUDE_L4D1_OFFSETS_LINUX_

const char* engine_dll = "engine.so";
int slots_offs = 94; // m_numGameSlots (in CGameServer::ExecGameTypeCfg)
int reservation_idx = 60; // CBaseServer::ReplyReservationRequest(netadr_s&, bf_read&) vtable
int maxhuman_idx = 132; // CTerrorGameRules::GetMaxHumanPlayers vtable

#endif //_INCLUDE_L4D1_OFFSETS_LINUX_
