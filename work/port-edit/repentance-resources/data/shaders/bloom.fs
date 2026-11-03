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
in float OutBloomAmount;
in vec2 OutRatio;

uniform sampler2D Texture0;

void main(void)
{
	vec4 sum = vec4(0);
	vec2 texcoord = vec2(TexCoord0);
	fragColor = Color0 * texture(Texture0, texcoord);
	int j, i;
	for( i=-4;i < 4; i++){
		for (j = -3; j < 3; j++) {
			vec4 col = texture(Texture0, texcoord + vec2(j, i) * OutRatio);
			sum += col * col * 0.05;
		}
	}
	fragColor += sum * OutBloomAmount;
}
