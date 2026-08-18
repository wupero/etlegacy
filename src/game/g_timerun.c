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
			// level.sortedClients is qsort-sorted by rank, so its index != clientNum;
			// send to the spectator's actual clientNum (was 'i' - a latent bug that
			// sent run events to the wrong client)
			trap_SendServerCommand(level.sortedClients[i], cmd);
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
 * @brief speedrun mod: numeric value of the group a client's /speedrun list
 *        currently browses.
 *
 * Falls back to 1 when the stored selection is not available on this map, or
 * when the map has no groups/runs at all.
 *
 * @param[in] client
 * @return the selected group number (a value from level.timerunGroups, or 1)
 */
int Timerun_ClientGroupValue(const gclient_t *client)
{
	int g = (client && client->sess.speedrunGroup > 0) ? client->sess.speedrunGroup : 1;

	if (level.numTimerunGroups <= 0)
	{
		return 1;
	}

	if (!Timerun_GroupAvailable(g))
	{
		g = 1;
	}

	return g;
}

/**
 * @brief speedrun mod: whether a group number is available on the current map.
 *
 * @param[in] group group number to test
 * @return qtrue when group is in level.timerunGroups
 */
qboolean Timerun_GroupAvailable(int group)
{
	int i;

	for (i = 0; i < level.numTimerunGroups; i++)
	{
		if (level.timerunGroups[i] == group)
		{
			return qtrue;
		}
	}

	return qfalse;
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

	trap_SendServerCommand(ent - g_entities, va("timerun_stop %d %d %d %d %d",
	                                             ent->client->sess.currentTimerun, time,
	                                             ent->client->sess.timerunStopSpeed,
	                                             ent->client->sess.timerunMaxSpeed,
	                                             ent->client->sess.speedrunMode));

	Timerun_SendToSpectators(ent, va("timerun_stop_spec %d %d %d %d %d %d",
	                                 ent->client->sess.currentTimerun, (int)(ent - g_entities), time,
	                                 ent->client->sess.timerunStopSpeed,
	                                 ent->client->sess.timerunMaxSpeed,
	                                 ent->client->sess.speedrunMode));

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

	trap_SendServerCommand(ent - g_entities, va("timerun_start %d %d %d %d",
	                                             index, client->ps.commandTime + 500,
	                                             client->sess.timerunStartSpeed,
	                                             client->sess.speedrunMode));

	Timerun_SendToSpectators(ent, va("timerun_start_spec %d %d %d %d %d",
	                                 index, (int)(ent - g_entities), client->ps.commandTime + 500,
	                                 client->sess.timerunStartSpeed,
	                                 client->sess.speedrunMode));

	// speedrun mod: private center-print confirmation (only the runner sees it;
	// display gated client-side on speedrun_debug)
	trap_SendServerCommand(ent - g_entities, "timerun_cp \"^2Run started\n\"");
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

	best = client->sess.timerunBestCheckpointTimes[index][client->sess.speedrunMode - 1][cp];

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
	Timerun_SendToSpectators(ent, va("timerun_check_spec %d %d %d", delta, time, status));

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

	client->sess.timerunLastTime[index][client->sess.speedrunMode - 1] = time;
	client->sess.timerunStopSpeed       = Timerun_HorizontalSpeed(ent);

	if (client->sess.timerunBestTime[index][client->sess.speedrunMode - 1] == 0 ||
	    time < client->sess.timerunBestTime[index][client->sess.speedrunMode - 1])
	{
		client->sess.timerunBestTime[index][client->sess.speedrunMode - 1] = time;
		memcpy(client->sess.timerunBestCheckpointTimes[index][client->sess.speedrunMode - 1],
		       client->sess.timerunCheckpointTimes,
		       sizeof(client->sess.timerunCheckpointTimes));
	}

	trap_SendServerCommand(ent - g_entities, va("timerun_stop %d %d %d %d %d",
	                                             index, time, client->sess.timerunStopSpeed,
	                                             client->sess.timerunMaxSpeed,
	                                             client->sess.speedrunMode));

	Timerun_SendToSpectators(ent, va("timerun_stop_spec %d %d %d %d %d %d",
	                                 index, (int)(ent - g_entities), time,
	                                 client->sess.timerunStopSpeed, client->sess.timerunMaxSpeed,
	                                 client->sess.speedrunMode));

	// speedrun mod: private center-print result (only the runner sees it)
	{
		int    min = time / 60000, t = time - min * 60000, sec = t / 1000, milli = t - sec * 1000;

		trap_SendServerCommand(ent - g_entities, va("timerun_cp \"^2Run finished^7 in ^2%02d:%02d.%03d\n\"",
		                                            min, sec, milli));
	}

	// speedrun mod: record the run to the backend (async). Only when the player
	// has a speedrun_key; otherwise tell them the time is not recorded.
	if (client->sess.speedrunKey[0])
	{
		G_API_SendRecord(ent, def, time);
	}
	else
	{
		// targeted to the finishing player only; client center-prints AND logs it
		// to that player's console (never broadcast, so the console line is private)
		trap_SendServerCommand(ent - g_entities,
		                       "timerun_nokey \"^3Time not recorded^7 - set /speedrun_key to record runs (get one at TO_BE_DECIDED after logging in)\n\"");
	}

	client->sess.timerunActive = qfalse;
}

