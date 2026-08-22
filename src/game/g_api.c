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
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**
 * @file g_api.c
 * @brief speedrun mod: backend HTTP API glue (game layer)
 *
 * Requests are enqueued through trap_API_Request (engine curl multi layer,
 * src/server/sv_game.c). Completed responses are polled every frame from
 * G_RunFrame via G_API_Frame.
 */

#include "g_local.h"
#include "json.h"

/**
 * @brief speedrun mod: formats a millisecond duration as MM:SS.mmm
 */
static void G_API_ApplyServerRecord(gentity_t *ent, int httpCode, const char *text);   ///< speedrun mod
static void G_API_PrintServerBest(gentity_t *ent, int httpCode, const char *text);      ///< speedrun mod
static void G_API_PrintRunLeaderboard(gentity_t *ent, int httpCode, const char *text);    ///< speedrun mod
static void G_API_PrintServerElo(gentity_t *ent, int httpCode, const char *text);          ///< speedrun mod

static void Timerun_FormatTimeMs(char *buf, int len, int timeMs)
{
	int min = timeMs / 60000, t = timeMs - min * 60000, sec = t / 1000, milli = t - sec * 1000;

	Com_sprintf(buf, len, "%02d:%02d.%03d", min, sec, milli);
}

/**
 * @brief speedrun mod: polls completed backend API requests and dispatches them.
 * @details Called every frame from G_RunFrame. The request id is the finishing
 * player's clientNum, so a response can be reported back to that player. A 200
 * response is the record result ({kind,place,totalPlayers}); anything else is
 * an error reported to the player's center print.
 */
void G_API_Frame(void)
{
	int  id, httpCode;
	char text[2048];

	while (trap_API_GetResult(&id, &httpCode, text, sizeof(text)))
	{
		cJSON *root;

		// speedrun mod: a server-elo leaderboard response (id = 4*MAX_CLIENTS + clientNum)
		if (id >= 4 * MAX_CLIENTS)
		{
			int cn = id - 4 * MAX_CLIENTS;

			if (cn >= 0 && cn < MAX_CLIENTS && g_entities[cn].client)
			{
				G_API_PrintServerElo(&g_entities[cn], httpCode, text);
			}
			continue;
		}

		// speedrun mod: a run-leaderboard response (id = 3*MAX_CLIENTS + clientNum)
		if (id >= 3 * MAX_CLIENTS)
		{
			int cn = id - 3 * MAX_CLIENTS;

			if (cn >= 0 && cn < MAX_CLIENTS && g_entities[cn].client)
			{
				G_API_PrintRunLeaderboard(&g_entities[cn], httpCode, text);
			}
			continue;
		}

		// speedrun mod: a server-best list response (id = 2*MAX_CLIENTS + clientNum)
		if (id >= 2 * MAX_CLIENTS)
		{
			int cn = id - 2 * MAX_CLIENTS;

			if (cn >= 0 && cn < MAX_CLIENTS && g_entities[cn].client)
			{
				G_API_PrintServerBest(&g_entities[cn], httpCode, text);
			}
			continue;
		}

		// speedrun mod: a server-record fetch response (id = MAX_CLIENTS + clientNum)
		if (id >= MAX_CLIENTS)
		{
			int cn = id - MAX_CLIENTS;

			if (cn >= 0 && cn < MAX_CLIENTS && g_entities[cn].client)
			{
				G_API_ApplyServerRecord(&g_entities[cn], httpCode, text);
			}
			continue;
		}

		if (httpCode != 200)
		{
			G_Printf("speedrun mod: API error (client %d, http %d): %s\n", id, httpCode, text);
			if (id >= 0 && id < MAX_CLIENTS && g_entities[id].client)
			{
				if (httpCode == 401)
				{
					trap_SendServerCommand(id, va("cp \"^3Time could not be recorded - speedrun_key could not be verified\n\""));
				}
				else
				{
					trap_SendServerCommand(id, va("cp \"^3Time could not be recorded (API http %d)\n\"", httpCode));
				}
			}
			continue;
		}

		root = cJSON_Parse(text);
		if (root)
		{
			cJSON *kItem = cJSON_GetObjectItem(root, "kind");
			cJSON *pItem = cJSON_GetObjectItem(root, "place");
			cJSON *tItem = cJSON_GetObjectItem(root, "totalPlayers");

			if (kItem && kItem->valuestring && pItem && tItem && id >= 0 && id < MAX_CLIENTS && g_entities[id].client)
			{
				const char *kind = kItem->valuestring;
				int        place = pItem->valueint;
				int        total = tItem->valueint;

				{
					int run = g_entities[id].client->sess.timerunRecordRun;

					if (run >= 0 && run < level.numTimeruns)
					{
						const char *netname = g_entities[id].client->pers.netname;
						const char *runName = level.timeruns[run].name;
						const char *mode    = (g_entities[id].client->sess.speedrunMode == 2) ? "infinite stamina" : "vanilla";
						char       timeStr[16];
						int        timeMs  = g_entities[id].client->sess.timerunLastTime[run][g_entities[id].client->sess.speedrunMode - 1];

						Timerun_FormatTimeMs(timeStr, sizeof(timeStr), timeMs);

						if (!Q_stricmp(kind, "WORLD_RECORD"))
						{
							trap_SendServerCommand(-1, va("timerun_record \"New ^1WR^7 in %s (%s): ^5%s^7 for ^3%s\"",
							                            runName, mode, timeStr, netname));
						}
						else if (!Q_stricmp(kind, "PERSONAL_BEST"))
						{
							trap_SendServerCommand(-1, va("timerun_record \"New ^4PB^7 %d/%d in %s (%s): ^5%s^7 for ^3%s\"",
							                            place, total, runName, mode, timeStr, netname));
						}
					}
				}
			}

			cJSON_Delete(root);
		}
		else
		{
			G_Printf("speedrun mod: API response not parseable: %s\n", text);
		}
	}
}

