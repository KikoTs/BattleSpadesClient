$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_menuMotion;
uniform vec4 u_menuBounds;
// Premultiplied cutout, inverse image width/height in drawable pixels, layer camera scale.
uniform vec4 u_menuAtmosphere;

vec4 imageSample(vec2 p)
{
    if (u_menuAtmosphere.x > 0.5 && (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0))
        return vec4(0.0, 0.0, 0.0, 0.0);
    return texture2D(s_texColor, mix(u_menuBounds.xy, u_menuBounds.zw,
        clamp(p, vec2(0.0, 0.0), vec2(1.0, 1.0))));
}

void main()
{
    vec2 original = (v_texcoord0 - u_menuBounds.xy) / (u_menuBounds.zw - u_menuBounds.xy);
    vec2 pixel = u_menuAtmosphere.yz;
    // A single camera translation preserves every silhouette and straight line.
    vec2 cameraTravel = vec2(12.0, 8.0) * u_menuAtmosphere.w;
    vec2 p = original + u_menuMotion.xy * cameraTravel * pixel;

    // A short symmetric exposure follows the camera path; no defocus or frame history.
    vec2 shutter = u_menuMotion.zw * cameraTravel * (1.0 / 90.0);
    shutter *= min(1.0, 1.0 / max(length(shutter), 0.001));
    shutter *= pixel;
    vec4 colour = imageSample(p) * 0.4;
    colour += (imageSample(p + shutter * 0.5) + imageSample(p - shutter * 0.5)) * 0.24;
    colour += (imageSample(p + shutter) + imageSample(p - shutter)) * 0.06;
    // Cutout textures are uploaded premultiplied, so bilinear filtering and the
    // shutter cannot pull dark/colored transparent pixels into object edges.
    // Convert back for the shared straight-alpha UI blend state.
    if (u_menuAtmosphere.x > 0.5)
        colour.rgb /= max(colour.a, 0.0001);
    gl_FragColor = colour * v_color0;
}
