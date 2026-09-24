/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_NETWORK_H
#define ENGINE_SHARED_NETWORK_H

#include "net_queue.h"

#include "huffman.h"
#include "legacy/network7.h"
#include "ringbuffer.h"
#include "zstd_dict.h"

/*

CURRENT:
	packet header: 7 bytes (9 bytes for connless)
		unsigned char flags_ack;    // 6bit flags, 2bit ack
		unsigned char ack;          // 8bit ack
		unsigned char numchunks;    // 8bit chunks
		unsigned char token[4];     // 32bit token
		// ffffffaa
		// aaaaaaaa
		// NNNNNNNN
		// TTTTTTTT
		// TTTTTTTT
		// TTTTTTTT
		// TTTTTTTT

	packet header (CONNLESS):
		unsigned char flag_version;				// 6bit flags, 2bits version
		unsigned char token[4];					// 32bit token
		unsigned char responsetoken[4];			// 32bit response token

		// ffffffvv
		// TTTTTTTT
		// TTTTTTTT
		// TTTTTTTT
		// TTTTTTTT
		// RRRRRRRR
		// RRRRRRRR
		// RRRRRRRR
		// RRRRRRRR

	if the token isn't explicitely set by any means, it must be set to
	0xffffffff

	chunk header: 2-3 bytes
		unsigned char flags_size; // 2bit flags, 6 bit size
		unsigned char size_seq; // 6bit size, 2bit seq
		(unsigned char seq;) // 8bit seq, if vital flag is set
*/

enum
{
	NETFLAG_ALLOWSTATELESS = 1,
	NETSENDFLAG_VITAL = 1,
	NETSENDFLAG_CONNLESS = 2,
	NETSENDFLAG_FLUSH = 4,

	NETSTATE_OFFLINE = 0,
	NETSTATE_CONNECTING,
	NETSTATE_ONLINE,

	NETBANTYPE_SOFT = 1,
	NETBANTYPE_DROP = 2,

	NETCREATE_FLAG_RANDOMPORT = 1,
};

enum
{
	NET_MAX_CHUNKHEADERSIZE = 3,

	// packets
	NET_PACKETHEADERSIZE = 7,
	NET_PACKETHEADERSIZE_CONNLESS = NET_PACKETHEADERSIZE + 2,
	NET_MAX_PACKETHEADERSIZE = NET_PACKETHEADERSIZE_CONNLESS,

	NET_MAX_PACKETSIZE = 1400,
	NET_MAX_PAYLOAD = NET_MAX_PACKETSIZE - NET_MAX_PACKETHEADERSIZE,

	NET_PACKETVERSION = 1,

	NET_PACKETFLAG_CONTROL = 1,
	NET_PACKETFLAG_RESEND = 2,
	NET_PACKETFLAG_COMPRESSION = 4,
	NET_PACKETFLAG_CONNLESS = 8,

	// The payload is compressed with zstd + embedded dictionary instead of the
	// legacy Huffman coder. Only ever set on a connection that negotiated
	// NET_CTRLFLAG_ZSTD_DICT during the handshake, so peers that did not
	// negotiate keep exchanging plain NET_PACKETFLAG_COMPRESSION packets.
	// Bit 4 was unused before the flag got 6 bits, so old peers just ignore it.
	NET_PACKETFLAG_COMPRESSION_ZSTD = 16,

	NET_MAX_PACKET_CHUNKS = 256,

	// token
	NET_SEEDTIME = 16,

	NET_TOKENCACHE_SIZE = 64,
	NET_TOKENCACHE_ADDRESSEXPIRY = NET_SEEDTIME,
	NET_TOKENCACHE_PACKETEXPIRY = 5,
};
enum
{
	NET_TOKEN_MAX = 0xffffffff,
	NET_TOKEN_NONE = NET_TOKEN_MAX,
	NET_TOKEN_MASK = NET_TOKEN_MAX,
};
enum
{
	NET_TOKENFLAG_ALLOWBROADCAST = 1,
	NET_TOKENFLAG_RESPONSEONLY = 2,

	NET_TOKENREQUEST_DATASIZE = 512,

	//
	NET_MAX_CLIENTS = 128,
	NET_MAX_CONSOLE_CLIENTS = 4,

