/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "network_translator.h"

#include <base/system.h>

#include <engine/message.h>
#include <engine/shared/compression.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include "network7.h"

namespace legacy
{
	// Decodes the 0.7 int-packed string encoding (a copy of IntsToStr from
	// gamecore.h, which lives in game-shared and is not available here).
	// Writes exactly 4 * Num bytes, the last one forced to NUL.
	static void IntsToStr(const int *pInts, int Num, char *pStr)
	{
		while(Num)
		{
			pStr[0] = (((*pInts) >> 24) & 0xff) - 128;
			pStr[1] = (((*pInts) >> 16) & 0xff) - 128;
			pStr[2] = (((*pInts) >> 8) & 0xff) - 128;
			pStr[3] = ((*pInts) & 0xff) - 128;
			pStr += 4;
			pInts++;
			Num--;
		}
		pStr[-1] = 0;
	}

	static int FinishMsg(CPacker *pPacker, void *pOut, int OutSize)
	{
		if(pPacker->Error())
			return -2;
		if(pPacker->Size() > OutSize)
			return -2;
		mem_copy(pOut, pPacker->Data(), pPacker->Size());
		return pPacker->Size();
	}

	static int CopyRawMsg(int MsgId8, CUnpacker *pUnpacker, void *pOut, int OutSize)
	{
		const int Remaining = pUnpacker->RemainingSize();
		const unsigned char *pRaw = pUnpacker->GetRaw(Remaining);
		if(!pRaw)
			return -2;
		CMsgPacker Packer(MsgId8);
		Packer.AddRaw(pRaw, Remaining);
		return FinishMsg(&Packer, pOut, OutSize);
	}

	// 0.7 object type -> 0.8 object type. -1 means the type is handled
	// specially (TeeInfo/Tuning synthesis) or has no 0.8 equivalent.
	static int MapObjectType7To8(int Type7)
	{
		switch(Type7)
		{
			case protocol7::NETOBJTYPE_PLAYERINPUT: return NETOBJTYPE_PLAYERINPUT;
			case protocol7::NETOBJTYPE_PROJECTILE: return NETOBJTYPE_PROJECTILE;
			case protocol7::NETOBJTYPE_LASER: return NETOBJTYPE_LASER;
			case protocol7::NETOBJTYPE_PICKUP: return NETOBJTYPE_PICKUP;
			case protocol7::NETOBJTYPE_FLAG: return NETOBJTYPE_FLAG;
			case protocol7::NETOBJTYPE_GAMEDATA: return NETOBJTYPE_GAMEDATA;
			case protocol7::NETOBJTYPE_GAMEDATATEAM: return NETOBJTYPE_GAMEDATATEAM;
			case protocol7::NETOBJTYPE_GAMEDATAFLAG: return NETOBJTYPE_GAMEDATAFLAG;
			case protocol7::NETOBJTYPE_CHARACTERCORE: return NETOBJTYPE_CHARACTERCORE;
			case protocol7::NETOBJTYPE_CHARACTER: return NETOBJTYPE_CHARACTER;
			case protocol7::NETOBJTYPE_SPECTATORINFO: return NETOBJTYPE_SPECTATORINFO;
			case protocol7::NETOBJTYPE_GAMEDATARACE: return NETOBJTYPE_GAMEDATARACE;
		}
		// The event block moved by one: 0.7 uses 16..22, 0.8 uses 15..21.
		if(Type7 >= protocol7::NETEVENTTYPE_COMMON && Type7 <= protocol7::NETEVENTTYPE_DAMAGE)
			return Type7 - 1;
		return -1;
	}

	// 0.7 and 0.8 Votes enums differ: 0.8 dropped UNKNOWN and appended RUN_*.
	static int MapVoteType7To8(int Type7)
	{
		switch(Type7)
		{
			case protocol7::VOTE_START_OP: return VOTE_START_OP;
			case protocol7::VOTE_START_KICK: return VOTE_START_KICK;
			case protocol7::VOTE_START_SPEC: return VOTE_START_SPEC;
			case protocol7::VOTE_END_ABORT: return VOTE_END_ABORT;
			case protocol7::VOTE_END_PASS: return VOTE_END_PASS;
			case protocol7::VOTE_END_FAIL: return VOTE_END_FAIL;
		}
		return -1;
	}

	void CNetworkTranslator::CClientState7::Reset()
	{
		m_Active = false;
		m_Local = 0;
		m_Team = 0;
		m_Country = 0;
		mem_zero(m_aName, sizeof(m_aName));
		mem_zero(m_aClan, sizeof(m_aClan));
		mem_zero(m_aaSkinPartNames, sizeof(m_aaSkinPartNames));
		mem_zero(m_aUseCustomColors, sizeof(m_aUseCustomColors));
		mem_zero(m_aSkinPartColors, sizeof(m_aSkinPartColors));
	}

	CNetworkTranslator::CNetworkTranslator()
	{
		m_UuidGameDataPrediction = calculate_uuid("game-data-prediction@netobj.teeworlds.wiki");
		m_UuidPlayerInfoExtra = calculate_uuid("player-info-extra@netobj.teeworlds.wiki");

		// The static item sizes must match what each protocol's own server uses,
		// otherwise the delta size fields desync.
		Net7ConfigureSnapshotDelta(&m_Delta7);
		CNetObjHandler Handler8;
		for(int i = 0; i < NUM_NETOBJTYPES; i++)
			m_Delta8.SetStaticsize(i, Handler8.GetObjSize(i));

		// the storage pointers must be valid before Reset() purges them
		m_Snapshots7.Init();
		Reset();
	}

	CNetworkTranslator::~CNetworkTranslator()
	{
	}

	void CNetworkTranslator::Reset()
	{
		for(int i = 0; i < MAX_CLIENTS7; i++)
			m_aClients7[i].Reset();
		m_TuningValid = false;
		mem_zero(m_aTuneParams7, sizeof(m_aTuneParams7));
		m_Snapshots7.PurgeAll();
		m_Snapshots7.Init();
		m_EmptySnap.Clear();
		m_NumUuidObjects = 0;
		m_CurrentRecvTick7 = 0;
		ResetSnapshotParts();
		ResetBase8();
	}

	void CNetworkTranslator::ResetSnapshotParts()
	{
		mem_zero(m_aParts7, sizeof(m_aParts7));
		m_NumParts7 = 0;
		m_LastPartSize7 = 0;
	}

	void CNetworkTranslator::ResetBase8()
	{
		m_Base8Data.set_size(0);
		m_pBase8 = &m_EmptySnap;
		m_BaseTick8 = -1;
	}

	void CNetworkTranslator::SetBase8(int GameTick, int Snap8Size)
	{
		m_Base8Data.set_size(Snap8Size);
		mem_copy(m_Base8Data.base_ptr(), m_Snap8Data.base_ptr(), Snap8Size);
		m_pBase8 = (CSnapshot *) m_Base8Data.base_ptr();
		m_BaseTick8 = GameTick;
	}