static qboolean Timerun_ZoneContains(const gentity_t *zone, const vec3_t point);   // speedrun mod

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

	// speedrun mod: a player can only run runs of their selected group — zones
	// of every other group are inert for them (start/checkpoint/stop). Since
	// /speedrun_group is blocked during a run, a player can never be mid-run on
	// a run outside their group, so the stop zone always matches.
	if (def->group != Timerun_ClientGroupValue(other->client))
	{
		return;
	}

	// speedrun mod: the engine's contact test only sees the enclosing AABB of a
	// rotated zone - re-check the player against the real cube
	if (!Timerun_ZoneContains(self, other->client->ps.origin))
	{
		return;
	}

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
 * @brief speedrun mod: precise containment test for a (possibly rotated) cube
 *        zone. The engine's trigger contact only sees the axis-aligned bounds
 *        enclosing the rotated cube, so the touch handler re-checks the player
 *        against the real cube (inverse-rotated by yaw).
 * @return qtrue when the point lies inside the zone's cube.
 */
static qboolean Timerun_ZoneContains(const gentity_t *zone, const vec3_t point)
{
	vec3_t local;
	float  yaw, c, s;

	VectorSubtract(point, zone->r.currentOrigin, local);
	yaw = zone->TargetAngles[2];

	if (yaw != 0.0f)
	{
		// rotate the offset back into the zone's local frame (yaw around Z)
		c = cosf(DEG2RAD(yaw));
		s = sinf(DEG2RAD(yaw));

		float x = local[0], y = local[1];

		local[0] =  c * x + s * y;
		local[1] = -s * x + c * y;
	}

	return (fabs(local[0]) <= zone->rotate[0]
	        && fabs(local[1]) <= zone->rotate[0]
	        && fabs(local[2]) <= zone->rotate[0]);
}

/**
 * @brief Spawns one invisible touch zone for a timerun
 * @param[in] index    run index
 * @param[in] zoneType start/stop/checkpoint
 * @param[in] origin   zone center
 * @param[in] radius   half-extent of the cube
 * @param[in] yaw      rotation around Z in degrees
 * @param[in] ordinal  checkpoint ordinal (definition order)
 *
 * @details The trigger's linked bounds are the axis-aligned box enclosing the
 * (possibly rotated) cube so the engine fires the touch; Timerun_ZoneContains
 * does the exact rotated-cube test before dispatching. The real radius and yaw
 * are kept on the entity (rotate[0] / TargetAngles[2]).
 */
