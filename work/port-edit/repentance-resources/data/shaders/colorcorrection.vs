#ifdef GL_ES
precision highp float;
#endif

#if __VERSION__ < 140
#define in attribute
#define out varying
#endif

in vec3 Position;
in vec2 TexCoord;
in float Exposure;
in float Gamma;

out vec2 TexCoord0;
out float ExposureOut;
out float GammaOut;

uniform mat4 Transform;

void main(void)
{
	ExposureOut = Exposure;
	GammaOut = Gamma;
	gl_Position = Transform * vec4(Position.xyz, 1.0);
	TexCoord0 = TexCoord;
}
