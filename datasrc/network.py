from datatypes import *

Pickups = Enum("PICKUP", ["HEALTH", "ARMOR", "GRENADE", "SHOTGUN", "LASER", "NINJA", "GUN", "HAMMER"])
Emotes = Enum("EMOTE", ["NORMAL", "PAIN", "HAPPY", "SURPRISE", "ANGRY", "BLINK"])
Emoticons = Enum("EMOTICON", ["OOP", "EXCLAMATION", "HEARTS", "DROP", "DOTDOT", "MUSIC", "SORRY", "GHOST", "SUSHI", "SPLATTEE", "DEVILTEE", "ZOMG", "ZZZ", "WTF", "EYES", "QUESTION"])
Votes = Enum("VOTE", ["START_OP", "START_KICK", "START_SPEC", "RUN_OP", "RUN_KICK", "RUN_SPEC", "END_ABORT", "END_PASS", "END_FAIL"])
ChatModes = Enum("CHAT", ["NONE", "ALL", "TEAM", "WHISPER"])

TeeFlags = Flags("TEEFLAG", ["ADMIN", "CHATTING", "SCOREBOARD", "READY", "DEAD", "WATCHING", "BOT", "HIDDEN_IN_BOARD", "LOCAL"])
GameFlags = Flags("GAMEFLAG", ["TEAMS", "FLAGS", "SURVIVAL", "RACE"])
GameStateFlags = Flags("GAMESTATEFLAG", ["WARMUP", "SUDDENDEATH", "ROUNDOVER", "GAMEOVER", "PAUSED", "STARTCOUNTDOWN"])
CoreEventFlags = Flags("COREEVENTFLAG", ["GROUND_JUMP", "AIR_JUMP", "HOOK_ATTACH_PLAYER", "HOOK_ATTACH_GROUND", "HOOK_HIT_NOHOOK"])
RaceFlags = Flags("RACEFLAG", ["HIDE_KILLMSG", "FINISHMSG_AS_CHAT", "KEEP_WANTED_WEAPON"])

GameMsgIDs = Enum("GAMEMSG", ["TEAM_SWAP", "SPEC_INVALID_ID", "TEAM_SHUFFLE", "TEAM_BALANCE", "CTF_DROP", "CTF_RETURN",

							"TEAM_ALL", "TEAM_BALANCE_VICTIM", "CTF_GRAB",

							"CTF_CAPTURE",

							"GAME_PAUSED",
                            
							"GAME_CANCELLED"])

GamePredictionFlags = Flags("GAMEPREDICTIONFLAG", ["EVENT", "INPUT"])

# Number of tuning parameters in CTuningParams. Kept in sync with src/game/tuning.h
# by a static_assert in src/game/gamecore.cpp.
NUM_TUNES = 32

RawHeader = '''

#include <engine/message.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex.h>

enum
{
	INPUT_STATE_MASK=0x3f
};

enum
{
	TEAM_SPECTATORS=-1,
	TEAM_RED,
	TEAM_BLUE,
	NUM_TEAMS,

	FLAG_MISSING=-3,
	FLAG_ATSTAND,
	FLAG_TAKEN,

	SPEC_FREEVIEW=0,
	SPEC_PLAYER,
	SPEC_FLAGRED,
	SPEC_FLAGBLUE,
	NUM_SPECMODES,

	SKINPART_BODY = 0,
	SKINPART_MARKING,
	SKINPART_DECORATION,
	SKINPART_HANDS,
	SKINPART_FEET,
	SKINPART_EYES,
	NUM_SKINPARTS,

	VOTE_CHOICE_NO = -1,
	VOTE_CHOICE_PASS = 0,
	VOTE_CHOICE_YES = 1
};
'''

RawSource = '''
#include <engine/message.h>
#include "protocol.h"
'''

Enums = [
	Pickups,
	Emotes,
	Emoticons,
	Votes,
	ChatModes,
	GameMsgIDs,
]

Flags = [
	TeeFlags,
	GameFlags,
	GameStateFlags,
	CoreEventFlags,
	RaceFlags,
    GamePredictionFlags,
]

