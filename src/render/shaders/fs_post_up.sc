$input v_texcoord0

#include <bgfx_shader.sh>

// Bloom pyramid: this level plus a 9-tap tent upsample of the level below.

SAMPLER2D(s_postColor, 0);
SAMPLER2D(s_postLow, 1);

// xy = 1 / lower level texture size.
uniform vec4 u_postTexel;

void main()
{
    vec2 uv = v_texcoord0;
    vec2 d = u_postTexel.xy;
    vec3 low = texture2D(s_postLow, uv).xyz * 4.0;
    low += (texture2D(s_postLow, uv + vec2(-d.x, 0.0)).xyz +
            texture2D(s_postLow, uv + vec2(d.x, 0.0)).xyz +
            texture2D(s_postLow, uv + vec2(0.0, -d.y)).xyz +
            texture2D(s_postLow, uv + vec2(0.0, d.y)).xyz) * 2.0;
    low += texture2D(s_postLow, uv + vec2(-d.x, -d.y)).xyz +
           texture2D(s_postLow, uv + vec2(d.x, -d.y)).xyz +
           texture2D(s_postLow, uv + vec2(-d.x, d.y)).xyz +
           texture2D(s_postLow, uv + vec2(d.x, d.y)).xyz;
    gl_FragColor = vec4(texture2D(s_postColor, uv).xyz + low / 16.0, 1.0);
}
