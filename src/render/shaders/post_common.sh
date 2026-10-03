// Shared helpers for the world post-processing passes.
//
// u_postDepthMode: x = 1 when clip depth is [-1, 1] (OpenGL), y = +1 when
// texture v grows with clip y (render-target origin bottom-left), -1 otherwise.
// u_postProj: x = tan(fov_x / 2), y = tan(fov_y / 2), z = near, w = far.

float postLinearDepth(float depth, vec4 depthMode, vec4 proj)
{
    float n = proj.z;
    float f = proj.w;
    if (depthMode.x > 0.5)
    {
        float z = depth * 2.0 - 1.0;
        return 2.0 * n * f / (f + n - z * (f - n));
    }
    return n * f / (f - depth * (f - n));
}

vec2 postNdcXy(vec2 uv, vec4 depthMode)
{
    return vec2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) * depthMode.y);
}

vec3 postViewPosition(vec2 uv, float linearDepth, vec4 depthMode, vec4 proj)
{
    vec2 ndc = postNdcXy(uv, depthMode);
    return vec3(ndc.x * proj.x * linearDepth, ndc.y * proj.y * linearDepth, linearDepth);
}

float postLuma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}