static void Timerun_SpawnZone(int index, int zoneType, const vec3_t origin, float radius, float yaw, int ordinal)
{
	gentity_t *zone = G_Spawn();
	vec3_t    corners[8];
	float     c = 0, s = 0;
	int       i;

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
	VectorSet(zone->rotate, radius, radius, radius);
	zone->TargetAngles[2] = yaw;

	if (yaw != 0.0f)
	{
		c = cosf(DEG2RAD(yaw));
		s = sinf(DEG2RAD(yaw));
	}

	// compute the 8 rotated corners and take their AABB for the engine bounds
	for (i = 0; i < 8; i++)
	{
		float x = (i & 1) ? radius : -radius;
		float y = (i & 2) ? radius : -radius;
		float z = (i & 4) ? radius : -radius;

		if (yaw != 0.0f)
		{
			corners[i][0] = c * x - s * y;
			corners[i][1] = s * x + c * y;
		}
		else
		{
			corners[i][0] = x;
			corners[i][1] = y;
		}
		corners[i][2] = z;
	}

	VectorCopy(corners[0], zone->r.mins);
	VectorCopy(corners[0], zone->r.maxs);

	for (i = 1; i < 8; i++)
	{
		zone->r.mins[0] = corners[i][0] < zone->r.mins[0] ? corners[i][0] : zone->r.mins[0];
		zone->r.mins[1] = corners[i][1] < zone->r.mins[1] ? corners[i][1] : zone->r.mins[1];
		zone->r.mins[2] = corners[i][2] < zone->r.mins[2] ? corners[i][2] : zone->r.mins[2];
		zone->r.maxs[0] = corners[i][0] > zone->r.maxs[0] ? corners[i][0] : zone->r.maxs[0];
		zone->r.maxs[1] = corners[i][1] > zone->r.maxs[1] ? corners[i][1] : zone->r.maxs[1];
		zone->r.maxs[2] = corners[i][2] > zone->r.maxs[2] ? corners[i][2] : zone->r.maxs[2];
	}

	trap_LinkEntity(zone);
}

/**
 * @brief speedrun mod: sends the zone geometry (origins + radius, one command per
 *        zone) to a client for the speedrun_debug blue-box overlay and the zone
 *        markers. clientNum -1 broadcasts. Called from ClientBegin so fresh
 *        connects and map changes both receive the current map's zones, and from
 *        Timerun_UpdateClientMarkers when the group a client should see changes.
 */
void Timerun_SendZoneDebugToClient(int clientNum)
{
	int i, j;
	int selGroup;

	// speedrun mod: tell the client to drop any previously received zone
	// geometry first, so a group change (or map_restart re-send on ClientBegin)
	// can't leave boxes/markers of runs from another group on screen.
	trap_SendServerCommand(clientNum, "timerun_zones_clear");

	if (clientNum < 0 || clientNum >= MAX_CLIENTS || !g_entities[clientNum].client)
	{
		return;
	}

	// speedrun mod: only the runs of the group this client should see are pushed
	// (their own selection, or the followed player's when spectating), so the
	// client's markers/debug boxes always match that group's runs.
	selGroup = Timerun_ClientMarkerGroup(clientNum);

	for (i = 0; i < level.numTimeruns; i++)
	{
		timerunDef_t *def = &level.timeruns[i];

		if (def->group != selGroup)
		{
			continue;
		}

		for (j = 0; j < def->numStarts; j++)
		{
			trap_SendServerCommand(clientNum, va("timerun_zones %d %d %f %f %f %f %f",
			                                     i, TIMERUN_ZONE_START,
			                                     def->startOrigins[j][0], def->startOrigins[j][1], def->startOrigins[j][2],
			                                     def->startRadius[j], def->startYaw[j]));
		}

		trap_SendServerCommand(clientNum, va("timerun_zones %d %d %f %f %f %f %f",
		                                     i, TIMERUN_ZONE_STOP,
		                                     def->stopOrigin[0], def->stopOrigin[1], def->stopOrigin[2],
		                                     def->stopRadius, def->stopYaw));

		for (j = 0; j < def->numCheckpoints; j++)
		{
			trap_SendServerCommand(clientNum, va("timerun_zones %d %d %f %f %f %f %f",
			                                     i, TIMERUN_ZONE_CHECKPOINT,
			                                     def->checkpointOrigins[j][0], def->checkpointOrigins[j][1], def->checkpointOrigins[j][2],
			                                     def->checkpointRadius[j], def->checkpointYaw[j]));
		}
	}

	level.lastMarkerGroup[clientNum] = selGroup;
}

