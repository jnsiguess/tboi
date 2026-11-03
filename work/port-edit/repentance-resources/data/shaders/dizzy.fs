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
in vec2 TextureSize0;
in vec4 Ratio0;
in float Time0;
in float Amount0;
in float PixelationAmount0;

uniform sampler2D Texture0;

vec2 mirrorclamp(vec2 uv, vec2 m)
{
	return uv + max(vec2(0.0, 0.0), -2.0 * uv) + min(vec2(0.0, 0.0), -2.0 * (uv - m));
}

void main(void)
{
	float time = Time0 * 6.28318530718;
	float amount = Amount0;
	float am2 = pow(amount, 0.5);
	
	float rot = amount * 0.3 * cos(0.8 * time);
	mat2 rmat = mat2(cos(rot), -sin(rot), sin(rot), cos(rot));
	
	vec2 center = 0.5 * TextureSize0.xy;
	vec2 texcoord = rmat * (TexCoord0 - center) + center;
	
	vec2 amp = amount * vec2(0.016, 0.016) / Ratio0.xy;
	vec2 freq = vec2(8.0, 12.0);
	float speed = 1.0;
	vec2 uv = mirrorclamp(texcoord + amp * sin(freq * 3.141593 * Ratio0.xy * (TexCoord0.yx - Ratio0.wz) + speed*time), TextureSize0.xy);
	
	float mul = 1.0 + am2 * 0.6;
	float saturate = min(amount, 1.0) * 0.7;
	float exponent = 1.0 + pow(amount, 5.0) * 100.0;
	vec3 colorAdd = min(am2, 1.0) * (0.16 + 0.1 * sin(vec3(0.0, 1.0, 2.0) + vec3(0.4, 0.5, 0.6) * time));
	
	fragColor = vec4(mul * (pow(saturate + colorAdd + texture(Texture0, uv).xyz, vec3(exponent, exponent, exponent))), 1.0);
}
