/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>

#include "netban.h"
#include "network.h"
#include "config.h"
#include "protocol.h"

bool CNetServer::Open(NETADDR BindAddr, CConfig *pConfig, IConsole *pConsole, IEngine *pEngine, CNetBan *pNetBan,
	int MaxClients, int MaxClientsPerIP, NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	// zero out the whole structure
	mem_zero(this, sizeof(*this));

	// the zeroing above wiped the queue locks, so create them again
	m_InboundPackets.Setup();
	m_OutboundPackets.Setup();
	m_Outbound.clear();
	m_PendingDrops.Setup();

	// open socket
	NETSOCKET Socket = net_udp_create(BindAddr, 0);
	if(!Socket.type)
		return false;

	// init
	m_pNetBan = pNetBan;
	Init(Socket, pConfig, pConsole, pEngine);

	m_TokenManager.Init(this);
	m_TokenCache.Init(this, &m_TokenManager);

	m_NumClients = 0;
	SetMaxClients(MaxClients);
	SetMaxClientsPerIP(MaxClientsPerIP);

	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		m_aSlots[i].m_Connection.Init(this, true);

	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_UserPtr = pUser;

	return true;
}

void CNetServer::StartThread()
{
	if(m_pThread)
		return;

	m_ThreadShutdown = false;
	m_ThreadRunning = false;
	m_pThread = thread_init(CNetServer::ThreadEntry, this);

	// the thread owns the socket from here on; wait until it is up so a
	// caller that immediately pumps the network does not race it
	while(!m_ThreadRunning && !m_ThreadShutdown)
		thread_sleep(1);
}

void CNetServer::StopThread()
{
	if(!m_pThread)
		return;

	m_ThreadShutdown = true;
	thread_wait(m_pThread);
	m_pThread = 0;

	// the game thread will not drain them anymore
	m_InboundPackets.Clear();
	m_OutboundPackets.Clear();
	m_PendingDrops.Clear();
	m_Outbound.clear();
}

void CNetServer::ThreadEntry(void *pUser)
{
	((CNetServer *) pUser)->ThreadMain();
}

void CNetServer::ThreadMain()
{
	m_InNetworkThread = true;
	m_ThreadRunning = true;

	while(!m_ThreadShutdown)
	{
		// wait for a packet, but never longer than one tick so connection
		// timeouts and resends stay on schedule
		Wait(1000 / SERVER_TICK_SPEED / 2);
		RunThread();
	}

	// send whatever the game thread queued before the shutdown
	RunThread();

	m_InNetworkThread = false;
	m_ThreadRunning = false;
}

void CNetServer::ApplyPendingDrops()
{
	CNetPendingDrop Drop;
	while(m_PendingDrops.Pop(Drop))
	{
		if(Drop.m_ClientID < 0 || Drop.m_ClientID >= NET_MAX_CLIENTS)
			continue;
		if(m_aSlots[Drop.m_ClientID].m_Connection.State() == NET_CONNSTATE_OFFLINE)
			continue;

		if(m_pfnDelClient)
			m_pfnDelClient(Drop.m_ClientID, Drop.m_aReason, m_UserPtr);

		m_aSlots[Drop.m_ClientID].m_Connection.Disconnect(Drop.m_aReason);
		m_NumClients--;
	}
}

void CNetServer::RunThread()
{
	ApplyPendingDrops();

	Update();

	// hand everything the socket produced to the game thread
	CNetChunk Packet;
	TOKEN ResponseToken;
	while(Recv(&Packet, &ResponseToken))
	{
		CNetPacketEntry Entry;
		Entry.Set(&Packet, ResponseToken);
		m_InboundPackets.Push(Entry);
	}

	// and send back what the game thread produced
	m_OutboundPackets.Drain(m_Outbound);
	for(int i = 0; i < m_Outbound.size(); i++)
		Send(&m_Outbound[i].m_Chunk, m_Outbound[i].m_ResponseToken);
	m_Outbound.clear();
}

void CNetServer::DrainPackets(array<CNetPacketEntry> &Out)
{
	m_InboundPackets.Drain(Out);
}

void CNetServer::FreePacket(CNetPacketEntry &Entry)
{
	Entry.m_Chunk.m_pData = 0;
	Entry.m_Chunk.m_DataSize = 0;
}

void CNetServer::Close(const char *pReason)
{
	StopThread();

	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		Drop(i, pReason);

	Shutdown();
}

