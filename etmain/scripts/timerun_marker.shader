// speedrun mod: solid-ish 2D diamond markers floating above timerun zone points.
// normal alpha blend (not additive): clearly visible, depth-tested against the
// world so markers behind walls stay hidden. no depthwrite: markers never
// occlude each other or the zone boxes.
gfx/2d/timerun_marker
{
	cull none
	nopicmip
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		alphaGen const 0.85
		rgbGen vertex
	}
}
