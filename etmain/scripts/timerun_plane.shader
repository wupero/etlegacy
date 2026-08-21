// speedrun mod: translucent see-through walls for /draw_plane debug.
// cull none (twosided): every wall renders from any side.
// normal alpha blend (not additive): the shared triangle edge of each quad
// only double-blends to a(2-a) instead of 2a, so there's no harsh diagonal
// line - the walls read as clean translucent planes.
// no depthwrite: walls are see-through (back walls visible through front ones).
gfx/2d/timerun_plane
{
	cull none
	nopicmip
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		alphaGen const 0.45
		rgbGen vertex
	}
}
