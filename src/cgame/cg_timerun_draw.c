/**
 * @file cg_timerun_draw.c
 * @brief speedrun mod HUD drawing
 */

#include "cg_local.h"

/**
 * @brief CG_DrawCheckpointLine
 * @details speedrun mod: the latest checkpoint's time and delta vs best,
 * drawn under the timer in a smaller font. Green = faster than best,
 * red = slower, white = first-ever or equal.
 */
static void CG_DrawCheckpointLine(void)
{
	int    idx, t, d, min, sec, milli, dmin, dsec, dmilli;
	char   status[32];
	vec4_t color = { 1.f, 1.f, 1.f, 1.f };
	float  x, y, w;

	// only during an active run, and only for 2 seconds after the checkpoint
	if (!cg.timerunActive || !cg.timerunCheckPointChecked
	    || cg.time - cg.timerunCheckpointDrawTime > 2000)
	{
		return;
	}

	idx = cg.timerunCheckPointChecked - 1;   // latest checkpoint

	if (cg.timerunCheckStatus[idx] == 0)
	{
		// no previous checkpoint time: show the full time of reaching it
		t     = cg.timerunCheckPointTime[idx];
		min   = t / 60000;
		t    -= min * 60000;
		sec   = t / 1000;
		milli = t - sec * 1000;
		Com_sprintf(status, sizeof(status), "%02d:%02d.%03d", min, sec, milli);
	}
	else
	{
		// previous checkpoint time known: show the delta
		d      = cg.timerunCheckPointDiff[idx];
		dmin   = d / 60000;
		d     -= dmin * 60000;
		dsec   = d / 1000;
		dmilli = d - dsec * 1000;

		if (cg.timerunCheckStatus[idx] == 2)
		{
			color[0] = 0.f;   // faster than best: green
			color[1] = 1.f;
			color[2] = 0.f;
			Com_sprintf(status, sizeof(status), "-%02d:%02d.%03d", dmin, dsec, dmilli);
		}
		else if (cg.timerunCheckStatus[idx] == 3)
		{
			color[0] = 1.f;   // slower than best: red
			color[1] = 0.2f;
			color[2] = 0.2f;
			Com_sprintf(status, sizeof(status), "+%02d:%02d.%03d", dmin, dsec, dmilli);
		}
		else
		{
			// exactly equal
			Com_sprintf(status, sizeof(status), "+00:00.000");
		}
	}

	// just below the main timer, smaller font
	w = CG_Text_Width_Ext(status, 0.15f, 0, &cgs.media.limboFont1);
	x = 320.f + cgs.wideXoffset - 0.5f * w;
	y = 480.f - 72.f + CG_Text_Height_Ext(status, 0.3f, 0, &cgs.media.limboFont1) + 4.f;

	CG_Text_Paint_Ext(x, y, 0.15f, 0.15f, color, status, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont1);
}

/**
 * @brief CG_DrawTimer
 * @details speedrun mod: always-visible run timer on timerun maps, bottom-center.
 * Shows the current run time while a run is active (MM:SS.mmm), the final time
 * after a finished run (until the next run starts or the run is aborted by
 * death/load/kill), and 00:00.000 otherwise. Room is left below the timer for
 * the checkpoint/delta line (drawn in a smaller font).
 */
void CG_DrawTimer(void)
{
	int    clientNum = cg.clientNum;
	int    t, min, sec, milli;
	char   status[32];
	vec4_t color = { 1.f, 1.f, 1.f, 1.f };
	float  x, y, w;

	if (!isTimerun.integer)
	{
		return;
	}

	// spectators watch the followed player's finished time
	if (cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR)
	{
		clientNum = cg.snap->ps.clientNum;
	}

	if (cg.timerunActive)
	{
		// server sent startTime+500; keep the ETrun +/-500 symmetry
		t = cg.time - (cg.timerunStartTime - 500);
	}
	else if (cg.timerunFinishedTime[clientNum])
	{
		int best = cg.timerunBestTime[clientNum][cg.currentTimerun];

		// final time stays visible until the next run starts or an abort;
		// show the delta vs the previous best run next to it when known
		t = cg.timerunFinishedTime[clientNum];

		if (best > 0)
		{
			int d, dmin, dsec, dmilli;

			// the delta needs the RAW time: compute it before the MM:SS.mmm
			// split below mutates t (a >= 1:00 finish would otherwise delta
			// against its sub-minute part only, faking a green new best)
			d      = t - best;
			dmin   = abs(d) / 60000;
			dsec   = (abs(d) / 1000) % 60;
			dmilli = abs(d) % 1000;

			min   = t / 60000;
			t    -= min * 60000;
			sec   = t / 1000;
			milli = t - sec * 1000;

			if (d < 0)
			{
				Vector4Set(color, 0.f, 1.f, 0.f, 1.f);   // new best: green
				Com_sprintf(status, sizeof(status), "%02d:%02d.%03d (-%02d:%02d.%03d)",
				            min, sec, milli, dmin, dsec, dmilli);
			}
			else if (d > 0)
			{
				Vector4Set(color, 1.f, 0.2f, 0.2f, 1.f); // slower: red
				Com_sprintf(status, sizeof(status), "%02d:%02d.%03d (+%02d:%02d.%03d)",
				            min, sec, milli, dmin, dsec, dmilli);
			}
			else
			{
				Com_sprintf(status, sizeof(status), "%02d:%02d.%03d (+00:00.000)",
				            min, sec, milli);
			}
		}
		else
		{
			min   = t / 60000;
			t    -= min * 60000;
			sec   = t / 1000;
			milli = t - sec * 1000;

			Com_sprintf(status, sizeof(status), "%02d:%02d.%03d", min, sec, milli);
		}

		CG_Text_Paint_Ext(320.f + cgs.wideXoffset - 0.5f * CG_Text_Width_Ext(status, 0.3f, 0, &cgs.media.limboFont1),
		                  480.f - 72.f, 0.3f, 0.3f, color, status, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont1);

		return;   // finished state drawn (no checkpoint line)
	}
	else
	{
		t = 0;
	}

	if (t < 0)
	{
		t = 0;
	}

	min   = t / 60000;
	t    -= min * 60000;
	sec   = t / 1000;
	milli = t - sec * 1000;

	Com_sprintf(status, sizeof(status), "%02d:%02d.%03d", min, sec, milli);

	// bottom-center (HUD virtual space is 640x480); the checkpoint/delta
	// line will sit below the timer, in a smaller font
	w = CG_Text_Width_Ext(status, 0.3f, 0, &cgs.media.limboFont1);
	x = 320.f + cgs.wideXoffset - 0.5f * w;
	y = 480.f - 72.f;

	CG_Text_Paint_Ext(x, y, 0.3f, 0.3f, color, status, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont1);

	// speedrun mod: checkpoint time/delta line under the timer
	CG_DrawCheckpointLine();
}


