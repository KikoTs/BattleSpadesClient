$input v_texcoord0

#include <bgfx_shader.sh>

// Bloom pyramid: halve the resolution with a 4-tap bilinear box (16 texels).

SAMPLER2D(s_postColor, 0);

// xy = 1 / source texture size.
uniform vec4 u_postTexel;

void main()
{
    vec2 uv = v_texcoord0;
    vec2 h = u_postTexel.xy;
    vec3 c = (texture2D(s_postColor, uv + vec2(-h.x, -h.y)).xyz +
              texture2D(s_postColor, uv + vec2(h.x, -h.y)).xyz +
              texture2D(s_postColor, uv + vec2(-h.x, h.y)).xyz +
              texture2D(s_postColor, uv + vec2(h.x, h.y)).xyz) * 0.25;
    gl_FragColor = vec4(c, 1.0);
}
