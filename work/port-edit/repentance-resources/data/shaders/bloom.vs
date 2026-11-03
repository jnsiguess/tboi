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
in float BloomAmount;
in vec2 Ratio;

out vec4 Color0;
out vec2 TexCoord0;
out float OutBloomAmount;
out vec2 OutRatio;

uniform mat4 Transform;

void main(void)
{
	OutBloomAmount = BloomAmount;
	OutRatio = Ratio;
	Color0 = Color;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
}
