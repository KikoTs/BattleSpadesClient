$input v_lightPosition

#include <bgfx_shader.sh>

/**
 * The shadow pass renders to a depth-only target, so the colour written here is
 * never sampled. bgfx still requires a fragment stage to link a program.
 */
void main()
{
    gl_FragColor = vec4_splat(v_lightPosition.z / max(v_lightPosition.w, 1e-6));
}
