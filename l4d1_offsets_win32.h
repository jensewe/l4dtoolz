#ifndef _INCLUDE_L4D1_OFFSETS_WIN32_
#define _INCLUDE_L4D1_OFFSETS_WIN32_

int sv_offs = 6; // IServer pointer (in IVEngineServer::CreateFakeClient)
int slots_offs = 96; // m_numGameSlots (in CGameServer::ExecGameTypeCfg)
int reserved_offs = 46; // IsReserved() (in CBaseServer::ReplyReservationRequest)
int reservation_idx = 59; // CBaseServer::ReplyReservationRequest(netadr_s&, bf_read&) vtable
int maxhuman_idx = 131; // CTerrorGameRules::GetMaxHumanPlayers vtable

#endif //_INCLUDE_L4D1_OFFSETS_WIN32_
