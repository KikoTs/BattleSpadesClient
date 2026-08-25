$input v_texcoord0, v_color0, v_life01, v_particleWorld

#include <bgfx_shader.sh>

SAMPLER2D(s_particleAtlas, 0);
SAMPLER2D(s_particleLut, 1);

// x: 0 = alpha or premultiplied, 1 = additive.
// y: 0 = ordinary atlas*tint, 1 = retail LUT. The renderer binds either the
// glow-cube LUT or the smoke-trail LUT for the current batch.
// z: 1 enables the High/Ultra bounded point-light response. Legacy/Low/Medium
// remain byte-for-byte on the recovered unlit particle path.
uniform vec4 u_particleMode;
uniform vec4 u_pointLightPositionRadius[8];
uniform vec4 u_pointLightColorIntensity[8];

void main()
{
    vec4 texel = texture2D(s_particleAtlas, v_texcoord0);
    vec4 color;
    if (u_particleMode.y > 0.5)
    {
        // Retail particle_lut_frag indexes the horizontal gradient with the
        // sprite red channel. draw.pyd sub_10033D70 forwards
        // remaining/lifetime in gl_Vertex.w, but retail's OpenGL upload and
        // bgfx's decoded PNG use opposite row origins. The original starts
        // white/yellow and crosses the orange band as it ages; the verified
        // bgfx coordinate is therefore elapsed life directly. Using
        // 1-elapsed sampled the lower red band at 0.16 s and made the recovered
        // RPG burst visibly pink.
        vec4 lut = texture2D(s_particleLut, vec2(texel.r, v_life01));
        // particle_lut_frag ignores gl_Color completely. The animated atlas
        // alpha and LUT alpha are the complete retail coverage equation.
        color = vec4(lut.rgb, texel.a * lut.a);
    }
    else
    {
        color = texel * v_color0;
    }

    // High and Ultra let short-lived world lights tint ordinary smoke/debris.
    // Additive glow sprites are already light sources, so relighting them would
    // feed the blast into itself and clip the authored yellow LUT to white.
    if (u_particleMode.z > 0.5 && u_particleMode.x < 0.5)
    {
        vec3 incident = vec3(0.0, 0.0, 0.0);
        for (int point_index = 0; point_index < 8; ++point_index)
        {
            vec4 position_radius = u_pointLightPositionRadius[point_index];
            vec4 color_intensity = u_pointLightColorIntensity[point_index];
            float radius = max(position_radius.w, 0.001);
            float distance_to_light = length(position_radius.xyz - v_particleWorld);
            float range01 = clamp(1.0 - distance_to_light / radius, 0.0, 1.0);
            incident += color_intensity.rgb * color_intensity.w * range01 * range01;
        }
        // Smoke should catch the flash without becoming another lamp.  The
        // bounded response preserves the source alpha and authored hue.
        color.rgb += color.rgb * min(incident * 0.45, vec3(2.0, 2.0, 2.0));
    }

    // Retail particle_frag and particle_lut_frag contain no fog calculation.
    // The terrain may disappear into the map atmosphere while a short-lived
    // blast stays saturated; fogging here was the washed-out outdoor VFX bug.

    if (color.a < 0.004)
    {
        discard;
    }

    gl_FragColor = color;
}