	const CNetworkTranslator::CClientState7 *CNetworkTranslator::ClientState(int ClientID) const
	{
		if(ClientID < 0 || ClientID >= MAX_CLIENTS7)
			return 0;
		return &m_aClients7[ClientID];
	}

	void CNetworkTranslator::BuildUuidMap(const CSnapshot *pSnapshot)
	{
		m_NumUuidObjects = 0;
		for(int i = 0; i < pSnapshot->NumItems(); i++)
		{
			const CSnapshotItem *pItem = pSnapshot->GetItem(i);
			if(pItem->Type() != protocol7::NETOBJTYPE_EX)
				continue;
			if(pSnapshot->GetItemSize(i) < (int) sizeof(Uuid))
				continue;
			Uuid ItemUuid;
			for(size_t b = 0; b < sizeof(Uuid) / sizeof(unsigned int); b++)
				uint_to_bytes_be(&ItemUuid.m_aData[b * sizeof(unsigned int)], pItem->Data()[b]);
			int Kind = UUID_KIND_NONE;
			if(ItemUuid == m_UuidGameDataPrediction)
				Kind = UUID_KIND_GAMEDATAPREDICTION;
			else if(ItemUuid == m_UuidPlayerInfoExtra)
				Kind = UUID_KIND_PLAYERINFOEXTRA;
			if(Kind != UUID_KIND_NONE && m_NumUuidObjects < MAX_UUID_OBJECTS)
			{
				m_aUuidObjects[m_NumUuidObjects].m_SyntheticType = pItem->ID();
				m_aUuidObjects[m_NumUuidObjects].m_Kind = Kind;
				m_NumUuidObjects++;
			}
		}
	}

	int CNetworkTranslator::UuidObjectKind(int Type) const
	{
		for(int i = 0; i < m_NumUuidObjects; i++)
			if(m_aUuidObjects[i].m_SyntheticType == Type)
				return m_aUuidObjects[i].m_Kind;
		return UUID_KIND_NONE;
	}

	void CNetworkTranslator::AbsorbClientInfo(int ClientID, int Local, int Team, int Country,
		const char *pName, const char *pClan, const char *const *papSkinPartNames,
		const int *pUseCustomColors, const int *pSkinPartColors)
	{
		if(ClientID < 0 || ClientID >= MAX_CLIENTS7)
			return;
		CClientState7 *pState = &m_aClients7[ClientID];
		pState->m_Active = true;
		pState->m_Local = Local;
		pState->m_Team = Team;
		pState->m_Country = Country == -1 ? 0xFFFF : Country;
		str_copy_fixed(pState->m_aName, pName, sizeof(pState->m_aName));
		str_copy_fixed(pState->m_aClan, pClan, sizeof(pState->m_aClan));
		for(int p = 0; p < NUM_SKINPARTS; p++)
		{
			str_copy_fixed(pState->m_aaSkinPartNames[p], papSkinPartNames[p], sizeof(pState->m_aaSkinPartNames[p]));
			pState->m_aUseCustomColors[p] = pUseCustomColors[p];
			pState->m_aSkinPartColors[p] = pSkinPartColors[p];
		}
	}

	void CNetworkTranslator::AbsorbDeClientInfo(int ClientID, const CSnapshotItem *pItem)
	{
		if(ClientID < 0 || ClientID >= MAX_CLIENTS7)
			return;
		const int *pData = pItem->Data();
		CClientState7 *pState = &m_aClients7[ClientID];
		pState->m_Active = true;
		pState->m_Local = pData[0];
		pState->m_Team = pData[1];
		// the demo object stores strings in the 0.7 int-packed encoding
		IntsToStr(&pData[2], 4, pState->m_aName);
		IntsToStr(&pData[6], 3, pState->m_aClan);
		pState->m_Country = pData[9];
		for(int p = 0; p < NUM_SKINPARTS; p++)
			IntsToStr(&pData[10 + p * 6], 6, pState->m_aaSkinPartNames[p]);
		mem_copy(pState->m_aUseCustomColors, &pData[46], sizeof(pState->m_aUseCustomColors));
		mem_copy(pState->m_aSkinPartColors, &pData[52], sizeof(pState->m_aSkinPartColors));
	}

