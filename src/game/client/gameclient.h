/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef GAME_CLIENT_GAMECLIENT_H
#define GAME_CLIENT_GAMECLIENT_H

#include <base/tl/partial_array.h>
#include <base/vmath.h>
#include <engine/client.h>
#include <engine/console.h>
#include <game/gamecore.h>
#include <game/layers.h>
#include "render.h"
#include "ui.h"

class CGameClient : public IGameClient
{
	class CStack
	{
	public:
		enum
		{
			MAX_COMPONENTS = 64,
		};

		CStack();
		void Add(class CComponent *pComponent);

		class CComponent *m_apComponents[MAX_COMPONENTS];
		int m_Num;
	};

	CStack m_All;
	CStack m_Input;
	CNetObjHandler m_NetObjHandler;

	class IEngine *m_pEngine;
	class IInput *m_pInput;
	class IGraphics *m_pGraphics;
	class ITextRender *m_pTextRender;
	class IClient *m_pClient;
	class ISound *m_pSound;
	class CConfig *m_pConfig;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	class IDemoPlayer *m_pDemoPlayer;
	class IDemoRecorder *m_pDemoRecorder;
	class IServerBrowser *m_pServerBrowser;
	class IFriends *m_pFriends;
	class IBlacklist *m_pBlacklist;

	CLayers m_Layers;
	class CCollision m_Collision;
	CUI m_UI;

	void ProcessEvents();
	void ProcessTriggeredEvents(int Events, vec2 Pos);
	void UpdatePositions();

	int m_PredictedTick;
	int m_LastNewPredictedTick;

	int m_LastGameStartTick;
	int m_LastFlagCarrierRed;
	int m_LastFlagCarrierBlue;

