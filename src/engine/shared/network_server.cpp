/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/external/md5/md5.h>

#include "netban.h"
#include "network.h"
#include "protocol.h"


bool CNetServer::Open(NETADDR BindAddr, CNetBan *pNetBan, int MaxClients, int MaxClientsPerIP, int Flags)
{
	// zero out the whole structure
	mem_zero(this, sizeof(*this));

	// open socket
	m_Socket = net_udp_create(BindAddr);
	if(!m_Socket.type)
		return false;

	m_pNetBan = pNetBan;

	// clamp clients
	m_MaxClients = MaxClients;
	if(m_MaxClients > NET_MAX_CLIENTS)
		m_MaxClients = NET_MAX_CLIENTS;
	if(m_MaxClients < 1)
		m_MaxClients = 1;

	m_MaxClientsPerIP = MaxClientsPerIP;

	secure_random_fill(m_SecurityTokenSeed, sizeof(m_SecurityTokenSeed));
	
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		m_aSlots[i].m_Connection.Init(m_Socket, true);

	return true;
}

int CNetServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_UserPtr = pUser;
	return 0;
}

int CNetServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_CLIENTREJOIN pfnClientRejoin, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnClientRejoin = pfnClientRejoin;
	m_pfnDelClient = pfnDelClient;
	m_UserPtr = pUser;
	return 0;
}

int CNetServer::Close()
{
	// TODO: implement me
	return 0;
}

int CNetServer::Drop(int ClientID, int Type, const char *pReason)
{
	// TODO: insert lots of checks here
	/*NETADDR Addr = ClientAddr(ClientID);

	dbg_msg("net_server", "client dropped. cid=%d ip=%d.%d.%d.%d reason=\"%s\"",
		ClientID,
		Addr.ip[0], Addr.ip[1], Addr.ip[2], Addr.ip[3],
		pReason
		);*/
	if(m_pfnDelClient)
		m_pfnDelClient(ClientID, Type, pReason, m_UserPtr);

	m_aSlots[ClientID].m_Connection.Disconnect(pReason);

	return 0;
}

int CNetServer::Update()
{
	int64 Now = time_get();
	for(int i = 0; i < MaxClients(); i++)
	{
		m_aSlots[i].m_Connection.Update();
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR)
		{
			if(Now - m_aSlots[i].m_Connection.ConnectTime() < time_freq() && NetBan())
				NetBan()->BanAddr(ClientAddr(i), 60, "Stressing network");
			else
				Drop(i, CLIENTDROPTYPE_STRESSING, m_aSlots[i].m_Connection.ErrorString());
		}
	}

	return 0;
}

SECURITY_TOKEN CNetServer::GetToken(const NETADDR &Addr)
{
	md5_state_t md5;
	md5_byte_t digest[16];
	SECURITY_TOKEN SecurityToken;
	md5_init(&md5);

	md5_append(&md5, (unsigned char*)m_SecurityTokenSeed, sizeof(m_SecurityTokenSeed));
	md5_append(&md5, (unsigned char*)&Addr, sizeof(Addr));

	md5_finish(&md5, digest);
	SecurityToken = ToSecurityToken(digest);

	if (SecurityToken == NET_SECURITY_TOKEN_UNKNOWN ||
		SecurityToken == NET_SECURITY_TOKEN_UNSUPPORTED)
			SecurityToken = 1;

	return SecurityToken;
}

void CNetServer::SendControl(NETADDR &Addr, int ControlMsg, const void *pExtra, int ExtraSize, SECURITY_TOKEN SecurityToken)
{
	CNetBase::SendControlMsg(m_Socket, &Addr, 0, ControlMsg, pExtra, ExtraSize, SecurityToken);
}

int CNetServer::NumClientsWithAddr(NETADDR Addr)
{
	NETADDR ThisAddr = Addr, OtherAddr;

	int FoundAddr = 0;
	ThisAddr.port = 0;

	for(int i = 0; i < MaxClients(); ++i)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE ||
			(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR &&
				(!m_aSlots[i].m_Connection.m_TimeoutProtected ||
				 !m_aSlots[i].m_Connection.m_TimeoutSituation)))
			continue;

		OtherAddr = *m_aSlots[i].m_Connection.PeerAddress();
		OtherAddr.port = 0;
		if(!net_addr_comp(&ThisAddr, &OtherAddr))
			FoundAddr++;
	}

	return FoundAddr;
}

