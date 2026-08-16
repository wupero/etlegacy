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

		if (httpCode != 200)
		{
			G_Printf("speedrun mod: API error (client %d, http %d): %s\n", id, httpCode, text);
			if (id >= 0 && id < MAX_CLIENTS && g_entities[id].client)
			{
				trap_SendServerCommand(id, va("cp \"^3Time could not be recorded (API http %d)\n\"", httpCode));
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

				if (!Q_stricmp(kind, "WORLD_RECORD"))
				{
					trap_SendServerCommand(id, va("cp \"^2World record! ^7Place 1 of %d\n\"", total));
				}
				else if (!Q_stricmp(kind, "PERSONAL_BEST"))
				{
					trap_SendServerCommand(id, va("cp \"^2New personal best - ^7place %d of %d\n\"", place, total));
				}
				else
				{
					trap_SendServerCommand(id, va("cp \"^7Recorded - ^7place %d of %d\n\"", place, total));
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
	char       header[192];
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

	Com_sprintf(header, sizeof(header), "X-SpeedRun-Key: %s", client->sess.speedrunKey);

	off = Com_sprintf(body, sizeof(body), "{\"runId\":\"%s\",\"timeMs\":%d,\"checkpointsMs\":[",
	                 def->id, timeMs);
	for (i = 0; i < def->numCheckpoints && off < (int)sizeof(body) - 2; i++)
	{
		off += Com_sprintf(body + off, sizeof(body) - off, "%s%d", i ? "," : "",
		                  client->sess.timerunCheckpointTimes[i]);
	}
	Com_sprintf(body + off, sizeof(body) - off, "],\"mode\":%d}", client->sess.speedrunMode);

	trap_API_Request(ent - g_entities, ent - g_entities, header, url, body);
}
