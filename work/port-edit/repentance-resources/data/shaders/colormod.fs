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
in float Amount0;
in float Brightness0;
in float Contrast0;

uniform sampler2D Texture0;

const vec3 _lum = vec3(0.212671, 0.715160, 0.072169);

void main()
{
	//ColorizeOut = vec4(-1.2, -0.8, 0.0, -0.15);
	//ColorizeOut = vec4(0.0, 1.0, -1.0, -0.4); // downpour
	//BrightnessOut = -0.02;
	//ContrastOut = 0.9;
	
	vec4 color = texture(Texture0, TexCoord0);
	color.rgb = mix(color.rgb, dot(color.rgb, _lum) * Color0.rgb, Amount0);
	color.rgb += vec3(Brightness0, Brightness0, Brightness0);
	color.rgb = (color.rgb - vec3(0.5, 0.5, 0.5)) * Contrast0 + vec3(0.5, 0.5, 0.5);

	fragColor = color;
}
