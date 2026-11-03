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
in vec4 Ratio0;
in float Amount0;
in float PixelationAmount0;

uniform sampler2D Texture0;

void main(void)
{
	vec2 newTexCoord = TexCoord0;
	
	newTexCoord.x += (0.001 / Ratio0.x) * sin(
		(newTexCoord.x - Ratio0.z) * 10.0 * Ratio0.x +
		(newTexCoord.y - Ratio0.w) * 100.0 * Ratio0.y +
		Amount0 * 6.28318530718
	);
	fragColor = texture(Texture0, newTexCoord.xy) * Color0;
}
