/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_LEGACY_NETWORK_TRANSLATOR_H
#define ENGINE_SHARED_LEGACY_NETWORK_TRANSLATOR_H

#include <base/tl/array.h>
#include <base/uuid.h>

#include <engine/shared/network.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>

class CPacker;
class CUnpacker;

/*
	Translates a 0.7 peer's game traffic into the 0.8 world and back.

	The translator owns the 0.7 snapshot history (for decoding incoming deltas)
	and the 0.8 baseline (for encoding outgoing deltas). It never touches the
	frozen 0.7 handshake.

	Chunk output points into translator-owned storage that stays valid until the
	next call to the same method.
*/
namespace legacy
{
	class CNetworkTranslator
	{
	public:
		enum
		{
			// A 0.7 server never has more than this many clients.
			MAX_CLIENTS7 = 64,
			// CTuningParams has this many parameters; the 0.8 Tuning object uses
			// the same count (guarded by a static_assert in gamecore.cpp).
			NUM_TUNES = 32,
			// Upper bound on chunks a single input chunk can translate into.
			MAX_OUT_CHUNKS = 256,
		};

		// Identity of one 0.7 client, captured from Sv_ClientInfo (network) or
		// De_ClientInfo (demo). Names/skins are raw strings without a
		// terminator, matching the 0.8 TeeInfo fields one to one, so
		// BuildSnapshot8 can copy them verbatim.
		class CClientState7
		{
		public:
			CClientState7() { Reset(); }
			void Reset();

			bool m_Active;
			int m_Local;
			int m_Team;
			int m_Country;
			char m_aName[MAX_NAME_ARRAY_SIZE - 1];
			char m_aClan[MAX_CLAN_ARRAY_SIZE - 1];
			char m_aaSkinPartNames[6][MAX_SKIN_ARRAY_SIZE - 1];
			int m_aUseCustomColors[6];
			int m_aSkinPartColors[6];
		};

	private:
		enum
		{
			MAX_UUID_OBJECTS = 8,
			UUID_KIND_NONE = 0,
			UUID_KIND_GAMEDATAPREDICTION,
			UUID_KIND_PLAYERINFOEXTRA,
		};

		struct CUuidObject
		{
			int m_SyntheticType;
			int m_Kind;
		};

		CClientState7 m_aClients7[MAX_CLIENTS7];

		// Last tuning captured from Sv_TuneParams / De_TuneParams.
		bool m_TuningValid;
		int m_aTuneParams7[NUM_TUNES];

		CSnapshotDelta m_Delta7; // configured with the 0.7 object sizes
		CSnapshotDelta m_Delta8; // configured with the 0.8 object sizes
		CSnapshotStorage m_Snapshots7;
		CSnapshot m_EmptySnap;

		array<unsigned char> m_Snap7Data; // scratch for the decoded 0.7 snapshot
		array<unsigned char> m_Snap8Data; // scratch for the rebuilt 0.8 snapshot
		array<unsigned char> m_Base8Data; // stored 0.8 baseline
		CSnapshot *m_pBase8;
		int m_BaseTick8;

		Uuid m_UuidGameDataPrediction;
		Uuid m_UuidPlayerInfoExtra;

		CUuidObject m_aUuidObjects[MAX_UUID_OBJECTS];
		int m_NumUuidObjects;

		// Reassembly of multi-part 0.7 snapshots.
		array<unsigned char> m_Incoming7Data;
		unsigned char m_aParts7[CSnapshot::MAX_PARTS / 8];
		int m_NumParts7;
		int m_CurrentRecvTick7;
		int m_LastPartSize7;

		// Chunk output storage, one buffer per output chunk.
		array<unsigned char> m_aServerOut[MAX_OUT_CHUNKS];
		array<unsigned char> m_aClientOut[MAX_OUT_CHUNKS];

		// --- internals ---
		void BuildUuidMap(const CSnapshot *pSnapshot);
		int UuidObjectKind(int Type) const;
		void AbsorbClientInfo(int ClientID, int Local, int Team, int Country,
			const char *pName, const char *pClan, const char *const *papSkinPartNames,
			const int *pUseCustomColors, const int *pSkinPartColors);
		void AbsorbDeClientInfo(int ClientID, const CSnapshotItem *pItem);
		int BuildSnapshot8(const CSnapshot *pSnap7);
		void SetBase8(int GameTick, int Snap8Size);
		void ResetSnapshotParts();

