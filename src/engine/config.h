/* (c) Magnus Auvinen. See license.txt in the root of the distribution for more information. */
/* (c) Teeworlds Archive Project Contributors.                                               */
/* This is a modified version of Teeworlds - see license.txt for details.                    */
#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include "kernel.h"

class IConfigManager : public IInterface
{
	MACRO_INTERFACE("config", 0)
public:
	typedef void (*SAVECALLBACKFUNC)(IConfigManager *pConfigManager, void *pUserData);

	virtual void Init(int FlagMask) = 0;
	virtual void Reset() = 0;
	virtual void RestoreStrings() = 0;
	virtual void Save(const char *pFilename = 0) = 0;
	virtual class CConfig *Values() = 0;

	virtual void RegisterCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData) = 0;

	virtual void WriteLine(const char *pLine) = 0;
};

extern IConfigManager *CreateConfigManager();

#endif
