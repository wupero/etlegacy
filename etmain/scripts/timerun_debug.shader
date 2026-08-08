// speedrun mod: semi-transparent blue debug boxes for timerun zones.
// cull none (twosided): every wall renders from inside AND outside.
// low-alpha additive + no depthwrite: walls stay see-through, so all 6
// faces of a zone are visible at once (ETrun-style trigger boxes). An
// opaque box would only show the 3 faces facing the camera.
gfx/2d/timerun_debug
{
	cull none
	nopicmip
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE
		alphaGen const 0.25
		rgbGen vertex
	}
}
