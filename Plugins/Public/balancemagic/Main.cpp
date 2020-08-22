// Balance Magic for Discovery FLHook
// September 2018 by Kazinsal etc.
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
#include <PluginUtilities.h>

bool UserCmd_SnacClassic(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage);
float HandleEquipmentDamage(DamageList *dmg, ushort subObjID, float setHealth, DamageEntry::SubObjFate fate, uint iDmgToSpaceID, float multiplier = 1.0f, float daMult = 1.0f);
float GetDamageAdjustMultiplier();
typedef void(*wprintf_fp)(std::wstring format, ...);
typedef bool(*_UserCmdProc)(uint, const wstring &, const wstring &, const wchar_t*);

struct DamageMultiplier {
	float fighter;
	float freighter;
	float transport;
	float gunboat;
	float cruiser;
	float battlecruiser;
	float battleship;
	float solar;
};

struct EquipDamageMultipliers {
	float equipMultiplier;
	float pierceMultiplier;
	float pierceShieldMultiplier;
};

struct USERCMD
{
	wchar_t* wszCmd;
	_UserCmdProc proc;
	wchar_t* usage;
};

USERCMD UserCmds[] =
{
	{ L"/snacclassic", UserCmd_SnacClassic, L"Usage: /snacclassic" },
};

int iLoadedDamageAdjusts = 0;
int iLoadedAllowEnergyDamageAdjusts = 0;
int iLoadedPiercingWeapons = 0;

map<uint, DamageMultiplier> mapDamageAdjust;
map<uint, EquipDamageMultipliers> mapPiercingWeapons;
map<uint, float> mapAllowEnergyDamageAdjust;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

/// Load the configuration
void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	mapDamageAdjust.clear();
	iLoadedDamageAdjusts = 0;

	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\balancemagic.cfg";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("DamageAdjust"))
			{
				while (ini.read_value())
				{
					DamageMultiplier stEntry = { 0.0f };
					stEntry.fighter = ini.get_value_float(0);
					stEntry.freighter = ini.get_value_float(1) ? ini.get_value_float(1) : stEntry.fighter;
					stEntry.transport = ini.get_value_float(2) ? ini.get_value_float(2) : stEntry.freighter;
					stEntry.gunboat = ini.get_value_float(3) ? ini.get_value_float(3) : stEntry.transport;
					stEntry.cruiser = ini.get_value_float(4) ? ini.get_value_float(4) : stEntry.gunboat;
					stEntry.battlecruiser = ini.get_value_float(5) ? ini.get_value_float(5) : stEntry.cruiser;
					stEntry.battleship = ini.get_value_float(6) ? ini.get_value_float(6) : stEntry.battlecruiser;
					stEntry.solar = ini.get_value_float(7) ? ini.get_value_float(7) : stEntry.battleship;
					mapDamageAdjust[CreateID(ini.get_name_ptr())] = stEntry;
					++iLoadedDamageAdjusts;
				}
			}
			if (ini.is_header("AllowEnergyDamage"))
			{
				while (ini.read_value())
				{
					mapAllowEnergyDamageAdjust[CreateID(ini.get_name_ptr())] = ini.get_value_float(0);
					ConPrint(L"Loaded Energy Damage Weapon: %s (%u)- %0.2f", ini.get_name(), CreateID(ini.get_name_ptr()), ini.get_value_float(0));
					++iLoadedAllowEnergyDamageAdjusts;
				}
			}
			if (ini.is_header("EquipAndPiercingWeapons"))
			{
				while (ini.read_value())
				{
					EquipDamageMultipliers stEntry = { 0.0f };
					stEntry.equipMultiplier = ini.get_value_float(0);
					stEntry.pierceMultiplier = ini.get_value_float(1);
					stEntry.pierceShieldMultiplier = ini.get_value_float(2);
					mapPiercingWeapons[CreateID(ini.get_name_ptr())] = stEntry;
					++iLoadedPiercingWeapons;
				}
			}
		}
		ini.close();
	}

	ConPrint(L"BALANCEMAGIC: Loaded %u damage adjusts.\n", iLoadedDamageAdjusts);
	ConPrint(L"BALANCEMAGIC: Loaded %u energy damage exceptions.\n", iLoadedAllowEnergyDamageAdjusts);
	ConPrint(L"BALANCEMAGIC: Loaded %u piercing weapons.\n", iLoadedPiercingWeapons);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();
	}
	return true;
}