	int CNetworkTranslator::BuildSnapshot8(const CSnapshot *pSnap7)
	{
		BuildUuidMap(pSnap7);

		const CSnapshotItem *apPlayerInfo[MAX_CLIENTS7];
		const CSnapshotItem *apPlayerInfoRace[MAX_CLIENTS7];
		bool aHasFlagsExtra[MAX_CLIENTS7];
		int aFlagsExtra[MAX_CLIENTS7];
		mem_zero(apPlayerInfo, sizeof(apPlayerInfo));
		mem_zero(apPlayerInfoRace, sizeof(apPlayerInfoRace));
		mem_zero(aHasFlagsExtra, sizeof(aHasFlagsExtra));
		mem_zero(aFlagsExtra, sizeof(aFlagsExtra));
		int GameDataPredictionFlags = -1;

		const int NumItems = pSnap7->NumItems();
		for(int i = 0; i < NumItems; i++)
		{
			const CSnapshotItem *pItem = pSnap7->GetItem(i);
			const int Type = pItem->Type();
			const int ID = pItem->ID();
			if(Type == protocol7::NETOBJTYPE_PLAYERINFO)
			{
				if(ID >= 0 && ID < MAX_CLIENTS7)
					apPlayerInfo[ID] = pItem;
			}
			else if(Type == protocol7::NETOBJTYPE_PLAYERINFORACE)
			{
				if(ID >= 0 && ID < MAX_CLIENTS7)
					apPlayerInfoRace[ID] = pItem;
			}
			else if(Type == protocol7::NETOBJTYPE_DE_CLIENTINFO)
			{
				AbsorbDeClientInfo(ID, pItem);
			}
			else if(Type == protocol7::NETOBJTYPE_DE_TUNEPARAMS)
			{
				if(pSnap7->GetItemSize(i) >= (int) sizeof(protocol7::CNetObj_De_TuneParams))
				{
					mem_copy(m_aTuneParams7, pItem->Data(), sizeof(m_aTuneParams7));
					m_TuningValid = true;
				}
			}
			else
			{
				const int Kind = UuidObjectKind(Type);
				if(Kind == UUID_KIND_PLAYERINFOEXTRA)
				{
					if(ID >= 0 && ID < MAX_CLIENTS7 && pSnap7->GetItemSize(i) >= (int) sizeof(protocol7::CNetObj_PlayerInfoExtra))
					{
						aHasFlagsExtra[ID] = true;
						aFlagsExtra[ID] = pItem->Data()[1]; // m_PlayerFlagsExtra
					}
				}
				else if(Kind == UUID_KIND_GAMEDATAPREDICTION)
				{
					if(pSnap7->GetItemSize(i) >= (int) sizeof(protocol7::CNetObj_GameDataPrediction))
						GameDataPredictionFlags = pItem->Data()[0];
				}
			}
		}

		CSnapshotBuilder Builder;
		Builder.Init();

		for(int i = 0; i < NumItems; i++)
		{
			const CSnapshotItem *pItem = pSnap7->GetItem(i);
			const int Type = pItem->Type();
			const int ID = pItem->ID();
			const int Size = pSnap7->GetItemSize(i);

			// absorbed / folded into other objects
			if(Type == protocol7::NETOBJTYPE_PLAYERINFO || Type == protocol7::NETOBJTYPE_PLAYERINFORACE || Type == protocol7::NETOBJTYPE_DE_CLIENTINFO || Type == protocol7::NETOBJTYPE_DE_GAMEINFO || Type == protocol7::NETOBJTYPE_DE_TUNEPARAMS || UuidObjectKind(Type) != UUID_KIND_NONE)
				continue;

			if(Type == protocol7::NETOBJTYPE_GAMEDATA)
			{
				if(Size < (int) sizeof(CNetObj_GameData) - (int) sizeof(int))
					continue;
				CNetObj_GameData *pOut = (CNetObj_GameData *) Builder.NewItem(NETOBJTYPE_GAMEDATA, ID, sizeof(CNetObj_GameData));
				if(!pOut)
					return -1;
				const int *pIn = pItem->Data();
				pOut->m_GameStartTick = pIn[0];
				pOut->m_GameStateFlags = pIn[1];
				pOut->m_GameStateEndTick = pIn[2];
				pOut->m_PredictionFlags = GameDataPredictionFlags >= 0 ? GameDataPredictionFlags : (GAMEPREDICTIONFLAG_EVENT | GAMEPREDICTIONFLAG_INPUT);
				continue;
			}

			const int Type8 = MapObjectType7To8(Type);
			if(Type8 < 0)
				continue;
			void *pOut = Builder.NewItem(Type8, ID, Size);
			if(!pOut)
				return -1;
			mem_copy(pOut, pItem->Data(), Size);
		}

		// PlayerInfo + the 0.7 identity table + Race/Extra -> one TeeInfo(id=ClientID)
		for(int ClientID = 0; ClientID < MAX_CLIENTS7; ClientID++)
		{
			if(!apPlayerInfo[ClientID])
				continue;
			const int *pInfo = apPlayerInfo[ClientID]->Data();
			const CClientState7 *pState = &m_aClients7[ClientID];

			CNetObj_TeeInfo *pTeeInfo = (CNetObj_TeeInfo *) Builder.NewItem(NETOBJTYPE_TEEINFO, ClientID, sizeof(CNetObj_TeeInfo));
			if(!pTeeInfo)
				return -1;

			int Flags = pInfo[0] & 0x7f; // ADMIN..BOT keep the same bit values
			if(aHasFlagsExtra[ClientID] && (aFlagsExtra[ClientID] & 1)) // PLAYERFLAGEXTRA_HIDDEN_IN_BOARD
				Flags |= TEEFLAG_HIDDEN_IN_BOARD;
			if(pState->m_Local)
				Flags |= TEEFLAG_LOCAL;

			pTeeInfo->m_LatencyAndCountry = ((pInfo[2] & 0xffff) << 16) | (pState->m_Country & 0xffff);
			pTeeInfo->m_Team = pState->m_Team;
			pTeeInfo->m_Flag = Flags;
			pTeeInfo->m_Score = pInfo[1];
			pTeeInfo->m_RaceStartTick = apPlayerInfoRace[ClientID] ? apPlayerInfoRace[ClientID]->Data()[0] : -1;
			// the identity strings match the TeeInfo fields one to one
			mem_copy(pTeeInfo->m_aName, pState->m_aName, sizeof(pTeeInfo->m_aName));
			mem_copy(pTeeInfo->m_aClan, pState->m_aClan, sizeof(pTeeInfo->m_aClan));
			mem_copy(pTeeInfo->m_aaSkinPartNames, pState->m_aaSkinPartNames, sizeof(pTeeInfo->m_aaSkinPartNames));
			mem_copy(pTeeInfo->m_aUseCustomColors, pState->m_aUseCustomColors, sizeof(pTeeInfo->m_aUseCustomColors));
			mem_copy(pTeeInfo->m_aSkinPartColors, pState->m_aSkinPartColors, sizeof(pTeeInfo->m_aSkinPartColors));
		}

		// Tuning is a singleton snapshot object; prefix copy of the 0.7 values.
		if(m_TuningValid)
		{
			CNetObj_Tuning *pTuning = (CNetObj_Tuning *) Builder.NewItem(NETOBJTYPE_TUNING, 0, sizeof(CNetObj_Tuning));
			if(!pTuning)
				return -1;
			mem_copy(pTuning->m_aTuneParams, m_aTuneParams7, sizeof(pTuning->m_aTuneParams));
		}

		m_Snap8Data.set_size(Builder.RequiredSize());
		return Builder.Finish(m_Snap8Data.base_ptr());
	}

	void CNetworkTranslator::StoreSnapshot7Copy(int GameTick, int DeltaTick)
	{
		// An empty 0.7 delta means the snapshot at GameTick is identical to
		// the one at DeltaTick. The 0.7 server stores a snapshot for every
		// tick and deltas later snapshots against GameTick once the client
		// acks it, so the tick must exist in the 0.7 history. If the source
		// tick is unknown the tick is left absent on purpose: later deltas
		// then fail cleanly and the server falls back to a full snapshot
		// instead of us decoding against a made-up baseline.
		if(DeltaTick >= 0)
		{
			CSnapshot *pStored = 0;
			const int StoredSize = m_Snapshots7.Get(DeltaTick, 0, &pStored, 0);
			if(StoredSize < 0 || !pStored)
				return;
			m_Snapshots7.Add(GameTick, time_get(), StoredSize, pStored, 0);
		}
		else
		{
			m_EmptySnap.Clear();
			m_Snapshots7.Add(GameTick, time_get(), sizeof(CSnapshot), &m_EmptySnap, 0);
		}
		m_Snapshots7.PurgeUntil(GameTick - SERVER_TICK_SPEED * 3);
	}