void CNetServer::Drop(int ClientID, const char *pReason)
{
	/*
		A drop from the game thread only records the request: the connection and
		the DelClient callback belong to the network thread. Applying it on the
		game thread would race the connection state and call the game back from
		the wrong thread.
	*/
	if(IsGameThread())
	{
		m_PendingDrops.Push(CNetPendingDrop{ClientID, pReason != 0 ? pReason : "dropped"});
		return;
	}

	if(ClientID < 0 || ClientID >= NET_MAX_CLIENTS || m_aSlots[ClientID].m_Connection.State() == NET_CONNSTATE_OFFLINE)
		return;

	if(m_pfnDelClient)
		m_pfnDelClient(ClientID, pReason, m_UserPtr);

	m_aSlots[ClientID].m_Connection.Disconnect(pReason);
	m_NumClients--;
}

int CNetServer::Update()
{
	int64 Now = time_get();
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
			continue;

		m_aSlots[i].m_Connection.Update();
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR)
		{
			if(Now - m_aSlots[i].m_Connection.ConnectTime() < time_freq() && NetBan())
			{
				if(NetBan()->BanAddr(ClientAddr(i), 60, "Stressing network") == -1)
					Drop(i, m_aSlots[i].m_Connection.ErrorString());
			}
			else
				Drop(i, m_aSlots[i].m_Connection.ErrorString());
		}
	}

	m_TokenManager.Update();
	m_TokenCache.Update();

	return 0;
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk, TOKEN *pResponseToken)
{
	while(1)
	{
		// check for a chunk
		if(m_RecvUnpacker.IsActive() && m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		NETADDR Addr;
		int Result = UnpackPacket(&Addr, m_RecvUnpacker.m_aBuffer, &m_RecvUnpacker.m_Data);
		// no more packets for now
		if(Result > 0)
			break;

		if(!Result)
		{
			// check for bans
			char aBuf[128];
			int LastInfoQuery;
			if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf), &LastInfoQuery))
			{
				// banned, reply with a message (5 second cooldown)
				int Time = time_timestamp();
				if(LastInfoQuery + 5 < Time)
				{
					SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1);
				}
				continue;
			}

			bool Found = false;
			// try to find matching slot
			for(int i = 0; i < NET_MAX_CLIENTS; i++)
			{
				if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
					continue;

				if(net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), &Addr, true) == 0)
				{
					if(m_aSlots[i].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr))
					{
						if(m_RecvUnpacker.m_Data.m_DataSize)
						{
							if(!(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONNLESS))
								m_RecvUnpacker.Start(&Addr, &m_aSlots[i].m_Connection, i);
							else
							{
								pChunk->m_Flags = NETSENDFLAG_CONNLESS;
								pChunk->m_Address = *m_aSlots[i].m_Connection.PeerAddress();
								pChunk->m_ClientID = i;
								pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
								pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
								if(pResponseToken)
									*pResponseToken = NET_TOKEN_NONE;
								return 1;
							}
						}
					}
					Found = true;
				}
			}

			if(Found)
				continue;

			int Accept = m_TokenManager.ProcessMessage(&Addr, &m_RecvUnpacker.m_Data);
			if(Accept <= 0)
				continue;

			if(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONTROL)
			{
				if(m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_CONNECT)
				{
					// A 0.8 server only talks to 0.8 clients. A 0.7 client does
					// not send the generation marker, so reject it here, before
					// any slot is allocated, instead of mis-parsing its 0.7
					// traffic.
					if(!Net8HasGenerationMarker(m_RecvUnpacker.m_Data.m_aChunkData, m_RecvUnpacker.m_Data.m_DataSize, NET_CTRL_CONNECT_CAPABILITY_OFFSET))
					{
						// silent ignore
						// const char WrongGenMsg[] = "wrong version generation";
						// SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, WrongGenMsg, sizeof(WrongGenMsg));
						continue;
					}

					// check if there are free slots
					if(m_NumClients >= m_MaxClients)
					{
						const char FullMsg[] = "This server is full";
						SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, FullMsg, sizeof(FullMsg));
						continue;
					}

					// only allow a specific number of players with the same ip
					int FoundAddr = 1;

					bool Continue = false;
					for(int i = 0; i < NET_MAX_CLIENTS; i++)
					{
						if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
							continue;

						if(!net_addr_comp(&Addr, m_aSlots[i].m_Connection.PeerAddress(), false))
						{
							if(FoundAddr++ >= m_MaxClientsPerIP)
							{
								char aBuf[128];
								str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIP);
								SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1);
								Continue = true;
								break;
							}
						}
					}

					if(Continue)
						continue;

					for(int i = 0; i < NET_MAX_CLIENTS; i++)
					{
						if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
						{
							m_NumClients++;
							m_aSlots[i].m_Connection.SetToken(m_RecvUnpacker.m_Data.m_Token);
							m_aSlots[i].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr);
							if(m_pfnNewClient)
								m_pfnNewClient(i, m_UserPtr);
							break;
						}
					}
				}
				else if(m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_TOKEN)
					m_TokenCache.AddToken(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, NET_TOKENFLAG_RESPONSEONLY);
			}
			else if(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONNLESS)
			{
				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				pChunk->m_ClientID = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
				pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
				if(pResponseToken)
					*pResponseToken = m_RecvUnpacker.m_Data.m_ResponseToken;
				return 1;
			}
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk, TOKEN Token)
{
	/*
		The game thread never touches the socket or a connection, so a send
		from there is queued and performed by the network thread on its next
		pass. The network thread itself (resends, control messages, connless
		replies) keeps sending directly, which also guarantees the queue never
		feeds back into itself.
	*/
	if(IsGameThread())
	{
		CNetPacketEntry Entry;
		Entry.Set(pChunk, Token);
		m_OutboundPackets.Push(Entry);
		return 0;
	}

	if(pChunk->m_Flags & NETSENDFLAG_CONNLESS)
	{
		if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
		{
			dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
			return -1;
		}

		if(pChunk->m_ClientID == -1)
		{
			for(int i = 0; i < NET_MAX_CLIENTS; i++)
			{
				if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
					continue;

				if(net_addr_comp(&pChunk->m_Address, m_aSlots[i].m_Connection.PeerAddress(), true) == 0)
				{
					// upgrade the packet, now that we know its recipent
					pChunk->m_ClientID = i;
					break;
				}
			}
		}

		if(Token != NET_TOKEN_NONE)
		{
			SendPacketConnless(&pChunk->m_Address, Token, m_TokenManager.GenerateToken(&pChunk->m_Address), pChunk->m_pData, pChunk->m_DataSize);
		}
		else
		{
			if(pChunk->m_ClientID == -1)
			{
				m_TokenCache.SendPacketConnless(&pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize);
			}
			else
			{
				dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
				dbg_assert(pChunk->m_ClientID < NET_MAX_CLIENTS, "errornous client id");
				dbg_assert(m_aSlots[pChunk->m_ClientID].m_Connection.State() != NET_CONNSTATE_OFFLINE, "errornous client id");

				m_aSlots[pChunk->m_ClientID].m_Connection.SendPacketConnless((const char *) pChunk->m_pData, pChunk->m_DataSize);
			}
		}
	}
	else
	{
		if(pChunk->m_DataSize + NET_MAX_CHUNKHEADERSIZE >= NET_MAX_PAYLOAD)
		{
			dbg_msg("netclient", "chunk payload too big. %d. dropping chunk", pChunk->m_DataSize);
			return -1;
		}

		int Flags = 0;
		dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
		dbg_assert(pChunk->m_ClientID < NET_MAX_CLIENTS, "errornous client id");
		dbg_assert(m_aSlots[pChunk->m_ClientID].m_Connection.State() != NET_CONNSTATE_OFFLINE, "errornous client id");

		if(pChunk->m_Flags & NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientID].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags & NETSENDFLAG_FLUSH)
				m_aSlots[pChunk->m_ClientID].m_Connection.Flush();
		}
		else
		{
			Drop(pChunk->m_ClientID, m_aSlots[pChunk->m_ClientID].m_Connection.State() == NET_CONNSTATE_ERROR ? m_aSlots[pChunk->m_ClientID].m_Connection.ErrorString() : "Error sending data");
		}
	}
	return 0;
}

void CNetServer::SetMaxClients(int MaxClients)
{
	m_MaxClients = clamp(MaxClients, 1, int(NET_MAX_CLIENTS));
}

void CNetServer::SetMaxClientsPerIP(int MaxClientsPerIP)
{
	m_MaxClientsPerIP = clamp(MaxClientsPerIP, 1, int(NET_MAX_CLIENTS));
}
