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

struct ClassConfig { //Generalized naming of struct for reuse on different class-based values.
	float fighter;
	float freighter;
	float transport;
	float gunboat;
	float cruiser;
	float battlecruiser;
	float battleship;
	float solar;

	//Since this struct is reused for different things, this switch statement is implemented as a function within the struct instead of duplicating the switch statement.
	float GetShipClass(int shipclass, int defaultRet)
	{
		switch (shipclass)
		{
		case 0: case 1: case 3:
			//0 - Light Fighter, 1 - Heavy Fighter, 3 - Very Heavy Fighter
			return this->fighter;
			break;
		case 2: case 4: case 5: case 19:
			//2 - Freighter, 4 - Super Heavy Fighter, 5 - Bomber, 19 - Repair Ship
			return this->freighter;
			break;
		case 6: case 7: case 8: case 9: case 10:
			//6 - Transport, 7 - Train, 8 - Heavy Transport, 9 - Super Train, 10 - Liner
			return this->transport;
			break;
		case 11: case 12:
			//11 - Gunship, 12 - Gunboat
			return this->gunboat;
			break;
		case 13: case 14:
			//13 - Destroyer, 14 - Cruiser
			return this->cruiser;
			break;
		case 15:
			// 15, Battlecruiser
			return this->battlecruiser;
			break;
		case 16: case 17: case 18:
			//16 - Battleship, 17 - Carrier, 18 - Dreadnought
			return this->battleship;
			break;
		default:
			return defaultRet;
			break;
		}
	}
	//Function made to remove duplicate code between different uses of struct
	void LoadData(INI_Reader* ini)
	{
		this->fighter = ini->get_value_float(0);
		this->freighter = ini->get_value_float(1) ? ini->get_value_float(1) : this->fighter;
		this->transport = ini->get_value_float(2) ? ini->get_value_float(2) : this->freighter;
		this->gunboat = ini->get_value_float(3) ? ini->get_value_float(3) : this->transport;
		this->cruiser = ini->get_value_float(4) ? ini->get_value_float(4) : this->gunboat;
		this->battlecruiser = ini->get_value_float(5) ? ini->get_value_float(5) : this->cruiser;
		this->battleship = ini->get_value_float(6) ? ini->get_value_float(6) : this->battlecruiser;
		this->solar = ini->get_value_float(7) ? ini->get_value_float(7) : this->battleship;
	}
};

struct MagicWeapon {
	ClassConfig DamageMultiplier; //Class-based DamageMultiplier
	ClassConfig RepairFlats; //Class-based Repair flat value
	ClassConfig RepairPercent; //Class-based Repair percentage value

