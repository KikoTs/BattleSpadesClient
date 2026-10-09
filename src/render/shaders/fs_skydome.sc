$input v_color0, v_texcoord0, v_skyDirection

#include <bgfx_shader.sh>

SAMPLER2D(s_skyTexture, 0);
uniform vec4 u_skyUvTime;
uniform vec4 u_cameraPosition;
uniform vec4 u_fogParams;

void main()
{
    gl_FragColor = texture2D(s_skyTexture, v_texcoord0) * v_color0;
    if (u_skyUvTime.w > 0.5)
    {
        // The 0.75 cylinder ends in solid fog. Keep that same background
        // everywhere a 64-high map could meet the cut, then reveal the chosen
        // sky smoothly above it. Other uses of this shader (decals/lasers)
        // leave the per-draw flag zero.
        float elevation = -v_skyDirection.z / max(length(v_skyDirection.xy), 0.001);
        float terrain_top = (u_cameraPosition.z - 176.0) / max(u_fogParams.w, 1.0);
        gl_FragColor.a *= smoothstep(terrain_top, terrain_top + 0.2, elevation);
    }
}