	int CNetworkTranslator::TranslateSnapshotDelta(int GameTick, int DeltaTick, const void *pDelta7, int DeltaSize7, void *pOut, int OutSize)
	{
		const CSnapshot *pBase7 = &m_EmptySnap;
		int BaseSize7 = (int) sizeof(CSnapshot);
		if(DeltaTick >= 0)
		{
			CSnapshot *pStored = 0;
			BaseSize7 = m_Snapshots7.Get(DeltaTick, 0, &pStored, 0);
			if(BaseSize7 < 0 || !pStored)
				return -1; // unknown baseline, the next full snapshot resyncs us
			pBase7 = pStored;
		}

		// A decoded snapshot is never larger than its baseline plus the delta
		// contents; every delta item contributes its raw data plus a small
		// per-item snapshot header, hence the factor two.
		const int Bound = BaseSize7 + DeltaSize7 * 2 + 256;
		m_Snap7Data.set_size(Bound);
		CSnapshot *pTo7 = (CSnapshot *) m_Snap7Data.base_ptr();
		const int Snap7Size = m_Delta7.UnpackDelta(pBase7, pTo7, pDelta7, DeltaSize7);
		if(Snap7Size < 0)
			return Snap7Size;

		m_Snapshots7.Add(GameTick, time_get(), Snap7Size, pTo7, 0);
		m_Snapshots7.PurgeUntil(GameTick - SERVER_TICK_SPEED * 3);

		const int Snap8Size = BuildSnapshot8(pTo7);
		if(Snap8Size < 0)
			return -1;

		const int DeltaSize8 = m_Delta8.CreateDelta(m_pBase8, (CSnapshot *) m_Snap8Data.base_ptr(), pOut);
		if(DeltaSize8 < 0)
			return DeltaSize8;

		SetBase8(GameTick, Snap8Size);
		return DeltaSize8;
	}

	int CNetworkTranslator::EmitSnapshot8(int GameTick, int DeltaField, int DeltaSize8, const void *pDelta8, CNetChunk *pOutChunks, int MaxOutChunks)
	{
		if(MaxOutChunks < 1)
			return 0;

		if(DeltaSize8 <= 0)
		{
			CMsgPacker Packer(NETMSG_SNAPEMPTY, true);
			Packer.AddInt(GameTick);
			Packer.AddInt(DeltaField);
			if(Packer.Error())
				return 0;
			m_aServerOut[0].set_size(Packer.Size());
			mem_copy(m_aServerOut[0].base_ptr(), Packer.Data(), Packer.Size());
			pOutChunks[0].m_ClientID = 0;
			pOutChunks[0].m_Flags = NETSENDFLAG_FLUSH;
			pOutChunks[0].m_DataSize = Packer.Size();
			pOutChunks[0].m_pData = m_aServerOut[0].base_ptr();
			m_BaseTick8 = GameTick;
			return 1;
		}

		array<unsigned char> aCompressed;
		aCompressed.set_size(DeltaSize8 + DeltaSize8 / 2 + 4096);
		const int CompSize = (int) CVariableInt::Compress(pDelta8, DeltaSize8, aCompressed.base_ptr(), aCompressed.size());
		if(CompSize < 0)
			return 0;

		const int Crc = m_pBase8->Crc();
		const int MaxSize = MAX_SNAPSHOT_PACKSIZE;
		const int NumPackets = (CompSize + MaxSize - 1) / MaxSize;
		if(NumPackets < 1 || NumPackets > MaxOutChunks)
			return 0;

		int n = 0;
		for(int Left = CompSize; Left > 0; n++)
		{
			const int Chunk = Left < MaxSize ? Left : MaxSize;
			Left -= Chunk;

			CMsgPacker Packer(NumPackets == 1 ? NETMSG_SNAPSINGLE : NETMSG_SNAP, true);
			Packer.AddInt(GameTick);
			Packer.AddInt(DeltaField);
			if(NumPackets > 1)
			{
				Packer.AddInt(NumPackets);
				Packer.AddInt(n);
			}
			Packer.AddInt(Crc);
			Packer.AddInt(Chunk);
			Packer.AddRaw(aCompressed.base_ptr() + n * MaxSize, Chunk);
			if(Packer.Error())
				return 0;

			m_aServerOut[n].set_size(Packer.Size());
			mem_copy(m_aServerOut[n].base_ptr(), Packer.Data(), Packer.Size());
			pOutChunks[n].m_ClientID = 0;
			pOutChunks[n].m_Flags = NETSENDFLAG_FLUSH;
			pOutChunks[n].m_DataSize = Packer.Size();
			pOutChunks[n].m_pData = m_aServerOut[n].base_ptr();
		}
		return n;
	}

	int CNetworkTranslator::TranslateCompleteSnapshot7(int GameTick, int DeltaTick, int CompleteSize, CNetChunk *pOutChunks, int MaxOutChunks)
	{
		ResetSnapshotParts();
		m_CurrentRecvTick7 = GameTick;

		if(CompleteSize <= 0)
		{
			// empty snapshot: forward the tick, keep the 0.8 baseline, but
			// record the tick on the 0.7 side so later deltas against it
			// can be decoded
			StoreSnapshot7Copy(GameTick, DeltaTick);
			return EmitSnapshot8(GameTick, GameTick - m_BaseTick8, 0, 0, pOutChunks, MaxOutChunks);
		}

		array<unsigned char> aDecompressed;
		aDecompressed.set_size(CompleteSize * 4 + 1024);
		const int IntSize = (int) CVariableInt::Decompress(m_Incoming7Data.base_ptr(), CompleteSize, aDecompressed.base_ptr(), aDecompressed.size());
		if(IntSize < 0)
			return 0;

		array<unsigned char> aDelta8;
		aDelta8.set_size(IntSize * 4 + 1024 * 1024);
		// A full 0.7 snapshot means the peer believes the client has no
		// usable baseline. Make the 0.8 delta self-contained as well,
		// otherwise a client that lost an earlier snapshot would keep
		// rejecting deltas against a baseline it does not have.
		if(DeltaTick < 0)
			ResetBase8();
		const int BaseTickBefore = m_BaseTick8;
		const int DeltaSize8 = TranslateSnapshotDelta(GameTick, DeltaTick, aDecompressed.base_ptr(), IntSize, aDelta8.base_ptr(), aDelta8.size());
		if(DeltaSize8 < 0)
			return 0;

		return EmitSnapshot8(GameTick, GameTick - BaseTickBefore, DeltaSize8, aDelta8.base_ptr(), pOutChunks, MaxOutChunks);
	}