Objects = [

	NetObject("PlayerInput", [
		NetIntRange("m_Direction", -1, 1),
		NetIntAny("m_TargetX"),
		NetIntAny("m_TargetY"),

		NetBool("m_Jump"),
		NetIntAny("m_Fire"),
		NetBool("m_Hook"),

		NetFlag("m_PlayerFlags", TeeFlags),

		# 0 means "no wanted weapon", `1+weapon` means that `weapon` is wanted,
		# and ninja is not a valid wanted weapon.
		NetIntRange("m_WantedWeapon", 0, 'NUM_WEAPONS-1'),
		NetIntAny("m_NextWeapon"),
		NetIntAny("m_PrevWeapon"),
	]),

	NetObject("Projectile", [
		NetIntAny("m_X"),
		NetIntAny("m_Y"),
		NetIntAny("m_VelX"),
		NetIntAny("m_VelY"),

		NetIntRange("m_Type", 0, 'NUM_WEAPONS-1'),
		NetTick("m_StartTick"),
	]),

	NetObject("Laser", [
		NetIntAny("m_X"),
		NetIntAny("m_Y"),
		NetIntAny("m_FromX"),
		NetIntAny("m_FromY"),

		NetTick("m_StartTick"),
	]),

	NetObject("Pickup", [
		NetIntAny("m_X"),
		NetIntAny("m_Y"),

		NetEnum("m_Type", Pickups),
	]),

	NetObject("Flag", [
		NetIntAny("m_X"),
		NetIntAny("m_Y"),

		NetIntRange("m_Team", 'TEAM_RED', 'TEAM_BLUE')
	]),

	# GameData now also carries the prediction flags that used to live in the
	# UUID object GameDataPrediction.
	NetObject("GameData", [
		NetTick("m_GameStartTick"),
		NetFlag("m_GameStateFlags", GameStateFlags),
		NetTick("m_GameStateEndTick"),
		NetFlag("m_PredictionFlags", GamePredictionFlags),
	]),

	NetObject("GameDataTeam", [
		NetIntAny("m_TeamscoreRed"),
		NetIntAny("m_TeamscoreBlue"),
	]),

	NetObject("GameDataFlag", [
		NetIntRange("m_FlagCarrierRed", 'FLAG_MISSING', 'MAX_TEES-1'),
		NetIntRange("m_FlagCarrierBlue", 'FLAG_MISSING', 'MAX_TEES-1'),
		NetTick("m_FlagDropTickRed"),
		NetTick("m_FlagDropTickBlue"),
	]),

	NetObject("CharacterCore", [
		NetTick("m_Tick"),
		NetIntAny("m_X"),
		NetIntAny("m_Y"),
		NetIntAny("m_VelX"),
		NetIntAny("m_VelY"),

		NetIntAny("m_Angle"),
		NetIntRange("m_Direction", -1, 1),

		NetIntRange("m_Jumped", 0, 3),
		NetIntRange("m_HookedPlayer", -1, 'MAX_TEES-1'),
		NetIntRange("m_HookState", -1, 5),
		NetTick("m_HookTick"),

		NetIntAny("m_HookX"),
		NetIntAny("m_HookY"),
		NetIntAny("m_HookDx"),
		NetIntAny("m_HookDy"),
	]),

	NetObject("Character:CharacterCore", [
		NetIntRange("m_Health", 0, 'max_int'),
		NetIntRange("m_Armor", 0, 'max_int'),
		NetIntRange("m_MaxHealth", 1, 'max_int'),
		NetIntRange("m_MaxArmor", 1, 'max_int'),
		NetIntAny("m_AmmoCount"),
		NetIntRange("m_Weapon", -1, 'NUM_WEAPONS-1'),
		NetEnum("m_Emote", Emotes),
		NetTick("m_AttackTick"),
		NetFlag("m_TriggeredEvents", CoreEventFlags),
	]),

	# Identity layer. There is no back-reference to a client slot: the item ID
	# *is* the TeeInfoID, and [0, MAX_CLIENTS) is a real client while
	# [MAX_CLIENTS, MAX_TEES) is a bot.
	NetObject("TeeInfo", [
		NetIntAny("m_LatencyAndCountry"), # high 16 bits latency, low 16 bits country
		NetIntRange("m_Team", 'TEAM_SPECTATORS', 'TEAM_BLUE'),
		NetFlag("m_Flag", TeeFlags),
		NetIntAny("m_Score"),
		NetIntRange("m_RaceStartTick", -1, 'max_int'),

		# Identity strings travel as plain character arrays holding the raw
		# content without a terminator: the local buffer needs the extra '\0'
		# of MAX_*_ARRAY_SIZE, the wire field does not, so it is one byte
		# smaller and holds the full content.
		NetChar("m_aName", "MAX_NAME_ARRAY_SIZE - 1"),
		NetChar("m_aClan", "MAX_CLAN_ARRAY_SIZE - 1"),

		NetArray(NetChar("m_aaSkinPartNames", "MAX_SKIN_ARRAY_SIZE - 1"), 6),
		NetArray(NetBool("m_aUseCustomColors"), 6),
		NetArray(NetIntAny("m_aSkinPartColors"), 6),
	]),

	NetObject("SpectatorInfo", [
		NetIntRange("m_SpecMode", 0, 'NUM_SPECMODES-1'),
		NetIntRange("m_SpectatorID", -1, 'MAX_TEES-1'),
		NetIntAny("m_X"),
		NetIntAny("m_Y"),
	]),

	## Race
	NetObject("GameDataRace", [
		NetIntRange("m_BestTime", -1, 'max_int'),
		NetIntRange("m_Precision", 0, 3),
		NetFlag("m_RaceFlags", RaceFlags),
	]),

	## Tuning (singleton, item id 0). Replaces the Sv_TuneParams message and the
	## De_TuneParams demo object: tuning travels in the snapshot and is therefore
	## delta compressed and recorded for free.
	NetObject("Tuning", [
		NetArray(NetIntAny("m_aTuneParams"), NUM_TUNES),
	]),

	## Events

	NetEvent("Common", [
		NetIntAny("m_X"),
		NetIntAny("m_Y"),
	]),


	NetEvent("Explosion:Common", []),
	NetEvent("Spawn:Common", []),
	NetEvent("HammerHit:Common", []),

	NetEvent("Death:Common", [
		NetIntRange("m_ClientID", 0, 'MAX_TEES-1'),
	]),

	NetEvent("SoundWorld:Common", [
		NetIntRange("m_SoundID", 0, 'NUM_SOUNDS-1'),
	]),

	NetEvent("SoundGlobal", [
		NetIntRange("m_SoundID", 0, 'NUM_SOUNDS-1')
	]),

	NetEvent("Damage:Common", [
		NetIntRange("m_ClientID", 0, 'MAX_TEES-1'),
		NetIntAny("m_Angle"),
		NetIntRange("m_HealthAmount", 0, 9),
		NetIntRange("m_ArmorAmount", 0, 9),
		NetBool("m_Self"),
	]),
]