		// Message-level translation. Returns bytes written to pOut, -1 if the
		// message is dropped, -2 on error.
		int TranslateServerMsg(const void *pMsg7, int Size7, void *pOut, int OutSize);
		int TranslateClientMsg(const void *pMsg8, int Size8, void *pOut, int OutSize);

		// Snapshot delta translation. Returns the 0.8 delta size, -1 if it could
		// not be decoded, or a negative delta error code.
		int TranslateSnapshotDelta(int GameTick, int DeltaTick, const void *pDelta7, int DeltaSize7, void *pOut, int OutSize);

		// Emits 0.8 snapshot chunks for an already translated delta.
		int EmitSnapshot8(int GameTick, int DeltaField, int DeltaSize8, const void *pDelta8, CNetChunk *pOutChunks, int MaxOutChunks);

		// Handles a 0.7 NETMSG_SNAP/SNAPSINGLE/SNAPEMPTY chunk.
		int HandleSnapshot7(CUnpacker *pUnpacker, int MsgId, CNetChunk *pOutChunks, int MaxOutChunks);
		int TranslateCompleteSnapshot7(int GameTick, int DeltaTick, int CompleteSize, CNetChunk *pOutChunks, int MaxOutChunks);
		// An empty 0.7 delta means the snapshot at GameTick is identical to
		// the one at DeltaTick; mirrors that tick into the 0.7 snapshot
		// history so later deltas against it can be decoded.
		void StoreSnapshot7Copy(int GameTick, int DeltaTick);

	public:
		CNetworkTranslator();
		~CNetworkTranslator();

		// Reset all per-session state (baselines, 0.7 client table, tune params).
		void Reset();

		// --- server/demo -> 0.8 client -------------------------------------
		// Translate one raw 0.7 chunk (message or snapshot payload, as delivered
		// by CNetClient::Recv) into 0.8 chunks.
		//   pInData/pInSize: the 0.7 chunk payload (a packed message starting with
		//                    its msg id, or a snapshot delta payload).
		//   pOutChunks:      caller-provided array of at least MaxOutChunks chunks.
		//   Returns the number of 0.8 chunks written (0 = absorbed/dropped).
		int TranslateServerChunk(const void *pInData, int InSize, CNetChunk *pOutChunks, int MaxOutChunks);

		// --- 0.8 client -> 0.7 server --------------------------------------
		// Translate one 0.8 chunk produced by the client into 0.7 chunks.
		//   Flags: the NETSENDFLAG_* the chunk was queued with; they are
		//          propagated to the output chunks unchanged. Delivery
		//          semantics must survive translation: NETMSG_INPUT etc. are
		//          sent non-vital, and forcing them vital would make the net
		//          layer resend every input reliably.
		//   Same contract as above otherwise.
		int TranslateClientChunk(const void *pInData, int InSize, int Flags, CNetChunk *pOutChunks, int MaxOutChunks);

		// Translate a fully reconstructed 0.7 snapshot (as produced by the demo
		// player's CSnapshotDelta::UnpackDelta) into a full 0.8 snapshot.
		// Writes at most OutSize bytes to pOutSnap and returns the number of
		// bytes written, or -1 if it does not fit / fails. It does not require a
		// prior baseline.
		int TranslateServerSnapshot(const void *pInSnap, int InSize, void *pOutSnap, int OutSize);

		// Messages implied by a 0.7 snapshot. 0.7 demos carry the game info as
		// the De_GameInfo snapshot object, while 0.8 reads it from the
		// Sv_GameInfo message, so the demo path must emit it explicitly.
		// Returns the number of chunks written to pOutChunks.
		int TranslateServerSnapshotMessages(const void *pInSnap, int InSize, CNetChunk *pOutChunks, int MaxOutChunks);

		// --- inspection (tests / integration) ------------------------------
		const CClientState7 *ClientState(int ClientID) const;
		bool TuningValid() const { return m_TuningValid; }
		const int *TuneParams() const { return m_aTuneParams7; }
		// The full 0.8 snapshot emitted by the last translated snapshot.
		const CSnapshot *BaseSnapshot8() const { return m_pBase8; }
		int BaseTick8() const { return m_BaseTick8; }
		void ResetBase8();
	};
} // namespace legacy

#endif // ENGINE_SHARED_LEGACY_NETWORK_TRANSLATOR_H
