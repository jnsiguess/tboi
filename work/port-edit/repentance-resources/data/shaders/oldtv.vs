#ifdef GL_ES
precision highp float;
#endif

#if __VERSION__ < 140
#define in attribute
#define out varying
#endif

in vec3 Position;
in vec2 TexCoord;
in float Time;
in float Amount;
in vec4 ScreenSize;

out vec2 TexCoord0;
out float TimeOut;
out float AmountOut;
out vec4 OutScreenSize;

uniform mat4 Transform;

void main(void)
{
	TimeOut = Time;
	AmountOut = Amount;
	OutScreenSize = ScreenSize;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
}