Messages = [

	### Server messages

	# -- server info / session --
	NetMessage("Sv_Motd", [
		NetString("m_pMessage"),
	]),

	NetMessage("Sv_Broadcast", [
		NetString("m_pMessage"),
	]),

	NetMessage("Sv_ServerSettings", [
		NetBool("m_KickVote"),
		NetIntRange("m_KickMin", 0, 'MAX_CLIENTS'),
		NetBool("m_SpecVote"),
		NetBool("m_TeamLock"),
		NetBool("m_TeamBalance"),
		NetIntRange("m_PlayerSlots", 0, 'MAX_CLIENTS'),
        NetBool("m_AllowSpecVoting", default=False),
	]),

	NetMessage("Sv_GameInfo", [
		NetFlag("m_GameFlags", GameFlags),

		NetIntRange("m_ScoreLimit", 0, 'max_int'),
		NetIntRange("m_TimeLimit", 0, 'max_int'),

		NetIntRange("m_MatchNum", 0, 'max_int'),
		NetIntRange("m_MatchCurrent", 0, 'max_int'),
	]),

	NetMessage("Sv_GameMsg", []),

	# Slimmed down: identity now lives in the TeeInfo snapshot object, including
	# the local marker (TEEFLAG_LOCAL) and everything the join/leave chat
	# line needs. This message only announces that a client slot entered.
	NetMessage("Sv_ClientEnter", [
		NetIntRange("m_ClientID", 0, 'MAX_CLIENTS-1'),
	]),

	NetMessage("Sv_ClientDrop", [
		NetIntRange("m_ClientID", 0, 'MAX_CLIENTS-1'),
		NetStringStrict("m_pReason"),
	]),

	NetMessage("Sv_ReadyToEnter", []),

	# -- chat / kill --
	NetMessage("Sv_Chat", [
		NetIntRange("m_Mode", 0, 'NUM_CHATS-1'),
		NetIntRange("m_ClientID", -1, 'MAX_TEES-1'),
		NetIntRange("m_TargetID", -1, 'MAX_TEES-1'),
		NetStringStrict("m_pMessage"),
	]),

	NetMessage("Sv_KillMsg", [
		NetIntRange("m_Killer", -2, 'MAX_TEES-1'),
		NetIntRange("m_Victim", 0, 'MAX_TEES-1'),
		NetIntRange("m_Weapon", -3, 'NUM_WEAPONS-1'),
		NetIntAny("m_ModeSpecial"),
		NetIntRange("m_Assist", -1, 'MAX_TEES-1', default=-1),
	]),

	# -- team / spectator --
	NetMessage("Sv_Team", [
		NetIntRange("m_ClientID", -1, 'MAX_TEES-1'),
		NetIntRange("m_Team", 'TEAM_SPECTATORS', 'TEAM_BLUE'),
		NetBool("m_Silent"),
		NetTick("m_CooldownTick"),
	]),

	# -- voting --
	NetMessage("Sv_VoteClearOptions", []),

	NetMessage("Sv_VoteOptionListAdd", []),

	NetMessage("Sv_VoteOptionAdd", [
		NetStringStrict("m_pDescription"),
	]),

	NetMessage("Sv_VoteOptionRemove", [
		NetStringStrict("m_pDescription"),
	]),

	NetMessage("Sv_VoteSet", [
		NetIntRange("m_ClientID", -1, 'MAX_TEES-1'),
		NetEnum("m_Type", Votes),
		NetIntRange("m_Timeout", 0, 60),
		NetStringStrict("m_pDescription"),
		NetStringStrict("m_pReason"),
	]),

	NetMessage("Sv_VoteStatus", [
		NetIntRange("m_Yes", 0, 'MAX_TEES'),
		NetIntRange("m_No", 0, 'MAX_TEES'),
		NetIntRange("m_Pass", 0, 'MAX_TEES'),
		NetIntRange("m_Total", 0, 'MAX_TEES'),
	]),

	# -- presentation / misc --
	NetMessage("Sv_WeaponPickup", [
		NetIntRange("m_Weapon", 0, 'NUM_WEAPONS-1'),
	]),

	NetMessage("Sv_Emoticon", [
		NetIntRange("m_ClientID", 0, 'MAX_TEES-1'),
		NetEnum("m_Emoticon", Emoticons),
	]),

	# -- race (moved into the main table) --
	NetMessage("Sv_RaceFinish", [
		NetIntRange("m_ClientID", 0, 'MAX_TEES-1'),
		NetIntRange("m_Time", -1, 'max_int'),
		NetIntAny("m_Diff"),
		NetBool("m_RecordPersonal"),
		NetBool("m_RecordServer", default=False),
	]),

	NetMessage("Sv_Checkpoint", [
		NetIntAny("m_Diff"),
	]),

	# -- server commands --
	NetMessage("Sv_CommandInfo", [
			NetStringStrict("m_Name"),
			NetStringStrict("m_ArgsFormat"),
			NetStringStrict("m_HelpText")
	]),

	NetMessage("Sv_CommandInfoRemove", [
			NetStringStrict("m_Name")
	]),

	### Client messages

	# -- chat / kill --
	NetMessage("Cl_Say", [
		NetIntRange("m_Mode", 0, 'NUM_CHATS-1'),
		NetIntRange("m_Target", -1, 'MAX_TEES-1'),
		NetStringStrict("m_pMessage"),
	]),

	NetMessage("Cl_Kill", []),

	# -- team / spectator --
	NetMessage("Cl_SetTeam", [
		NetIntRange("m_Team", 'TEAM_SPECTATORS', 'TEAM_BLUE'),
	]),

	NetMessage("Cl_SetSpectatorMode", [
		NetIntRange("m_SpecMode", 0, 'NUM_SPECMODES-1'),
		NetIntRange("m_SpectatorID", -1, 'MAX_TEES-1'),
	]),

	# -- voting --
	NetMessage("Cl_Vote", [
		NetIntRange("m_Vote", 'VOTE_CHOICE_NO', 'VOTE_CHOICE_YES'),
	]),

	NetMessage("Cl_CallVote", [
		NetStringStrict("m_Type"),
		NetStringStrict("m_Value"),
		NetStringStrict("m_Reason"),
		NetBool("m_Force"),
	]),

	# -- presentation / misc --
	NetMessage("Cl_Emoticon", [
		NetEnum("m_Emoticon", Emoticons),
	]),

	NetMessage("Cl_SkinChange", [
		NetArray(NetStringStrict("m_apSkinPartNames"), 6),
		NetArray(NetBool("m_aUseCustomColors"), 6),
		NetArray(NetIntAny("m_aSkinPartColors"), 6),
	]),

	NetMessage("Cl_ReadyChange", []),

	# -- client session --
	NetMessage("Cl_StartInfo", [
		NetStringStrict("m_pName"),
		NetStringStrict("m_pClan"),
		NetIntAny("m_Country"),
		NetArray(NetStringStrict("m_apSkinPartNames"), 6),
		NetArray(NetBool("m_aUseCustomColors"), 6),
		NetArray(NetIntAny("m_aSkinPartColors"), 6),
	]),

	# -- commands --
	NetMessage("Cl_Command", [
			NetStringStrict("m_Name"),
			NetStringStrict("m_Arguments")
	]),

]