/**
 * @brief speedrun mod: emits the 6 faces of a (possibly yaw-rotated) box as
 * world-space polys using the timerun debug shader.
 * @details Shared by the timerun zone overlay and the /draw_box command.
 * Each cube face is a 4-vertex poly; the box is centered on the given origin
 * with the given radius (cube half-extent) and yaw (rotation around Z).
 */
static void CG_AddDebugBox(const vec3_t center, float radius, float yaw)
{
	float c, s;
	vec3_t    corners[8];
	polyVert_t verts[4];
	int       face, v;
	static const int faceVerts[6][4] =
	{
		{ 0, 1, 2, 3 },   // bottom
		{ 4, 5, 6, 7 },   // top
		{ 0, 1, 5, 4 },   // front
		{ 1, 2, 6, 5 },   // right
		{ 2, 3, 7, 6 },   // back
		{ 3, 0, 4, 7 }    // left
	};
	int k, corner;

	if (yaw != 0.0f)
	{
		c = cosf(DEG2RAD(yaw));
		s = sinf(DEG2RAD(yaw));
	}
	else
	{
		c = 1.0f;
		s = 0.0f;
	}

	// 8 corners of the (possibly rotated) cube around the box origin
	for (corner = 0; corner < 8; corner++)
	{
		float x = (corner & 1) ? radius : -radius;
		float y = (corner & 2) ? radius : -radius;
		float z = (corner & 4) ? radius : -radius;

		corners[corner][0] = center[0] + c * x - s * y;
		corners[corner][1] = center[1] + s * x + c * y;
		corners[corner][2] = center[2] + z;
	}

	for (face = 0; face < 6; face++)
	{
		for (v = 0; v < 4; v++)
		{
			k = faceVerts[face][v];
			VectorCopy(corners[k], verts[v].xyz);
			verts[v].st[0] = 0;
			verts[v].st[1] = 0;
			// solid blue, fully opaque
			verts[v].modulate[0] = 0;
			verts[v].modulate[1] = 0;
			verts[v].modulate[2] = 255;
			verts[v].modulate[3] = 255;
		}
		trap_R_AddPolyToScene(cgs.media.timerunDebugShader, 4, verts);
	}
}

/**
 * @brief speedrun mod: draws the timerun zones (start/stop/checkpoints) as blue
 * cubes when speedrun_debug is 1, so their covered area is visible in-game.
 * @details The zone geometry is pushed by the server (timerun_zones commands on
 * ClientBegin) and drawn every frame via world-space polys. The single box set
 * with the /draw_box client command is drawn on top of the zones.
 */
void CG_DrawTimerunZones(void)
{
	int run, i;

	if (!speedrun_debug.integer)
	{
		return;
	}

	for (run = 0; run < MAX_TIMERUNS; run++)
	{
		for (i = 0; i < cg.timerunDebugZoneCount[run]; i++)
		{
			CG_AddDebugBox(cg.timerunDebugZoneOrigins[run][i],
			              cg.timerunDebugZoneRadius[run][i],
			              cg.timerunDebugZoneYaw[run][i]);
		}
	}

	// speedrun mod: /draw_box helper box (replaces the previous one on redraw)
	if (cg.drawBoxValid)
	{
		CG_AddDebugBox(cg.drawBoxOrigin, cg.drawBoxRadius, cg.drawBoxYaw);
	}
}