/**
 * @brief speedrun mod: POSTs a finished run to the records backend.
 * @details Called from Timerun_StopRun on a valid finish. Builds the
 * RecordTimeRequest body ({runId,timeMs,checkpointsMs,mode}) and enqueues an
 * async POST to g_apiUrl (the /api/speedruns endpoint) carrying the player's
 * key in the X-SpeedRun-Key header. Does nothing if the player has no key set.
 *
 * @param[in] ent    the finishing player
 * @param[in] def    the run definition (for its id and checkpoint count)
 * @param[in] timeMs final run time in ms
 */
void G_API_SendRecord(gentity_t *ent, timerunDef_t *def, int timeMs)
{
	gclient_t *client = ent->client;
	char       url[MAX_STRING_CHARS];
	char       header[256];
	char       apiKey[128];
	char       body[1024];
	int        i, off;

	if (!client->sess.speedrunKey[0])
	{
		return;   // no key - Timerun_StopRun already told the player
	}

	trap_Cvar_VariableStringBuffer("g_apiUrl", url, sizeof(url));
	if (!url[0])
	{
		G_Printf("speedrun mod: g_apiUrl is not set - run time not recorded\n");
		return;
	}

	// g_apiUrl is the API base (e.g. http://127.0.0.1:8090/api); the records
	// endpoint lives at <base>/speedruns on the backend (SpeedrunController).
	Q_strcat(url, sizeof(url), "/speedruns");

	trap_Cvar_VariableStringBuffer("g_apiKey", apiKey, sizeof(apiKey));

	// Send both the player key and the server's API key. libcurl splits a header
	// string on CRLF into multiple headers, so both fit in the single customHeader.
	Com_sprintf(header, sizeof(header), "X-SpeedRun-Key: %s\r\nX-Api-Key: %s",
	           client->sess.speedrunKey, apiKey);

	off = Com_sprintf(body, sizeof(body), "{\"runId\":\"%s\",\"timeMs\":%d,\"checkpointsMs\":[",
	                 def->id, timeMs);
	for (i = 0; i < def->numCheckpoints && off < (int)sizeof(body) - 2; i++)
	{
		off += Com_sprintf(body + off, sizeof(body) - off, "%s%d", i ? "," : "",
		                  client->sess.timerunCheckpointTimes[i]);
	}
	off += Com_sprintf(body + off, sizeof(body) - off, "],\"staminaPercents\":[");
	for (i = 0; i < def->numCheckpoints && off < (int)sizeof(body) - 2; i++)
	{
		off += Com_sprintf(body + off, sizeof(body) - off, "%s%d", i ? "," : "",
		                  client->sess.timerunCheckpointStamina[i]);
	}
	// append the run-end stamina percent after the checkpoint percents
	Com_sprintf(body + off, sizeof(body) - off, "%s%d],\"mode\":%d}",
	           def->numCheckpoints ? "," : "", client->sess.timerunStopStamina,
	           client->sess.speedrunMode);

	// Stash the finished run so the async response (G_API_Frame, keyed by clientNum)
	// can format the record notification even if the player has since started a new run.
	client->sess.timerunRecordRun = client->sess.currentTimerun;

	trap_API_Request(ent - g_entities, ent - g_entities, header, url, body);
}

