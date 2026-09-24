/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>

#include "network.h"
#include "protocol.h"

bool CNetClient::Open(NETADDR BindAddr, CConfig *pConfig, IConsole *pConsole, IEngine *pEngine, int Flags)
{
	// open socket
	NETSOCKET Socket;
	Socket = net_udp_create(BindAddr, (Flags & NETCREATE_FLAG_RANDOMPORT) ? 1 : 0);
	if(!Socket.type)
		return false;

	// clean it
	mem_zero(this, sizeof(*this));

	// the zeroing above wiped the queue locks, so create them again
	m_InboundPackets.Setup();
	m_OutboundPackets.Setup();
	m_PendingConnects.Setup();
	m_PendingDisconnects.Setup();
	m_Outbound.clear();

	// init
	Init(Socket, pConfig, pConsole, pEngine);
	m_Connection.Init(this, false);

	m_TokenManager.Init(this);
	m_TokenCache.Init(this, &m_TokenManager);

	m_Flags = Flags;

	return true;
}

void CNetClient::StartThread()
{
	if(m_pThread)
		return;

	m_ThreadShutdown = false;
	m_ThreadRunning = false;
	m_pThread = thread_init(CNetClient::ThreadEntry, this);

	while(!m_ThreadRunning && !m_ThreadShutdown)
		thread_sleep(1);
}

void CNetClient::StopThread()
{
	if(!m_pThread)
		return;

	m_ThreadShutdown = true;
	thread_wait(m_pThread);
	m_pThread = 0;

	m_InboundPackets.Clear();
	m_OutboundPackets.Clear();
	m_PendingConnects.Clear();
	m_PendingDisconnects.Clear();
	m_Outbound.clear();
}

void CNetClient::ThreadEntry(void *pUser)
{
	((CNetClient *) pUser)->ThreadMain();
}

void CNetClient::ThreadMain()
{
	m_InNetworkThread = true;
	m_ThreadRunning = true;

	while(!m_ThreadShutdown)
	{
		// poll the socket; the wait keeps the thread from spinning
		Wait(1000 / SERVER_TICK_SPEED / 4);
		RunThread();
	}

	RunThread();

	m_InNetworkThread = false;
	m_ThreadRunning = false;
}

void CNetClient::ApplyPendingControl()
{
	CNetPendingConnect Connect;
	if(m_PendingConnects.Pop(Connect) && Connect.m_Valid)
	{
		m_Connection.Connect(&Connect.m_Addr);
		// drop any older connect that queued up behind this one
		while(m_PendingConnects.Pop(Connect))
			;
	}
}

void CNetClient::ApplyPendingDisconnects()
{
	CNetPendingDisconnect Disconnect;
	while(m_PendingDisconnects.Pop(Disconnect))
	{
		if(m_Connection.State() != NET_CONNSTATE_OFFLINE)
			m_Connection.Disconnect(Disconnect.m_aReason);
	}
}

void CNetClient::RunThread()
{
	ApplyPendingControl();
	ApplyPendingDisconnects();

	m_Connection.Update();
	if(m_Connection.State() == NET_CONNSTATE_ERROR)
		m_Connection.Disconnect(m_Connection.ErrorString());
	m_TokenManager.Update();
	m_TokenCache.Update();

	// hand everything the socket produced to the game thread
	CNetChunk Packet;
	TOKEN ResponseToken;
	while(Recv(&Packet, &ResponseToken))
	{
		CNetPacketEntry Entry;
		Entry.Set(&Packet, ResponseToken);
		m_InboundPackets.Push(Entry);
	}

	// and write back what the game thread produced
	m_OutboundPackets.Drain(m_Outbound);
	for(int i = 0; i < m_Outbound.size(); i++)
		Send(&m_Outbound[i].m_Chunk, m_Outbound[i].m_ResponseToken);
	m_Outbound.clear();
}

void CNetClient::DrainPackets(array<CNetPacketEntry> &Out)
{
	m_InboundPackets.Drain(Out);
}

void CNetClient::FreePacket(CNetPacketEntry &Entry)
{
	Entry.m_Chunk.m_pData = 0;
	Entry.m_Chunk.m_DataSize = 0;
}

void CNetClient::Close()
{
	StopThread();

	if(m_Connection.State() != NET_CONNSTATE_OFFLINE)
		m_Connection.Disconnect("Client shutdown");
	Shutdown();
}