	static void ConTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConKill(IConsole::IResult *pResult, void *pUserData);
	static void ConReadyChange(IConsole::IResult *pResult, void *pUserData);
	static void ConScreenshot(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSkinChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainFriendUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainBlacklistUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainXmasHatUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void EvolveCharacter(CNetObj_Character *pCharacter, int Tick);

	void LoadFonts();

	static void ScreenshotCallback(void *pUser, const char *pPath);

public:
	IKernel *Kernel() { return IInterface::Kernel(); }
	IEngine *Engine() const { return m_pEngine; }
	class IGraphics *Graphics() const { return m_pGraphics; }
	class IClient *Client() const { return m_pClient; }
	class CUI *UI() { return &m_UI; }
	class ISound *Sound() const { return m_pSound; }
	class IInput *Input() const { return m_pInput; }
	class IStorage *Storage() const { return m_pStorage; }
	class CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() { return m_pConsole; }
	class ITextRender *TextRender() const { return m_pTextRender; }
	class IDemoPlayer *DemoPlayer() const { return m_pDemoPlayer; }
	class IDemoRecorder *DemoRecorder() const { return m_pDemoRecorder; }
	class IServerBrowser *ServerBrowser() const { return m_pServerBrowser; }
	class CRenderTools *RenderTools() { return &m_RenderTools; }
	class CLayers *Layers() { return &m_Layers; }
	class CCollision *Collision() { return &m_Collision; }
	class IFriends *Friends() { return m_pFriends; }
	class IBlacklist *Blacklist() { return m_pBlacklist; }

	const char *NetobjFailedOn() { return m_NetObjHandler.FailedObjOn(); }
	int NetobjNumFailures() { return m_NetObjHandler.NumObjFailures(); }
	const char *NetmsgFailedOn() { return m_NetObjHandler.FailedMsgOn(); }

	bool m_SuppressEvents;

	// TODO: move this
	CTuningParams m_Tuning;

	enum
	{
		SERVERMODE_PURE = 0,
		SERVERMODE_MOD,
		SERVERMODE_PUREMOD,
	};
	int m_ServerMode;

	int m_DemoSpecMode;
	int m_DemoSpecID;

	vec2 m_LocalCharacterPos;

	// Whether we should use/render predicted entities. Depends on client
	// and game state.
	bool ShouldUsePredicted() const;

	// Whether we should use/render predictions for a specific `ClientID`.
	// Should check `ShouldUsePredicted` before checking this.
	bool ShouldUsePredictedChar(int ClientID) const;

	// Replaces `pPrevChar`, `pPlayerChar`, and `IntraTick` with their predicted
	// counterparts for `ClientID`. Should check `ShouldUsePredictedChar`
	// before using this.
	void UsePredictedChar(
		CNetObj_Character *pPrevChar,
		CNetObj_Character *pPlayerChar,
		float *IntraTick,
		int ClientID) const;

	vec2 GetCharPos(int ClientID, bool Predicted = false) const;

	// ---

	struct CPlayerInfoItem
	{
		const CNetObj_TeeInfo *m_pTeeInfo;
		int m_ClientID;
	};

	// snap pointers
	struct CSnapState
	{
		const CNetObj_Character *m_pLocalCharacter;
		const CNetObj_Character *m_pLocalPrevCharacter;
		const CNetObj_TeeInfo *m_pLocalInfo;
		const CNetObj_SpectatorInfo *m_pSpectatorInfo;
		const CNetObj_SpectatorInfo *m_pPrevSpectatorInfo;
		const CNetObj_Flag *m_apFlags[2];
		const CNetObj_GameData *m_pGameData;
		const CNetObj_GameDataTeam *m_pGameDataTeam;
		const CNetObj_GameDataFlag *m_pGameDataFlag;
		const CNetObj_GameDataRace *m_pGameDataRace;
		int m_GameDataFlagSnapID;

		int m_NotReadyCount;
		int m_AliveCount[NUM_TEAMS];

		// TeeID-indexed snapshot state lives in CGameClient, not here, because
		// CSnapState is cleared with mem_zero and those containers own storage.

		// spectate data
		struct CSpectateInfo
		{
			bool m_Active;
			int m_SpecMode;
			int m_SpectatorID;
			bool m_UsePosition;
			vec2 m_Position;
		} m_SpecInfo;

		//
		struct CCharacterInfo
		{
			bool m_Active;

			// snapshots
			CNetObj_Character m_Prev;
			CNetObj_Character m_Cur;

			// interpolated position
			vec2 m_Position;
		};
	};

	CSnapState m_Snap;

	// client data
	struct CClientData
	{
		char m_aName[MAX_NAME_ARRAY_SIZE];
		char m_aClan[MAX_CLAN_ARRAY_SIZE];
		int m_Country;
		char m_aaSkinPartNames[NUM_SKINPARTS][MAX_SKIN_ARRAY_SIZE];
		int m_aUseCustomColors[NUM_SKINPARTS];
		int m_aSkinPartColors[NUM_SKINPARTS];
		int m_SkinPartIDs[NUM_SKINPARTS];
		int m_Team;
		int m_Emoticon;
		int m_EmoticonStart;
		CCharacterCore m_Predicted;
		// Snapshotted from m_Predicted just before the final predicted tick, so
		// the two bracket [PredGameTick - 1, PredGameTick], the interval
		// PredIntraGameTick interpolates over. Applying that tick's input only
		// to m_Predicted is why the two can disagree about the hook state.
		CCharacterCore m_PrevPredicted;

		CTeeRenderInfo m_SkinInfo; // this is what the server reports
		CTeeRenderInfo m_RenderInfo; // this is what we use

		CNetObj_Character m_Evolved;

		float m_Angle;
		// A player exists as soon as its TeeInfo appears in a snapshot;
		// Sv_ClientEnter is only an early hint and Sv_ClientDrop clears it.
		bool m_Active;
		bool m_ChatIgnore;
		bool m_Friend;

		// TeeInfoID of this Tee. Real clients use [0, MAX_CLIENTS), bots the
		// sparse [MAX_CLIENTS, MAX_TEES) range.
		int m_TeeInfoID;

		// Whether this entry holds a real identity. A whole part is handed out
		// at once, so an entry can be addressable while still uninitialised,
		// and m_TeeInfoID defaults to 0 so it cannot tell the two apart.
		bool m_Initialized;

		void UpdateRenderInfo(CGameClient *pGameClient, int ClientID, bool UpdateSkinInfo);
		void UpdateBotRenderInfo(CGameClient *pGameClient, const CNetObj_TeeInfo *pTeeInfo);
		void Reset(CGameClient *pGameClient, int CLientID);
	};

	// Every array indexed by a TeeID has to cover the whole id space, because
	// TeeIDs are not limited to MAX_CLIENTS: bots occupy the sparse
	// [MAX_CLIENTS, MAX_TEES) range and ids arrive straight from the network
	// (TeeInfo, Character, Sv_KillMsg, Sv_Emoticon, Sv_RaceFinish, ...).
	// partial_array keeps that affordable by allocating a part only once an
	// index inside it is touched.
	enum
	{
		TEE_PART_SIZE = 1024,
		TEE_PARTS = 64,
	};
	static_assert(TEE_PARTS * TEE_PART_SIZE >= MAX_TEES, "TeeID storage must cover the whole TeeID space");

	typedef partial_array<CClientData, TEE_PARTS, TEE_PART_SIZE> CTeeClientDataArray;
	typedef partial_array<CSnapState::CCharacterInfo, TEE_PARTS, TEE_PART_SIZE> CTeeCharacterArray;
	// The snapshot owns the TeeInfo objects, so only the pointers are borrowed.
	typedef partial_array<const CNetObj_TeeInfo *, TEE_PARTS, TEE_PART_SIZE, allocator_non_owning<const CNetObj_TeeInfo *>> CTeeInfoArray;

	CTeeClientDataArray m_aClients;
	CTeeCharacterArray m_aCharacters;
	CTeeInfoArray m_apTeeInfos;

	// Player infos ordered by score, rebuilt every snapshot. Dense on purpose:
	// the scoreboard and HUD walk it front to back, and TeeIDs are too sparse
	// to walk directly.
	array<CPlayerInfoItem> m_aInfoByScore;

	// TeeInfoIDs seen in the current snapshot, for iteration.
	array<int> m_aTeeIDs;

	// TeeInfoIDs that had an identity when the current snapshot started, i.e.
	// the tees of the previous one. m_aTeeIDs is cleared and refilled while the
	// snapshot is being read, so it cannot answer which ids went away; this is
	// the set that gets marked inactive first and then expired.
	//
	// Both real clients and bots live here. Nothing distinguishes them: a tee
	// exists exactly as long as its TeeInfo is being snapped, which is the same
	// rule the server follows and the only one that also covers a bot's display
	// id being recycled to a different bot.
	array<int> m_aKnownTeeIDs;

	// Returns the identity data for any TeeInfoID, real client or bot, or 0
	// when no Tee with that id is known. Never allocates.
	CClientData *GetClientData(int TeeInfoID);
	const CClientData *GetClientData(int TeeInfoID) const;

	// Whether an identity actually exists for TeeInfoID. The slot merely being
	// addressable is not enough: a part is handed out as a whole, so the part
	// holding real clients also covers every bot id in it, still empty.
	bool HasClientData(int TeeInfoID) const;

	// Same, but creates the identity on first use. Unlike GetClientData this
	// allocates the part holding TeeInfoID, so it needs a checked id. Only the
	// TeeInfo pass calls it, which is also what records the id in m_aTeeIDs; an
	// identity created without that would never be marked inactive or expired.
	CClientData *GetOrCreateClientData(int TeeInfoID);

	// Drops the identity of the tee at Index in m_aKnownTeeIDs. Takes the index
	// because callers already have it, and re-finding it by id would make
	// expiring many tees quadratic.
	void RemoveIdentity(int Index);

	// Returns the snapshot Character state for any TeeInfoID, or 0.
	CSnapState::CCharacterInfo *GetCharacterInfo(int TeeInfoID);
	const CSnapState::CCharacterInfo *GetCharacterInfo(int TeeInfoID) const;

	// Same, but creates the Character state on first use.
	CSnapState::CCharacterInfo *GetOrCreateCharacterInfo(int TeeInfoID);

	// Returns the TeeInfo snapshot object for any TeeInfoID, or 0.
	const CNetObj_TeeInfo *GetTeeInfo(int TeeInfoID) const;

	// Appends the TeeInfoID of every Tee with an active Character in the
	// current snapshot, real clients and bots alike.
	void CollectActiveTeeIDs(array<int> &IDs) const;

	int m_LocalClientID;
	int m_TeamCooldownTick;
	float m_TeamChangeTime;
	bool m_IsXmasDay;
	float m_LastSkinChangeTime;
	int m_IdentityState;
	bool m_IsEasterDay;
	bool m_InitComplete;

	struct CGameInfo
	{
		int m_GameFlags;
		int m_ScoreLimit;
		int m_TimeLimit;
		int m_MatchNum;
		int m_MatchCurrent;

		int m_NumPlayers;
		int m_aTeamSize[NUM_TEAMS];
	};

	CGameInfo m_GameInfo;

	struct CServerSettings
	{
		bool m_KickVote;
		int m_KickMin;
		bool m_SpecVote;
		bool m_TeamLock;
		bool m_TeamBalance;
		int m_PlayerSlots;
		bool m_AllowSpecVoting;
	} m_ServerSettings;

	CRenderTools m_RenderTools;

	void OnReset();
	void OnSoundLoaded();

	// hooks
	virtual void OnConnected() override;
	virtual void OnRender() override;
	virtual void OnUpdate() override;
	virtual void OnRelease();
	virtual void OnInit() override;
	virtual void OnConsoleInit() override;
	virtual void OnStateChange(int NewState, int OldState) override;
	virtual void OnMapUnload() override;
	virtual void OnMessage(int MsgId, CUnpacker *pUnpacker) override;
	virtual void OnNewSnapshot() override;
	virtual void OnDemoRecSnap() override;
	virtual void OnPredict() override;
	virtual void OnDemoRecorderStart() override;
	virtual int OnSnapInput(int *pData) override;
	virtual void OnShutdown() override;
	virtual void OnEnterGame() override;
	virtual void OnRconLine(const char *pLine) override;
	virtual void OnGameOver();
	virtual void OnStartGame();

	virtual const char *GetItemName(int Type) const override;
	virtual const char *Version() const override;
	virtual const char *NetVersion() const override;
	virtual const char *NetVersionHashUsed() const override;
	virtual const char *NetVersionHashReal() const override;
	virtual int ClientVersion() const override;
	virtual int GetNumPlayers() const override;
	void GetPlayerLabel(char *aBuf, int BufferSize, int ClientID, const char *ClientName);
	void StartRendering();

	bool IsXmas() const;
	bool IsEaster() const;
	int RacePrecision() const { return m_Snap.m_pGameDataRace ? m_Snap.m_pGameDataRace->m_Precision : 3; }
	bool IsWorldPaused() const { return m_Snap.m_pGameData && (m_Snap.m_pGameData->m_GameStateFlags & (GAMESTATEFLAG_PAUSED | GAMESTATEFLAG_ROUNDOVER | GAMESTATEFLAG_GAMEOVER)); }
	bool IsDemoPlaybackPaused() const;
	float GetAnimationPlaybackSpeed() const;

	//
	void DoEnterMessage(const char *pName, int ClientID, int Team);
	void DoLeaveMessage(const char *pName, int ClientID, const char *pReason);
	void DoTeamChangeMessage(const char *pName, int ClientID, int Team);

	int GetClientID(const char *pName);
	int GetRealClientID(int SnapClientID);

	// ----- gamedata prediction helper -----
	bool GameDataPredictInput() { return !m_Snap.m_pGameData || m_Snap.m_pGameData->m_PredictionFlags & GAMEPREDICTIONFLAG_INPUT; }
	bool GameDataPredictEvent() { return !m_Snap.m_pGameData || m_Snap.m_pGameData->m_PredictionFlags & GAMEPREDICTIONFLAG_EVENT; }

	// ----- send functions -----
	// TODO: move these
	void SendSwitchTeam(int Team);
	void SendStartInfo();
	void SendKill();
	void SendReadyChange();
	void SendSkinChange();

	void CopyScreenshot(const char *pPath);
	// pointers to all systems
	class CGameConsole *m_pGameConsole;
	class CBinds *m_pBinds;
	class CBroadcast *m_pBroadcast;
	class CParticles *m_pParticles;
	class CMenus *m_pMenus;
	class CSkins *m_pSkins;
	class CCountryFlags *m_pCountryFlags;
	class CFlow *m_pFlow;
	class CChat *m_pChat;
	class CDamageInd *m_pDamageind;
	class CCamera *m_pCamera;
	class CControls *m_pControls;
	class CEffects *m_pEffects;
	class CSounds *m_pSounds;
	class CMotd *m_pMotd;
	class CMapComponent *m_pMapComponent;
	class CVoting *m_pVoting;
	class CScoreboard *m_pScoreboard;
	class CStats *m_pStats;
	class CItems *m_pItems;
};

void FormatTime(char *pBuf, int Size, int Time, int Precision);
void FormatTimeDiff(char *pBuf, int Size, int Time, int Precision, bool ForceSign = true);

const char *Localize(const char *pStr, const char *pContext = "")
	GNUC_ATTRIBUTE((format_arg(1)));

#endif
