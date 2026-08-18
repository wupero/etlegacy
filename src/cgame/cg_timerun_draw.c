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
	y = 480.f - 52.f + CG_Text_Height_Ext(status, 0.3f, 0, &cgs.media.limboFont1) + 4.f;

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
		int best = cg.timerunBestTime[clientNum][cg.currentTimerun][cg.timerunMode[clientNum] - 1];

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
		                  480.f - 52.f, 0.3f, 0.3f, color, status, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont1);

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
	y = 480.f - 52.f;

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
static void CG_AddDebugBox(const vec3_t center, const vec3_t halfExtent, float yaw,
                    const vec3_t color)
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
		float x = (corner & 1) ? halfExtent[0] : -halfExtent[0];
		float y = (corner & 2) ? halfExtent[1] : -halfExtent[1];
		float z = (corner & 4) ? halfExtent[2] : -halfExtent[2];

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
			verts[v].modulate[0] = (byte)color[0];
			verts[v].modulate[1] = (byte)color[1];
			verts[v].modulate[2] = (byte)color[2];
			verts[v].modulate[3] = 255;
		}
		trap_R_AddPolyToScene(cgs.media.timerunDebugShader, 4, verts);
	}
}

/**
 * @brief speedrun mod: emits ONE camera-facing 2D diamond (billboard) at origin.
 * @details Separate function on purpose: the marker look (shape/size) will
 * change and only this function should need editing. Color is a parameter so
 * future per-type coloring (start/checkpoint/stop) is a call-site-only change.
 */
#define TIMERUN_MARKER_HALF_SIZE 16.0f
void CG_AddMarkerDiamond(const vec3_t origin, const vec3_t color)
{
	polyVert_t verts[4];
	vec3_t     right, up;
	int        i;

	// billboard basis from the current view (right/up of the camera)
	VectorCopy(cg.refdef_current->viewaxis[1], right);
	VectorCopy(cg.refdef_current->viewaxis[2], up);

	VectorMA(origin,  TIMERUN_MARKER_HALF_SIZE, up,    verts[0].xyz);  // top
	VectorMA(origin,  TIMERUN_MARKER_HALF_SIZE, right, verts[1].xyz);  // right
	VectorMA(origin, -TIMERUN_MARKER_HALF_SIZE, up,    verts[2].xyz);  // bottom
	VectorMA(origin, -TIMERUN_MARKER_HALF_SIZE, right, verts[3].xyz);  // left

	for (i = 0; i < 4; i++)
	{
		verts[i].st[0] = 0;
		verts[i].st[1] = 0;
		verts[i].modulate[0] = (byte)color[0];
		verts[i].modulate[1] = (byte)color[1];
		verts[i].modulate[2] = (byte)color[2];
		verts[i].modulate[3] = 255;
	}

	trap_R_AddPolyToScene(cgs.media.timerunMarkerShader, 4, verts);
}

/**
 * @brief speedrun mod: qtrue if the point lies inside the /draw_box box.
 * @details Mirrors the server's Timerun_ZoneContains: the offset is rotated
 * back into the box's local frame (yaw around Z) and compared against the
 * radius half-extent on all three axes.
 */
static qboolean CG_DrawBoxContains(const vec3_t point)
{
	vec3_t local;
	float  yaw, c, s;

	VectorSubtract(point, cg.drawBoxOrigin, local);
	yaw = cg.drawBoxYaw;

	if (yaw != 0.0f)
	{
		c = cosf(DEG2RAD(yaw));
		s = sinf(DEG2RAD(yaw));

		float x = local[0], y = local[1];

		local[0] =  c * x + s * y;
		local[1] = -s * x + c * y;
	}

	return (fabs(local[0]) <= cg.drawBoxRadius
	        && fabs(local[1]) <= cg.drawBoxRadius
	        && fabs(local[2]) <= cg.drawBoxRadius);
}

/**
 * @brief speedrun mod: per-axis containment test for the /draw_plane box.
 * @details Same as CG_DrawBoxContains but compares against the per-axis
 *          half-extents (drawPlaneHalfExtent) instead of a uniform radius.
 */
