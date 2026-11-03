#ifdef GL_ES
precision highp float;
#endif

#if __VERSION__ < 140
#define in attribute
#define out varying
#endif

in vec3 Position;
in vec4 Color;
in vec2 TexCoord;
in vec4 ColorizeIn;
in vec3 ColorOffsetIn;
in vec2 TextureSize;
in float PixelationAmount;
in vec3 ClipPlane;

out vec4 Color0;
out vec2 TexCoord0;
out vec4 ColorizeOut;
out vec3 ColorOffsetOut;
out vec2 TextureSizeOut;
out float PixelationAmountOut;
out vec3 ClipPlaneOut;

uniform mat4 Transform;

void main(void)
{
	ColorizeOut = ColorizeIn;
	ColorOffsetOut = ColorOffsetIn;
	
	Color0 = Color;
	TextureSizeOut = TextureSize;
	PixelationAmountOut = PixelationAmount;
	ClipPlaneOut = ClipPlane;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
}