// Command-Option-X-O
bool UserCmd_SnacClassic(uint iClientID, const wstring &wscCmd, const wstring &wscParam, const wchar_t *usage)
{
	uint baseID = 0;
	pub::Player::GetBase(iClientID, baseID);
	if (!baseID)
	{
		PrintUserCmdText(iClientID, L"ERR cannot engage time machine while undocked");
		return true;
	}

	int iSNACs = 0;
	int iRemHoldSize;
	list<CARGO_INFO> lstCargo;
	HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iRemHoldSize);

	foreach(lstCargo, CARGO_INFO, it)
	{
		if ((*it).bMounted)
			continue;

		if (it->iArchID == CreateID("dsy_snova_civ"))
		{
			iSNACs += it->iCount;
			pub::Player::RemoveCargo(iClientID, it->iID, it->iCount);
		}
	}

	if (iSNACs)
	{
		unsigned int good = CreateID("dsy_snova_classic");
		pub::Player::AddCargo(iClientID, good, iSNACs, 1.0, false);
		PrintUserCmdText(iClientID, L"The time machine ate %i modern-day SNACs and gave back old rusty ones from a bygone era.", iSNACs);
	}
	else
	{
		PrintUserCmdText(iClientID, L"The time machine was disappointed to find you had no unmounted SNACs to relinquish unto it");
	}
	return true;
}

/**
This function is called by FLHook when a user types a chat string. We look at the
string they've typed and see if it starts with one of the above commands. If it
does we try to process it.
*/
bool UserCmd_Process(uint iClientID, const wstring &wscCmd)
{
	returncode = DEFAULT_RETURNCODE;

	wstring wscCmdLineLower = ToLower(wscCmd);

	// If the chat string does not match the USER_CMD then we do not handle the
	// command, so let other plugins or FLHook kick in. We require an exact match
	for (uint i = 0; (i < sizeof(UserCmds) / sizeof(USERCMD)); i++)
	{
		if (wscCmdLineLower.find(UserCmds[i].wszCmd) == 0)
		{
			// Extract the parameters string from the chat string. It should
			// be immediately after the command and a space.
			wstring wscParam = L"";
			if (wscCmd.length() > wcslen(UserCmds[i].wszCmd))
			{
				if (wscCmd[wcslen(UserCmds[i].wszCmd)] != ' ')
					continue;
				wscParam = wscCmd.substr(wcslen(UserCmds[i].wszCmd) + 1);
			}

			// Dispatch the command to the appropriate processing function.
			if (UserCmds[i].proc(iClientID, wscCmd, wscParam, UserCmds[i].usage))
			{
				// We handled the command tell FL hook to stop processing this
				// chat string.
				returncode = SKIPPLUGINS_NOFUNCTIONCALL; // we handled the command, return immediatly
				return true;
			}
		}
	}
	return false;
}

