$input a_position
$output v_lightPosition

#include <bgfx_shader.sh>

/**
 * Depth-only pass that fills the sun's shadow map.
 *
 * Shares the world vertex buffers, so it must read only a_position: the
 * colour, face and occlusion attributes are present in the layout but
 * deliberately unused here.
 */
void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_lightPosition = gl_Position;
}