static qboolean CG_DrawPlaneContains(const vec3_t point)
{
	vec3_t local;
	float  yaw, c, s;

	VectorSubtract(point, cg.drawPlaneOrigin, local);
	yaw = cg.drawPlaneYaw;

	if (yaw != 0.0f)
	{
		c = cosf(DEG2RAD(yaw));
		s = sinf(DEG2RAD(yaw));

		float x = local[0], y = local[1];

		local[0] =  c * x + s * y;
		local[1] = -s * x + c * y;
	}

	return (fabs(local[0]) <= cg.drawPlaneHalfExtent[0]
	        && fabs(local[1]) <= cg.drawPlaneHalfExtent[1]
	        && fabs(local[2]) <= cg.drawPlaneHalfExtent[2]);
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
			vec3_t blue = { 0, 0, 255 };

			CG_AddDebugBox(cg.timerunDebugZoneOrigins[run][i],
			              cg.timerunDebugZoneHalfExtent[run][i],
			              cg.timerunDebugZoneYaw[run][i], blue);
		}
	}

	// speedrun mod: /draw_box helper box (replaces the previous one on redraw).
	// Drawn red so it stands apart from the mapped lua zones (blue).
	if (cg.drawBoxValid)
	{
		vec3_t  red = { 255, 0, 0 };
		vec3_t  he  = { cg.drawBoxRadius, cg.drawBoxRadius, cg.drawBoxRadius };
		qboolean inside = CG_DrawBoxContains(cg.predictedPlayerState.origin);

		CG_AddDebugBox(cg.drawBoxOrigin, he, cg.drawBoxYaw, red);

		// edge-triggered center print on entry, like the timerun zone touches
		if (inside && !cg.drawBoxWasInside)
		{
			CG_Printf("speedrun_debug: draw_box touched\n");
			CG_PriorityCenterPrint("^1draw_box touched\n", 1);
		}
		cg.drawBoxWasInside = inside;
	}

	// speedrun mod: /draw_plane helper box (per-axis half-extents). Drawn magenta
	// to stand apart from the draw_box red box and the mapped lua zones (blue).
	if (cg.drawPlaneValid)
	{
		vec3_t  magenta = { 255, 0, 255 };
		qboolean inside  = CG_DrawPlaneContains(cg.predictedPlayerState.origin);

		CG_AddDebugBox(cg.drawPlaneOrigin, cg.drawPlaneHalfExtent, cg.drawPlaneYaw, magenta);

		if (inside && !cg.drawPlaneWasInside)
		{
			CG_Printf("speedrun_debug: draw_plane touched\n");
			CG_PriorityCenterPrint("^1draw_plane touched\n", 1);
		}
		cg.drawPlaneWasInside = inside;
	}
}

// zone types as sent by the server in the timerun_zones command
// (mirrors the game-side enum in g_timerun.c)
#define TIMERUN_ZONE_START      1
#define TIMERUN_ZONE_CHECKPOINT 2
#define TIMERUN_ZONE_STOP       3
#define TIMERUN_MARKER_Z_OFFSET 70.0f

/**
 * @brief speedrun mod: draws the diamond marker for one stored zone.
 * @details Color by zone type: start green, checkpoint cyan, stop white.
 */
static void CG_AddZoneMarker(int run, int i)
{
	vec3_t origin, color;

	switch (cg.timerunDebugZoneTypes[run][i])
	{
	case TIMERUN_ZONE_CHECKPOINT:
		VectorSet(color, 0, 255, 255);    // checkpoint cyan
		break;
	case TIMERUN_ZONE_STOP:
		VectorSet(color, 255, 255, 255);    // stop
		break;
	default:
		VectorSet(color, 0, 255, 0);        // start
		break;
	}

	VectorCopy(cg.timerunDebugZoneOrigins[run][i], origin);
	origin[2] += TIMERUN_MARKER_Z_OFFSET;

	CG_AddMarkerDiamond(origin, color);
}

/**
 * @brief speedrun mod: draws the zone point markers (speedrun_markers 1).
 * @details With speedrun_debug on, every zone of every run shows. Without
 * debug the markers guide the player: all start markers while no run is
 * active, only the next checkpoint of the active run, and only the stop once
 * all checkpoints are passed — so the current goal is never ambiguous.
 * Uses the per-zone geometry the server pushes on ClientBegin (timerun_zones
 * commands); zone counts are 0 on non-timerun maps, so nothing draws there.
 */