	NET_MAX_SEQUENCE = 1 << 10,
	NET_SEQUENCE_MASK = NET_MAX_SEQUENCE - 1,

	NET_CONNSTATE_OFFLINE = 0,
	NET_CONNSTATE_TOKEN = 1,
	NET_CONNSTATE_CONNECT = 2,
	NET_CONNSTATE_PENDING = 3,
	NET_CONNSTATE_ONLINE = 4,
	NET_CONNSTATE_ERROR = 5,

	NET_CHUNKFLAG_VITAL = 1,
	NET_CHUNKFLAG_RESEND = 2,

	NET_CTRLMSG_KEEPALIVE = 0,
	NET_CTRLMSG_CONNECT = 1,
	NET_CTRLMSG_ACCEPT = 2,
	NET_CTRLMSG_CLOSE = 4,
	NET_CTRLMSG_TOKEN = 5,

	NET_CONN_BUFFERSIZE = 1024 * 32,

	NET_ENUM_TERMINATOR
};

// Packet payload codec, negotiated per connection during the handshake.
// The values are frozen in legacy::network7 so that the 0.7 translator and the
// live engine can never drift apart.
enum
{
	NET_COMPRESSION_HUFFMAN = legacy::NET7_COMPRESSION_HUFFMAN, // legacy coder, always available
	NET_COMPRESSION_ZSTD = legacy::NET7_COMPRESSION_ZSTD, // zstd with the embedded dictionary
};

// Capability bits exchanged while connecting. The client advertises its own
// bits in NET_CTRLMSG_CONNECT (in the extended token request buffer, right
// behind the 4 byte token), the server answers with the codec it picked in
// NET_CTRLMSG_ACCEPT. A peer that does not understand the byte leaves it zero,
// which means plain Huffman and therefore stays wire compatible.
enum
{
	NET_CTRLFLAG_ZSTD_DICT = legacy::NET7_CTRLFLAG_ZSTD_DICT,
};

// 0.8 generation marker. The native handshake carries {'T','W','8'} in front of
// the capability byte so a 0.8 server can tell a 0.8 client from a 0.7 one and
// reject the latter instead of mis-parsing its traffic. A client always opens
// with the marker; the legacy (0.7) stack omits it once the peer's ACCEPT
// reveals that the server is a 0.7 one.
enum
{
	NET_GENERATION_MARKER_SIZE = 3,
	NET_GENERATION_MARKER_0 = 'T',
	NET_GENERATION_MARKER_1 = 'W',
	NET_GENERATION_MARKER_2 = '8',
};

// Where the capability bits sit in the handshake. Chunk data starts with the
// control byte, so the extended NET_CTRLMSG_CONNECT request buffer (which
// begins with the 4 byte token) is shifted by one.
enum
{
	NET_CTRL_REQUEST_CAPABILITY_OFFSET = legacy::NET7_CTRL_REQUEST_CAPABILITY_OFFSET, // inside m_aRequestTokenBuf
	NET_CTRL_CONNECT_CAPABILITY_OFFSET = legacy::NET7_CTRL_CONNECT_CAPABILITY_OFFSET, // inside m_aChunkData
	NET_CTRL_ACCEPT_CAPABILITY_OFFSET = legacy::NET7_CTRL_ACCEPT_CAPABILITY_OFFSET, // inside m_aChunkData

	// capability byte positions once the 0.8 generation marker is present
	NET_CTRL_REQUEST_CAPABILITY_OFFSET_8 = NET_CTRL_REQUEST_CAPABILITY_OFFSET + NET_GENERATION_MARKER_SIZE,
	NET_CTRL_CONNECT_CAPABILITY_OFFSET_8 = NET_CTRL_CONNECT_CAPABILITY_OFFSET + NET_GENERATION_MARKER_SIZE,
	NET_CTRL_ACCEPT_CAPABILITY_OFFSET_8 = NET_CTRL_ACCEPT_CAPABILITY_OFFSET + NET_GENERATION_MARKER_SIZE,
};

// Write the 0.8 generation marker at pChunkData[Offset]. Returns false (and
// writes nothing) when the buffer is too small.
bool Net8WriteGenerationMarker(unsigned char *pChunkData, int ChunkDataSize, int Offset);

// Check whether pChunkData[Offset] holds the 0.8 generation marker.
bool Net8HasGenerationMarker(const unsigned char *pChunkData, int ChunkDataSize, int Offset);

