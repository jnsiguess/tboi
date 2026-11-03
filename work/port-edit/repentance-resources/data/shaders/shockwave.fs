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
in vec4 Shockwave1Out;
in vec4 Shockwave2Out;
in vec2 OutRatio;

uniform sampler2D Texture0;

void main(void)
{
	vec2 newTexCoord = TexCoord0;
	float distance1 = length((TexCoord0.st - Shockwave1Out.xy) * OutRatio);
	float time1 = Shockwave1Out.z;
	bool combine = false;
	if (Shockwave1Out.x > -100.0 && distance1 <= time1 + 0.05 && distance1 >= time1 - 0.05) {
		float diff = (distance1 - time1);
		float powDiff = 1.0-pow(abs(diff*20.0), 0.8);
		vec2 diffUV = normalize((TexCoord0.st - Shockwave1Out.xy) * OutRatio);
		newTexCoord = TexCoord0.st + (diffUV * powDiff * Shockwave1Out.a) / OutRatio;
		combine = true;
	}
	float distance2 = length((TexCoord0.st - Shockwave2Out.xy) * OutRatio);
	float time2 = Shockwave2Out.z;
	if (Shockwave2Out.x > -100.0 && distance2 <= time2 + 0.05 && distance2 >= time2 - 0.05) {
		float diff = (distance2 - time2);
		float powDiff = 1.0-pow(abs(diff*20.0), 0.8);
		vec2 diffUV = normalize((TexCoord0.st - Shockwave2Out.xy) * OutRatio);
		vec2 calcCoord = TexCoord0.st + (diffUV * powDiff * Shockwave2Out.a) / OutRatio;
		if (combine)
			newTexCoord = (calcCoord + newTexCoord) * 0.5;
		else
			newTexCoord = calcCoord;
	}
	fragColor = texture(Texture0, newTexCoord.st) * Color0;
}
