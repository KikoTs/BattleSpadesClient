$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// xy = authored per-retail-draw UV speed, z = refresh-independent 60 Hz
// retail draw count. Passing seconds here slows every layer by about 60x.
uniform vec4 u_skyUvTime;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0 + (u_skyUvTime.xy * u_skyUvTime.z);
}