/**
 * @brief speedrun mod: applies a server-record fetch response to the player's
 *        per-run best arrays (sess.timerunBestTime / BestCheckpointTimes) so
 *        checkpoint/end deltas work from the first run. Only acts on HTTP 200;
 *        404 (no stored record yet) leaves the arrays at 0 (they get set on the
 *        first finish, server-side).
 */
static void G_API_ApplyServerRecord(gentity_t *ent, int httpCode, const char *text)
{
	gclient_t *client = ent->client;
	cJSON     *root;

	if (httpCode != 200)
	{
		return;   // 404 = no record; best stays 0 until the first finish
	}

	root = cJSON_Parse(text);
	if (!root)
	{
		G_Printf("speedrun mod: server-record response not parseable: %s\n", text);
		return;
	}

	{
		cJSON *rItem = cJSON_GetObjectItem(root, "runId");
		cJSON *mItem = cJSON_GetObjectItem(root, "mode");
		cJSON *tItem = cJSON_GetObjectItem(root, "timeMs");
		cJSON *cItem = cJSON_GetObjectItem(root, "checkpointsMs");
		int   index;
		int   mode;

		for (index = 0; index < level.numTimeruns; index++)
		{
			if (rItem && rItem->valuestring && !Q_stricmp(level.timeruns[index].id, rItem->valuestring))
			{
				break;
			}
		}

		if (index >= level.numTimeruns || !tItem)
		{
			cJSON_Delete(root);
			return;
		}

		mode = (mItem && mItem->valueint >= 1 && mItem->valueint <= 2) ? mItem->valueint : client->sess.speedrunMode;

		// Apply the fetched PB only when it beats anything set server-side since
		// the fetch fired (e.g. the player finished first and promoted their time).
		// Keep the better (lower) of the two; a fetched value must never clobber a
		// fresh run that was faster.
		if (client->sess.timerunBestTime[index][mode - 1] == 0 ||
		    tItem->valueint < client->sess.timerunBestTime[index][mode - 1])
		{
			client->sess.timerunBestTime[index][mode - 1] = tItem->valueint;

			if (cItem)
			{
				int count = cJSON_GetArraySize(cItem);
				int i;

				for (i = 0; i < count && i < MAX_TIMERUN_CHECKPOINTS; i++)
				{
					cJSON *cp = cJSON_GetArrayItem(cItem, i);

					if (cp)
					{
						client->sess.timerunBestCheckpointTimes[index][mode - 1][i] = cp->valueint;
					}
				}
			}
		}

		// ALWAYS push the (possibly run-set) best to the runner AND any spectator
		// following them so the end-of-run delta display works (it reads
		// cg.timerunBestTime on the client, indexed by the runner's clientNum —
		// which is arg 1). Checkpoint deltas already come from the server. Sending
		// unconditionally fixes the fetch race where the player finishes before the
		// response lands: without it the client is never seeded, its finish delta
		// stays blank ("white"), and later runs compare against the just-finished
		// run's time instead of the stored best.
		if (client->sess.timerunBestTime[index][mode - 1] != 0)
		{
			trap_SendServerCommand(ent - g_entities, va("timerun_best %d %d %d %d", (int)(ent - g_entities), index, mode,
			                         client->sess.timerunBestTime[index][mode - 1]));
			Timerun_SendToSpectators(ent, va("timerun_best %d %d %d %d", (int)(ent - g_entities), index, mode,
			                         client->sess.timerunBestTime[index][mode - 1]));
		}
	}

	cJSON_Delete(root);
}