	int CNetworkTranslator::HandleSnapshot7(CUnpacker *pUnpacker, int MsgId, CNetChunk *pOutChunks, int MaxOutChunks)
	{
		const int GameTick = pUnpacker->GetInt();
		const int DeltaTick = GameTick - pUnpacker->GetInt();
		if(pUnpacker->Error())
			return 0;

		// drop stale snapshots (e.g. reordered UDP packets), mirroring the
		// native client
		if(GameTick < m_CurrentRecvTick7)
			return 0;

		if(MsgId == NETMSG_SNAPEMPTY)
			return TranslateCompleteSnapshot7(GameTick, DeltaTick, 0, pOutChunks, MaxOutChunks);

		int NumParts = 1;
		int Part = 0;
		if(MsgId == NETMSG_SNAP)
		{
			NumParts = pUnpacker->GetInt();
			Part = pUnpacker->GetInt();
			if(NumParts < 1 || NumParts > CSnapshot::MAX_PARTS || Part < 0 || Part >= NumParts)
				return 0;
		}

		const int Crc = pUnpacker->GetInt();
		(void) Crc; // the 0.8 CRC is recomputed from the rebuilt snapshot
		const int PartSize = pUnpacker->GetInt();
		if(PartSize < 0 || PartSize > MAX_SNAPSHOT_PACKSIZE)
			return 0;
		const char *pData = (const char *) pUnpacker->GetRaw(PartSize);
		if(pUnpacker->Error())
			return 0;

		if(MsgId == NETMSG_SNAPSINGLE)
		{
			if(m_Incoming7Data.size() < MAX_SNAPSHOT_PACKSIZE)
				m_Incoming7Data.set_size(MAX_SNAPSHOT_PACKSIZE);
			if(pData)
				mem_copy(m_Incoming7Data.base_ptr(), pData, PartSize);
			return TranslateCompleteSnapshot7(GameTick, DeltaTick, PartSize, pOutChunks, MaxOutChunks);
		}

		if(GameTick != m_CurrentRecvTick7)
		{
			ResetSnapshotParts();
			m_CurrentRecvTick7 = GameTick;
		}

		const int BufSize = CSnapshot::MAX_PARTS * MAX_SNAPSHOT_PACKSIZE;
		if(m_Incoming7Data.size() < BufSize)
			m_Incoming7Data.set_size(BufSize);
		if(pData)
			mem_copy(m_Incoming7Data.base_ptr() + Part * MAX_SNAPSHOT_PACKSIZE, pData, PartSize);

		const int PartByte = Part / 8;
		const unsigned char PartMask = (unsigned char) (1 << (Part % 8));
		if(!(m_aParts7[PartByte] & PartMask))
		{
			m_aParts7[PartByte] |= PartMask;
			m_NumParts7++;
		}
		m_LastPartSize7 = PartSize;

		if(m_NumParts7 != NumParts)
			return 0;

		const int CompleteSize = (NumParts - 1) * MAX_SNAPSHOT_PACKSIZE + m_LastPartSize7;
		return TranslateCompleteSnapshot7(GameTick, DeltaTick, CompleteSize, pOutChunks, MaxOutChunks);
	}

