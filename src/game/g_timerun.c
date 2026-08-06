/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * @file g_timerun.c
 */

#include "g_local.h"

enum
{
	TIMERUN_ZONE_START      = 1,
	TIMERUN_ZONE_CHECKPOINT = 2,
	TIMERUN_ZONE_STOP       = 3
};

/**
 * @brief Horizontal (x/y) speed of a player in units per second
 * @param[in] ent
 * @return
 */
static int Timerun_HorizontalSpeed(gentity_t *ent)
{
	return (int)sqrt(Square(ent->client->ps.velocity[0]) + Square(ent->client->ps.velocity[1]));
}

/**
 * @brief Sends a server command to all spectators currently following ent
 * @param[in] ent
 * @param[in] cmd
 */
static void Timerun_SendToSpectators(gentity_t *ent, const char *cmd)
{
	int i;

	for (i = 0; i < level.numConnectedClients; i++)
	{
		gentity_t *player = &g_entities[level.sortedClients[i]];

		if (player->client && player->client->sess.sessionTeam == TEAM_SPECTATOR &&
		    player->client->sess.spectatorState == SPECTATOR_FOLLOW &&
		    player->client->sess.spectatorClient == ent - g_entities)
		{
			trap_SendServerCommand(i, cmd);
		}
	}
}

/**
 * @brief Aborts (time 0) or ends a timerun, notifying the client and its spectators
 * @param[in] ent
 * @param[in] time final time, or 0 when the run was aborted
 */
void notify_timerun_stop(gentity_t *ent, int time)
{
	if (!ent->client || !ent->client->sess.timerunActive)
	{
		return;
	}

	trap_SendServerCommand(ent - g_entities, va("timerun_stop %d %d %d %d",
	                                             ent->client->sess.currentTimerun, time,
	                                             ent->client->sess.timerunStopSpeed,
	                                             ent->client->sess.timerunMaxSpeed));

	Timerun_SendToSpectators(ent, va("timerun_stop_spec %d %d %d %d %d",
	                                 ent->client->sess.currentTimerun, (int)(ent - g_entities), time,
	                                 ent->client->sess.timerunStopSpeed,
	                                 ent->client->sess.timerunMaxSpeed));

	ent->client->sess.timerunActive = qfalse;
}

/**
 * @brief Starts a timerun for ent
 * @param[in] ent
 * @param[in] index index into level.timeruns
 * @param[in] def
 */
static void Timerun_StartRun(gentity_t *ent, int index, timerunDef_t *def)
{
	gclient_t *client = ent->client;

	if (client->sess.timerunActive || client->ps.pm_type != PM_NORMAL ||
	    client->ps.stats[STAT_HEALTH] <= 0)
	{
		return;
	}

	if (def->blockPrejump && Timerun_HorizontalSpeed(ent) > 600)
	{
		trap_SendServerCommand(ent - g_entities, "cp \"^dPrejump blocked: start the run from a standstill\n\"");
		return;
	}

	client->sess.timerunActive            = qtrue;
	client->sess.currentTimerun           = index;
	client->sess.timerunStartTime         = client->ps.commandTime;
	client->sess.timerunStartSpeed        = Timerun_HorizontalSpeed(ent);
	client->sess.timerunStopSpeed         = 0;
	client->sess.timerunMaxSpeed          = 0;
	client->sess.timerunCheckpointsPassed = 0;
	memset(client->sess.timerunCheckpointTimes, 0, sizeof(client->sess.timerunCheckpointTimes));

	trap_SendServerCommand(ent - g_entities, va("timerun_start %d %d %d",
	                                             index, client->ps.commandTime + 500,
	                                             client->sess.timerunStartSpeed));

	Timerun_SendToSpectators(ent, va("timerun_start_spec %d %d %d %d",
	                                 index, (int)(ent - g_entities), client->ps.commandTime + 500,
	                                 client->sess.timerunStartSpeed));
}

/**
 * @brief Records a checkpoint for the active timerun
 * @param[in] ent
 * @param[in] index
 */
static void Timerun_Checkpoint(gentity_t *ent, int index)
{
	gclient_t *client = ent->client;
	int       cp, delta, best, status;

	if (!client->sess.timerunActive || client->sess.currentTimerun != index)
	{
		return;
	}

	if (client->sess.timerunCheckpointsPassed >= MAX_TIMERUN_CHECKPOINTS)
	{
		return;
	}

	cp    = client->sess.timerunCheckpointsPassed++;
	delta = client->ps.commandTime - client->sess.timerunStartTime;

	client->sess.timerunCheckpointTimes[cp] = delta;

	best = client->sess.timerunBestCheckpointTimes[index][cp];

	if (best == 0)
	{
		status = 0;      // first-ever
	}
	else if (delta == best)
	{
		status = 1;      // equal
	}
	else if (delta < best)
	{
		status = 2;      // faster
	}
	else
	{
		status = 3;      // slower
	}

	trap_SendServerCommand(ent - g_entities, va("timerun_check %d %d %d", delta, client->ps.commandTime, status));
	Timerun_SendToSpectators(ent, va("timerun_check_spec %d %d %d", delta, client->ps.commandTime, status));
}

