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
in float Amount;
in vec2 Ratio;
in vec2 Offset;
in vec2 PlayerPos;
in vec2 PlayerVel;
out vec4 Color0;
out vec2 TexCoord0;
out float OutTime;
out float OutAmount;
out vec2 OutRatio;
out vec2 OutOffset;
out vec2 OutPlayerPos;
out vec2 OutPlayerVel;

uniform mat4 Transform;

void main(void)
{
	OutTime = Time;
	OutAmount = Amount;
	OutRatio = Ratio;
	OutOffset = Offset;
	OutPlayerPos = PlayerPos;
	OutPlayerVel = PlayerVel;
	Color0 = Color;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
}
