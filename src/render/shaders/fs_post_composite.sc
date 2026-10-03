$input v_texcoord0

#include <bgfx_shader.sh>

// Final world resolve: scale the scene to the output size (bilinear or the
// edge-adaptive upscale), add bloom, then colour-vision correction, gamma and
// brightness. The HUD and menus draw after this pass and are never touched.
//
// The edge-adaptive path follows the published AMD FidelityFX Super
// Resolution 1.0 EASU algorithm (MIT licence, see THIRD_PARTY_NOTICES.md):
// a 12-tap neighbourhood, a local gradient direction and edge strength from
// luma, a Lanczos-2 approximation stretched along the edge, and de-ringing
// against the four nearest texels.

SAMPLER2D(s_postColor, 0);
SAMPLER2D(s_postBloom, 1);

// xy = 1 / source size, zw = source size.
uniform vec4 u_postTexel;
// x = 1 for the edge-adaptive upscale.
uniform vec4 u_postMode;
// x = brightness, y = 1 / gamma, z = bloom intensity, w = colour-vision on.
uniform vec4 u_postGrade;
uniform vec4 u_postCvd0;
uniform vec4 u_postCvd1;
uniform vec4 u_postCvd2;

vec3 easuFetch(vec2 base, vec2 offset)
{
    return texture2DLod(s_postColor, base + offset * u_postTexel.xy, 0.0).xyz;
}

float easuLuma(vec3 c)
{
    return c.z * 0.5 + (c.x * 0.5 + c.y);
}

void easuSet(inout vec2 dir, inout float len, float w,
             float lA, float lB, float lC, float lD, float lE)
{
    // lA above, lB left, lC centre, lD right, lE below.
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / max(lenX, 1e-5);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, 1e-5);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len2,
             float lob, float clp, vec3 c)
{
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x);
    v *= len2;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

vec3 easu(vec2 uv)
{
    vec2 pp = uv * u_postTexel.zw - vec2(0.5, 0.5);
    vec2 fp = floor(pp);
    pp -= fp;
    vec2 base = (fp + vec2(0.5, 0.5)) * u_postTexel.xy;

    //    b c
    //  e f g h
    //  i j k l
    //    n o
    vec3 b = easuFetch(base, vec2(0.0, -1.0));
    vec3 c = easuFetch(base, vec2(1.0, -1.0));
    vec3 e = easuFetch(base, vec2(-1.0, 0.0));
    vec3 f = easuFetch(base, vec2(0.0, 0.0));
    vec3 g = easuFetch(base, vec2(1.0, 0.0));
    vec3 h = easuFetch(base, vec2(2.0, 0.0));
    vec3 i = easuFetch(base, vec2(-1.0, 1.0));
    vec3 j = easuFetch(base, vec2(0.0, 1.0));
    vec3 k = easuFetch(base, vec2(1.0, 1.0));
    vec3 l = easuFetch(base, vec2(2.0, 1.0));
    vec3 n = easuFetch(base, vec2(0.0, 2.0));
    vec3 o = easuFetch(base, vec2(1.0, 2.0));

    float bL = easuLuma(b);
    float cL = easuLuma(c);
    float eL = easuLuma(e);
    float fL = easuLuma(f);
    float gL = easuLuma(g);
    float hL = easuLuma(h);
    float iL = easuLuma(i);
    float jL = easuLuma(j);
    float kL = easuLuma(k);
    float lL = easuLuma(l);
    float nL = easuLuma(n);
    float oL = easuLuma(o);

    vec2 dir = vec2(0.0, 0.0);
    float len = 0.0;
    easuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    easuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    easuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    easuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

    float dirR = dir.x * dir.x + dir.y * dir.y;
    bool zero = dirR < 1.0 / 32768.0;
    dirR = zero ? 1.0 : inversesqrt(dirR);
    dir.x = zero ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + (0.25 - 0.04 - 0.5) * len;
    float clp = 1.0 / lob;

    vec3 aC = vec3(0.0, 0.0, 0.0);
    float aW = 0.0;
    easuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    easuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    easuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    easuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    easuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    easuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    easuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    easuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    easuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    easuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);

    vec3 minimum = min(min(f, g), min(j, k));
    vec3 maximum = max(max(f, g), max(j, k));
    return clamp(aC / aW, minimum, maximum);
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 colour = u_postMode.x > 0.5 ? easu(uv) : texture2D(s_postColor, uv).xyz;
    if (u_postGrade.z > 0.0)
    {
        colour += texture2D(s_postBloom, uv).xyz * u_postGrade.z;
    }
    colour = clamp(colour, 0.0, 1.0);
    if (u_postGrade.w > 0.5)
    {
        colour = clamp(vec3(dot(u_postCvd0.xyz, colour), dot(u_postCvd1.xyz, colour),
                            dot(u_postCvd2.xyz, colour)), 0.0, 1.0);
    }
    colour = pow(colour, vec3(u_postGrade.y, u_postGrade.y, u_postGrade.y));
    colour = clamp(colour + vec3(u_postGrade.x, u_postGrade.x, u_postGrade.x), 0.0, 1.0);
    gl_FragColor = vec4(colour, 1.0);
}
