$input v_texcoord0

#include <bgfx_shader.sh>
#include "post_common.sh"

// Screen-space ambient obscurance from the scene depth alone.
//
// Each pixel is rebuilt in view space, its normal is taken from the nearer of
// its depth neighbours (so block edges do not smear the normal across a
// silhouette), and a spiral of samples within a world-space radius counts how
// much of the hemisphere above the surface is closed off. The result is a
// visibility factor the world pass multiplies the colour by.

SAMPLER2D(s_postDepth, 1);

uniform vec4 u_postProj;
uniform vec4 u_postDepthMode;
// x = radius in blocks, y = sample count (1..16), z = strength, w = bias.
uniform vec4 u_postAo;
// xy = 1 / depth texture size.
uniform vec4 u_postTexel;

vec3 aoPosition(vec2 uv)
{
    // Snap to a depth texel centre and rebuild the position from that same
    // point. Between texels (every pixel of a half-resolution pass) the
    // rounding of a point fetch differs between backends, and a position built
    // from a neighbouring texel's depth tilts every flat surface into
    // self-occlusion.
    uv = (floor(uv / u_postTexel.xy) + vec2(0.5, 0.5)) * u_postTexel.xy;
    float depth = texture2DLod(s_postDepth, uv, 0.0).x;
    return postViewPosition(uv, postLinearDepth(depth, u_postDepthMode, u_postProj),
                            u_postDepthMode, u_postProj);
}

void main()
{
    vec2 uv = (floor(v_texcoord0 / u_postTexel.xy) + vec2(0.5, 0.5)) * u_postTexel.xy;
    float depth = texture2DLod(s_postDepth, uv, 0.0).x;
    if (depth >= 0.99999)
    {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }
    vec3 p = aoPosition(uv);
    vec2 dx = vec2(u_postTexel.x, 0.0);
    vec2 dy = vec2(0.0, u_postTexel.y);
    vec3 right = aoPosition(uv + dx) - p;
    vec3 left = p - aoPosition(uv - dx);
    vec3 down = aoPosition(uv + dy) - p;
    vec3 up = p - aoPosition(uv - dy);
    vec3 tangentX = abs(right.z) < abs(left.z) ? right : left;
    vec3 tangentY = abs(down.z) < abs(up.z) ? down : up;
    vec3 n = normalize(cross(tangentX, tangentY));
    if (dot(n, p) > 0.0)
    {
        n = -n;
    }

    float radius = u_postAo.x;
    float count = u_postAo.y;
    // Screen-space reach of the world radius at this depth.
    vec2 reach = vec2(0.5 * radius / (p.z * u_postProj.x), 0.5 * radius / (p.z * u_postProj.y));
    // Interleaved gradient noise rotates the spiral per pixel; the world pass
    // blurs the result, so the pattern never shows.
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float rotation = noise * 6.2831853;
    float occlusion = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        float index = float(i);
        if (index < count)
        {
            float t = (index + 0.5) / count;
            float angle = index * 2.3999632 + rotation;
            vec2 offset = vec2(cos(angle), sin(angle)) * reach * t;
            vec3 v = aoPosition(uv + offset) - p;
            float distance2 = dot(v, v);
            float range = 1.0 - clamp(distance2 / (radius * radius), 0.0, 1.0);
            occlusion += max(dot(n, v) * inversesqrt(distance2 + 1e-4) - u_postAo.w, 0.0) * range;
        }
    }
    float visibility = clamp(1.0 - u_postAo.z * occlusion / max(count, 1.0), 0.0, 1.0);
    gl_FragColor = vec4(visibility, visibility, visibility, 1.0);
}