/**
 * @brief speedrun mod: fetches a keyed player's stored best run (end time +
 *        checkpoint times) for a run+mode, to seed the server-side best arrays
 *        on their first run this session. GET <base>/speedruns/<runId>/server-record?mode=N
 *        with the player's key in X-SpeedRun-Key. A 404 (no record) is ignored.
 * @param[in] ent the player
 * @param[in] def the run definition (for its id)
 */
void G_API_FetchServerRecord(gentity_t *ent, timerunDef_t *def)
{
	gclient_t *client = ent->client;
	char       url[MAX_STRING_CHARS];
	char       header[256];
	char       apiKey[128];

	if (!client->sess.speedrunKey[0])
	{
		return;
	}

	trap_Cvar_VariableStringBuffer("g_apiUrl", url, sizeof(url));
	if (!url[0])
	{
		return;
	}

	// GET <base>/speedruns/<runId>/server-record?mode=<mode>
	Q_strcat(url, sizeof(url), va("/speedruns/%s/server-record?mode=%d", def->id, client->sess.speedrunMode));

	trap_Cvar_VariableStringBuffer("g_apiKey", apiKey, sizeof(apiKey));

	// player key + server API key (libcurl splits a header string on CRLF)
	Com_sprintf(header, sizeof(header), "X-SpeedRun-Key: %s\r\nX-Api-Key: %s",
	            client->sess.speedrunKey, apiKey);

	// id = MAX_CLIENTS + clientNum marks this as a server-record fetch response
	// in G_API_Frame (a record-send response uses id = clientNum). Empty body => GET.
	trap_API_Request(MAX_CLIENTS + (ent - g_entities), ent - g_entities, header, url, "");
}

/**
 * @brief speedrun mod: /speedrun_records - fetches the map's top server
 *        records for the player's current speedrun mode. GET
 *        <base>/speedruns/map/<mapname>/server-best?mode=<mode> with the server's
 *        API key. The response (a JSON array of {runName,timeMs,playerName}) is
 *        printed to the requesting player when it arrives.
 */
void G_API_FetchServerBest(gentity_t *ent)
{
	gclient_t *client = ent->client;
	char       url[MAX_STRING_CHARS];
	char       mapname[MAX_QPATH];

	trap_Cvar_VariableStringBuffer("g_apiUrl", url, sizeof(url));
	if (!url[0])
	{
		return;
	}

	trap_Cvar_VariableStringBuffer("mapname", mapname, sizeof(mapname));

	// GET <base>/speedruns/map/<mapname>/server-best?mode=<mode> (public, no auth)
	Q_strcat(url, sizeof(url), va("/speedruns/map/%s/server-best?mode=%d", mapname, client->sess.speedrunMode));

	// remember the mode this query used, so the response header is consistent
	client->sess.timerunRecordsMode = client->sess.speedrunMode;

	// id = 2*MAX_CLIENTS + clientNum marks this as a server-best list response
	// (NULL header + empty body => a plain GET with no keys)
	trap_API_Request(2 * MAX_CLIENTS + (ent - g_entities), ent - g_entities, NULL, url, "");
}

/**
 * @brief speedrun mod: prints a server-best list response to the requesting player.
 * @details Response is a JSON array of {runName,timeMs,playerName}. Prints a
 *          header ("Top times for <Mode> mode:", with blank lines around it)
 *          then one line per record. The mode label is Vanilla / Infinite
 *          stamina (from the mode stashed at request time).
 */