typedef int (*NETFUNC_DELCLIENT)(int ClientID, const char *pReason, void *pUser);
typedef int (*NETFUNC_NEWCLIENT)(int ClientID, void *pUser);

typedef unsigned int TOKEN;

struct CNetChunk
{
	// -1 means that it's a connless packet
	// 0 on the client means the server
	int m_ClientID;
	NETADDR m_Address; // only used when cid == -1
	int m_Flags;
	int m_DataSize;
	const void *m_pData;
};

class CNetChunkHeader
{
public:
	int m_Flags;
	int m_Size;
	int m_Sequence;

	unsigned char *Pack(unsigned char *pData);
	unsigned char *Unpack(unsigned char *pData);
};

class CNetChunkResend
{
public:
	int m_Flags;
	int m_DataSize;
	unsigned char *m_pData;

	int m_Sequence;
	int64 m_LastSendTime;
	int64 m_FirstSendTime;
};

class CNetPacketConstruct
{
public:
	TOKEN m_Token;
	TOKEN m_ResponseToken; // only used in connless packets
	int m_Flags;
	int m_Ack;
	int m_NumChunks;
	int m_DataSize;
	// Codec to use for this packet, set by the connection from its negotiated
	// state. Zero (NET_COMPRESSION_HUFFMAN) for control packets and for
	// connections that did not negotiate zstd.
	int m_Compression;
	unsigned char m_aChunkData[NET_MAX_PAYLOAD];
};

class CNetBase
{
	class CNetInitializer
	{
	public:
		CNetInitializer()
		{
			// init the network
			net_init();
		}
	};
	static CNetInitializer m_NetInitializer;

	class CConfig *m_pConfig;
	class IEngine *m_pEngine;
	NETSOCKET m_Socket;
	IOHANDLE m_DataLogSent;
	IOHANDLE m_DataLogRecv;
	CHuffman m_Huffman;
	CZstdDict m_Zstd;
	unsigned char m_aRequestTokenBuf[NET_TOKENREQUEST_DATASIZE];

public:
	CNetBase();
	~CNetBase();
	CConfig *Config() { return m_pConfig; }
	class IEngine *Engine() { return m_pEngine; }
	int NetType() { return m_Socket.type; }

	void Init(NETSOCKET Socket, class CConfig *pConfig, class IConsole *pConsole, class IEngine *pEngine);
	void Shutdown();
	void UpdateLogHandles();
	void Wait(int Time);

	void SendControlMsg(const NETADDR *pAddr, TOKEN Token, int Ack, int ControlMsg, const void *pExtra, int ExtraSize);
	void SendControlMsgWithToken(const NETADDR *pAddr, TOKEN Token, int Ack, int ControlMsg, TOKEN MyToken, bool Extended, bool GenerationMarker = true);
	void SendPacketConnless(const NETADDR *pAddr, TOKEN Token, TOKEN ResponseToken, const void *pData, int DataSize);
	void SendPacket(const NETADDR *pAddr, CNetPacketConstruct *pPacket);
	int UnpackPacket(NETADDR *pAddr, unsigned char *pBuffer, CNetPacketConstruct *pPacket);
};

class CNetTokenManager
{
public:
	void Init(CNetBase *pNetBase, int SeedTime = NET_SEEDTIME);
	void Update();

	void GenerateSeed();

	int ProcessMessage(const NETADDR *pAddr, const CNetPacketConstruct *pPacket);

	bool CheckToken(const NETADDR *pAddr, TOKEN Token, TOKEN ResponseToken, bool *BroadcastResponse);
	TOKEN GenerateToken(const NETADDR *pAddr) const;
	static TOKEN GenerateToken(const NETADDR *pAddr, int64 Seed);

private:
	CNetBase *m_pNetBase;

	int64 m_Seed;
	int64 m_PrevSeed;

	TOKEN m_GlobalToken;
	TOKEN m_PrevGlobalToken;

	int m_SeedTime;
	int64 m_NextSeedTime;
};

typedef void (*FSendCallback)(int TrackID, void *pUser);
struct CSendCBData
{
	FSendCallback m_pfnCallback;
	void *m_pCallbackUser;
	int m_TrackID;
};

