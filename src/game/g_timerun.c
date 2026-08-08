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
 * @brief Whether ent is a client currently running a timerun
 * @param[in] ent
 * @return qtrue when the client has an active run
 */
qboolean Timerun_ClientIsRunning(gentity_t *ent)
{
	return ent && ent->client && ent->client->sess.timerunActive;
}

/**
 * @brief speedrun mod: whether a timerun def may be loaded under the active config
 *
 * The active config (g_customConfig) selects which run types are allowed:
 * shortruns -> "short", fullmaprun -> "full". Defs whose type is missing are
 * blocked whenever a filter config is active; they only load under configs
 * with no filter (or none at all). Rejected defs are never spawned and never
 * reach the client (no zone, no CS_TIMERUNS entry).
 *
 * @param[in] def
 * @return qtrue when the def may be registered
 */
qboolean Timerun_DefAllowedByConfig(const timerunDef_t *def)
{
	static const struct
	{
		const char *config;
		const char *type;
	} filters[] =
	{
		{ "shortruns",  "short" },
		{ "fullmaprun", "full"  },
	};
	char  activeConfig[MAX_CVAR_VALUE_STRING];
	int   i;
	const char *allowedType = NULL;

	trap_Cvar_VariableStringBuffer("g_customConfig", activeConfig, sizeof(activeConfig));

	for (i = 0; i < (int)ARRAY_LEN(filters); i++)
	{
		if (!Q_stricmp(activeConfig, filters[i].config))
		{
			allowedType = filters[i].type;
			break;
		}
	}

	if (!allowedType)
	{
		return qtrue;    // config has no run filter — everything loads
	}

	// a filter is active: the def must carry the matching type
	return def->type[0] && !Q_stricmp(def->type, allowedType);
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
 * @brief speedrun mod: ends every active timerun (e.g. on a config change)
 *
 * Runs live in client->sess, which survives map_restart — the path a config
 * vote takes (G_configSet queues map_restart 0 GS_RESET) — so without this
 * a run would keep ticking through a config change. Called from G_configSet.
 */
void Timerun_StopAllRuns(void)
{
	int i;

	for (i = 0; i < level.maxclients; i++)
	{
		gentity_t *ent = g_entities + i;

		if (ent->client && ent->client->sess.timerunActive)
		{
			notify_timerun_stop(ent, 0);
		}
	}
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

	// speedrun mod: runs only start while the actual match is running
	// (blocks GS_WARMUP, GS_WARMUP_COUNTDOWN, GS_INTERMISSION, ...)
	if (g_gamestate.integer != GS_PLAYING)
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

	// speedrun mod: re-arm this run's checkpoint zones for the new run
	{
		int i;

		for (i = 0; i < MAX_GENTITIES; i++)
		{
			gentity_t *zone = &g_entities[i];

			if (zone->inuse && zone->classname && !Q_stricmp(zone->classname, "timerun_zone")
			    && zone->count == index && zone->count2 == TIMERUN_ZONE_CHECKPOINT)
			{
				zone->s.time  = 0;
				zone->s.time2 = 0;   // speedrun mod: re-arm the no-run touch message too
			}
		}
	}
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

	// speedrun mod: private center-print confirmation (only the runner sees it;
	// display gated client-side on speedrun_debug)
	trap_SendServerCommand(ent - g_entities, va("timerun_cp \"^2Run started: ^n%s\n\"", def->name));
}

/**
 * @brief Records a checkpoint for the active timerun
 * @param[in] ent
 * @param[in] zone the checkpoint zone touched (its count3 is the ordinal)
 */
static void Timerun_Checkpoint(gentity_t *ent, gentity_t *zone)
{
	gclient_t *client = ent->client;
	int       index   = zone->count;
	int       cp, delta, best, status, time;

	// speedrun mod: checkpoint touched with NO active run — private center-print,
	// once per zone per run cycle (no time: there is no run being timed). The
	// s.time2 flag edge-triggers it (the touch fires every frame while the zone
	// is overlapped) and is re-armed when a run starts.
	if (!client->sess.timerunActive)
	{
		if (!zone->s.time2)
		{
			zone->s.time2 = 1;
			trap_SendServerCommand(ent - g_entities, va("timerun_cp \"^2Checkpoint ^n%d^7 touched - ^dno run started\n\"",
			                                            zone->count3 + 1));
		}
		return;
	}

	if (client->sess.currentTimerun != index)
	{
		return;
	}

	// sequential checkpoints: only the checkpoint whose ordinal equals the
	// count reached so far may fire (cp0 first, then cp1, ...). Out-of-order
	// touches leave the zone armed (s.time stays 0) so it can fire later
	// when its turn comes.
	if (client->sess.timerunCheckpointsPassed != zone->count3)
	{
		return;
	}

	// fire once per run: the touch repeats every frame while overlapping
	if (zone->s.time)
	{
		return;
	}
	zone->s.time = 1;

	cp   = client->sess.timerunCheckpointsPassed++;
	time = client->ps.commandTime - client->sess.timerunStartTime;

	client->sess.timerunCheckpointTimes[cp] = time;

	best = client->sess.timerunBestCheckpointTimes[index][cp];

	if (best == 0)
	{
		status = 0;      // first-ever
		delta  = 0;
	}
	else if (time == best)
	{
		status = 1;      // equal
		delta  = 0;
	}
	else if (time < best)
	{
		status = 2;      // faster
		delta  = best - time;
	}
	else
	{
		status = 3;      // slower
		delta  = time - best;
	}

	trap_SendServerCommand(ent - g_entities, va("timerun_check %d %d %d", delta, time, status));
	Timerun_SendToSpectators(ent, va("timerun_check_spec %d %d %d", delta, client->ps.commandTime, status));

	// speedrun mod: private center-print checkpoint confirmation (only the runner)
	{
		int    min = time / 60000, t = time - min * 60000, sec = t / 1000, milli = t - sec * 1000;

		trap_SendServerCommand(ent - g_entities, va("timerun_cp \"^2Checkpoint ^n%d^7 reached - ^2%02d:%02d.%03d\n\"",
		                                            cp + 1, min, sec, milli));
	}
}

/**
 * @brief Finishes (or aborts) the active timerun at the stop zone
 * @param[in] ent
 * @param[in] index
 * @param[in] def
 */
static void Timerun_StopRun(gentity_t *ent, gentity_t *zone, int index, timerunDef_t *def)
{
	gclient_t *client = ent->client;
	int       time;

	if (!client->sess.timerunActive || client->sess.currentTimerun != index ||
	    client->ps.pm_type != PM_NORMAL || client->ps.stats[STAT_HEALTH] <= 0)
	{
		return;
	}

	// speedrun mod: a run only counts when ALL checkpoints were reached —
	// otherwise the stop zone silently does nothing and the run keeps going
	// (no "missing checkpoint" hint: a stop zone stacked on the last
	// checkpoint position would otherwise spam confusing feed entries)
	if (client->sess.timerunCheckpointsPassed < def->numCheckpoints)
	{
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

	// speedrun mod: private center-print result (only the runner sees it)
	{
		int    min = time / 60000, t = time - min * 60000, sec = t / 1000, milli = t - sec * 1000;

		trap_SendServerCommand(ent - g_entities, va("timerun_cp \"^2Run finished: ^n%s^7 in ^2%02d:%02d.%03d\n\"",
		                                            def->name, min, sec, milli));
	}

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
		Timerun_Checkpoint(other, self);
		break;
	case TIMERUN_ZONE_STOP:
		Timerun_StopRun(other, self, self->count, def);
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
static void Timerun_SpawnZone(int index, int zoneType, const vec3_t origin, float radius, int ordinal)
{
	gentity_t *zone = G_Spawn();

	zone->classname  = "timerun_zone";
	zone->s.eType    = ET_TRIGGER_MULTIPLE;
	zone->r.contents = CONTENTS_TRIGGER;
	zone->touch      = Timerun_ZoneTouch;
	zone->count      = index;
	zone->count2     = zoneType;
	zone->count3     = ordinal;   ///< checkpoint ordinal (definition order)
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

		for (j = 0; j < def->numStarts; j++)
		{
			Timerun_SpawnZone(i, TIMERUN_ZONE_START, def->startOrigins[j], def->radius, 0);
		}

		Timerun_SpawnZone(i, TIMERUN_ZONE_STOP, def->stopOrigin, def->radius, 0);

		for (j = 0; j < def->numCheckpoints; j++)
		{
			Timerun_SpawnZone(i, TIMERUN_ZONE_CHECKPOINT, def->checkpointOrigins[j], def->radius, j);
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