/**
 * @brief speedrun mod: the group whose run markers/debug boxes a client should
 *        see. A spectator sees the group of the player they follow; everyone
 *        else (including a spectator not following anyone) sees their own
 *        selected group.
 *
 * @param[in] clientNum
 * @return a group number from level.timerunGroups (fallback 1)
 */
int Timerun_ClientMarkerGroup(int clientNum)
{
	gclient_t *client;

	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return 1;
	}

	client = g_entities[clientNum].client;

	if (!client)
	{
		return 1;
	}

	if (client->sess.sessionTeam == TEAM_SPECTATOR &&
	    client->sess.spectatorState == SPECTATOR_FOLLOW &&
	    client->sess.spectatorClient >= 0 && client->sess.spectatorClient < MAX_CLIENTS &&
	    g_entities[client->sess.spectatorClient].client)
	{
		return Timerun_ClientGroupValue(g_entities[client->sess.spectatorClient].client);
	}

	return Timerun_ClientGroupValue(client);
}

/**
 * @brief speedrun mod: re-pushes a client's run markers whenever the group they
 *        should see (their own, or their followed player's) has changed. Called
 *        every frame from ClientEndFrame; a no-op when unchanged.
 *
 * @param[in] clientNum
 */
void Timerun_UpdateClientMarkers(int clientNum)
{
	int mg;

	if (clientNum < 0 || clientNum >= MAX_CLIENTS || !g_entities[clientNum].client)
	{
		return;
	}

	mg = Timerun_ClientMarkerGroup(clientNum);

	if (mg != level.lastMarkerGroup[clientNum])
	{
		Timerun_SendZoneDebugToClient(clientNum);   // records level.lastMarkerGroup
	}
}

/**
 * @brief speedrun mod: builds level.timerunGroups from the unique numeric group
 *        values of the loaded defs (first-seen/lua order).
 */
static void Timerun_BuildGroupTable(void)
{
	int i, j, g;

	level.numTimerunGroups = 0;

	for (i = 0; i < level.numTimeruns; i++)
	{
		g = level.timeruns[i].group;

		for (j = 0; j < level.numTimerunGroups; j++)
		{
			if (level.timerunGroups[j] == g)
			{
				break;
			}
		}

		if (j == level.numTimerunGroups && j < MAX_TIMERUN_GROUPS)
		{
			level.timerunGroups[j] = g;
			level.numTimerunGroups++;
		}
	}
}

/**
 * @brief Spawns all timerun zones from the lua-defined registry and exposes the run
 *        names via CS_TIMERUNS configstrings. Called once per map load, after
 *        G_LuaTimerunLoadMap().
 */
void G_InitTimeruns(void)
{
	int i, j;

	Timerun_BuildGroupTable();

	for (i = 0; i < level.numTimeruns; i++)
	{
		timerunDef_t *def = &level.timeruns[i];

		for (j = 0; j < def->numStarts; j++)
		{
			Timerun_SpawnZone(i, TIMERUN_ZONE_START, def->startOrigins[j], def->startRadius[j], def->startYaw[j], 0);
		}

		Timerun_SpawnZone(i, TIMERUN_ZONE_STOP, def->stopOrigin, def->stopRadius, def->stopYaw, 0);

		for (j = 0; j < def->numCheckpoints; j++)
		{
			Timerun_SpawnZone(i, TIMERUN_ZONE_CHECKPOINT, def->checkpointOrigins[j], def->checkpointRadius[j], def->checkpointYaw[j], j);
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

	G_Printf("Timeruns: %d run(s), %d group(s) defined for this map\n", level.numTimeruns, level.numTimerunGroups);
}
