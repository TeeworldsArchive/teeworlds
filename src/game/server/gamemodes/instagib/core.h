#ifndef GAME_SERVER_GAMEMODES_INSTAGIB_CORE_H
#define GAME_SERVER_GAMEMODES_INSTAGIB_CORE_H

const int INSTA_MAGIC_MAX_DAMAGE = 100;
const int INSTA_MAGIC_MIN_DAMAGE = 98;

class CInstaCore
{
protected:
	bool m_GreandeMode;

public:
	void InitCore(bool GrenadeMode);
	void SetBasicWeapon(class CCharacter *pChr);
	int OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon);
};

#endif // GAME_SERVER_GAMEMODES_INSTAGIB_CORE_H
