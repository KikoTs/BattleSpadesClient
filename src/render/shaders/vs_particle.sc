$input a_position, a_texcoord0, i_data0, i_data1, i_data2
$output v_texcoord0, v_color0, v_life01, v_particleWorld

#include <bgfx_shader.sh>

// xy: 1/frames_x, 1/frames_y. Instances carry their own frames_x in i_data2.w
// so one draw can mix sheet layouts within a batch.
uniform vec4 u_atlasGrid;

void main()
{
    vec3  centre = i_data0.xyz;
    float size   = i_data0.w;
    float rot    = i_data2.x;

    // Camera-facing basis, extracted in a way that survives every backend.
    //
    // bgfx hands all backends the same float[16], but `m[a][b]` does NOT mean
    // the same thing in all of them: it is column a, row b in GLSL, and row a,
    // column b in HLSL, SPIR-V and Metal. Retail's literal expression
    // (aoslib/shader_source/billboard_vert.py) is correct only because retail is
    // GLSL 110. Transcribed here it read the TRANSPOSE on dx11, spirv and metal
    // -- which is every backend that actually ships on Windows.
    //
    // The failure was not subtle once traced: the quad normal came out as
    // (right.z, up.z, -forward.z), and this camera's right vector is
    // (sin yaw, -cos yaw, 0), so right.z is identically zero and the normal
    // carried NO yaw term. Sprites therefore stood in a fixed world plane --
    // face-on when looking along y, and exactly edge-on, i.e. completely
    // invisible, when looking along x.
    //
    // A row vector times the matrix extracts a ROW, and world_renderer writes
    // the camera basis into rows 0 and 1. mul() is defined for both conventions,
    // so this reads identically everywhere.
    vec3 right = mul(vec4(1.0, 0.0, 0.0, 0.0), u_view).xyz;
    vec3 up    = mul(vec4(0.0, 1.0, 0.0, 0.0), u_view).xyz;

    float c = cos(rot);
    float s = sin(rot);
    vec2  offset = vec2(a_position.x * c - a_position.y * s,
                        a_position.x * s + a_position.y * c) * size;

    vec3 world = centre + right * offset.x + up * offset.y;
    gl_Position = mul(u_viewProj, vec4(world, 1.0));

    // Sheet cell selection is row-major, matching the retail frame walker.
    float columns = max(i_data2.w, 1.0);
    float frame   = floor(i_data2.z);
    float column  = mod(frame, columns);
    float row     = floor(frame / columns);
    v_texcoord0 = (vec2(column, row) + a_texcoord0) * u_atlasGrid.xy;

    v_color0 = i_data1;
    v_life01 = i_data2.y;
    // Lighting is evaluated once from the particle centre.  Every fragment
    // of one billboard therefore receives one stable world-space sample;
    // using the camera-facing corner position makes a large smoke sprite
    // shimmer as it rotates around the camera.
    v_particleWorld = centre;
}
