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
in float Amount;
in float Brightness;
in float Contrast;

out vec2 TexCoord0;
out vec4 Color0;
out float Amount0;
out float Brightness0;
out float Contrast0;

uniform mat4 Transform;

void main()
{
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	Color0 = Color;
	TexCoord0 = TexCoord;
	Amount0 = Amount;
	Brightness0 = Brightness;
	Contrast0 = Contrast;
}
