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

/**
 * @brief speedrun mod: polls completed backend API requests and dispatches them.
 * @details Called every frame from G_RunFrame. Responses are matched by the id
 * given at request time; for now (echo test) every response is printed.
 */
void G_API_Frame(void)
{
	int  id, httpCode;
	char text[2048];

	while (trap_API_GetResult(&id, &httpCode, text, sizeof(text)))
	{
		G_Printf("speedrun mod: API response (id %d, http %d): %s\n", id, httpCode, text);
	}
}

/**
 * @brief speedrun mod: /speedrun_apitest - POSTs a test payload to g_apiUrl
 * @details Used to verify the engine->game async API pipeline against the
 * backend's test endpoint (/api/echo).
 */
void Cmd_SpeedrunApiTest_f(gentity_t *ent, unsigned int dwCommand, int value)
{
	char url[MAX_STRING_CHARS];
	char body[512];
	int  clientNum = ent ? ent - g_entities : -1;

	trap_Cvar_VariableStringBuffer("g_apiUrl", url, sizeof(url));

	if (!url[0])
	{
		if (ent)
		{
			trap_SendServerCommand(clientNum, "cp \"^3speedrun_apitest: g_apiUrl is not set\n\"");
		}
		else
		{
			G_Printf("speedrun mod: g_apiUrl is not set\n");
		}
		return;
	}

	Com_sprintf(body, sizeof(body), "{\"speedrun\":\"apitest\",\"player\":\"%s\"}",
	            ent && ent->client ? ent->client->pers.netname : "console");

	trap_API_Request(1, clientNum, url, body);

	if (ent)
	{
		trap_SendServerCommand(clientNum, va("cp \"^2API test request sent to %s\n\"", url));
	}
	else
	{
		G_Printf("speedrun mod: API test request sent to %s\n", url);
	}
}