bool CNetServer::Connlimit(NETADDR Addr)
{
	int64 Now = time_get();
	int Oldest = 0;

	for(int i = 0; i < NET_CONNLIMIT_IPS; ++i)
	{
		// If that IP address already tried to connect.
		if(!net_addr_comp(&m_aSpamConns[i].m_Addr, &Addr))
		{
			if(m_aSpamConns[i].m_Time > Now - time_freq() * g_Config.m_SvConnlimitTime)
			{
				if(m_aSpamConns[i].m_Conns >= g_Config.m_SvConnlimit)
					return true; // Too many connections from this IP address
			}
			else
			{
				m_aSpamConns[i].m_Time = Now;
				m_aSpamConns[i].m_Conns = 0;
			}
			m_aSpamConns[i].m_Conns++; // Increase its number of attempts.
			return false;
		}

		if(m_aSpamConns[i].m_Time < m_aSpamConns[Oldest].m_Time)
			Oldest = i;
	}

	m_aSpamConns[Oldest].m_Addr = Addr;
	m_aSpamConns[Oldest].m_Time = Now;
	m_aSpamConns[Oldest].m_Conns = 1;
	return false;
}
bool CNetServer::DistConnlimit()
{
	int64 Now = time_get();
	int Oldest = 0;

	int NbConns = 0;

	for(int i = 0; i < NET_CONNLIMIT_DDOS; ++i)
	{
		if(m_aDistSpamConns[i] > Now - time_freq() * g_Config.m_SvDistConnlimitTime)
		{
			NbConns++;
		}

		if(m_aDistSpamConns[i] < m_aDistSpamConns[Oldest])
			Oldest = i;
	}

	m_aDistSpamConns[Oldest] = Now;
	return NbConns >= g_Config.m_SvDistConnlimit;
}

int CNetServer::TryAcceptClient(NETADDR &Addr, SECURITY_TOKEN SecurityToken)
{
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), true);
	if (Connlimit(Addr))
	{
		const char Msg[] = "Too many connections in a short time";
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, Msg, sizeof(Msg), SecurityToken);
		dbg_msg("server", "Refusing connection from %s (too many from this client)", aAddrStr);
		return -1; // failed to add client
	}

	// check for sv_max_clients_per_ip
	if (NumClientsWithAddr(Addr) + 1 > m_MaxClientsPerIP)
	{
		dbg_msg("server", "Refusing connection from %s (too many from this address)", aAddrStr);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIP);
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, SecurityToken);
		return -1; // failed to add client
	}

	// If we're getting lots of connection attempts from many (probably spoofed) IP
	// addressed trying to fill all the slots
	if (DistConnlimit()) {
		// If SecurityToken the client does not support tokens
		// (ie. vanilla teeworlds)
		if (SecurityToken == NET_SECURITY_TOKEN_UNSUPPORTED) {
			dbg_msg("server", "Refusing connection from %s (does not support security token)", aAddrStr);
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "This server is currently under attack, and is restricted to clients that support anti-spoof protection (DDNet-like)");
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, SecurityToken);
			return -1; // failed to add client
		}
		// If the SecurityToken is invalid
		else if (SecurityToken != GetToken(Addr)) {
			dbg_msg("server", "Refusing connection from %s (bad security token)", aAddrStr);
			return -1; // failed to add client
		}
	}

	int Slot = -1;
	for(int i = 0; i < MaxClients(); i++)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
		{
			Slot = i;
			break;
		}
	}

	if (Slot == -1)
	{
		const char FullMsg[] = "This server is full";
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, FullMsg, sizeof(FullMsg), SecurityToken);

		return -1; // failed to add client
	}

	// init connection slot
	m_aSlots[Slot].m_Connection.DirectInit(Addr, SecurityToken);

	m_pfnNewClient(Slot, m_UserPtr);

	return Slot; // done
}