void __stdcall HkCb_AddDmgEntry(DamageList *dmg, ushort subObjID, float setHealth, DamageEntry::SubObjFate fate)
{
	returncode = DEFAULT_RETURNCODE;

	if (iDmgToSpaceID == 0 && iDmgTo != 0)
		pub::Player::GetShip(iDmgTo, iDmgToSpaceID);

	//uint iCause = dmg->get_cause();
	//wstring wscType = L"";
	//switch (iCause)
	//{
	//case 0x05:
	//	wscType = L"Missile/Torpedo";
	//	break;
	//case 0x07:
	//	wscType = L"Mine";
	//	break;
	//case 0x06: case 0xC0: case 0x15:
	//	wscType = L"Wasp/Hornet";
	//	break;
	//case 0x01:
	//	wscType = L"Collision";
	//	break;
	//case 0x02:
	//	wscType = L"Gun";
	//	break;
	//default:
	//	wscType = L"Gun";
	//	break;
	//}
	//wstring wscMsg = L"HkCb_AddDmgEntry - cause: %cause, subObjID: %u, setHealth: %0.2f, fate: %u, munition: %u, dmgtospaceid: %u, dmgto: %u \n";
	//wscMsg = ReplaceStr(wscMsg, L"%cause", wscType);
	//ConPrint(wscMsg, subObjID, setHealth, fate, iDmgMunitionID, iDmgToSpaceID, iDmgTo);

	if (subObjID == 2)
	{
		if (fate == 0)
		{
			ConPrint(L"Skipping normal powercore damage entry. \n");
			returncode = PLUGIN_RETURNCODE::SKIPPLUGINS_NOFUNCTIONCALL;
		}
		else
		{
			ConPrint(L"Allowing custom powercore damage entry. \n");
		}
		return;
	}

	if (iDmgToSpaceID && iDmgMunitionID)
	{
		float daMult = GetDamageAdjustMultiplier();
		if (subObjID > 1) //Handle this first because this projectile might end up hitting the hull. This way we can damage still DamageAdjust the hull hit.
		{
			map<uint, EquipDamageMultipliers>::iterator iter = mapPiercingWeapons.find(iDmgMunitionID);
			if (iter != mapPiercingWeapons.end())
			{
				ConPrint(L"PiercingWeapon found: %0.2f, %0.2f, %0.2f \n", iter->second.equipMultiplier, iter->second.pierceMultiplier, iter->second.pierceShieldMultiplier);
				float setDamage = 0;
				float pierceMult = 0;
				if (subObjID != 65521)
				{
					pierceMult = iter->second.pierceMultiplier;
					setDamage = HandleEquipmentDamage(dmg, subObjID, setHealth, fate, iDmgToSpaceID, iter->second.equipMultiplier, daMult);
				}
				else
				{
					pierceMult = iter->second.pierceShieldMultiplier;
					float curr, max;
					bool bShieldsUp;
					pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
					setDamage = HandleEquipmentDamage(dmg, subObjID, setHealth, fate, iDmgToSpaceID, 1, daMult);
					ConPrint(L"Dealing %0.2f * %0.2f (%0.2f) shield damage before piercing. Expected Shield: %0.2f / %0.2f \n", setDamage, daMult, setDamage * daMult, curr - setDamage * daMult, max);
				}

				if (pierceMult > 0.0f) //If piercing multiplier is not 0, handle the rest as if the damage hit the hull.
				{
					float curr, max;
					pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
					ConPrint(L"Dealing %0.2f * %0.2f (%0.2f) piercing damage to ship. Expected Hull: %0.2f / %0.2f \n", setDamage * daMult, pierceMult, setDamage * daMult * pierceMult, curr - setDamage * daMult * pierceMult, max);
					setHealth = curr - setDamage * pierceMult;
					subObjID = 1;
				}
			}
		}
		else
		{
			map<uint, float>::iterator iter = mapAllowEnergyDamageAdjust.find(iDmgMunitionID);
			if (iter != mapAllowEnergyDamageAdjust.end())
			{
				if (subObjID == 1)
					HandleEquipmentDamage(dmg, 2, iter->second, (DamageEntry::SubObjFate)0, iDmgToSpaceID, 1.0f);
			}
		}

		if (daMult!=1)
		{
			float curr, max;
			bool bShieldsUp;

			if (subObjID == 1) // 1 is base (hull)
				pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
			else if (subObjID == 65521) // 65521 is shield (bubble, not equipment)
				pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
			else
				return; // If hit mounted equipment - do not continue with uninitialized variables.

			setHealth = curr - (curr - setHealth) * daMult;

			//// Deduce: if not fighter nor freighter, then it's obviously solar object.
			//if (iTargetType != OBJ_FIGHTER && iTargetType != OBJ_FREIGHTER)
			//{
			//	setHealth = curr - (curr - setHealth) * iter->second.solar;
			//}
			//else
			//{
			//	uint iArchID;
			//	pub::SpaceObj::GetSolarArchetypeID(iDmgToSpaceID, iArchID);
			//	uint targetShipClass = Archetype::GetShip(iArchID)->iShipClass;
			//	if (targetShipClass == 0 || targetShipClass == 1 || targetShipClass == 3)
			//		setHealth = curr - (curr - setHealth) * iter->second.fighter;
			//	else if (targetShipClass == 2 || targetShipClass == 4 || targetShipClass == 5 || targetShipClass == 19)
			//		setHealth = curr - (curr - setHealth) * iter->second.freighter;
			//	else if (targetShipClass < 11)
			//		setHealth = curr - (curr - setHealth) * iter->second.transport;
			//	else if (targetShipClass < 13)
			//		setHealth = curr - (curr - setHealth) * iter->second.gunboat;
			//	else if (targetShipClass < 15)
			//		setHealth = curr - (curr - setHealth) * iter->second.cruiser;
			//	else if (targetShipClass < 16)
			//		setHealth = curr - (curr - setHealth) * iter->second.battlecruiser;
			//	else if (targetShipClass < 19)
			//		setHealth = curr - (curr - setHealth) * iter->second.battleship;
			//}
		}
		// Fix wrong shield rebuild time bug.
		if (setHealth < 0)
			setHealth = 0;

		// Fix wrong death message bug.
		if (iDmgTo && subObjID == 1)
			ClientInfo[iDmgTo].dmgLast = *dmg;

		// Add damage entry instead of FLHook Core.
		dmg->add_damage_entry(subObjID, setHealth, fate);
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;

		iDmgTo = 0;
		iDmgToSpaceID = 0;
		iDmgMunitionID = 0;
	}
}

