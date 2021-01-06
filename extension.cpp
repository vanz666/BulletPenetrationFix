/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include "iplayerinfo.h"
#include <IEngineTrace.h>

class CTraceFilterSimple : public CTraceFilter
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask ) = 0;
	virtual void SetPassEntity( const IHandleEntity *pPassEntity ) = 0;
	virtual void SetCollisionGroup( int iCollisionGroup ) = 0;
};

class CTraceFilterSkipTwoEntities : public CTraceFilterSimple
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask ) = 0;
	virtual void SetPassEntity2( const IHandleEntity *pPassEntity2 ) = 0;
};

class CBasePlayer;

enum LagCompensationType
{
	LAG_COMPENSATE_BOUNDS,
	LAG_COMPENSATE_HITBOXES,
	LAG_COMPENSATE_HITBOXES_ALONG_RAY,
};

class ILagCompensationManager
{
public:
	virtual void StartLagCompensation( CBasePlayer *player, LagCompensationType lagCompensationType, const Vector &weaponPos, const QAngle &weaponAngles, float weaponRange ) = 0;
	virtual void FinishLagCompensation( CBasePlayer *player ) = 0;
	virtual void AddAdditionalEntity( CBaseEntity *pEntity ) = 0;
	virtual void RemoveAdditionalEntity( CBaseEntity *pEntity ) = 0;
};

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

BulletPenetrationFix g_BulletPenetrationFix;		/**< Global singleton for extension's main interface */
SMEXT_LINK(&g_BulletPenetrationFix);

IGameConfig *g_pGameConf = NULL;

int g_SH_SkipTwoEntitiesShouldHitEntity = 0;
int g_SH_SimpleShouldHitEntity = 0;
int g_SH_StartLagCompensation = 0;
int g_SH_FinishLagCompensation = 0;

char *g_pPhysboxToClientMap = NULL;
bool g_IsCurrentlyDoingLagComp = false;
int g_LagCompPlayerTeam = 0;

SH_DECL_HOOK2(CTraceFilterSkipTwoEntities, ShouldHitEntity, SH_NOATTRIB, 0, bool, IHandleEntity *, int);
SH_DECL_HOOK2(CTraceFilterSimple, ShouldHitEntity, SH_NOATTRIB, 0, bool, IHandleEntity *, int);
SH_DECL_HOOK5_void(ILagCompensationManager, StartLagCompensation, SH_NOATTRIB, 0, CBasePlayer *, LagCompensationType, const Vector &, const QAngle &, float);
SH_DECL_HOOK1_void(ILagCompensationManager, FinishLagCompensation, SH_NOATTRIB, 0, CBasePlayer *);

bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	if (!g_IsCurrentlyDoingLagComp)
		RETURN_META_VALUE(MRES_IGNORED, true);

	if (META_RESULT_ORIG_RET(bool) == false)
		RETURN_META_VALUE(MRES_IGNORED, false);

	IServerUnknown *pUnk = (IServerUnknown *)pHandleEntity;
	CBaseEntity *pEnt = pUnk->GetBaseEntity();
	
	if (!pEnt)
		RETURN_META_VALUE(MRES_IGNORED, true);

	int index = gamehelpers->EntityToBCompatRef(pEnt);

	int iTeam = 0;

	if (index > SM_MAXPLAYERS && g_pPhysboxToClientMap && index < 2048)
	{
		index = g_pPhysboxToClientMap[index];
	}

	if (index >= -3 && index <= -1)
	{
		iTeam = -index;
	}
	else if (index < 1 || index > SM_MAXPLAYERS)
	{
		RETURN_META_VALUE(MRES_IGNORED, true);
	}

	if (!iTeam)
	{
		IGamePlayer *pPlayer = playerhelpers->GetGamePlayer(index);
		if (!pPlayer || !pPlayer->GetEdict())
			RETURN_META_VALUE(MRES_IGNORED, true);

		IPlayerInfo *pInfo = pPlayer->GetPlayerInfo();
		if (!pInfo)
			RETURN_META_VALUE(MRES_IGNORED, true);

		iTeam = pInfo->GetTeamIndex();
	}

	if (iTeam == g_LagCompPlayerTeam)
		RETURN_META_VALUE(MRES_SUPERCEDE, false);

	RETURN_META_VALUE(MRES_IGNORED, true);
}

void StartLagCompensation(CBasePlayer *player, LagCompensationType lagCompensationType, const Vector &weaponPos, const QAngle &weaponAngles, float weaponRange)
{
	int index = gamehelpers->EntityToBCompatRef((CBaseEntity *)player);
	
	IGamePlayer *pPlayer = playerhelpers->GetGamePlayer(index);
	if (!pPlayer || !pPlayer->GetEdict())
		RETURN_META(MRES_IGNORED);

	IPlayerInfo *pInfo = pPlayer->GetPlayerInfo();
	if (!pInfo)
		RETURN_META(MRES_IGNORED);

	g_LagCompPlayerTeam = pInfo->GetTeamIndex();
	
	g_IsCurrentlyDoingLagComp = true;
	
	RETURN_META(MRES_IGNORED);
}

void FinishLagCompensation(CBasePlayer *player)
{
	g_IsCurrentlyDoingLagComp = false;
	
	RETURN_META(MRES_IGNORED);
}

cell_t PhysboxToClientMap(IPluginContext *pContext, const cell_t *params)
{
	if (params[2])
		pContext->LocalToPhysAddr(params[1], (cell_t **)&g_pPhysboxToClientMap);
	else
		g_pPhysboxToClientMap = NULL;

	return 0;
}

const sp_nativeinfo_t g_Natives[] =
{
	{ "PhysboxToClientMap", PhysboxToClientMap },
	{ NULL, NULL }
};

