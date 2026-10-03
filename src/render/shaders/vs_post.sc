$input a_position, a_texcoord0
$output v_texcoord0

#include <bgfx_shader.sh>

// One fullscreen triangle in clip space. The renderer bakes the texture
// coordinates for the backend's render-target origin into the vertices, so
// every post pass samples its source upright on D3D, Vulkan, Metal and GL.
void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
}
