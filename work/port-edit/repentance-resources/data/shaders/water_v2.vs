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
in float Time;
in vec3 ColorMul;
in vec2 CurrentDir;
in vec2 TextureSize;
in float PixelationAmount;

out vec4 Color0;
out vec2 TexCoord0;
out float Time0;
out vec3 ColorMul0;
out vec2 CurrentDir0;
out vec2 TextureSize0;
out float PixelationAmount0;

uniform mat4 Transform;

void main()
{
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	Color0 = Color;
	TexCoord0 = TexCoord;
	Time0 = Time;
	ColorMul0 = ColorMul;
	CurrentDir0 = CurrentDir;
	TextureSize0 = TextureSize;
	PixelationAmount0 = PixelationAmount;
}