static void G_API_PrintServerBest(gentity_t *ent, int httpCode, const char *text)
{
	gclient_t *client = ent->client;
	cJSON     *root;

	if (httpCode != 200)
	{
		trap_SendServerCommand(ent - g_entities, va("print \"^3Could not load server records (API http %d)\n\"", httpCode));
		return;
	}

	root = cJSON_Parse(text);
	if (!root || !cJSON_IsArray(root))
	{
		trap_SendServerCommand(ent - g_entities, "print \"^3Could not load server records (bad response)\n\"");
		if (root)
		{
			cJSON_Delete(root);
		}
		return;
	}

	{
		const char *modeLabel = (client->sess.timerunRecordsMode == 2) ? "Infinite stamina" : "Vanilla";
		int         count     = cJSON_GetArraySize(root);
		int         i;

		trap_SendServerCommand(ent - g_entities, va("print \"\n^7Top times for %s mode:\n\n\"", modeLabel));

		for (i = 0; i < count; i++)
		{
			cJSON *item  = cJSON_GetArrayItem(root, i);
			cJSON *rItem = item ? cJSON_GetObjectItem(item, "runName") : NULL;
			cJSON *tItem = item ? cJSON_GetObjectItem(item, "timeMs") : NULL;
			cJSON *pItem = item ? cJSON_GetObjectItem(item, "playerName") : NULL;

			if (rItem && rItem->valuestring && tItem && pItem && pItem->valuestring)
			{
				char timeStr[16];

				Timerun_FormatTimeMs(timeStr, sizeof(timeStr), tItem->valueint);
				trap_SendServerCommand(ent - g_entities, va("print \"^2%s^7 - %s - %s\n\"",
				                            rItem->valuestring, timeStr, pItem->valuestring));
			}
		}
	}

	cJSON_Delete(root);
}


/**
 * @brief speedrun mod: /speedrun_records <num> - fetches a single run's
 *        leaderboard for the player's current speedrun mode. GET
 *        <base>/speedruns/<runId>/leaderboard?mode=<mode> with the player's key
 *        in X-SpeedRun-Key (so the API can flag isCurrentPlayer). The response
 *        (a JSON array of {place,timeMs,playerName,isCurrentPlayer}) is printed
 *        to the requesting player when it arrives.
 * @param[in] ent the player
 * @param[in] def the run definition (for its id)
 */
void G_API_FetchRunLeaderboard(gentity_t *ent, timerunDef_t *def)
{
	gclient_t *client = ent->client;
	char       url[MAX_STRING_CHARS];
	char       header[256];
	char       apiKey[128];

	trap_Cvar_VariableStringBuffer("g_apiUrl", url, sizeof(url));
	if (!url[0])
	{
		return;
	}

	// GET <base>/speedruns/<runId>/leaderboard?mode=<mode>
	Q_strcat(url, sizeof(url), va("/speedruns/%s/leaderboard?mode=%d", def->id, client->sess.speedrunMode));

	trap_Cvar_VariableStringBuffer("g_apiKey", apiKey, sizeof(apiKey));

	// The player's key lets the API flag isCurrentPlayer (green row); include it
	// when present so a keyed player's row is highlighted. Without a key the
	// leaderboard still loads, just with no green highlight. libcurl splits a
	// header string on CRLF into multiple headers.
	if (client->sess.speedrunKey[0])
	{
		Com_sprintf(header, sizeof(header), "X-SpeedRun-Key: %s\r\nX-Api-Key: %s",
		            client->sess.speedrunKey, apiKey);
	}
	else
	{
		Com_sprintf(header, sizeof(header), "X-Api-Key: %s", apiKey);
	}

	// remember the run + mode this query used, so the response is labelled right
	client->sess.timerunRecordsMode = client->sess.speedrunMode;
	client->sess.timerunRecordsRun  = def - level.timeruns;

	// id = 3*MAX_CLIENTS + clientNum marks this as a leaderboard response in
	// G_API_Frame. Empty body => GET.
	trap_API_Request(3 * MAX_CLIENTS + (ent - g_entities), ent - g_entities, header, url, "");
}