class CNetTokenCache
{
public:
	CNetTokenCache();
	~CNetTokenCache();
	void Init(CNetBase *pNetBase, const CNetTokenManager *pTokenManager);
	void SendPacketConnless(const NETADDR *pAddr, const void *pData, int DataSize, CSendCBData *pCallbackData = 0);
	void PurgeStoredPacket(int TrackID);
	void FetchToken(const NETADDR *pAddr);
	void AddToken(const NETADDR *pAddr, TOKEN PeerToken, int TokenFlag);
	TOKEN GetToken(const NETADDR *pAddr);
	void Update();

private:
	class CConnlessPacketInfo
	{
	private:
		static int m_UniqueID;

	public:
		CConnlessPacketInfo() : m_TrackID(CConnlessPacketInfo::m_UniqueID++) {}

		NETADDR m_Addr;
		int m_DataSize;
		char m_aData[NET_MAX_PAYLOAD];
		int64 m_Expiry;
		int64 m_LastTokenRequest;
		const int m_TrackID;
		FSendCallback m_pfnCallback;
		void *m_pCallbackUser;
		CConnlessPacketInfo *m_pNext;
	};

	struct CAddressInfo
	{
		NETADDR m_Addr;
		TOKEN m_Token;
		int64 m_Expiry;
	};

	TStaticRingBuffer<CAddressInfo,
		NET_TOKENCACHE_SIZE * sizeof(CAddressInfo),
		CRingBufferBase::FLAG_RECYCLE>
		m_TokenCache;

	CConnlessPacketInfo *m_pConnlessPacketList; // TODO: enhance this, dynamic linked lists
						    // are bad for performance
	CNetBase *m_pNetBase;
	const CNetTokenManager *m_pTokenManager;
};

class CNetConnection
{
	// TODO: is this needed because this needs to be aware of
	// the ack sequencing number and is also responible for updating
	// that. this should be fixed.
	friend class CNetRecvUnpacker;

private:
	unsigned short m_Sequence;
	unsigned short m_Ack;
	unsigned short m_PeerAck;
	unsigned m_State;

	// Negotiated packet payload codec (NET_COMPRESSION_*). Stays Huffman until
	// the handshake agreed on zstd.
	int m_Compression;

	// When set, the handshake omits the 0.8 generation marker and speaks the
	// frozen 0.7 form. The client sets this itself once the peer's ACCEPT
	// identifies it as a 0.7 server; 0.7 demo playback does not use a
	// connection.
	bool m_Legacy;

	int m_RemoteClosed;
	bool m_BlockCloseMsg;

	TStaticRingBuffer<CNetChunkResend, NET_CONN_BUFFERSIZE> m_Buffer;

	int64 m_LastUpdateTime;
	int64 m_LastRecvTime;
	int64 m_LastSendTime;

	char m_ErrorString[256];

	CNetPacketConstruct m_Construct;

	TOKEN m_Token;
	TOKEN m_PeerToken;
	NETADDR m_PeerAddr;

	NETSTATS m_Stats;
	CNetBase *m_pNetBase;

	//
	void Reset();
	void ResetStats();
	void SetError(const char *pString);
	void AckChunks(int Ack);

	int QueueChunkEx(int Flags, int DataSize, const void *pData, int Sequence);
	void SendControl(int ControlMsg, const void *pExtra, int ExtraSize);
	void SendControlWithToken(int ControlMsg);
	void SendAccept();
	void ResendChunk(CNetChunkResend *pResend);
	void Resend();

	static TOKEN GenerateToken(const NETADDR *pPeerAddr);

public:
	void Init(CNetBase *pNetBase, bool BlockCloseMsg);
	int Connect(NETADDR *pAddr);
	void Disconnect(const char *pReason);

	void SetToken(TOKEN Token);

	// True once the peer's ACCEPT identified it as a 0.7 server.
	bool IsLegacy() const { return m_Legacy; }

	TOKEN Token() const { return m_Token; }
	TOKEN PeerToken() const { return m_PeerToken; }
	class CConfig *Config() { return m_pNetBase->Config(); }

	int Update();
	int Flush();

	int Feed(CNetPacketConstruct *pPacket, NETADDR *pAddr);
	int QueueChunk(int Flags, int DataSize, const void *pData);
	void SendPacketConnless(const char *pData, int DataSize);

