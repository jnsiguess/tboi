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

in vec2 TexCoord0;
in float ExposureOut;
in float GammaOut;

uniform sampler2D Texture0;

/*
 * THE LAST STRETCH, SHARP AND STILL (the port, 13.10.2026).
 *
 * This pass lays the finished scene on the window, and the window is almost
 * never a whole multiple of it: 3120 x 1440 is 5.333 pixels to the canvas
 * unit at ORIGINAL, 0.889 to a scene pixel at x6, 1.333 at x4. Point-sampled,
 * a texel comes out 5 pixels wide or 6 by where it lands, and as the camera
 * creeps the extra pixel walks along every wall (the owner, 13.10.2026).
 * Smoothed plainly, the pixel art goes soft.
 *
 * So each texel is read as a solid block, and only the one screen pixel that
 * straddles the border between two texels is blended, by how much of it each
 * side covers: the "sharp bilinear" of pixel-art scalers. An edge then sits
 * exactly where it belongs, to a fraction of a pixel, in every frame, and the
 * blocks stay as hard as the stretch allows. The port turns the texture's
 * filter to LINEAR for this one draw (stretch.c) and puts it back after.
 *
 * How many screen pixels a texel covers is taken from the coordinate's own
 * derivative, so the same file serves every multiplier, FULLSCREEN on or off,
 * a cutscene's canvas, and the 1:1 copy into the HQX4 surface (there the
 * blend band is empty and every read lands on a texel's middle). Below one
 * pixel to a texel the scene is being REDUCED, and plain bilinear is the
 * right reading of it.
 */
/*
 * LIGHTER (21.10.2026, the renderer audit): this pass runs on every window
 * pixel in every mode, and on a Mali-G57 MP1 the size query, the two divides
 * by it and the reciprocals cost 2-4 ms a frame. The port now hands the
 * texture's size in (PortSharp, stretch.c): xy the size in texels, zw its
 * reciprocal. PortSharp.x == 0: the stretch is a whole number - every screen
 * pixel lands inside one texel, the read is the plain one and the texture
 * stays NEAREST. PortSharp.x < 0: the size is not known on this device, the
 * shader asks for it as before.
 */
uniform vec4 PortSharp;

vec2 sharp(vec2 uv)
{
#if __VERSION__ >= 300
	vec2 size = PortSharp.xy, inv = PortSharp.zw;
	if (PortSharp.x < 0.0) {
		size = vec2(textureSize(Texture0, 0));
		inv = 1.0 / size;
	}
	vec2 t = uv * size;
	vec2 per = 1.0 / max(vec2(abs(dFdx(t.x)), abs(dFdy(t.y))), vec2(1e-6));
	per = max(per, vec2(1.0));                  /* reduced: plain bilinear */
	vec2 band = 0.5 - 0.5 / per;                /* the solid middle of a texel, half of it */
	vec2 d = fract(t) - 0.5;
	vec2 f = (d - clamp(d, -band, band)) * per + 0.5;
	return (floor(t) + f) * inv;
#else
	return uv;
#endif
}

void main(void)
{
	vec2 uv = PortSharp.x == 0.0 ? TexCoord0 : sharp(TexCoord0);
	fragColor = texture( Texture0, uv );
	vec4 Exposed = fragColor * ExposureOut;
	vec4 FinalColor = pow(clamp(Exposed, 0.0, 1.0), vec4(GammaOut));
	fragColor.rgb = FinalColor.rgb;
}
