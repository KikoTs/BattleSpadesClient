$input v_texcoord0

#include <bgfx_shader.sh>
#include "post_common.sh"

// The world-only screen-space pass, run before the first-person view is drawn
// on top: applies the (depth-aware blurred) ambient occlusion and the camera
// motion blur. Neither can touch the held weapon or the HUD, which are drawn
// after this pass.

SAMPLER2D(s_postColor, 0);
SAMPLER2D(s_postDepth, 1);
SAMPLER2D(s_postAo, 2);

uniform vec4 u_postProj;
uniform vec4 u_postDepthMode;
// x = ambient occlusion on, y = motion blur on, z = shutter fraction.
uniform vec4 u_postFlags;
// xy = 1 / ambient occlusion texture size.
uniform vec4 u_postAoTexel;
// Current clip -> previous clip, for camera-only motion vectors.
uniform mat4 u_postReproject;

float worldVisibility(vec2 uv, float centreDepth)
{
    float total = 0.0;
    float weights = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 tap = uv + vec2(float(x), float(y)) * u_postAoTexel.xy;
            float tapDepth = postLinearDepth(texture2DLod(s_postDepth, tap, 0.0).x,
                                             u_postDepthMode, u_postProj);
            float weight = 1.0 / (1.0 + 32.0 * abs(tapDepth - centreDepth) / centreDepth);
            total += texture2DLod(s_postAo, tap, 0.0).x * weight;
            weights += weight;
        }
    }
    return total / weights;
}

void main()
{
    vec2 uv = v_texcoord0;
    float depth = texture2DLod(s_postDepth, uv, 0.0).x;
    vec3 colour = texture2DLod(s_postColor, uv, 0.0).xyz;

    if (u_postFlags.y > 0.5)
    {
        float clipDepth = u_postDepthMode.x > 0.5 ? depth * 2.0 - 1.0 : depth;
        vec4 previous = mul(u_postReproject, vec4(postNdcXy(uv, u_postDepthMode), clipDepth, 1.0));
        vec2 previousNdc = previous.xy / max(previous.w, 1e-5);
        vec2 previousUv = vec2(previousNdc.x * 0.5 + 0.5, previousNdc.y * u_postDepthMode.y * 0.5 + 0.5);
        vec2 velocity = (uv - previousUv) * u_postFlags.z;
        float speed = length(velocity);
        // A long streak is a camera cut, not motion: cap it.
        if (speed > 0.06)
        {
            velocity *= 0.06 / speed;
        }
        if (speed > 0.0005)
        {
            vec3 sum = vec3(0.0, 0.0, 0.0);
            for (int i = 0; i < 8; ++i)
            {
                float t = (float(i) + 0.5) / 8.0 - 0.5;
                sum += texture2DLod(s_postColor, uv + velocity * t, 0.0).xyz;
            }
            colour = sum * 0.125;
        }
    }

    if (u_postFlags.x > 0.5 && depth < 0.99999)
    {
        colour *= worldVisibility(uv, postLinearDepth(depth, u_postDepthMode, u_postProj));
    }
    gl_FragColor = vec4(colour, 1.0);
}