	const char *ErrorString();
	void SignalResend();
	int State() const { return m_State; }
	int Compression() const { return m_Compression; }
	const NETADDR *PeerAddress() const { return &m_PeerAddr; }

	void ResetErrorString() { m_ErrorString[0] = 0; }
	const char *ErrorString() const { return m_ErrorString; }

	// Needed for GotProblems in NetClient
	int64 LastRecvTime() const { return m_LastRecvTime; }
	int64 ConnectTime() const { return m_LastUpdateTime; }

	int AckSequence() const { return m_Ack; }
	// The backroom is ack-NET_MAX_SEQUENCE/2. Used for knowing if we acked a packet or not
	static int IsSeqInBackroom(int Seq, int Ack);
};

class CConsoleNetConnection
{
private:
	int m_State;

	NETADDR m_PeerAddr;
	NETSOCKET m_Socket;

	char m_aBuffer[NET_MAX_PACKETSIZE];
	int m_BufferOffset;

	char m_aErrorString[256];

	bool m_LineEndingDetected;
	char m_aLineEnding[3];

public:
	void Init(NETSOCKET Socket, const NETADDR *pAddr);
	void Disconnect(const char *pReason);

	int State() const { return m_State; }
	const NETADDR *PeerAddress() const { return &m_PeerAddr; }
	const char *ErrorString() const { return m_aErrorString; }

	void Reset();
	int Update();
	int Send(const char *pLine);
	int Recv(char *pLine, int MaxLength);
};

class CNetRecvUnpacker
{
	bool m_Valid;

public:
	NETADDR m_Addr;
	CNetConnection *m_pConnection;
	int m_CurrentChunk;
	int m_ClientID;
	CNetPacketConstruct m_Data;
	unsigned char m_aBuffer[NET_MAX_PACKETSIZE];

	CNetRecvUnpacker() { Clear(); }
	bool IsActive() { return m_Valid; }
	void Clear();
	void Start(const NETADDR *pAddr, CNetConnection *pConnection, int ClientID);
	int FetchChunk(CNetChunk *pChunk);
};

/*
	A packet on its way between the network thread and the game thread.

	The payload is copied into the entry instead of pointing into the receive
	buffer, so the network thread can keep receiving while the game thread
	still holds the packet. m_Chunk.m_pData points into m_aData.
*/
class CNetPacketEntry
{
public:
	CNetChunk m_Chunk;
	TOKEN m_ResponseToken;
	unsigned char m_aData[NET_MAX_PACKETSIZE];

	CNetPacketEntry()
	{
		mem_zero(&m_Chunk, sizeof(m_Chunk));
		m_ResponseToken = NET_TOKEN_NONE;
	}

	CNetPacketEntry(const CNetPacketEntry &Other) { CopyFrom(Other); }
	CNetPacketEntry &operator=(const CNetPacketEntry &Other)
	{
		if(this != &Other)
			CopyFrom(Other);
		return *this;
	}
	CNetPacketEntry(CNetPacketEntry &&Other) noexcept { CopyFrom(Other); }
	CNetPacketEntry &operator=(CNetPacketEntry &&Other) noexcept
	{
		if(this != &Other)
			CopyFrom(Other);
		return *this;
	}

	// build an entry from a chunk, copying its payload
	void Set(const CNetChunk *pChunk, TOKEN ResponseToken)
	{
		m_Chunk = *pChunk;
		m_ResponseToken = ResponseToken;
		if(pChunk->m_pData && pChunk->m_DataSize > 0 && pChunk->m_DataSize <= (int) sizeof(m_aData))
		{
			mem_copy(m_aData, pChunk->m_pData, pChunk->m_DataSize);
			m_Chunk.m_pData = m_aData;
		}
		else
		{
			m_Chunk.m_pData = 0;
			m_Chunk.m_DataSize = 0;
		}
	}

private:
	void CopyFrom(const CNetPacketEntry &Other)
	{
		m_Chunk = Other.m_Chunk;
		m_ResponseToken = Other.m_ResponseToken;
		if(Other.m_Chunk.m_pData == Other.m_aData)
			m_Chunk.m_pData = m_aData;
		if(Other.m_Chunk.m_pData && Other.m_Chunk.m_DataSize > 0 && Other.m_Chunk.m_DataSize <= (int) sizeof(m_aData))
			mem_copy(m_aData, Other.m_aData, Other.m_Chunk.m_DataSize);
	}
};