	int CNetworkTranslator::TranslateServerMsg(const void *pMsg7, int Size7, void *pOut, int OutSize)
	{
		CMsgUnpacker Unpacker(pMsg7, Size7);
		if(Unpacker.Error())
			return -2;
		const int MsgId7 = Unpacker.Type();

		// Sv_TuneParams has no declared fields; the 32 raw CTuningParams ints
		// follow the empty message.
		if(MsgId7 == protocol7::NETMSGTYPE_SV_TUNEPARAMS)
		{
			for(int i = 0; i < NUM_TUNES; i++)
				m_aTuneParams7[i] = Unpacker.GetInt();
			if(Unpacker.Error())
				return -2;
			m_TuningValid = true;
			return -1; // consumed, synthesized into the Tuning snapshot object
		}

		// Messages whose payload is not described by the table: copy it verbatim.
		if(MsgId7 == protocol7::NETMSGTYPE_SV_VOTEOPTIONLISTADD)
			return CopyRawMsg(NETMSGTYPE_SV_VOTEOPTIONLISTADD, &Unpacker, pOut, OutSize);
		if(MsgId7 == protocol7::NETMSGTYPE_SV_GAMEMSG)
			return CopyRawMsg(NETMSGTYPE_SV_GAMEMSG, &Unpacker, pOut, OutSize);

		static protocol7::CNetObjHandler Handler7;
		void *pRaw = Handler7.SecureUnpackMsg(MsgId7, &Unpacker);
		if(!pRaw)
			return -2;

		switch(MsgId7)
		{
			case protocol7::NETMSGTYPE_SV_MOTD:
			{
				protocol7::CNetMsg_Sv_Motd *pIn = (protocol7::CNetMsg_Sv_Motd *) pRaw;
				CNetMsg_Sv_Motd Out;
				Out.m_pMessage = pIn->m_pMessage;
				CMsgPacker Packer(NETMSGTYPE_SV_MOTD);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_BROADCAST:
			{
				protocol7::CNetMsg_Sv_Broadcast *pIn = (protocol7::CNetMsg_Sv_Broadcast *) pRaw;
				CNetMsg_Sv_Broadcast Out;
				Out.m_pMessage = pIn->m_pMessage;
				CMsgPacker Packer(NETMSGTYPE_SV_BROADCAST);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_CHAT:
			{
				protocol7::CNetMsg_Sv_Chat *pIn = (protocol7::CNetMsg_Sv_Chat *) pRaw;
				CNetMsg_Sv_Chat Out;
				Out.m_Mode = pIn->m_Mode;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_TargetID = pIn->m_TargetID;
				Out.m_pMessage = pIn->m_pMessage;
				CMsgPacker Packer(NETMSGTYPE_SV_CHAT);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_TEAM:
			{
				protocol7::CNetMsg_Sv_Team *pIn = (protocol7::CNetMsg_Sv_Team *) pRaw;
				// 0.8 derives team membership from the TeeInfo snapshot object,
				// so team changes must update the identity table too
				if(pIn->m_ClientID >= 0 && pIn->m_ClientID < MAX_CLIENTS7)
					m_aClients7[pIn->m_ClientID].m_Team = pIn->m_Team;
				CNetMsg_Sv_Team Out;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_Team = pIn->m_Team;
				Out.m_Silent = pIn->m_Silent;
				Out.m_CooldownTick = pIn->m_CooldownTick;
				CMsgPacker Packer(NETMSGTYPE_SV_TEAM);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_KILLMSG:
			{
				protocol7::CNetMsg_Sv_KillMsg *pIn = (protocol7::CNetMsg_Sv_KillMsg *) pRaw;
				CNetMsg_Sv_KillMsg Out;
				Out.m_Killer = pIn->m_Killer;
				Out.m_Victim = pIn->m_Victim;
				Out.m_Weapon = pIn->m_Weapon;
				Out.m_ModeSpecial = pIn->m_ModeSpecial;
				Out.m_Assist = pIn->m_Assist;
				CMsgPacker Packer(NETMSGTYPE_SV_KILLMSG);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_EXTRAPROJECTILE:
				return -1; // removed in 0.8
			case protocol7::NETMSGTYPE_SV_READYTOENTER:
			{
				CNetMsg_Sv_ReadyToEnter Out;
				CMsgPacker Packer(NETMSGTYPE_SV_READYTOENTER);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_WEAPONPICKUP:
			{
				protocol7::CNetMsg_Sv_WeaponPickup *pIn = (protocol7::CNetMsg_Sv_WeaponPickup *) pRaw;
				CNetMsg_Sv_WeaponPickup Out;
				Out.m_Weapon = pIn->m_Weapon;
				CMsgPacker Packer(NETMSGTYPE_SV_WEAPONPICKUP);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_EMOTICON:
			{
				protocol7::CNetMsg_Sv_Emoticon *pIn = (protocol7::CNetMsg_Sv_Emoticon *) pRaw;
				CNetMsg_Sv_Emoticon Out;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_Emoticon = pIn->m_Emoticon;
				CMsgPacker Packer(NETMSGTYPE_SV_EMOTICON);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_VOTECLEAROPTIONS:
			{
				CNetMsg_Sv_VoteClearOptions Out;
				CMsgPacker Packer(NETMSGTYPE_SV_VOTECLEAROPTIONS);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_VOTEOPTIONADD:
			{
				protocol7::CNetMsg_Sv_VoteOptionAdd *pIn = (protocol7::CNetMsg_Sv_VoteOptionAdd *) pRaw;
				CNetMsg_Sv_VoteOptionAdd Out;
				Out.m_pDescription = pIn->m_pDescription;
				CMsgPacker Packer(NETMSGTYPE_SV_VOTEOPTIONADD);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_VOTEOPTIONREMOVE:
			{
				protocol7::CNetMsg_Sv_VoteOptionRemove *pIn = (protocol7::CNetMsg_Sv_VoteOptionRemove *) pRaw;
				CNetMsg_Sv_VoteOptionRemove Out;
				Out.m_pDescription = pIn->m_pDescription;
				CMsgPacker Packer(NETMSGTYPE_SV_VOTEOPTIONREMOVE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_VOTESET:
			{
				protocol7::CNetMsg_Sv_VoteSet *pIn = (protocol7::CNetMsg_Sv_VoteSet *) pRaw;
				const int Type8 = MapVoteType7To8(pIn->m_Type);
				if(Type8 < 0)
					return -1;
				CNetMsg_Sv_VoteSet Out;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_Type = Type8;
				Out.m_Timeout = pIn->m_Timeout;
				Out.m_pDescription = pIn->m_pDescription;
				Out.m_pReason = pIn->m_pReason;
				CMsgPacker Packer(NETMSGTYPE_SV_VOTESET);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_VOTESTATUS:
			{
				protocol7::CNetMsg_Sv_VoteStatus *pIn = (protocol7::CNetMsg_Sv_VoteStatus *) pRaw;
				CNetMsg_Sv_VoteStatus Out;
				Out.m_Yes = pIn->m_Yes;
				Out.m_No = pIn->m_No;
				Out.m_Pass = pIn->m_Pass;
				Out.m_Total = pIn->m_Total;
				CMsgPacker Packer(NETMSGTYPE_SV_VOTESTATUS);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_SERVERSETTINGS:
			{
				protocol7::CNetMsg_Sv_ServerSettings *pIn = (protocol7::CNetMsg_Sv_ServerSettings *) pRaw;
				CNetMsg_Sv_ServerSettings Out;
				Out.m_KickVote = pIn->m_KickVote;
				Out.m_KickMin = pIn->m_KickMin;
				Out.m_SpecVote = pIn->m_SpecVote;
				Out.m_TeamLock = pIn->m_TeamLock;
				Out.m_TeamBalance = pIn->m_TeamBalance;
				Out.m_PlayerSlots = pIn->m_PlayerSlots;
				Out.m_AllowSpecVoting = pIn->m_AllowSpecVoting;
				CMsgPacker Packer(NETMSGTYPE_SV_SERVERSETTINGS);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_CLIENTINFO:
			{
				protocol7::CNetMsg_Sv_ClientInfo *pIn = (protocol7::CNetMsg_Sv_ClientInfo *) pRaw;
				AbsorbClientInfo(pIn->m_ClientID, pIn->m_Local, pIn->m_Team, pIn->m_Country,
					pIn->m_pName, pIn->m_pClan, pIn->m_apSkinPartNames, pIn->m_aUseCustomColors, pIn->m_aSkinPartColors);
				if(pIn->m_Silent)
					return -1;
				CNetMsg_Sv_ClientEnter Out;
				Out.m_ClientID = pIn->m_ClientID;
				CMsgPacker Packer(NETMSGTYPE_SV_CLIENTENTER);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_GAMEINFO:
			{
				protocol7::CNetMsg_Sv_GameInfo *pIn = (protocol7::CNetMsg_Sv_GameInfo *) pRaw;
				CNetMsg_Sv_GameInfo Out;
				Out.m_GameFlags = pIn->m_GameFlags;
				Out.m_ScoreLimit = pIn->m_ScoreLimit;
				Out.m_TimeLimit = pIn->m_TimeLimit;
				Out.m_MatchNum = pIn->m_MatchNum;
				Out.m_MatchCurrent = pIn->m_MatchCurrent;
				CMsgPacker Packer(NETMSGTYPE_SV_GAMEINFO);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_CLIENTDROP:
			{
				protocol7::CNetMsg_Sv_ClientDrop *pIn = (protocol7::CNetMsg_Sv_ClientDrop *) pRaw;
				if(pIn->m_ClientID >= 0 && pIn->m_ClientID < MAX_CLIENTS7)
					m_aClients7[pIn->m_ClientID].Reset();
				if(pIn->m_Silent)
					return -1;
				CNetMsg_Sv_ClientDrop Out;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_pReason = pIn->m_pReason;
				CMsgPacker Packer(NETMSGTYPE_SV_CLIENTDROP);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_DE_CLIENTENTER:
			{
				protocol7::CNetMsg_De_ClientEnter *pIn = (protocol7::CNetMsg_De_ClientEnter *) pRaw;
				const int ClientID = pIn->m_ClientID;
				if(ClientID >= 0 && ClientID < MAX_CLIENTS7)
				{
					CClientState7 *pState = &m_aClients7[ClientID];
					pState->m_Active = true;
					pState->m_Team = pIn->m_Team;
					str_copy_fixed(pState->m_aName, pIn->m_pName, sizeof(pState->m_aName));
				}
				CNetMsg_Sv_ClientEnter Out;
				Out.m_ClientID = ClientID;
				CMsgPacker Packer(NETMSGTYPE_SV_CLIENTENTER);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_DE_CLIENTLEAVE:
			{
				protocol7::CNetMsg_De_ClientLeave *pIn = (protocol7::CNetMsg_De_ClientLeave *) pRaw;
				if(pIn->m_ClientID >= 0 && pIn->m_ClientID < MAX_CLIENTS7)
					m_aClients7[pIn->m_ClientID].Reset();
				CNetMsg_Sv_ClientDrop Out;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_pReason = pIn->m_pReason;
				CMsgPacker Packer(NETMSGTYPE_SV_CLIENTDROP);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_SKINCHANGE:
			{
				protocol7::CNetMsg_Sv_SkinChange *pIn = (protocol7::CNetMsg_Sv_SkinChange *) pRaw;
				const int ClientID = pIn->m_ClientID;
				if(ClientID >= 0 && ClientID < MAX_CLIENTS7)
				{
					CClientState7 *pState = &m_aClients7[ClientID];
					for(int p = 0; p < NUM_SKINPARTS; p++)
					{
						str_copy_fixed(pState->m_aaSkinPartNames[p], pIn->m_apSkinPartNames[p], sizeof(pState->m_aaSkinPartNames[p]));
						pState->m_aUseCustomColors[p] = pIn->m_aUseCustomColors[p];
						pState->m_aSkinPartColors[p] = pIn->m_aSkinPartColors[p];
					}
				}
				return -1; // absorbed into the TeeInfo snapshot object
			}
			case protocol7::NETMSGTYPE_SV_RACEFINISH:
			{
				protocol7::CNetMsg_Sv_RaceFinish *pIn = (protocol7::CNetMsg_Sv_RaceFinish *) pRaw;
				CNetMsg_Sv_RaceFinish Out;
				Out.m_ClientID = pIn->m_ClientID;
				Out.m_Time = pIn->m_Time;
				Out.m_Diff = pIn->m_Diff;
				Out.m_RecordPersonal = pIn->m_RecordPersonal;
				Out.m_RecordServer = pIn->m_RecordServer;
				CMsgPacker Packer(NETMSGTYPE_SV_RACEFINISH);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_CHECKPOINT:
			{
				protocol7::CNetMsg_Sv_Checkpoint *pIn = (protocol7::CNetMsg_Sv_Checkpoint *) pRaw;
				CNetMsg_Sv_Checkpoint Out;
				Out.m_Diff = pIn->m_Diff;
				CMsgPacker Packer(NETMSGTYPE_SV_CHECKPOINT);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_COMMANDINFO:
			{
				protocol7::CNetMsg_Sv_CommandInfo *pIn = (protocol7::CNetMsg_Sv_CommandInfo *) pRaw;
				CNetMsg_Sv_CommandInfo Out;
				Out.m_Name = pIn->m_Name;
				Out.m_ArgsFormat = pIn->m_ArgsFormat;
				Out.m_HelpText = pIn->m_HelpText;
				CMsgPacker Packer(NETMSGTYPE_SV_COMMANDINFO);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case protocol7::NETMSGTYPE_SV_COMMANDINFOREMOVE:
			{
				protocol7::CNetMsg_Sv_CommandInfoRemove *pIn = (protocol7::CNetMsg_Sv_CommandInfoRemove *) pRaw;
				CNetMsg_Sv_CommandInfoRemove Out;
				Out.m_Name = pIn->m_Name;
				CMsgPacker Packer(NETMSGTYPE_SV_COMMANDINFOREMOVE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
		}
		return -1;
	}

	int CNetworkTranslator::TranslateClientMsg(const void *pMsg8, int Size8, void *pOut, int OutSize)
	{
		CMsgUnpacker Unpacker(pMsg8, Size8);
		if(Unpacker.Error())
			return -2;
		const int MsgId8 = Unpacker.Type();

		static CNetObjHandler Handler8;
		void *pRaw = Handler8.SecureUnpackMsg(MsgId8, &Unpacker);
		if(!pRaw)
			return -2;

		switch(MsgId8)
		{
			case NETMSGTYPE_CL_SAY:
			{
				CNetMsg_Cl_Say *pIn = (CNetMsg_Cl_Say *) pRaw;
				protocol7::CNetMsg_Cl_Say Out;
				Out.m_Mode = pIn->m_Mode;
				Out.m_Target = pIn->m_Target;
				Out.m_pMessage = pIn->m_pMessage;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_SAY);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_KILL:
			{
				protocol7::CNetMsg_Cl_Kill Out;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_KILL);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_SETTEAM:
			{
				CNetMsg_Cl_SetTeam *pIn = (CNetMsg_Cl_SetTeam *) pRaw;
				protocol7::CNetMsg_Cl_SetTeam Out;
				Out.m_Team = pIn->m_Team;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_SETTEAM);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_SETSPECTATORMODE:
			{
				CNetMsg_Cl_SetSpectatorMode *pIn = (CNetMsg_Cl_SetSpectatorMode *) pRaw;
				protocol7::CNetMsg_Cl_SetSpectatorMode Out;
				Out.m_SpecMode = pIn->m_SpecMode;
				Out.m_SpectatorID = pIn->m_SpectatorID;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_SETSPECTATORMODE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_VOTE:
			{
				CNetMsg_Cl_Vote *pIn = (CNetMsg_Cl_Vote *) pRaw;
				protocol7::CNetMsg_Cl_Vote Out;
				Out.m_Vote = pIn->m_Vote;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_VOTE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_CALLVOTE:
			{
				CNetMsg_Cl_CallVote *pIn = (CNetMsg_Cl_CallVote *) pRaw;
				protocol7::CNetMsg_Cl_CallVote Out;
				Out.m_Type = pIn->m_Type;
				Out.m_Value = pIn->m_Value;
				Out.m_Reason = pIn->m_Reason;
				Out.m_Force = pIn->m_Force;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_CALLVOTE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_EMOTICON:
			{
				CNetMsg_Cl_Emoticon *pIn = (CNetMsg_Cl_Emoticon *) pRaw;
				protocol7::CNetMsg_Cl_Emoticon Out;
				Out.m_Emoticon = pIn->m_Emoticon;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_EMOTICON);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_SKINCHANGE:
			{
				CNetMsg_Cl_SkinChange *pIn = (CNetMsg_Cl_SkinChange *) pRaw;
				protocol7::CNetMsg_Cl_SkinChange Out;
				for(int p = 0; p < NUM_SKINPARTS; p++)
				{
					Out.m_apSkinPartNames[p] = pIn->m_apSkinPartNames[p];
					Out.m_aUseCustomColors[p] = pIn->m_aUseCustomColors[p];
					Out.m_aSkinPartColors[p] = pIn->m_aSkinPartColors[p];
				}
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_SKINCHANGE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_READYCHANGE:
			{
				protocol7::CNetMsg_Cl_ReadyChange Out;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_READYCHANGE);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_STARTINFO:
			{
				CNetMsg_Cl_StartInfo *pIn = (CNetMsg_Cl_StartInfo *) pRaw;
				protocol7::CNetMsg_Cl_StartInfo Out;
				Out.m_pName = pIn->m_pName;
				Out.m_pClan = pIn->m_pClan;
				Out.m_Country = pIn->m_Country;
				for(int p = 0; p < NUM_SKINPARTS; p++)
				{
					Out.m_apSkinPartNames[p] = pIn->m_apSkinPartNames[p];
					Out.m_aUseCustomColors[p] = pIn->m_aUseCustomColors[p];
					Out.m_aSkinPartColors[p] = pIn->m_aSkinPartColors[p];
				}
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_STARTINFO);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
			case NETMSGTYPE_CL_COMMAND:
			{
				CNetMsg_Cl_Command *pIn = (CNetMsg_Cl_Command *) pRaw;
				protocol7::CNetMsg_Cl_Command Out;
				Out.m_Name = pIn->m_Name;
				Out.m_Arguments = pIn->m_Arguments;
				CMsgPacker Packer(protocol7::NETMSGTYPE_CL_COMMAND);
				Out.Pack(&Packer);
				return FinishMsg(&Packer, pOut, OutSize);
			}
		}
		return -1;
	}

	int CNetworkTranslator::TranslateServerSnapshot(const void *pInSnap, int InSize, void *pOutSnap, int OutSize)
	{
		if(InSize < (int) sizeof(CSnapshot))
			return -1;
		const CSnapshot *pSnap7 = (const CSnapshot *) pInSnap;
		const int Snap8Size = BuildSnapshot8(pSnap7);
		if(Snap8Size < 0 || Snap8Size > OutSize)
			return -1;
		mem_copy(pOutSnap, m_Snap8Data.base_ptr(), Snap8Size);
		return Snap8Size;
	}

	int CNetworkTranslator::TranslateServerSnapshotMessages(const void *pInSnap, int InSize, CNetChunk *pOutChunks, int MaxOutChunks)
	{
		if(InSize < (int) sizeof(CSnapshot) || MaxOutChunks < 1)
			return 0;

		const CSnapshot *pSnap7 = (const CSnapshot *) pInSnap;
		for(int i = 0; i < pSnap7->NumItems(); i++)
		{
			const CSnapshotItem *pItem = pSnap7->GetItem(i);
			if(pItem->Type() != protocol7::NETOBJTYPE_DE_GAMEINFO)
				continue;
			if(pSnap7->GetItemSize(i) < (int) sizeof(protocol7::CNetObj_De_GameInfo))
				return 0;

			// rebuild it as the 0.7 Sv_GameInfo message and run it through the
			// normal message translation
			const protocol7::CNetObj_De_GameInfo *pIn = (const protocol7::CNetObj_De_GameInfo *) pItem->Data();
			protocol7::CNetMsg_Sv_GameInfo Msg;
			Msg.m_GameFlags = pIn->m_GameFlags;
			Msg.m_ScoreLimit = pIn->m_ScoreLimit;
			Msg.m_TimeLimit = pIn->m_TimeLimit;
			Msg.m_MatchNum = pIn->m_MatchNum;
			Msg.m_MatchCurrent = pIn->m_MatchCurrent;

			CMsgPacker Packer(protocol7::NETMSGTYPE_SV_GAMEINFO);
			Msg.Pack(&Packer);

			m_aServerOut[0].set_size(Packer.Size() + 4096);
			const int Written = TranslateServerMsg(Packer.Data(), Packer.Size(), m_aServerOut[0].base_ptr(), m_aServerOut[0].size());
			if(Written < 0)
				return 0;
			pOutChunks[0].m_ClientID = 0;
			pOutChunks[0].m_Flags = NETSENDFLAG_VITAL;
			pOutChunks[0].m_DataSize = Written;
			pOutChunks[0].m_pData = m_aServerOut[0].base_ptr();
			return 1;
		}
		return 0;
	}

	int CNetworkTranslator::TranslateServerChunk(const void *pInData, int InSize, CNetChunk *pOutChunks, int MaxOutChunks)
	{
		CMsgUnpacker Unpacker(pInData, InSize);
		if(Unpacker.Error())
			return 0;

		if(Unpacker.System())
		{
			const int MsgId = Unpacker.Type();
			if(MsgId == NETMSG_SNAP || MsgId == NETMSG_SNAPSINGLE || MsgId == NETMSG_SNAPEMPTY)
				return HandleSnapshot7(&Unpacker, MsgId, pOutChunks, MaxOutChunks);

			// A map change restarts the 0.7 tick domain and the server
			// resends the whole session state; keeping the old snapshot
			// history/baselines would alias the new ticks with stale ones.
			if(MsgId == NETMSG_MAP_CHANGE)
				Reset();

			// other system messages are frozen and pass through unchanged
			if(MaxOutChunks < 1)
				return 0;
			m_aServerOut[0].set_size(InSize);
			mem_copy(m_aServerOut[0].base_ptr(), pInData, InSize);
			pOutChunks[0].m_ClientID = 0;
			pOutChunks[0].m_Flags = NETSENDFLAG_VITAL;
			pOutChunks[0].m_DataSize = InSize;
			pOutChunks[0].m_pData = m_aServerOut[0].base_ptr();
			return 1;
		}

		if(MaxOutChunks < 1)
			return 0;
		m_aServerOut[0].set_size(InSize + 4096);
		const int Written = TranslateServerMsg(pInData, InSize, m_aServerOut[0].base_ptr(), m_aServerOut[0].size());
		if(Written < 0)
			return 0;
		pOutChunks[0].m_ClientID = 0;
		pOutChunks[0].m_Flags = NETSENDFLAG_VITAL;
		pOutChunks[0].m_DataSize = Written;
		pOutChunks[0].m_pData = m_aServerOut[0].base_ptr();
		return 1;
	}

	int CNetworkTranslator::TranslateClientChunk(const void *pInData, int InSize, int Flags, CNetChunk *pOutChunks, int MaxOutChunks)
	{
		if(MaxOutChunks < 1)
			return 0;

		// system message ids are frozen between 0.7 and 0.8, so NETMSG_READY /
		// NETMSG_ENTERGAME pass through unchanged
		CMsgUnpacker Unpacker(pInData, InSize);
		if(Unpacker.Error())
			return 0;
		if(Unpacker.System())
		{
			m_aClientOut[0].set_size(InSize);
			mem_copy(m_aClientOut[0].base_ptr(), pInData, InSize);
			pOutChunks[0].m_ClientID = 0;
			pOutChunks[0].m_Flags = Flags;
			pOutChunks[0].m_DataSize = InSize;
			pOutChunks[0].m_pData = m_aClientOut[0].base_ptr();
			return 1;
		}

		m_aClientOut[0].set_size(InSize + 4096);
		const int Written = TranslateClientMsg(pInData, InSize, m_aClientOut[0].base_ptr(), m_aClientOut[0].size());
		if(Written < 0)
			return 0;
		pOutChunks[0].m_ClientID = 0;
		pOutChunks[0].m_Flags = Flags;
		pOutChunks[0].m_DataSize = Written;
		pOutChunks[0].m_pData = m_aClientOut[0].base_ptr();
		return 1;
	}
} // namespace legacy
