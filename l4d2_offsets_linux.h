#ifndef _INCLUDE_L4D2_OFFSETS_LINUX_
#define _INCLUDE_L4D2_OFFSETS_LINUX_

const char* engine_dll = "engine_srv.so";
int slots_offs = 95; // m_numGameSlots (in CGameServer::ExecGameTypeCfg)
int reservation_idx = 62; // CBaseServer::ReplyReservationRequest(netadr_s&, bf_read&) vtable
int maxhuman_idx = 137; // CTerrorGameRules::GetMaxHumanPlayers vtable

#endif //_INCLUDE_L4D2_OFFSETS_LINUX_