// connection-less msg packet without token-support
void CNetServer::OnPreConnMsg(NETADDR &Addr, CNetPacketConstruct &Packet)
{
	bool IsCtrl = Packet.m_Flags&NET_PACKETFLAG_CONTROL;
	int CtrlMsg = m_RecvUnpacker.m_Data.m_aChunkData[0];

	if (IsCtrl && CtrlMsg == NET_CTRLMSG_CONNECT)
	{
		// accept client directy
		SendControl(Addr, NET_CTRLMSG_CONNECTACCEPT, 0, 0, NET_SECURITY_TOKEN_UNSUPPORTED);

		TryAcceptClient(Addr, NET_SECURITY_TOKEN_UNSUPPORTED);
	}
}

void CNetServer::OnConnCtrlMsg(NETADDR &Addr, int ClientID, int ControlMsg, const CNetPacketConstruct &Packet)
{
	if (ControlMsg == NET_CTRLMSG_CONNECT)
	{
		// got connection attempt inside of valid session
		// the client probably wants to reconnect
		bool SupportsToken = Packet.m_DataSize >=
								(int)(1 + sizeof(SECURITY_TOKEN_MAGIC) + sizeof(SECURITY_TOKEN)) &&
								!mem_comp(&Packet.m_aChunkData[1], SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC));

		if (SupportsToken)
		{
			// response connection request with token
			SECURITY_TOKEN Token = GetToken(Addr);
			SendControl(Addr, NET_CTRLMSG_CONNECTACCEPT, SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC), Token);
		}

		if (g_Config.m_Debug)
			dbg_msg("security", "client %d wants to reconnect", ClientID);
	}
	else if (ControlMsg == NET_CTRLMSG_ACCEPT && Packet.m_DataSize == 1 + sizeof(SECURITY_TOKEN))
	{
		SECURITY_TOKEN Token = ToSecurityToken(&Packet.m_aChunkData[1]);
		if (Token == GetToken(Addr))
		{
			// correct token
			// try to accept client
			if (g_Config.m_Debug)
				dbg_msg("security", "client %d reconnect");

			// reset netconn and process rejoin
			m_aSlots[ClientID].m_Connection.Reset(true);
			m_pfnClientRejoin(ClientID, m_UserPtr);
		}
	}
}

void CNetServer::OnTokenCtrlMsg(NETADDR &Addr, int ControlMsg, const CNetPacketConstruct &Packet)
{
	if (ClientExists(Addr))
		return; // silently ignore


	if (ControlMsg == NET_CTRLMSG_CONNECT)
	{
		bool SupportsToken = Packet.m_DataSize >=
								(int)(1 + sizeof(SECURITY_TOKEN_MAGIC) + sizeof(SECURITY_TOKEN)) &&
								!mem_comp(&Packet.m_aChunkData[1], SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC));

		if (SupportsToken)
		{
			// response connection request with token
			SECURITY_TOKEN Token = GetToken(Addr);
			SendControl(Addr, NET_CTRLMSG_CONNECTACCEPT, SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC), Token);
		}
	}
	else if (ControlMsg == NET_CTRLMSG_ACCEPT && Packet.m_DataSize == 1 + sizeof(SECURITY_TOKEN))
	{
		SECURITY_TOKEN Token = ToSecurityToken(&Packet.m_aChunkData[1]);
		if (Token == GetToken(Addr))
		{
			// correct token
			// try to accept client
			if (g_Config.m_Debug)
				dbg_msg("security", "new client (ddnet token)");
			TryAcceptClient(Addr, Token);
		}
		else
		{
			// invalid token
			if (g_Config.m_Debug)
				dbg_msg("security", "invalid token");
		}
	}
}

