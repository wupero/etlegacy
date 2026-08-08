// speedrun mod: solid blue debug boxes for timerun zones.
// cull none (twosided) so every wall renders from inside AND outside the box.
gfx/2d/timerun_debug
{
	cull none
	nopicmip
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}
