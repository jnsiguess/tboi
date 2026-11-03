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
in vec4 Shockwave1;
in vec4 Shockwave2;
in vec2 Ratio;

out vec4 Color0;
out vec2 TexCoord0;
out vec4 Shockwave1Out;
out vec4 Shockwave2Out;
out vec2 OutRatio;

uniform mat4 Transform;

void main(void)
{
	Shockwave1Out = Shockwave1;
	Shockwave2Out = Shockwave2;
	OutRatio = Ratio;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
	Color0 = Color;
}
