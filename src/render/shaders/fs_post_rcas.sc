$input v_texcoord0

#include <bgfx_shader.sh>

// Robust contrast-adaptive sharpening, after the published AMD FidelityFX
// Super Resolution 1.0 RCAS algorithm (MIT licence, see
// THIRD_PARTY_NOTICES.md): a 5-tap cross whose negative lobe is limited so
// the result can never leave the local [min, max] range, which is what keeps
// it from ringing or clipping.

SAMPLER2D(s_postColor, 0);

// xy = 1 / source size.
uniform vec4 u_postTexel;
// x = sharpness, 0 .. 1.
uniform vec4 u_postSharp;

void main()
{
    vec2 uv = v_texcoord0;
    vec2 t = u_postTexel.xy;
    vec3 b = texture2DLod(s_postColor, uv + vec2(0.0, -t.y), 0.0).xyz;
    vec3 d = texture2DLod(s_postColor, uv + vec2(-t.x, 0.0), 0.0).xyz;
    vec3 e = texture2DLod(s_postColor, uv, 0.0).xyz;
    vec3 f = texture2DLod(s_postColor, uv + vec2(t.x, 0.0), 0.0).xyz;
    vec3 h = texture2DLod(s_postColor, uv + vec2(0.0, t.y), 0.0).xyz;

    vec3 minimum = min(min(b, d), min(f, h));
    vec3 maximum = max(max(b, d), max(f, h));
    vec3 hitMin = minimum / max(4.0 * maximum, vec3(1e-5, 1e-5, 1e-5));
    vec3 hitMax = (vec3(1.0, 1.0, 1.0) - maximum) / min(4.0 * minimum - 4.0, vec3(-1e-5, -1e-5, -1e-5));
    vec3 lobeRgb = max(-hitMin, hitMax);
    // Written as clamp(): glsl-optimizer (GLSL 130) folds max(a, min(b, 0))
    // into a constant -0.1875 and over-sharpens every pixel.
    float lobe = clamp(max(lobeRgb.x, max(lobeRgb.y, lobeRgb.z)), -0.1875, 0.0) * u_postSharp.x;
    vec3 colour = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
    gl_FragColor = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