	//Misc weapon configs.
	float equipMultiplier;
	float pierceMultiplier;
	float pierceShieldMultiplier;
	float vampPercentage;
	float energyDamage;
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
int iLoadedVampireWeapons = 0;
int iLoadedRepairTools = 0;

map<uint, MagicWeapon> mapMagicWeapons;

//map<uint, DamageMultiplier> mapDamageAdjust;
//map<uint, EquipDamageMultipliers> mapPiercingWeapons;
//map<uint, float> mapAllowEnergyDamageAdjust;
//map<uint, float> mapVampireWeapons;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

/// Load the configuration
void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	mapMagicWeapons.clear();
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
					ClassConfig stEntry = { 0.0f };
					stEntry.LoadData(&ini);
					mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier = stEntry;
					ConPrint(L"Damage Multiplier Config Loaded: %u - %0.2f, %0.2f, %0.2f, %0.2f, %0.2f, %0.2f, %0.2f \n", CreateID(ini.get_name_ptr()), mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.fighter, mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.freighter, mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.transport, mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.gunboat, mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.cruiser, mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.battlecruiser, mapMagicWeapons[CreateID(ini.get_name_ptr())].DamageMultiplier.battleship);
					++iLoadedDamageAdjusts;
				}
			}
			if (ini.is_header("EnergyDamageWeapons"))
			{
				while (ini.read_value())
				{
					mapMagicWeapons[CreateID(ini.get_name_ptr())].energyDamage = ini.get_value_float(0);
					ConPrint(L"Energy Damage Config Loaded: %u - %0.2f \n", CreateID(ini.get_name_ptr()), mapMagicWeapons[CreateID(ini.get_name_ptr())].energyDamage);
					++iLoadedAllowEnergyDamageAdjusts;
				}
			}
			if (ini.is_header("EquipAndPiercingWeapons"))
			{
				while (ini.read_value())
				{
					mapMagicWeapons[CreateID(ini.get_name_ptr())].equipMultiplier = ini.get_value_float(0);
					mapMagicWeapons[CreateID(ini.get_name_ptr())].pierceMultiplier = ini.get_value_float(1);
					mapMagicWeapons[CreateID(ini.get_name_ptr())].pierceShieldMultiplier = ini.get_value_float(2);
					ConPrint(L"Piercing Config Loaded: %u - %0.2f, %0.2f, %0.2f \n", CreateID(ini.get_name_ptr()), mapMagicWeapons[CreateID(ini.get_name_ptr())].equipMultiplier, mapMagicWeapons[CreateID(ini.get_name_ptr())].pierceMultiplier, mapMagicWeapons[CreateID(ini.get_name_ptr())].pierceShieldMultiplier);
					++iLoadedPiercingWeapons;
				}
			}
			if (ini.is_header("VampireWeapons"))
			{
				while (ini.read_value())
				{
					uint vID = CreateID(ini.get_name_ptr());
					mapMagicWeapons[vID].vampPercentage = ini.get_value_float(0);
					ConPrint(L"Vampiric Config Loaded: %u - %0.2f \n", CreateID(ini.get_name_ptr()), mapMagicWeapons[CreateID(ini.get_name_ptr())].vampPercentage);
					++iLoadedVampireWeapons;
				}
			}
			if (ini.is_header("RepairTools"))
			{
				while (ini.read_value())
				{
					ClassConfig stEntry = { 0.0f };
					stEntry.LoadData(&ini);

					if (stEntry.fighter>=1)
						mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats = stEntry;
					else
						mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent = stEntry;
					if (stEntry.fighter>=1)
						ConPrint(L"Repair Tool (Flat) Loaded: %u - %0.2f, %0.2f, %0.2f, %0.2f, %0.2f, %0.2f, %0.2f \n", CreateID(ini.get_name_ptr()), mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.fighter, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.freighter, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.transport, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.gunboat, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.cruiser, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.battlecruiser, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairFlats.battleship);
					else
						ConPrint(L"Repair Tool (Percent) Loaded: %u - %0.2f, %0.2f, %0.2f, %0.2f, %0.2f, %0.2f, %0.2f \n", CreateID(ini.get_name_ptr()), mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.fighter, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.freighter, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.transport, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.gunboat, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.cruiser, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.battlecruiser, mapMagicWeapons[CreateID(ini.get_name_ptr())].RepairPercent.battleship);
					++iLoadedRepairTools;
				}
			}
		}
		ini.close();
	}

	ConPrint(L"BALANCEMAGIC: Loaded %u damage adjusts.\n", iLoadedDamageAdjusts);
	ConPrint(L"BALANCEMAGIC: Loaded %u energy damage exceptions.\n", iLoadedAllowEnergyDamageAdjusts);
	ConPrint(L"BALANCEMAGIC: Loaded %u piercing weapons.\n", iLoadedPiercingWeapons);
	ConPrint(L"BALANCEMAGIC: Loaded %u vampire weapons.\n", iLoadedVampireWeapons);
	ConPrint(L"BALANCEMAGIC: Loaded %u repair tools.\n", iLoadedRepairTools);
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

	if (iDmgToSpaceID == 0 && iDmgTo != 0) //Equipment hits do not have an iDmgToSpaceID but they do have an iDmgTo. Can use that to correct iDmgToSpaceID for player-related damage.
		pub::Player::GetShip(iDmgTo, iDmgToSpaceID);

	if (subObjID == 2) //Normal energy damage is completely blocked. Use Energy Damage config for Balance Magic to have weapons drain powercore. This avoids the 100% power refill bug.
	{
		if (fate == 0)
		{
			returncode = PLUGIN_RETURNCODE::SKIPPLUGINS_NOFUNCTIONCALL;
		}
		return;
	}

	float setDamage = 0.0f;
	if (iDmgToSpaceID && iDmgMunitionID)
	{
		map<uint, MagicWeapon>::iterator iter = mapMagicWeapons.find(iDmgMunitionID);
		if (iter != mapMagicWeapons.end()) //Reworked code to use a single map. This allows us to check for a weapon once and have all the weapon config data we need later.
		{
			ConPrint(L"A Magic Weapon just hit something.\n");
			uint iTargetType;
			float daMult = 0.0f;
			float repFlat = 0.0f;
			float repPerc = 0.0f;

			pub::SpaceObj::GetType(iDmgToSpaceID, iTargetType);
			if (iTargetType != OBJ_FIGHTER && iTargetType != OBJ_FREIGHTER)
			{
				ConPrint(L"The target is a solar.\n");
				daMult = iter->second.DamageMultiplier.solar;
				repFlat = iter->second.RepairFlats.solar;
				repPerc = iter->second.RepairPercent.solar;
			}
			else
			{
				uint iArchID;
				pub::SpaceObj::GetSolarArchetypeID(iDmgToSpaceID, iArchID);
				uint targetShipClass = Archetype::GetShip(iArchID)->iShipClass;

				ConPrint(L"The target is a class %u ship\n", targetShipClass);

				daMult = iter->second.DamageMultiplier.GetShipClass(targetShipClass, 1);
				repFlat = iter->second.RepairFlats.GetShipClass(targetShipClass, 0);
				repPerc = iter->second.RepairPercent.GetShipClass(targetShipClass, 0);
			}

			if (subObjID > 1) //Handle this first because this projectile might end up hitting the hull. This way we can damage still DamageAdjust the hull hit.
			{
				ConPrint(L"The target's shield or equipment was hit.\n");
				float pierceMult = 0.0f;
				if (subObjID != 65521)
				{
					pierceMult = iter->second.pierceMultiplier;
					setDamage = HandleEquipmentDamage(dmg, subObjID, setHealth, fate, iDmgToSpaceID, iter->second.equipMultiplier, daMult);
					ConPrint(L"Equipment Hit -- Magic Weapon has a %0.2f damage multiplier, %0.2f pierce multiplier, %0.2f equipment damage multiplier. Damage Dealt: %0.2f\n", daMult, iter->second.pierceMultiplier, iter->second.equipMultiplier, setDamage * daMult);
				}
				else
				{
					pierceMult = iter->second.pierceShieldMultiplier;
					float curr, max;
					bool bShieldsUp;
					pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
					setDamage = HandleEquipmentDamage(dmg, subObjID, setHealth, fate, iDmgToSpaceID, 1, daMult);
					ConPrint(L"Shield Hit -- Magic Weapon has a %0.2f damage multiplier, %0.2f pierce shield multiplier. Damage Dealt: %0.2f\n", daMult, iter->second.pierceShieldMultiplier, setDamage * daMult);
				}

				if (pierceMult > 0.0f) //If piercing multiplier is not 0, handle the rest as if the damage hit the hull.
				{
					float curr, max;
					pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
					setHealth = curr - setDamage * pierceMult;
					subObjID = 1;
					ConPrint(L"Handling pierced damage to hull. Expected Hull: %0.2f / %0.2f | Damage Amount: %0.2f\n", setHealth, max, setDamage * daMult * pierceMult);
				}
			}
			else
			{
				//If the hull is hit directly, check to see if any alternative handling is needed.
				if (repFlat > 0 || repPerc > 0) //If either of these are greater than 0, handle a repair.
				{
					float curr, max;
					if (subObjID == 1) // 1 is base (hull)
						pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
					else if (subObjID == 65521) // 65521 is shield (bubble, not equipment)
						goto CleanAndReturn; // If hit shield - do not continue with uninitialized variables.
					setHealth = curr + (max / 100 * repPerc + repFlat);

					if (setHealth > max) //Prevent health from going over the maximum value for the target.
						setHealth = max;

					// Add damage entry instead of FLHook Core.
					dmg->add_damage_entry(1, setHealth, (DamageEntry::SubObjFate)0);
					ConPrint(L"Handling repair of ship. Expected Hull: %0.2f / %0.2f | Repair Amount: %0.2f \n", curr, max, max / 100 * repPerc + repFlat);
					goto CleanAndReturn;
				}

				if (iter->second.energyDamage != 0) //If energy damage should be dealt, handle equipment damage to the powercore.
				{
					HandleEquipmentDamage(dmg, 2, iter->second.energyDamage, (DamageEntry::SubObjFate)0, iDmgToSpaceID, 1.0f);
				}

			}

			if (daMult != 1)
			{
				float curr, max;
				bool bShieldsUp;

				if (subObjID == 1) // 1 is base (hull)
					pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
				else if (subObjID == 65521) // 65521 is shield (bubble, not equipment)
					pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
				else
					return; // If hit mounted equipment - do not continue with uninitialized variables. (Should never hit this since equipment damage is handled above)
				setHealth = curr - (curr - setHealth) * daMult;

				ConPrint(L"Handling scaled damage to ship. Expected Hull: %0.2f / %0.2f | Damage Amount: %0.2f\n", setHealth, max, (curr - setHealth));

				// Add damage entry instead of FLHook Core.
				dmg->add_damage_entry(subObjID, setHealth, fate);
			}

			CleanAndReturn: //Using this label for returns in order to avoid instances where variable values are retained for some reason.

				// Fix wrong shield rebuild time bug.
			if (setHealth < 0)
				setHealth = 0;

			// Fix wrong death message bug.
			if (iDmgTo && subObjID == 1)
				ClientInfo[iDmgTo].dmgLast = *dmg;

			returncode = SKIPPLUGINS_NOFUNCTIONCALL;

			iDmgTo = 0;
			iDmgToSpaceID = 0;
			iDmgMunitionID = 0;
		}
	}

	//uint iArchID;
	//uint nSpaceID = dmg->get_inflictor_id();
	//pub::SpaceObj::GetSolarArchetypeID(nSpaceID, iArchID);
	//map<uint, float>::iterator iter = mapVampireWeapons.find(iArchID);
	//if (iter != mapVampireWeapons.end())
	//{
	//	float curr, max;
	//	bool bShieldsUp;
	//	if (setDamage == 0)
	//	{
	//		if (subObjID == 1) // 1 is base (hull)
	//			pub::SpaceObj::GetHealth(iDmgToSpaceID, curr, max);
	//		else if (subObjID == 65521) // 65521 is shield (bubble, not equipment)
	//			pub::SpaceObj::GetShieldHealth(iDmgToSpaceID, curr, max, bShieldsUp);
	//		else
	//			return;
	//		setDamage = curr - setHealth;
	//	}
	//	pub::SpaceObj::GetHealth(nSpaceID, curr, max);
	//	setHealth = curr + setDamage * iter->second;
	//	pub::SpaceObj::SetRelativeHealth(nSpaceID, setHealth / max);
	//}
}

float HandleEquipmentDamage(DamageList *dmg, ushort subObjID, float setHealth, DamageEntry::SubObjFate fate, uint iDmgToSpaceID, float edaMult, float daMult)
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
				else if (subObjID != 65521)
				{
					cship->get_sub_obj_hit_pts(subObjID, curr);
					cship->get_sub_obj_max_hit_pts(subObjID, max);
					setDamage = curr - setHealth;
					setHealth = curr - setDamage * edaMult * daMult;
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

void Plugin_Communication_Callback(PLUGIN_MESSAGE msg, void* data)
{
	returncode = DEFAULT_RETURNCODE;

	if (msg == COMBAT_DAMAGE_OVERRIDE)
	{
		returncode = SKIPPLUGINS;
		COMBAT_DAMAGE_OVERRIDE_STRUCT* info = reinterpret_cast<COMBAT_DAMAGE_OVERRIDE_STRUCT*>(data);
		map<uint, MagicWeapon>::iterator iter = mapMagicWeapons.find(info->iMunitionID);
		if (iter != mapMagicWeapons.end())
		{
			info->fDamageMultiplier = iter->second.DamageMultiplier.solar;
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