// a drop the game thread asked for, applied by the network thread
class CNetPendingDrop
{
public:
	int m_ClientID;
	char m_aReason[128];

	CNetPendingDrop() :
		m_ClientID(-1)
	{
		m_aReason[0] = 0;
	}
	CNetPendingDrop(int ClientID, const char *pReason) :
		m_ClientID(ClientID)
	{
		str_copy(m_aReason, pReason, sizeof(m_aReason));
	}
};

// a connect the game thread asked for, applied by the network thread
class CNetPendingConnect
{
public:
	NETADDR m_Addr;
	bool m_Valid;

	CNetPendingConnect() :
		m_Valid(false)
	{
		mem_zero(&m_Addr, sizeof(m_Addr));
	}
	CNetPendingConnect(const NETADDR *pAddr) :
		m_Addr(*pAddr),
		m_Valid(true)
	{
	}
};

// a disconnect the game thread asked for, applied by the network thread
class CNetPendingDisconnect
{
public:
	char m_aReason[256];

	CNetPendingDisconnect()
	{
		m_aReason[0] = 0;
	}
	CNetPendingDisconnect(const char *pReason)
	{
		str_copy(m_aReason, pReason != 0 ? pReason : "", sizeof(m_aReason));
	}
};

// server side
class CNetServer : public CNetBase
{
	struct CSlot
	{
	public:
		CNetConnection m_Connection;
	};

	class CNetBan *m_pNetBan;
	CSlot m_aSlots[NET_MAX_CLIENTS];
	int m_NumClients;
	int m_MaxClients;
	int m_MaxClientsPerIP;

	NETFUNC_NEWCLIENT m_pfnNewClient;
	NETFUNC_DELCLIENT m_pfnDelClient;
	void *m_UserPtr;

	CNetRecvUnpacker m_RecvUnpacker;

	CNetTokenManager m_TokenManager;
	CNetTokenCache m_TokenCache;

	/*
		The network thread.

		The socket, the connections, the resend queues and the tokens belong to
		this thread. The game thread only sees unpacked packets, which arrive
		through m_InboundPackets, and hands packets back with Send(), which
		queues them instead of touching the socket.

		A packet the game thread is done with is released with FreePacket(),
		because its payload points into the queue entry.
	*/
	void *m_pThread;
	volatile bool m_ThreadShutdown;
	volatile bool m_ThreadRunning;
	// only set inside the network thread, so Send/Drop can tell which side
	// they were called from without asking the OS for a thread id
	volatile bool m_InNetworkThread;

	// a packet on its way up to the game thread
	CNetQueue<CNetPacketEntry> m_InboundPackets;
	// a packet on its way down to clients
	CNetQueue<CNetPacketEntry> m_OutboundPackets;
	// drops the game thread requested
	CNetQueue<CNetPendingDrop> m_PendingDrops;

	// true when the caller is not the network thread
	bool IsGameThread() const { return m_pThread != 0 && !m_InNetworkThread; }
	void ApplyPendingDrops();

	void ThreadMain();
	static void ThreadEntry(void *pUser);
	void RunThread();

public:
	// stop the thread so a transport that is torn down without Close() does
	// not leave it running against freed memory
	~CNetServer();

	// true while the network thread is alive
	bool ThreadRunning() const { return m_ThreadRunning; }

	// ---- game thread ----
	// start the network thread after Open(); it takes over the socket
	void StartThread();
	// stop and join the network thread
	void StopThread();

	// the packets the network thread received since the last call, oldest
	// first. The game thread owns them until it clears the array.
	void DrainPackets(array<CNetPacketEntry> &Out);
	// release a packet obtained from DrainPackets()
	void FreePacket(CNetPacketEntry &Entry);

	//
	bool Open(NETADDR BindAddr, class CConfig *pConfig, class IConsole *pConsole, class IEngine *pEngine, class CNetBan *pNetBan,
		int MaxClients, int MaxClientsPerIP, NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser);
	void Close(const char *pReason);

	// the token parameter is only used for connless packets
	int Recv(CNetChunk *pChunk, TOKEN *pResponseToken = 0);
	int Send(CNetChunk *pChunk, TOKEN Token = NET_TOKEN_NONE);
	int Update();
	void AddToken(const NETADDR *pAddr, TOKEN Token) { m_TokenCache.AddToken(pAddr, Token, 0); }