float HandleEquipmentDamage(DamageList *dmg, ushort subObjID, float setHealth, DamageEntry::SubObjFate fate, uint iDmgToSpaceID, float multiplier, float daMult)
{
	float setDamage, curr, max;
	if (subObjID == 65521)
	{
		bool bShieldsUp;
		pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
		setDamage = curr - setHealth;
		setHealth = curr - setDamage * daMult;
		// Fix wrong shield rebuild time bug.
		if (setHealth < 0)
			setHealth = 0;
		dmg->add_damage_entry(subObjID, setHealth, fate);
		if (setDamage < 0)
			return 0;
		return setDamage;
	}
	else
	{
		unsigned int iDunno = 0;
		IObjInspectImpl *obj = NULL;

		if (GetShipInspect(iDmgToSpaceID, obj, iDunno)) {
			if (obj) {
				CShip* cship = (CShip*)HkGetEqObjFromObjRW((IObjRW*)obj);
				if (subObjID == 2)
				{
					setDamage = setHealth;
					curr = cship->get_power();
					setHealth = curr - setHealth;
					if (setHealth < 0)
						setHealth = 0;
					dmg->add_damage_entry(2, setHealth, (DamageEntry::SubObjFate)2);
				}
				else
				{
					cship->get_sub_obj_hit_pts(subObjID, curr);
					cship->get_sub_obj_max_hit_pts(subObjID, max);
					setDamage = curr - setHealth;
					ConPrint(L"Dealing %0.2f * %0.2f * %0.2f (%0.2f) equipment damage before piercing. Expected EqHP: %0.2f / %0.2f \n", setDamage, multiplier, daMult, setDamage * multiplier * daMult, curr - setDamage * multiplier * daMult, max);
					setHealth = curr - setDamage * multiplier * daMult;
					if (setHealth < 0)
					{
						setHealth = 0;
						fate = (DamageEntry::SubObjFate)2;
					}
					dmg->add_damage_entry(subObjID, setHealth, fate);
				}
				if (setDamage < 0)
					return 0;
				return setDamage;
			}
		}
	}
	return 0.0f;
}

float GetDamageAdjustMultiplier()
{
	uint iTargetType;
	pub::SpaceObj::GetType(iDmgToSpaceID, iTargetType);

	// Deduce: if not fighter nor freighter, then it's obviously solar object.
	map<uint, DamageMultiplier>::iterator iter = mapDamageAdjust.find(iDmgMunitionID);
	if (iter != mapDamageAdjust.end())
	{
		if (iTargetType != OBJ_FIGHTER && iTargetType != OBJ_FREIGHTER)
		{
			return iter->second.solar;
		}
		else
		{
			uint iArchID;
			pub::SpaceObj::GetSolarArchetypeID(iDmgToSpaceID, iArchID);
			uint targetShipClass = Archetype::GetShip(iArchID)->iShipClass;

			switch (targetShipClass)
			{
			case 0: case 1: case 2: case 3: case 4: case 5:
				return iter->second.fighter;
				break;
			case 6: case 7: case 8: case 9: case 10:
				return iter->second.freighter;
				break;
			case 11: case 12:
				return iter->second.transport;
				break;
			case 13: case 14:
				return iter->second.cruiser;
				break;
			case 15:
				return iter->second.battlecruiser;
				break;
			case 16: case 17: case 18:
				return iter->second.battleship;
				break;
			default:
				return 1;
				break;
			}
		}
	}
	return 1;
}

void Plugin_Communication_Callback(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;

	if (msg == COMBAT_DAMAGE_OVERRIDE)
	{
		returncode = SKIPPLUGINS;
		COMBAT_DAMAGE_OVERRIDE_STRUCT* info = reinterpret_cast<COMBAT_DAMAGE_OVERRIDE_STRUCT*>(data);
		map<uint, DamageMultiplier>::iterator iter = mapDamageAdjust.find(info->iMunitionID);
		if (iter != mapDamageAdjust.end())
		{
			info->fDamageMultiplier = iter->second.solar;
		}
	}
	return;
}

/** Functions to hook */
EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "Balance Magic plugin by Kazinsal";
	p_PI->sShortName = "balancemagic";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkCb_AddDmgEntry, PLUGIN_HkCb_AddDmgEntry, 9));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&Plugin_Communication_Callback, PLUGIN_Plugin_Communication, 10));
	return p_PI;
}
