#ifdef GL_ES
precision highp float;
#endif

#if __VERSION__ >= 140
out vec4 fragColor;
#else
#define in varying
#define fragColor gl_FragColor
#define texture texture2D
#endif

in vec4 Color0;
in vec2 TexCoord0;
in vec2 WrapSize0;

uniform sampler2D Texture0;

vec2 wrapx(vec2 uv, vec2 m)
{
	return uv + floor(uv / m) * (vec2(1.0, 1.0) - m);
}

void main(void)
{
	fragColor = texture(Texture0, wrapx(TexCoord0, WrapSize0)) * Color0;
}