	//
	void Drop(int ClientID, const char *pReason);

	// status requests
	const NETADDR *ClientAddr(int ClientID) const { return m_aSlots[ClientID].m_Connection.PeerAddress(); }
	class CNetBan *NetBan() const { return m_pNetBan; }

	//
	void SetMaxClients(int MaxClients);
	void SetMaxClientsPerIP(int MaxClientsPerIP);
};

class CNetConsole
{
	struct CSlot
	{
		CConsoleNetConnection m_Connection;
	};

	NETSOCKET m_Socket;
	class CNetBan *m_pNetBan;
	CSlot m_aSlots[NET_MAX_CONSOLE_CLIENTS];

	NETFUNC_NEWCLIENT m_pfnNewClient;
	NETFUNC_DELCLIENT m_pfnDelClient;
	void *m_UserPtr;

	CNetRecvUnpacker m_RecvUnpacker;

public:
	//
	bool Open(NETADDR BindAddr, class CNetBan *pNetBan, NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser);
	void Close();

	//
	int Recv(char *pLine, int MaxLength, int *pClientID = 0);
	int Send(int ClientID, const char *pLine);
	int Update();
	void SetLingerState(int State);

	//
	int AcceptClient(NETSOCKET Socket, const NETADDR *pAddr);
	void Drop(int ClientID, const char *pReason);

	// status requests
	const NETADDR *ClientAddr(int ClientID) const { return m_aSlots[ClientID].m_Connection.PeerAddress(); }
	class CNetBan *NetBan() const { return m_pNetBan; }
};

// client side
class CNetClient : public CNetBase
{
	CNetConnection m_Connection;
	CNetRecvUnpacker m_RecvUnpacker;

	CNetTokenCache m_TokenCache;
	CNetTokenManager m_TokenManager;

	int m_Flags;

	/*
		The network thread, split the same way as CNetServer: the socket and
		the connection belong to this thread, and the game thread only sees
		unpacked packets in m_InboundPackets. What the game thread sends is
		queued in m_OutboundPackets and written by this thread.

		Connect and Disconnect are forwarded as well, so the connection state
		machine is only advanced from this thread.
	*/
	void *m_pThread;
	volatile bool m_ThreadShutdown;
	volatile bool m_ThreadRunning;
	volatile bool m_InNetworkThread;

	CNetQueue<CNetPacketEntry> m_InboundPackets;
	CNetQueue<CNetPacketEntry> m_OutboundPackets;

	// control requests the game thread made for the network thread to apply
	CNetQueue<CNetPendingConnect> m_PendingConnects;
	CNetQueue<CNetPendingDisconnect> m_PendingDisconnects;

	void ThreadMain();
	static void ThreadEntry(void *pUser);
	void RunThread();
	void ApplyPendingControl();
	void ApplyPendingDisconnects();

	bool IsGameThread() const { return m_pThread != 0 && !m_InNetworkThread; }

public:
	// stop the thread so a transport that is torn down without Close() does
	// not leave it running against freed memory
	~CNetClient();

	// openness
	bool Open(NETADDR BindAddr, class CConfig *pConfig, class IConsole *pConsole, class IEngine *pEngine, int Flags);
	void Close();

	// ---- game thread ----
	// start/stop the network thread; Open() must have succeeded first
	void StartThread();
	void StopThread();
	// the packets the network thread received since the last call, oldest first
	void DrainPackets(array<CNetPacketEntry> &Out);
	void FreePacket(CNetPacketEntry &Entry);

	// connection state
	int Disconnect(const char *Reason);
	int Connect(NETADDR *Addr);

	// True when the peer turned out to be a 0.7 server during the handshake.
	bool IsLegacy() const { return m_Connection.IsLegacy(); }

	// communication
	int Recv(CNetChunk *pChunk, TOKEN *pResponseToken = 0);
	int Send(CNetChunk *pChunk, TOKEN Token = NET_TOKEN_NONE, CSendCBData *pCallbackData = 0);
	void PurgeStoredPacket(int TrackID);

	// pumping
	int Update();
	int Flush();

	int ResetErrorString();

	// error and state
	int State() const;
	bool GotProblems() const;
	const char *ErrorString() const;
};

#endif
