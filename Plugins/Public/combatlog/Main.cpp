// CombatLog for Discovery FLHook
// September 2020 by Jaz etc.
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

// includes 

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#ifndef byte
typedef unsigned char byte;
#endif

#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <list>
#include <map>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <math.h>
#include <chrono>
#include <PluginUtilities.h>

typedef void(*wprintf_fp)(std::wstring format, ...);
typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct DamageType
{
	float fShieldDamage;
	float fHullDamage;
	uint iCausedDeath;
};

struct DamageLog
{
	map<uint, DamageType> DamageReceived;
	map<uint, DamageType> DamageDealt;
};

map<uint, DamageLog> mapCombatLog;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void HandleLog(uint iClientID)
{
	PrintUserCmdText(iClientID, L"Damage Log (Received):");
	for (auto it = mapCombatLog[iClientID].DamageReceived.begin(); it != mapCombatLog[iClientID].DamageReceived.end(); it++) {
		PrintUserCmdText(iClientID, L"%s: %0.0f hull damage, %0.0f shield damage. Killed by: %u", Players.GetActiveCharacterName(it->first), it->second.fHullDamage, it->second.fShieldDamage, it->second.iCausedDeath);
	}
	PrintUserCmdText(iClientID, L"");
	PrintUserCmdText(iClientID, L"Damage Log (Dealt):");
	for (auto it = mapCombatLog[iClientID].DamageDealt.begin(); it != mapCombatLog[iClientID].DamageDealt.end(); it++) {
		PrintUserCmdText(iClientID, L"%s: %0.0f hull damage, %0.0f shield damage. Killed: %u", Players.GetActiveCharacterName(it->first), it->second.fHullDamage, it->second.fShieldDamage, it->second.iCausedDeath);
	}
	mapCombatLog[iClientID].DamageDealt.clear();
	mapCombatLog[iClientID].DamageReceived.clear();
}

void __stdcall HkCb_AddDmgEntry(DamageList *dmg, ushort subObjID, float setHealth, DamageEntry::SubObjFate fate)
{
	returncode = DEFAULT_RETURNCODE;

	bool bUnk1 = false;
	bool bUnk2 = false;
	float fUnk;
	pub::SpaceObj::GetInvincible(ClientInfo[iDmgTo].iShip, bUnk1, bUnk2, fUnk);
	//if so, suspress the dmg
	if (bUnk1 && bUnk2)
		return;

	uint iDmgFrom = HkGetClientIDByShip(dmg->get_inflictor_id());
	if (iDmgFrom && iDmgTo)
	{
		float curr, max, damage;
		bool bShieldsUp;
		
		// Ask the combat magic plugin if we need to do anything differently
		COMBAT_DAMAGE_OVERRIDE_STRUCT info;
		info.iMunitionID = iDmgMunitionID;
		info.fDamageMultiplier = 0.0f;
		Plugin_Communication(COMBAT_DAMAGE_OVERRIDE, &info);

		if (subObjID == 1) // 1 is base (hull)
			pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
		else if (subObjID == 65521) // 65521 is shield (bubble, not equipment)
			pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
		else
			return;
		damage = curr - setHealth;

		if (info.fDamageMultiplier != 0.0f)
			damage *= info.fDamageMultiplier;

		if (subObjID == 1)
		{
			mapCombatLog[iDmgTo].DamageReceived[iDmgFrom].fHullDamage += damage;
			mapCombatLog[iDmgFrom].DamageDealt[iDmgTo].fHullDamage += damage;
			if (setHealth == 0)
			{
				mapCombatLog[iDmgTo].DamageReceived[iDmgFrom].iCausedDeath++;
				mapCombatLog[iDmgFrom].DamageDealt[iDmgTo].iCausedDeath++;
				HandleLog(iDmgTo);
			}
		}
		else
		{
			mapCombatLog[iDmgTo].DamageReceived[iDmgFrom].fShieldDamage += damage;
			mapCombatLog[iDmgFrom].DamageDealt[iDmgTo].fShieldDamage += damage;
		}
	}
}

void __stdcall BaseEnter(uint iBaseID, uint iClientID)
{
	returncode = DEFAULT_RETURNCODE;
	if (mapCombatLog[iClientID].DamageDealt.size() != 0 || mapCombatLog[iClientID].DamageReceived.size() != 0)
		HandleLog(iClientID);
}

void Plugin_Communication_Callback(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;

	if (msg == COMBAT_LOG_KILL_OVERRIDE)
	{
		returncode = SKIPPLUGINS;
		COMBAT_LOG_KILL_OVERRIDE_STRUCT* info = reinterpret_cast<COMBAT_LOG_KILL_OVERRIDE_STRUCT*>(data);
		map<uint, DamageLog>::iterator iter = mapCombatLog.find(info->iDeadClientID);
		if (iter != mapCombatLog.end())
		{
			float highestReceived = 0;
			uint ihighestDamageDealer = 0;
			for (auto it = iter->second.DamageReceived.begin(); it != iter->second.DamageReceived.end(); it++) {
				if (it->second.fHullDamage > highestReceived)
				{
					uint iDeadShip = Players[info->iDeadClientID].iShipID;
					uint iKillerShip = Players[it->first].iShipID;
					if (iKillerShip)
					{
						if (HkDistance3DByShip(iDeadShip, iKillerShip) < 15000.0f)
						{
							highestReceived = it->second.fHullDamage;
							ihighestDamageDealer = it->first;
						}
					}

				}
			}
			info->iKillerClientID = ihighestDamageDealer;
		}
	}
	return;
}

/** Functions to hook */
EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Damage Tracker plugin by Jaz";
	p_PI->sShortName = "damagetrack";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkCb_AddDmgEntry, PLUGIN_HkCb_AddDmgEntry, 8));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&BaseEnter, PLUGIN_HkIServerImpl_BaseEnter, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_Callback, PLUGIN_Plugin_Communication, 10));
	return p_PI;
}
