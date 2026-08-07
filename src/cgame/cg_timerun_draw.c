/**
 * @file cg_timerun_draw.c
 * @brief speedrun mod HUD drawing
 */

#include "cg_local.h"

/**
 * @brief CG_DrawTimer
 * @details speedrun mod: always-visible run timer on timerun maps.
 * Shows the current run time while a run is active (MM:SS.mmm) and
 * 00:00.000 otherwise.
 */
void CG_DrawTimer(void)
{
	int   t, min, sec, milli;
	char  status[32];
	vec4_t color = { 1.f, 1.f, 1.f, 1.f };
	float x, y, w;

	if (!isTimerun.integer)
	{
		return;
	}

	if (cg.timerunActive)
	{
		// server sent startTime+500; keep the ETrun +/-500 symmetry
		t = cg.time - (cg.timerunStartTime - 500);
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

	// top-center (HUD virtual space is 640 wide)
	w = CG_Text_Width_Ext(status, 0.3f, 0, &cgs.media.limboFont1);
	x = 320.f + cgs.wideXoffset - 0.5f * w;
	y = 30.f;

	CG_Text_Paint_Ext(x, y, 0.3f, 0.3f, color, status, 0, 0, ITEM_TEXTSTYLE_SHADOWED, &cgs.media.limboFont1);
}
