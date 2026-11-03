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
in vec4 ColorizeOut;
in vec3 ColorOffsetOut;
in vec2 TextureSizeOut;
in float PixelationAmountOut;
in vec3 ClipPlaneOut;
in vec4 ChampionColorOut;

uniform sampler2D Texture0;
const vec3 _lum = vec3(0.212671, 0.715160, 0.072169);

#define USE_PREMULTIPLIED_ALPHA

void main(void)
{
	// Clip
	if(dot(gl_FragCoord.xy, ClipPlaneOut.xy) < ClipPlaneOut.z)
		discard;
	
	// Pixelate
	vec2 pa = vec2(1.0+PixelationAmountOut, 1.0+PixelationAmountOut) / TextureSizeOut;
	
	vec4 SourceColor = texture(Texture0, PixelationAmountOut > 0.0 ? TexCoord0 - mod(TexCoord0, pa) + pa * 0.5 : TexCoord0);
	if (SourceColor.a == 0.0) discard;

	if(SourceColor.a < 0.5)
	{
		// alpha < 0.5, leave color unchanged and remap opacity to [0;1]
#ifdef USE_PREMULTIPLIED_ALPHA
		SourceColor *= 2.00788; // 255/127
#else
		SourceColor.a *= 2.00788;
#endif
	}
	else
	{
		// alpha > 0.5, blend with champion color
#ifdef USE_PREMULTIPLIED_ALPHA
		SourceColor.rgb /= SourceColor.a;
		//SourceColor = mix(vec4(ChampionColorOut.rgb * dot(SourceColor.rgb, _lum), ChampionColorOut.a), vec4(SourceColor.rgb, 1.0), (SourceColor.a - 0.50196) * 2.00788);
		SourceColor = mix(vec4(SourceColor.rgb, 1.0), vec4(ChampionColorOut.rgb * dot(SourceColor.rgb, _lum), 1.0), ChampionColorOut.a * (1.0 - SourceColor.a) * 2.00788);
		SourceColor.rgb *= SourceColor.a;
#else
		//SourceColor = mix(vec4(ChampionColorOut.rgb * dot(SourceColor.rgb, _lum), ChampionColorOut.a), vec4(SourceColor.rgb, 1.0), (SourceColor.a - 0.50196) * 2.00788);
		SourceColor = mix(vec4(SourceColor.rgb, 1.0), vec4(ChampionColorOut.rgb * dot(SourceColor.rgb, _lum), 1.0), ChampionColorOut.a * (1.0 - SourceColor.a) * 2.00788);
#endif
	}
	
	vec4 Color = Color0 * SourceColor;
	vec3 Colorized = mix(Color.rgb, dot(Color.rgb, _lum) * ColorizeOut.rgb, ColorizeOut.a);
	fragColor = vec4(Colorized + ColorOffsetOut * Color.a, Color.a);
	
	// Color reduction
	fragColor.rgb = mix(fragColor.rgb, fragColor.rgb - mod(fragColor.rgb, 1.0/16.0), clamp(PixelationAmountOut, 0.0, 1.0));
}

/*
void main(void)
{
	vec4 SourceColor = texture(Texture0, TexCoord0);
	if(SourceColor.a < 0.5)
	{
		// alpha < 0.5, leave color unchanged and remap opacity to [0;1]
#ifdef USE_PREMULTIPLIED_ALPHA
		SourceColor *= 2.00788; // 255/127
#else
		SourceColor.a *= 2.00788;
#endif
	}
	else
	{
		// alpha > 0.5, blend with champion color
#ifdef USE_PREMULTIPLIED_ALPHA
		SourceColor.rgb /= SourceColor.a;
		SourceColor = mix(vec4(ChampionColorOut.rgb * dot(SourceColor.rgb, _lum), ChampionColorOut.a), vec4(SourceColor.rgb, 1.0), (SourceColor.a - 0.50196) * 2.00788);
		SourceColor.rgb *= SourceColor.a;
#else
		SourceColor = mix(vec4(ChampionColorOut.rgb * dot(SourceColor.rgb, _lum), ChampionColorOut.a), vec4(SourceColor.rgb, 1.0), (SourceColor.a - 0.50196) * 2.00788);
#endif
	}
	
	vec4 Color = Color0 * SourceColor;
	vec3 Colorized = mix(Color.rgb, dot(Color.rgb, _lum) * ColorizeOut.rgb, ColorizeOut.a);
	fragColor = vec4(Colorized + ColorOffsetOut * Color.a, Color.a);
}
*/
