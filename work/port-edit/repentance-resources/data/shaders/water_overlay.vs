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
in vec4 Ratio;
in float Amount;
in float PixelationAmount;

out vec4 Color0;
out vec2 TexCoord0;
out vec4 Ratio0;
out float Amount0;
out float PixelationAmount0;

uniform mat4 Transform;

void main()
{
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	Color0 = Color;
	TexCoord0 = TexCoord;
	Ratio0 = Ratio;
	Amount0 = Amount;
	PixelationAmount0 = PixelationAmount;
}