/**
 * @brief Finishes (or aborts) the active timerun at the stop zone
 * @param[in] ent
 * @param[in] index
 * @param[in] def
 */
static void Timerun_StopRun(gentity_t *ent, int index, timerunDef_t *def)
{
	gclient_t *client = ent->client;
	int       time;

	if (!client->sess.timerunActive || client->sess.currentTimerun != index ||
	    client->ps.pm_type != PM_NORMAL || client->ps.stats[STAT_HEALTH] <= 0)
	{
		return;
	}

	// not enough checkpoints passed -> the run does not count
	if (client->sess.timerunCheckpointsPassed < def->mincheckpoints)
	{
		notify_timerun_stop(ent, 0);
		return;
	}

	time = client->ps.commandTime - client->sess.timerunStartTime;

	client->sess.timerunLastTime[index] = time;
	client->sess.timerunStopSpeed       = Timerun_HorizontalSpeed(ent);

	if (client->sess.timerunBestTime[index] == 0 || time < client->sess.timerunBestTime[index])
	{
		client->sess.timerunBestTime[index] = time;
		memcpy(client->sess.timerunBestCheckpointTimes[index],
		       client->sess.timerunCheckpointTimes,
		       sizeof(client->sess.timerunCheckpointTimes));
	}

	trap_SendServerCommand(ent - g_entities, va("timerun_stop %d %d %d %d",
	                                             index, time, client->sess.timerunStopSpeed,
	                                             client->sess.timerunMaxSpeed));

	Timerun_SendToSpectators(ent, va("timerun_stop_spec %d %d %d %d %d",
	                                 index, (int)(ent - g_entities), time,
	                                 client->sess.timerunStopSpeed, client->sess.timerunMaxSpeed));

	client->sess.timerunActive = qfalse;
}

/**
 * @brief Zone touch: starts, checkpoints or stops runs based on the zone type
 * @param[in] self
 * @param[in] other
 * @param[in] trace - unused
 */
static void Timerun_ZoneTouch(gentity_t *self, gentity_t *other, trace_t *trace)
{
	timerunDef_t *def;

	if (!other->client || other->client->sess.sessionTeam == TEAM_SPECTATOR)
	{
		return;
	}

	if (self->count < 0 || self->count >= level.numTimeruns)
	{
		return;
	}

	def = &level.timeruns[self->count];

	switch (self->count2)
	{
	case TIMERUN_ZONE_START:
		Timerun_StartRun(other, self->count, def);
		break;
	case TIMERUN_ZONE_CHECKPOINT:
		Timerun_Checkpoint(other, self->count);
		break;
	case TIMERUN_ZONE_STOP:
		Timerun_StopRun(other, self->count, def);
		break;
	default:
		break;
	}
}

/**
 * @brief Spawns one invisible touch zone for a timerun
 * @param[in] index
 * @param[in] zoneType
 * @param[in] origin
 * @param[in] radius
 */
static void Timerun_SpawnZone(int index, int zoneType, const vec3_t origin, float radius)
{
	gentity_t *zone = G_Spawn();

	zone->classname  = "timerun_zone";
	zone->s.eType    = ET_TRIGGER_MULTIPLE;
	zone->r.contents = CONTENTS_TRIGGER;
	zone->touch      = Timerun_ZoneTouch;
	zone->count      = index;
	zone->count2     = zoneType;
	zone->r.svFlags |= SVF_NOCLIENT;

	VectorCopy(origin, zone->s.origin);
	VectorCopy(origin, zone->r.currentOrigin);
	VectorSet(zone->r.mins, -radius, -radius, -radius);
	VectorSet(zone->r.maxs, radius, radius, radius);

	trap_LinkEntity(zone);
}

/**
 * @brief Spawns all timerun zones from the lua-defined registry and exposes the run
 *        names via CS_TIMERUNS configstrings. Called once per map load, after
 *        G_LuaTimerunLoadMap().
 */
void G_InitTimeruns(void)
{
	int i, j;

	for (i = 0; i < level.numTimeruns; i++)
	{
		timerunDef_t *def = &level.timeruns[i];

		Timerun_SpawnZone(i, TIMERUN_ZONE_START, def->startOrigin, def->radius);
		Timerun_SpawnZone(i, TIMERUN_ZONE_STOP, def->stopOrigin, def->radius);

		for (j = 0; j < def->numCheckpoints; j++)
		{
			Timerun_SpawnZone(i, TIMERUN_ZONE_CHECKPOINT, def->checkpointOrigins[j], def->radius);
		}

		trap_SetConfigstring(CS_TIMERUNS + i, def->name);
	}

	// clear leftover configstrings from a previous map
	for (; i < MAX_TIMERUNS; i++)
	{
		trap_SetConfigstring(CS_TIMERUNS + i, "");
	}

	level.isTimerun = (level.numTimeruns > 0);
	trap_Cvar_Set("isTimerun", level.isTimerun ? "1" : "0");

	G_Printf("Timeruns: %d run(s) defined for this map\n", level.numTimeruns);
}