/**
 * @brief speedrun mod: prints a single run's leaderboard response to the
 *        requesting player.
 * @details Response is a JSON array of {place,timeMs,playerName,isCurrentPlayer}.
 *          Prints a dashed table with a header line (run name + mode), column
 *          widths sized to the content, and the row for the current player
 *          rendered entirely in green.
 */
static void G_API_PrintRunLeaderboard(gentity_t *ent, int httpCode, const char *text)
{
	gclient_t *client = ent->client;
	cJSON     *root;

	if (httpCode != 200)
	{
		trap_SendServerCommand(ent - g_entities, va("print \"^3Could not load leaderboard (API http %d)\n\"", httpCode));
		return;
	}

	root = cJSON_Parse(text);
	if (!root || !cJSON_IsArray(root))
	{
		trap_SendServerCommand(ent - g_entities, "print \"^3Could not load leaderboard (bad response)\n\"");
		if (root)
		{
			cJSON_Delete(root);
		}
		return;
	}

	{
		const char *modeLabel = (client->sess.timerunRecordsMode == 2) ? "Infinite stamina" : "Vanilla";
		const char *runName   = "";
		int         count     = cJSON_GetArraySize(root);
		int         i;
		int         placeW = 5, timeW = 9, playerW = 6;   // start from header widths

		if (client->sess.timerunRecordsRun >= 0 && client->sess.timerunRecordsRun < level.numTimeruns)
		{
			runName = level.timeruns[client->sess.timerunRecordsRun].name;
		}

		// first pass: size the columns to the widest cell
		for (i = 0; i < count; i++)
		{
			cJSON     *item  = cJSON_GetArrayItem(root, i);
			cJSON     *pItem = item ? cJSON_GetObjectItem(item, "place") : NULL;
			cJSON     *tItem = item ? cJSON_GetObjectItem(item, "timeMs") : NULL;
			cJSON     *nItem = item ? cJSON_GetObjectItem(item, "playerName") : NULL;
			char       buf[16];
			int        len;

			if (pItem)
			{
				Com_sprintf(buf, sizeof(buf), "%d", pItem->valueint);
				len = strlen(buf);
				if (len > placeW)
				{
					placeW = len;
				}
			}

			if (tItem)
			{
				Timerun_FormatTimeMs(buf, sizeof(buf), tItem->valueint);
				len = strlen(buf);
				if (len > timeW)
				{
					timeW = len;
				}
			}

			if (nItem && nItem->valuestring)
			{
				len = strlen(nItem->valuestring);
				if (len > playerW)
				{
					playerW = len;
				}
			}
		}

		{
			// column separator line: +-----+----------+--------+
			char sep[160];
			int  o = 0;
			int  c;

			sep[o++] = '+';
			for (c = 0; c < placeW + 2 && o < (int)sizeof(sep) - 1; c++)
			{
				sep[o++] = '-';
			}
			sep[o++] = '+';
			for (c = 0; c < timeW + 2 && o < (int)sizeof(sep) - 1; c++)
			{
				sep[o++] = '-';
			}
			sep[o++] = '+';
			for (c = 0; c < playerW + 2 && o < (int)sizeof(sep) - 1; c++)
			{
				sep[o++] = '-';
			}
			sep[o++] = '+';
			sep[o] = '\0';

			trap_SendServerCommand(ent - g_entities, va("print \"\n^7%s^7 (%s)\n\"", runName, modeLabel));
			trap_SendServerCommand(ent - g_entities, va("print \"^7%s\n\"", sep));
			trap_SendServerCommand(ent - g_entities, va("print \"^7| %-*s | %-*s | %-*s |\n\"", placeW, "Place", timeW, "Time", playerW, "Player"));
			trap_SendServerCommand(ent - g_entities, va("print \"^7%s\n\"", sep));

			for (i = 0; i < count; i++)
			{
				cJSON     *item  = cJSON_GetArrayItem(root, i);
				cJSON     *pItem = item ? cJSON_GetObjectItem(item, "place") : NULL;
				cJSON     *tItem = item ? cJSON_GetObjectItem(item, "timeMs") : NULL;
				cJSON     *nItem = item ? cJSON_GetObjectItem(item, "playerName") : NULL;
				cJSON     *cItem = item ? cJSON_GetObjectItem(item, "isCurrentPlayer") : NULL;
				char       placeBuf[16], timeBuf[16];
				const char *color;
				const char *name;

				if (!pItem || !tItem)
				{
					continue;
				}

				Com_sprintf(placeBuf, sizeof(placeBuf), "%d", pItem->valueint);
				Timerun_FormatTimeMs(timeBuf, sizeof(timeBuf), tItem->valueint);
				name  = (nItem && nItem->valuestring) ? nItem->valuestring : "";
				color = (cItem && cJSON_IsTrue(cItem)) ? "^2" : "^7";

				trap_SendServerCommand(ent - g_entities, va("print \"%s| %-*s | %-*s | %-*s |\n\"", color,
				                                     placeW, placeBuf, timeW, timeBuf, playerW, name));
			}

			trap_SendServerCommand(ent - g_entities, va("print \"^7%s\n\"", sep));
		}
	}

	cJSON_Delete(root);
}


