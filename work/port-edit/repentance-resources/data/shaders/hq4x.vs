#ifdef GL_ES
precision highp float;
#endif

#if __VERSION__ < 140
#define in attribute
#define out varying
#endif

in vec3 Position;
in vec2 TexCoord;
in vec3 ScreenSizePow2;

out vec2 TexCoord0;
out vec3 ScreenSizePow2Out;

uniform mat4 Transform;

void main(void)
{
	ScreenSizePow2Out = ScreenSizePow2;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
}