bool BulletPenetrationFix::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
	char conf_error[255] = "";
	if (!gameconfs->LoadGameConfigFile("BulletPenetrationFix", &g_pGameConf, conf_error, sizeof(conf_error)))
	{
		if (conf_error[0])
		{
			snprintf(error, maxlength, "Could not read BulletPenetrationFix.txt: %s", conf_error);
		}
		return false;
	}
	
	sharesys->AddNatives(myself, g_Natives);
	sharesys->RegisterLibrary(myself, "BulletPenetrationFix");
	
	ILagCompensationManager *lagcompensation;
	if (!g_pGameConf->GetAddress("lagcompensation", (void **)&lagcompensation) || lagcompensation == NULL)
	{
		snprintf(error, maxlength, "Failed to locate lagcompensation ptr");
		return false;
	}
	
	void *addr;
	int callOffset;
	int vtableOffset;
	int32_t funcOffset;
	CTraceFilterSkipTwoEntities *pCTraceFilterSkipTwoEntities;
	CTraceFilterSimple *pCTraceFilterSimple;
	
	if (!g_pGameConf->GetMemSig("FX_FireBullets", &addr) || addr == NULL)
	{
		snprintf(error, maxlength, "Failed to locate FX_FireBullets function");
		return false;
	}
	
	if (!g_pGameConf->GetOffset("FireBulletCallOffset", &callOffset) || callOffset == -1)
	{
		snprintf(error, maxlength, "Failed to find FireBulletCallOffset offset");
		return false;
	}
	
	// get relative address to CCSPlayer::FireBullet in FX_FireBullets
	funcOffset = *(int32_t *)((intptr_t)addr + callOffset);
	
	// calc absolute address of CCSPlayer::FireBullet
	addr = (void *)((intptr_t)addr + callOffset + 4 + funcOffset);
	
	if (!g_pGameConf->GetOffset("CTraceFilterSkipTwoEntitiesCallOffset", &callOffset) || callOffset == -1)
	{
		snprintf(error, maxlength, "Failed to find CTraceFilterSkipTwoEntitiesCallOffset offset");
		return false;
	}

	if (!g_pGameConf->GetOffset("CTraceFilterSkipTwoEntitiesVTableOffset", &vtableOffset) || vtableOffset == -1)
	{
		snprintf(error, maxlength, "Failed to find CTraceFilterSkipTwoEntitiesVTableOffset offset");
		return false;
	}
	
	// get relative address to CTraceFilterSkipTwoEntities constructor in CCSPlayer::FireBullet
	funcOffset = *(int32_t *)((intptr_t)addr + callOffset);
	
	// calc CTraceFilterSkipTwoEntities vtable address
	pCTraceFilterSkipTwoEntities = *(CTraceFilterSkipTwoEntities **)((intptr_t)addr + callOffset + 4 + funcOffset + vtableOffset);

	if (!g_pGameConf->GetOffset("CTraceFilterSimpleCallOffset", &callOffset) || callOffset == -1)
	{
		snprintf(error, maxlength, "Failed to find CTraceFilterSimpleCallOffset offset");
		return false;
	}

	if (!g_pGameConf->GetOffset("CTraceFilterSimpleVTableOffset", &vtableOffset) || vtableOffset == -1)
	{
		snprintf(error, maxlength, "Failed to find CTraceFilterSimpleVTableOffset offset");
		return false;
	}
	
	// get relative address to CTraceFilterSimple constructor in CCSPlayer::FireBullet
	funcOffset = *(int32_t *)((intptr_t)addr + callOffset);
	
	// calc CTraceFilterSimple vtable address
	pCTraceFilterSimple = *(CTraceFilterSimple **)((intptr_t)addr + callOffset + 4 + funcOffset + vtableOffset);

	g_SH_SkipTwoEntitiesShouldHitEntity = SH_ADD_DVPHOOK(CTraceFilterSkipTwoEntities, ShouldHitEntity, pCTraceFilterSkipTwoEntities, SH_STATIC(ShouldHitEntity), true);
	g_SH_SimpleShouldHitEntity = SH_ADD_DVPHOOK(CTraceFilterSimple, ShouldHitEntity, pCTraceFilterSimple, SH_STATIC(ShouldHitEntity), true);
	g_SH_StartLagCompensation = SH_ADD_HOOK(ILagCompensationManager, StartLagCompensation, lagcompensation, SH_STATIC(StartLagCompensation), false);
	g_SH_FinishLagCompensation = SH_ADD_HOOK(ILagCompensationManager, FinishLagCompensation, lagcompensation, SH_STATIC(FinishLagCompensation), false);

	return true;
}

void BulletPenetrationFix::SDK_OnUnload()
{
	if (g_SH_SkipTwoEntitiesShouldHitEntity != 0)
	{
		SH_REMOVE_HOOK_ID(g_SH_SkipTwoEntitiesShouldHitEntity);
		g_SH_SkipTwoEntitiesShouldHitEntity = 0;
	}

	if (g_SH_SimpleShouldHitEntity != 0)
	{
		SH_REMOVE_HOOK_ID(g_SH_SimpleShouldHitEntity);
		g_SH_SimpleShouldHitEntity = 0;
	}
	
	if (g_SH_StartLagCompensation != 0)
	{
		SH_REMOVE_HOOK_ID(g_SH_StartLagCompensation);
		g_SH_StartLagCompensation = 0;
	}

	if (g_SH_FinishLagCompensation != 0)
	{
		SH_REMOVE_HOOK_ID(g_SH_FinishLagCompensation);
		g_SH_FinishLagCompensation = 0;
	}
	
	gameconfs->CloseGameConfigFile(g_pGameConf);
}
