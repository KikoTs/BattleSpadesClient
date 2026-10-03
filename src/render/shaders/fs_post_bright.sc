$input v_texcoord0

#include <bgfx_shader.sh>

// Bloom bright pass: a 4-tap box downsample to half resolution, keeping only
// what rises above the soft-knee threshold.

SAMPLER2D(s_postColor, 0);

// xy = 1 / source texture size.
uniform vec4 u_postTexel;
// x = threshold, y = knee.
uniform vec4 u_postBloom;

void main()
{
    vec2 uv = v_texcoord0;
    vec2 h = u_postTexel.xy * 0.5;
    vec3 c = (texture2D(s_postColor, uv + vec2(-h.x, -h.y)).xyz +
              texture2D(s_postColor, uv + vec2(h.x, -h.y)).xyz +
              texture2D(s_postColor, uv + vec2(-h.x, h.y)).xyz +
              texture2D(s_postColor, uv + vec2(h.x, h.y)).xyz) * 0.25;
    float peak = max(c.x, max(c.y, c.z));
    float knee = u_postBloom.y;
    float soft = clamp(peak - u_postBloom.x + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, peak - u_postBloom.x) / max(peak, 1e-4);
    gl_FragColor = vec4(c * contribution, 1.0);
}