/**
 * @brief speedrun mod: /leaderboard - fetches the server-wide ELO leaderboard
 *        for the player's current speedrun mode. GET
 *        <base>/speedruns/server-elo?mode=<mode> with the player's key in
 *        X-SpeedRun-Key (so the API can flag isCurrentPlayer). The response (a
 *        JSON array of {place,eloPoints,playerName,isCurrentPlayer}) is printed
 *        to the requesting player when it arrives.
 * @param[in] ent the player
 */
void G_API_FetchServerElo(gentity_t *ent)
{
	gclient_t *client = ent->client;
	char       url[MAX_STRING_CHARS];
	char       header[256];
	char       apiKey[128];

	trap_Cvar_VariableStringBuffer("g_apiUrl", url, sizeof(url));
	if (!url[0])
	{
		return;
	}

	// GET <base>/speedruns/server-elo?mode=<mode>
	Q_strcat(url, sizeof(url), va("/speedruns/server-elo?mode=%d", client->sess.speedrunMode));

	trap_Cvar_VariableStringBuffer("g_apiKey", apiKey, sizeof(apiKey));

	// The player's key lets the API flag isCurrentPlayer (green row); include it
	// when present so a keyed player's row is highlighted. Without a key the
	// leaderboard still loads, just with no green highlight. libcurl splits a
	// header string on CRLF into multiple headers.
	if (client->sess.speedrunKey[0])
	{
		Com_sprintf(header, sizeof(header), "X-SpeedRun-Key: %s\r\nX-Api-Key: %s",
		            client->sess.speedrunKey, apiKey);
	}
	else
	{
		Com_sprintf(header, sizeof(header), "X-Api-Key: %s", apiKey);
	}

	// remember the mode this query used, so the response is labelled right
	client->sess.timerunRecordsMode = client->sess.speedrunMode;

	// id = 4*MAX_CLIENTS + clientNum marks this as a server-elo response in
	// G_API_Frame. Empty body => GET.
	trap_API_Request(4 * MAX_CLIENTS + (ent - g_entities), ent - g_entities, header, url, "");
}

/**
 * @brief speedrun mod: prints the server-wide ELO leaderboard response to the
 *        requesting player.
 * @details Response is a JSON array of {place,eloPoints,playerName,isCurrentPlayer}.
 *          Prints a dashed table with a header line (mode), column widths sized
 *          to the content, and the row for the current player rendered entirely
 *          in green.
 */
