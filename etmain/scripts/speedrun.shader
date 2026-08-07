// speedrun mod: translucent player ghost (used by speedrun_playerOpacity)
speedrun/ghost
{
	nopicmip
	nocompress
	nomipmaps
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
