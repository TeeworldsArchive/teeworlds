/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_HUNTER_CLASS_CIVIC_H
#define GAME_SERVER_GAMEMODES_HUNTER_CLASS_CIVIC_H

#include <game/server/gamemodes/hunter/hunter.h>

class CCivic : public CGameControllerHunter::IClass
{
public:
    CCivic(class CGameControllerHunter *pController) : CGameControllerHunter::IClass::IClass(pController) {};
    ~CCivic() {};
};

#endif