static void G_API_PrintServerElo(gentity_t *ent, int httpCode, const char *text)
{
	gclient_t *client = ent->client;
	cJSON     *root;

	if (httpCode != 200)
	{
		trap_SendServerCommand(ent - g_entities, va("print \"^3Could not load ELO leaderboard (API http %d)\n\"", httpCode));
		return;
	}

	root = cJSON_Parse(text);
	if (!root || !cJSON_IsArray(root))
	{
		trap_SendServerCommand(ent - g_entities, "print \"^3Could not load ELO leaderboard (bad response)\n\"");
		if (root)
		{
			cJSON_Delete(root);
		}
		return;
	}

	{
		const char *modeLabel = (client->sess.timerunRecordsMode == 2) ? "Infinite stamina" : "Vanilla";
		int         count     = cJSON_GetArraySize(root);
		int         i;
		int         placeW = 5, eloW = 3, playerW = 6;   // start from header widths

		// first pass: size the columns to the widest cell
		for (i = 0; i < count; i++)
		{
			cJSON *item  = cJSON_GetArrayItem(root, i);
			cJSON *pItem = item ? cJSON_GetObjectItem(item, "place") : NULL;
			cJSON *eItem = item ? cJSON_GetObjectItem(item, "eloPoints") : NULL;
			cJSON *nItem = item ? cJSON_GetObjectItem(item, "playerName") : NULL;
			char  buf[16];
			int   len;

			if (pItem)
			{
				Com_sprintf(buf, sizeof(buf), "%d", pItem->valueint);
				len = strlen(buf);
				if (len > placeW)
				{
					placeW = len;
				}
			}

			if (eItem)
			{
				Com_sprintf(buf, sizeof(buf), "%d", eItem->valueint);
				len = strlen(buf);
				if (len > eloW)
				{
					eloW = len;
				}
			}

			if (nItem && nItem->valuestring)
			{
				len = strlen(nItem->valuestring);
				if (len > playerW)
				{
					playerW = len;
				}
			}
		}

		{
			// column separator line: +-----+-------+--------+
			char sep[160];
			int  o = 0;
			int  c;

			sep[o++] = '+';
			for (c = 0; c < placeW + 2 && o < (int)sizeof(sep) - 1; c++)
			{
				sep[o++] = '-';
			}
			sep[o++] = '+';
			for (c = 0; c < eloW + 2 && o < (int)sizeof(sep) - 1; c++)
			{
				sep[o++] = '-';
			}
			sep[o++] = '+';
			for (c = 0; c < playerW + 2 && o < (int)sizeof(sep) - 1; c++)
			{
				sep[o++] = '-';
			}
			sep[o++] = '+';
			sep[o] = '\0';

			trap_SendServerCommand(ent - g_entities, va("print \"\n^7ELO leaderboard^7 (%s):\n\"", modeLabel));
			trap_SendServerCommand(ent - g_entities, va("print \"^7%s\n\"", sep));
			trap_SendServerCommand(ent - g_entities, va("print \"^7| %-*s | %-*s | %-*s |\n\"", placeW, "Place", eloW, "Elo", playerW, "Player"));
			trap_SendServerCommand(ent - g_entities, va("print \"^7%s\n\"", sep));

			for (i = 0; i < count; i++)
			{
				cJSON *item  = cJSON_GetArrayItem(root, i);
				cJSON *pItem = item ? cJSON_GetObjectItem(item, "place") : NULL;
				cJSON *eItem = item ? cJSON_GetObjectItem(item, "eloPoints") : NULL;
				cJSON *nItem = item ? cJSON_GetObjectItem(item, "playerName") : NULL;
				cJSON *cItem = item ? cJSON_GetObjectItem(item, "isCurrentPlayer") : NULL;
				char  placeBuf[16], eloBuf[16];
				const char *color;
				const char *name;

				if (!pItem || !eItem)
				{
					continue;
				}

				Com_sprintf(placeBuf, sizeof(placeBuf), "%d", pItem->valueint);
				Com_sprintf(eloBuf, sizeof(eloBuf), "%d", eItem->valueint);
				name  = (nItem && nItem->valuestring) ? nItem->valuestring : "";
				color = (cItem && cJSON_IsTrue(cItem)) ? "^2" : "^7";

				trap_SendServerCommand(ent - g_entities, va("print \"%s| %-*s | %-*s | %-*s |\n\"", color,
				                                     placeW, placeBuf, eloW, eloBuf, playerW, name));
			}

			trap_SendServerCommand(ent - g_entities, va("print \"^7%s\n\"", sep));
		}
	}

	cJSON_Delete(root);
}
