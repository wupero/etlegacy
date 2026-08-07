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
	int    idx, t, min, sec, milli, d, dmin, dsec, dmilli;
	char   status[64];
	vec4_t color = { 1.f, 1.f, 1.f, 1.f };
	float  x, y, w;

	if (!cg.timerunCheckPointChecked)
	{
		return;
	}

	idx  = cg.timerunCheckPointChecked - 1;   // latest checkpoint
	t    = cg.timerunCheckPointTime[idx];
	min  = t / 60000;
	t   -= min * 60000;
	sec  = t / 1000;
	milli = t - sec * 1000;

	if (cg.timerunCheckStatus[idx] == 2 || cg.timerunCheckStatus[idx] == 3)
	{
		d      = cg.timerunCheckPointDiff[idx];
		dmin   = d / 60000;
		d     -= dmin * 60000;
		dsec   = d / 1000;
		dmilli = d - dsec * 1000;

		Com_sprintf(status, sizeof(status), "%02d:%02d.%03d (%s%02d:%02d.%03d)",
		            min, sec, milli, cg.timerunCheckStatus[idx] == 2 ? "-" : "+", dmin, dsec, dmilli);

		if (cg.timerunCheckStatus[idx] == 2)
		{
			color[0] = 0.f;   // faster than best: green
			color[1] = 1.f;
			color[2] = 0.f;
		}
		else
		{
			color[0] = 1.f;   // slower than best: red
			color[1] = 0.2f;
			color[2] = 0.2f;
		}
	}
	else
	{
		Com_sprintf(status, sizeof(status), "%02d:%02d.%03d", min, sec, milli);
	}

	w = CG_Text_Width_Ext(status, 0.2f, 0, &cgs.media.limboFont1);
	x = 320.f + cgs.wideXoffset - 0.5f * w;
	y = 480.f - 40.f;

	CG_Text_Paint_Ext(x, y, 0.2f, 0.2f, color, status, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont1);
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
		// final time stays visible until the next run starts or an abort
		t = cg.timerunFinishedTime[clientNum];
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
