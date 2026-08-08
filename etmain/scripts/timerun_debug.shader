// speedrun mod: solid blue debug boxes for timerun zones.
// cull none (twosided) so every wall renders from inside AND outside the box.
// depthwrite: blended surfaces drop the depth mask unless explicitly requested,
// which made walls pile up additively; writing depth restores proper occlusion.
gfx/2d/timerun_debug
{
	cull none
	nopicmip
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE
		depthwrite
		rgbGen vertex
		alphaGen vertex
	}
}
