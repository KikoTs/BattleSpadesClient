$input a_position, a_color0, a_color1, a_color2, a_texcoord0, a_texcoord1
$output v_color0, v_surface, v_world, v_shade, v_shadow, v_placed, v_retail_uv, v_retail_meta

#include <bgfx_shader.sh>

uniform vec4 u_cameraPosition;
uniform vec4 u_fogParams;
/** World -> shadow texture coordinates, including the backend's depth/Y convention. */
uniform mat4 u_shadowMatrix;
// x: shading mode. 0 = passthrough (colour already carries its shade),
// 1 = classic (reproduce the baked retail tables per pixel),
// 2 = enhanced (real directional lighting).
uniform vec4 u_lightParams;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    if (u_lightParams.x > 0.5 && u_lightParams.x < 1.5 && a_color2.a > 0.5)
    {
        // vxl.pyd sub_100051C0 writes truncated RGB bytes BEFORE interpolation.
        // The VXL light byte is baked illumination, not gl_Vertex.w's flare bypass.
        v_color0.rgb = floor(a_color0.rgb * 255.0 * a_texcoord1 + 0.0001) / 255.0;
    }

    // Detached voxel components, projectiles and the viewmodel all live in
    // local mesh space, so both fog and lighting must be evaluated after the
    // model transform or their local origin reads as a distant world point.
    v_world = mul(u_model[0], vec4(a_position.xyz, 1.0)).xyz;

    // a_color1 carries the exposed-face index and the raw 0..3 corner
    // occlusion the mesher already computed. Both were uploaded but unread
    // until lighting moved to the GPU; reconstructing the normal from the
    // face index costs no extra bandwidth and needs no re-mesh.
    int face = int(a_color1.x * 255.0 + 0.5);
    vec3 normal = vec3(0.0, 0.0, -1.0);
    if      (face == 0) { normal = vec3(-1.0,  0.0,  0.0); }
    else if (face == 1) { normal = vec3( 1.0,  0.0,  0.0); }
    else if (face == 2) { normal = vec3( 0.0, -1.0,  0.0); }
    else if (face == 3) { normal = vec3( 0.0,  1.0,  0.0); }
    else if (face == 4) { normal = vec3( 0.0,  0.0, -1.0); }
    else                { normal = vec3( 0.0,  0.0,  1.0); }
    if (u_lightParams.x > 0.5 && u_lightParams.x < 1.5 &&
        a_color2.a > 0.1 && a_color2.a < 0.5)
    {
        normal = a_texcoord0.xyz;
    }
    normal = normalize(mul(u_model[0], vec4(normal, 0.0)).xyz);

    float occlusion = a_color1.y * 255.0 + 0.5;
    // The recovered placeholder curve {1.0, 0.80, 0.65, 0.50}, evaluated
    // rather than table-indexed so it interpolates cleanly.
    float ao = 1.0 - clamp(floor(occlusion), 0.0, 3.0) * 0.1667;

    v_surface = vec4(normal, ao);

    // vxl.pyd writes AO.xy and edge.zw as four floats from ao_cube512's
    // fifteen-cell atlas. Color1.z is the recovered corner selector
    // {0,1,3,2}; the original vertex shader decodes that byte into the four
    // texture corners used by the atlas' blue noise channel.
    v_retail_uv = a_texcoord0;
    float noise_corner = floor(a_color1.z * 255.0 + 0.5);
    v_retail_meta = vec4(floor(noise_corner / 2.0),
                         noise_corner - floor(noise_corner / 2.0) * 2.0,
                         a_color1.w,
                         a_color2.a);

    // Classic reproduces the exact face table the mesher used to bake:
    // {0.85, 0.85, 0.75, 0.75, 1.00, 0.60} indexed by face.
    float face_shade = 1.0;
    if      (face == 0 || face == 1) { face_shade = 0.85; }
    else if (face == 2 || face == 3) { face_shade = 0.75; }
    else if (face == 4)              { face_shade = 1.00; }
    else                             { face_shade = 0.60; }

    float classic_ao = 1.0;
    float level = clamp(floor(occlusion), 0.0, 3.0);
    if      (level < 0.5) { classic_ao = 1.00; }
    else if (level < 1.5) { classic_ao = 0.80; }
    else if (level < 2.5) { classic_ao = 0.65; }
    else                  { classic_ao = 0.50; }

    v_shade = (u_lightParams.x < 0.5) ? 1.0 : face_shade * classic_ao;

    v_shadow = mul(u_shadowMatrix, vec4(v_world, 1.0));

    // Light from placed blocks, baked per vertex by the mesher and interpolated
    // across the face for free.
    // Retail fog is radial distance computed per vertex, including homogeneous w=1.
    vec3 fog_offset = v_world - u_cameraPosition.xyz;
    v_placed = vec4(a_color2.rgb, sqrt(dot(fog_offset, fog_offset) + 1.0));
}
