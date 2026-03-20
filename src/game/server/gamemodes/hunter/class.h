/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifdef INCLUDE_HUNTER_CLASS_HEADER
    #include "class/civic.h"
    #include "class/hunter.h"
#else // INCLUDE_HUNTER_CLASS_HEADER
#ifdef REGISTER_HUNTER_CLASS
    // REGISTER_HUNTER_CLASS(CLASS, CLASS_ENUM)
    REGISTER_HUNTER_CLASS(CCivic, ID_CIVIC)
    REGISTER_HUNTER_CLASS(CHunter, ID_HUNTER)
#else // REGISTER_HUNTER_CLASS
#ifndef GAME_SERVER_GAMEMODES_HUNTER_CLASS_H
#define GAME_SERVER_GAMEMODES_HUNTER_CLASS_H

enum
{
    ID_NONE_CLASS,
#define REGISTER_HUNTER_CLASS(CLASS, CLASS_ENUM) \
    CLASS_ENUM,
    #include "class.h"
#undef REGISTER_HUNTER_CLASS
    NUM_CLASS_ID,
};

#endif // GAME_SERVER_GAMEMODES_HUNTER_CLASS_H
#endif // REGISTER_HUNTER_CLASS
#endif // INCLUDE_HUNTER_CLASS_HEADER