int CNetClient::Disconnect(const char *pReason)
{
	// the connection belongs to the network thread
	if(IsGameThread())
	{
		m_PendingDisconnects.Push(CNetPendingDisconnect(pReason));
		return 0;
	}

	m_Connection.Disconnect(pReason);
	return 0;
}

int CNetClient::Update()
{
	m_Connection.Update();
	if(m_Connection.State() == NET_CONNSTATE_ERROR)
		Disconnect(m_Connection.ErrorString());
	m_TokenManager.Update();
	m_TokenCache.Update();
	return 0;
}

int CNetClient::Connect(NETADDR *pAddr)
{
	if(IsGameThread())
	{
		m_PendingConnects.Push(CNetPendingConnect(pAddr));
		return 0;
	}

	m_Connection.Connect(pAddr);
	return 0;
}

int CNetClient::ResetErrorString()
{
	m_Connection.ResetErrorString();
	return 0;
}

int CNetClient::Recv(CNetChunk *pChunk, TOKEN *pResponseToken)
{
	while(1)
	{
		// check for a chunk
		if(m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		NETADDR Addr;
		int Result = UnpackPacket(&Addr, m_RecvUnpacker.m_aBuffer, &m_RecvUnpacker.m_Data);
		// no more packets for now
		if(Result > 0)
			break;

		if(!Result)
		{
			if(m_Connection.State() != NET_CONNSTATE_OFFLINE && m_Connection.State() != NET_CONNSTATE_ERROR && net_addr_comp(m_Connection.PeerAddress(), &Addr, true) == 0)
			{
				if(m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr))
				{
					if(!(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONNLESS))
						m_RecvUnpacker.Start(&Addr, &m_Connection, 0);
				}
			}
			else
			{
				int Accept = m_TokenManager.ProcessMessage(&Addr, &m_RecvUnpacker.m_Data);
				if(!Accept)
					continue;

				if(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONTROL)
				{
					if(m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_TOKEN)
						m_TokenCache.AddToken(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, NET_TOKENFLAG_ALLOWBROADCAST | NET_TOKENFLAG_RESPONSEONLY);
				}
				else if(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONNLESS && Accept != -1)
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
	}
	return 0;
}

int CNetClient::Send(CNetChunk *pChunk, TOKEN Token, CSendCBData *pCallbackData)
{
	/*
		The game thread never touches the socket, so what it sends is queued and
		written by the network thread. The network thread itself (control
		messages, resends) keeps sending directly, which also guarantees the
		queue never feeds back into itself.
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

		if(pChunk->m_ClientID == -1 && net_addr_comp(&pChunk->m_Address, m_Connection.PeerAddress(), true) == 0)
		{
			// upgrade the packet, now that we know its recipent
			pChunk->m_ClientID = 0;
		}

		if(Token != NET_TOKEN_NONE)
		{
			SendPacketConnless(&pChunk->m_Address, Token, m_TokenManager.GenerateToken(&pChunk->m_Address), pChunk->m_pData, pChunk->m_DataSize);
		}
		else
		{
			if(pChunk->m_ClientID == -1)
			{
				m_TokenCache.SendPacketConnless(&pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize, pCallbackData);
			}
			else
			{
				dbg_assert(pChunk->m_ClientID == 0, "errornous client id");
				m_Connection.SendPacketConnless((const char *) pChunk->m_pData, pChunk->m_DataSize);
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
		dbg_assert(pChunk->m_ClientID == 0, "errornous client id");

		if(pChunk->m_Flags & NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData);

		if(pChunk->m_Flags & NETSENDFLAG_FLUSH)
			m_Connection.Flush();
	}
	return 0;
}

void CNetClient::PurgeStoredPacket(int TrackID)
{
	m_TokenCache.PurgeStoredPacket(TrackID);
}

int CNetClient::State() const
{
	if(m_Connection.State() == NET_CONNSTATE_ONLINE)
		return NETSTATE_ONLINE;
	if(m_Connection.State() == NET_CONNSTATE_OFFLINE)
		return NETSTATE_OFFLINE;
	return NETSTATE_CONNECTING;
}

int CNetClient::Flush()
{
	return m_Connection.Flush();
}

bool CNetClient::GotProblems() const
{
	return (time_get() - m_Connection.LastRecvTime() > time_freq());
}

const char *CNetClient::ErrorString() const
{
	return m_Connection.ErrorString();
}