int CNetServer::GetClientSlot(const NETADDR &Addr)
{
	int Slot = -1;

	for(int i = 0; i < MaxClients(); i++)
	{
		if(m_aSlots[i].m_Connection.State() != NET_CONNSTATE_OFFLINE &&
			m_aSlots[i].m_Connection.State() != NET_CONNSTATE_ERROR &&
			net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), &Addr) == 0)

		{
			Slot = i;
		}
	}

	return Slot;
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk)
{
	while(1)
	{
		NETADDR Addr;

		// check for a chunk
		if(m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		int Bytes = net_udp_recv(m_Socket, &Addr, m_RecvUnpacker.m_aBuffer, NET_MAX_PACKETSIZE);

		// no more packets for now
		if(Bytes <= 0)
			break;
				
		// check if we just should drop the packet
		char aBuf[128];
		/* if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf)))
		{
			// banned, reply with a message
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf)+1, NET_SECURITY_TOKEN_UNSUPPORTED);
			continue;
		} */
				
		if(CNetBase::UnpackPacket(m_RecvUnpacker.m_aBuffer, Bytes, &m_RecvUnpacker.m_Data) == 0)
		{
			if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONNLESS)
			{
				//refuse server info for banned clients (vanilla behavior)
				if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf)))
					continue;

				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				pChunk->m_ClientID = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
				pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
				return 1;
			}
			else
			{
				// drop invalid ctrl packets
				if (m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONTROL &&
						m_RecvUnpacker.m_Data.m_DataSize == 0)
					continue;

				// normal packet, find matching slot
				int Slot = GetClientSlot(Addr);
				
				if (Slot != -1)
				{
					// found

					// control
					if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONTROL)
						OnConnCtrlMsg(Addr, Slot, m_RecvUnpacker.m_Data.m_aChunkData[0], m_RecvUnpacker.m_Data);

					if(m_aSlots[Slot].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr))
					{
						if(m_RecvUnpacker.m_Data.m_DataSize)
							m_RecvUnpacker.Start(&Addr, &m_aSlots[Slot].m_Connection, Slot);
					}
				}
				else
				{
					// not found, client that wants to connect

					//refuse connect for banned clients
					if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf)))
					{
						// banned, reply with a message
						CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf)+1, NET_SECURITY_TOKEN_UNSUPPORTED);
						continue;
 					}

					if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONTROL &&
						m_RecvUnpacker.m_Data.m_DataSize > 1)
						// got control msg with extra size (should support token)
						OnTokenCtrlMsg(Addr, m_RecvUnpacker.m_Data.m_aChunkData[0], m_RecvUnpacker.m_Data);
					else
						// got connection-less ctrl or sys msg
						OnPreConnMsg(Addr, m_RecvUnpacker.m_Data);
				}
			}
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk)
{
	if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
	{
		dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
		return -1;
	}

	if(pChunk->m_Flags&NETSENDFLAG_CONNLESS)
	{
		// send connectionless packet
		CNetBase::SendPacketConnless(m_Socket, &pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize);
	}
	else
	{
		int Flags = 0;
		dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
		dbg_assert(pChunk->m_ClientID < MaxClients(), "errornous client id");

		if(pChunk->m_Flags&NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientID].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags&NETSENDFLAG_FLUSH)
				m_aSlots[pChunk->m_ClientID].m_Connection.Flush();
		}
		else
		{
			Drop(pChunk->m_ClientID, CLIENTDROPTYPE_ERROR, "Error sending data");
		}
	}
	return 0;
}

void CNetServer::SetMaxClientsPerIP(int Max)
{
	// clamp
	if(Max < 1)
		Max = 1;
	else if(Max > NET_MAX_CLIENTS)
		Max = NET_MAX_CLIENTS;

	m_MaxClientsPerIP = Max;
}

const char* CNetServer::GetCaptcha(const NETADDR* pAddr, bool Debug)
{
	if(!m_lCaptcha.size())
		return "???";
	
	unsigned int IpHash = 0;
	for(unsigned int i=0; i<4; i++)
	{
		IpHash |= (pAddr->ip[i]<<(i*8));
	}
	
	unsigned int CaptchaId = IpHash%m_lCaptcha.size();
	
	if(Debug)
	{
		char aBuf[64];
		net_addr_str(pAddr, aBuf, 64, 0);
	}
	
	return m_lCaptcha[CaptchaId].m_aText;
}

void CNetServer::AddCaptcha(const char* pText)
{
	CCaptcha Captcha;
	str_copy(Captcha.m_aText, pText, sizeof(Captcha));
	m_lCaptcha.add(Captcha);
}