void CG_DrawTimerunMarkers(void)
{
	int run, i;

	if (!speedrun_markers.integer)
	{
		return;
	}

	if (speedrun_debug.integer)
	{
		// debug: every zone of every run
		for (run = 0; run < MAX_TIMERUNS; run++)
		{
			for (i = 0; i < cg.timerunDebugZoneCount[run]; i++)
			{
				CG_AddZoneMarker(run, i);
			}
		}
	}
	else if (!cg.timerunActive)
	{
		// no active run: only the starts, so every run's start point shows
		for (run = 0; run < MAX_TIMERUNS; run++)
		{
			for (i = 0; i < cg.timerunDebugZoneCount[run]; i++)
			{
				if (cg.timerunDebugZoneTypes[run][i] == TIMERUN_ZONE_START)
				{
					CG_AddZoneMarker(run, i);
				}
			}
		}
	}
	else
	{
		// active run: the next checkpoint, or the stop once every checkpoint is
		// passed. Checkpoint zones arrive in definition order and
		// timerunCheckPointChecked is the number passed so far, so its value is
		// the index of the next checkpoint.
		int next    = cg.timerunCheckPointChecked;
		int cpCount = 0;
		int target  = -1;
		int stop    = -1;

		if (cg.currentTimerun >= 0 && cg.currentTimerun < MAX_TIMERUNS)
		{
			for (i = 0; i < cg.timerunDebugZoneCount[cg.currentTimerun]; i++)
			{
				if (cg.timerunDebugZoneTypes[cg.currentTimerun][i] == TIMERUN_ZONE_CHECKPOINT)
				{
					if (cpCount == next)
					{
						target = i;
					}
					cpCount++;
				}
				else if (cg.timerunDebugZoneTypes[cg.currentTimerun][i] == TIMERUN_ZONE_STOP)
				{
					stop = i;
				}
			}
		}

		i = (target >= 0) ? target : stop;

		if (i >= 0)
		{
			CG_AddZoneMarker(cg.currentTimerun, i);
		}
	}

	// speedrun mod: diamond above the /draw_box helper box. Debug tool — only
	// drawn with debug on, same as the box itself.
	if (cg.drawBoxValid && speedrun_debug.integer)
	{
		vec3_t origin, white = { 255, 255, 255 };

		VectorCopy(cg.drawBoxOrigin, origin);
		origin[2] += TIMERUN_MARKER_Z_OFFSET;

		CG_AddMarkerDiamond(origin, white);
	}
}

/**
 * @brief speedrun mod: draws the run number inside each start marker diamond.
 * @details 2D pass: the diamonds are 3D polys rendered with the scene, so the
 * labels paint on top after the render, projected from the diamond center
 * (zone point + marker offset). The font size scales with the diamond's
 * projected height so the number always fits inside it. Visibility mirrors the
 * start diamonds (see CG_DrawTimerunMarkers): all runs' starts when debug is
 * on or no run is active, none while a run is active.
 */
void CG_DrawTimerunMarkerLabels(void)
{
	int         run, i, displayNum = 0;
	vec4_t      color = { 1.f, 1.f, 1.f, 1.f };
	fontHelper_t *font = &cgs.media.limboFont2;

	if (!speedrun_markers.integer || (!speedrun_debug.integer && cg.timerunActive))
	{
		return;
	}

	for (run = 0; run < MAX_TIMERUNS; run++)
	{
		// runs with pushed zones are exactly the selected group, in absolute/lua
		// order == /speedrun order, so their position is the displayed number
		if (cg.timerunDebugZoneCount[run] > 0)
		{
			displayNum++;
		}

		for (i = 0; i < cg.timerunDebugZoneCount[run]; i++)
		{
			float x, y, z, scale, w, h;
			vec3_t origin, trans;
			char   label[8];

			if (cg.timerunDebugZoneTypes[run][i] != TIMERUN_ZONE_START)
			{
				continue;
			}

			VectorCopy(cg.timerunDebugZoneOrigins[run][i], origin);
			origin[2] += TIMERUN_MARKER_Z_OFFSET;

			// the diamond is depth-tested against the world, so the label must
			// match: hide it when a wall blocks the line of sight (point trace,
			// same as the spectator floating labels)
			{
				trace_t tr;
				vec3_t  pointMins = { 0, 0, 0 }, pointMaxs = { 0, 0, 0 };

				CG_Trace(&tr, cg.refdef.vieworg, pointMins, pointMaxs, origin, -1, CONTENTS_SOLID);

				if (tr.fraction < 1.0f)
				{
					continue;
				}
			}

			if (!CG_WorldCoordToScreenCoordFloat(origin, &x, &y))
			{
				continue;
			}

			// depth along the view axis: sizes the text to the diamond's
			// projected height so the number always fits inside
			VectorSubtract(origin, cg.refdef.vieworg, trans);
			z = DotProduct(trans, cg.refdef.viewaxis[0]);

			if (z < 0.1f)
			{
				continue;
			}

			scale = TIMERUN_MARKER_HALF_SIZE * 240.0f * 0.8f
			        / (tanf(DEG2RAD(cg.refdef.fov_y) * 0.5f) * z
			           * CG_Text_Height_Ext("0", 1.0f, 0, font));
			scale = scale < 0.08f ? 0.08f : (scale > 0.6f ? 0.6f : scale);

			// 1-based run number, matches the /speedrun listing (group-scoped)
			Com_sprintf(label, sizeof(label), "%d", displayNum);

			w = CG_Text_Width_Ext_Float(label, scale, 0, font);
			h = CG_Text_Height_Ext_Float(label, scale, 0, font);

			CG_Text_Paint_Ext(x - w * 0.5f, y - h * 0.5f + 10.0f, scale, scale, color, label,
			                 0, 0, ITEM_TEXTSTYLE_SHADOWED, font);
		}
	}
}
