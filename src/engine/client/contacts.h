/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CLIENT_CONTACTS_H
#define ENGINE_CLIENT_CONTACTS_H

#include <engine/config.h>
#include <engine/contacts.h>
#include <engine/shared/console.h>

class IContactList
{
private:
	CContactInfo m_aContacts[CContactInfo::MAX_CONTACTS];
	int m_NumContacts;

public:
	IContactList();

	void ConfigSave(IConfigManager *pConfigManager, const char *pCmdStr);

	virtual void Init() = 0;

	int NumContacts() const { return m_NumContacts; }
	const CContactInfo *GetContact(int Index) const;
	int GetContactState(const char *pName, const char *pClan) const;
	bool IsContact(const char *pName, const char *pClan, bool PlayersOnly) const;

	void AddContact(const char *pName, const char *pClan);
	void RemoveContact(const char *pName, const char *pClan);
	void RemoveContact(int Index);
};

class CFriends : public IFriends, public IContactList
{
public:
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	void Init();

	// bridges
	inline virtual int NumFriends() const { return IContactList::NumContacts(); }
	inline virtual const CContactInfo *GetFriend(int Index) const { return IContactList::GetContact(Index); }
	inline virtual int GetFriendState(const char *pName, const char *pClan) const { return IContactList::GetContactState(pName, pClan); }
	inline virtual bool IsFriend(const char *pName, const char *pClan, bool PlayersOnly) const { return IContactList::IsContact(pName, pClan, PlayersOnly); }
	inline virtual void AddFriend(const char *pName, const char *pClan) { IContactList::AddContact(pName, pClan); }
	inline virtual void RemoveFriend(const char *pName, const char *pClan) { IContactList::RemoveContact(pName, pClan); }

	static void ConAddFriend(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveFriend(IConsole::IResult *pResult, void *pUserData);
};

class CBlacklist : public IBlacklist, public IContactList
{
public:
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	void Init();

	// bridges
	inline virtual bool IsIgnored(const char *pName, const char *pClan, bool PlayersOnly) const { return IContactList::IsContact(pName, pClan, PlayersOnly); }
	inline virtual void AddIgnoredPlayer(const char *pName, const char *pClan) { IContactList::AddContact(pName, pClan); }
	inline virtual void RemoveIgnoredPlayer(const char *pName, const char *pClan) { IContactList::RemoveContact(pName, pClan); }

	static void ConAddIgnore(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveIgnore(IConsole::IResult *pResult, void *pUserData);
};

#endif
