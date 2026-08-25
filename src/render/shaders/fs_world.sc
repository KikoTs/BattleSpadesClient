$input v_color0, v_surface, v_world, v_shade, v_shadow, v_placed, v_retail_uv, v_retail_meta

#include <bgfx_shader.sh>

SAMPLER2D(s_retailAo, 0);
uniform vec4 u_retailLight0Direction;
uniform vec4 u_retailLight1Direction;
uniform vec4 u_retailLight0Color;
uniform vec4 u_retailLight1Color;
uniform vec4 u_retailAmbient;
uniform vec4 u_retailViewDirection;

SAMPLER2DSHADOW(s_shadowMap, 1);
// x: 1 when a shadow map is bound. y: texel size. z: depth bias. w: PCF taps.
uniform vec4 u_shadowParams;

// x: self-illumination gain for authored emissive voxels.
// y: gain for light baked from placed blocks (the flare block).
// Both 0 under Legacy: retail had no emissive map voxels, and its flare light
// was baked into the map's own colours rather than added by the shader.
uniform vec4 u_emissiveParams;

SAMPLER2D(s_skylight, 2);
SAMPLER3D(s_emissiveVolume, 3);
// x: gain for light cast by emissive blocks. y: sun bounce fraction.
uniform vec4 u_indirectParams;
// x: 1 when the skylight horizon is bound. y: 1/map edge. z: falloff depth in
// blocks. w: how dark a fully enclosed interior gets.
uniform vec4 u_skylightParams;

uniform vec4 u_cameraPosition;
uniform vec4 u_fogParams;
// x: shading mode (0 passthrough, 1 classic, 2 enhanced)
// y: ambient occlusion strength
// z: specular strength
// w: specular exponent
uniform vec4 u_lightParams;
// xyz: unit vector from a surface toward the key light. w: key intensity.
uniform vec4 u_sunDirection;
// rgb: light colour. w: ambient intensity.
uniform vec4 u_sunColor;
// Hemispheric ambient, derived per map from its own sky gradient. Both are
// unit-luminance chroma, so a channel may exceed 1.0; the level lives in
// u_sunColor.w. That split is what lets a night map be raised to a playable
// brightness without losing the colour that makes it read as night.
uniform vec4 u_skyAmbient;
uniform vec4 u_groundAmbient;
// rgb: the colour the sky paints just above the horizon.
uniform vec4 u_fogHorizon;
// x: fog density. y: fog start fraction. z: tonemap white point.
uniform vec4 u_fogCurve;
// x: client-only model opacity. y/z: model-only albedo gain/contrast.
// Terrain and viewmodels leave y/z at zero and retain their authored albedo.
uniform vec4 u_modelOpacity;
// xyz: canonical-space position, w: radius. Unused slots have radius zero.
uniform vec4 u_pointLightPositionRadius[8];
// rgb: linear light colour, w: peak intensity.
uniform vec4 u_pointLightColorIntensity[8];

// xyz: the direction the hemispheric ambient treats as "up", in the SAME space
// as the interpolated normal. Every world pass passes (0,0,-1), because
// canonical map space is z-down. The viewmodel is drawn with an identity view
// matrix, so its normals are VIEW-space and it passes the camera-rotated world
// up instead -- without which the arm's top and its underside receive identical
// ambient and the whole model reads as flat.
uniform vec4 u_upAxis;

vec3 retail_calculate_lighting(vec3 albedo, vec3 light_direction,
                               vec3 half_vector, vec3 light_color,
                               vec3 normal, float directional_influence)
{
    float diffuse = max(0.3, dot(normal, light_direction));
    vec3 result = albedo * (diffuse * light_color);
    float specular = max(0.0, dot(normal, half_vector));
    result += pow(specular, 10.0) * 0.065 * light_color;
    return mix(result, albedo, directional_influence);
}

void main()
{
    vec3 albedo = v_color0.rgb;
    if (u_modelOpacity.y > 0.0)
    {
        albedo = clamp((albedo - vec3(0.5, 0.5, 0.5)) * u_modelOpacity.z +
                           vec3(0.5, 0.5, 0.5),
                       vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0)) *
                 u_modelOpacity.y;
    }
    vec3 normal = normalize(v_surface.xyz);
    float ao = v_surface.w;

    vec3 lit;
    if (u_lightParams.x < 0.5)
    {
        lit = albedo;
    }
    else if (u_lightParams.x < 1.5)
    {
        if (v_retail_meta.w > 0.5)
        {
            // Terrain normals are canonical x/y/z-down. Retail's OpenGL VXL
            // shader consumes x/y-up/z, hence this exact basis conversion.
            vec3 retail_normal = normalize(vec3(normal.x, -normal.z, normal.y));
            vec3 light0 = normalize(u_retailLight0Direction.xyz);
            vec3 light1 = normalize(u_retailLight1Direction.xyz);
            vec3 eye = normalize(u_retailViewDirection.xyz);
            vec3 dir0 = retail_calculate_lighting(
                albedo, light0, normalize(light0 + eye),
                u_retailLight0Color.rgb, retail_normal, v_retail_meta.z);
            vec3 dir1 = retail_calculate_lighting(
                albedo, light1, normalize(light1 + eye),
                u_retailLight1Color.rgb, retail_normal, v_retail_meta.z);
            vec3 ambient = u_retailAmbient.rgb * u_retailAmbient.a;
            vec3 combined = clamp(ambient + dir0 + dir1,
                                  vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
            vec4 ao_sample = texture2D(s_retailAo, v_retail_uv.xy);
            vec4 edge_sample = texture2D(s_retailAo, v_retail_uv.zw);
            lit = combined * (ao_sample.r + 0.35);
            lit += (1.0 - edge_sample.g) * albedo * 0.3;
            lit *= texture2D(s_retailAo, v_retail_meta.xy).b;
        }
        else
        {
            // Detached KV6/effect meshes are not VXL atlas records. Preserve
            // their compatibility face table under Legacy.
            lit = albedo * v_shade;
        }
    }
    else
    {
        // A key light, an opposed fill and a hemispheric ambient, following
        // the retail map_frag structure (two lights plus ambient) but
        // evaluated per pixel against a real normal instead of a constant.
        vec3 light_dir = normalize(u_sunDirection.xyz);
        // Retail's map_frag used max(0.3, dot(N, L)) rather than a clamp at
        // zero, so no face was ever fully unlit by the key. That floor is most
        // of why retail's blocks read as solid colour with gentle facing
        // variation instead of half the world falling into darkness, and
        // matching it is what makes the shading feel like the real game.
        // Shadows still multiply the key, so occluded ground genuinely darkens.
        float facing = max(dot(normal, light_dir), 0.28);
        float key = facing * u_sunDirection.w;

        float occlusion = mix(1.0, ao, clamp(u_lightParams.y, 0.0, 1.0));

        // Cast shadows. The key light is occluded where the sun's depth map says
        // something else was closer; ambient is deliberately left untouched, so
        // shadowed surfaces stay lit by the sky rather than going black.
        float sunlight = 1.0;
        if (u_shadowParams.x > 0.5)
        {
            vec3 coord = v_shadow.xyz / max(v_shadow.w, 1e-6);
            // bgfx hands us clip space in the backend's own depth convention.
        #if BGFX_SHADER_LANGUAGE_GLSL
            coord = coord * 0.5 + 0.5;
        #else
            coord.xy = coord.xy * vec2(0.5, -0.5) + 0.5;
        #endif
            // Outside the cascade there is no information, so assume lit rather
            // than shadowing the whole world beyond the map's edge.
            if (coord.x > 0.0 && coord.x < 1.0 && coord.y > 0.0 && coord.y < 1.0 &&
                coord.z > 0.0 && coord.z < 1.0)
            {
                // Slope-scaled bias: a surface nearly edge-on to the sun needs
                // far more offset than one facing it, and a constant bias large
                // enough for the former visibly detaches the latter's contact.
                float slope = clamp(1.0 - dot(normal, normalize(u_sunDirection.xyz)), 0.0, 1.0);
                float bias = u_shadowParams.z * (1.0 + slope * 3.0);
                float depth = coord.z - bias;
                float texel = u_shadowParams.y;

                if (u_shadowParams.w > 0.0)
                {
                    // Eight taps spiralled over a fixed disc, plus the centre.
                    //
                    // The penumbra a filter can produce is bounded by how far its
                    // widest tap reaches, so the 3x3 box this replaces was only
                    // one texel across and read almost exactly like no filter --
                    // which is why bridge shadows looked stamped on. Two things
                    // fix that:
                    //
                    //  - Width. The disc radius comes from the tier, in texels.
                    //  - Distribution. Advancing by the golden angle and taking
                    //    the radius as sqrt of the tap fraction places the taps
                    //    uniformly by AREA. A plain ring puts all eight at the
                    //    same distance, which just moves the hard edge outward
                    //    instead of softening it.
                    //
                    // The pattern is deliberately NOT rotated per pixel. Rotation
                    // is the usual advice and does hide the banding a sparse fixed
                    // kernel produces, but it pays for that with per-pixel noise,
                    // and this renderer has no temporal filter to resolve noise
                    // into anything. That trade was measured for the SKYLIGHT
                    // filter below, where a rotated kernel visibly speckled
                    // GreatWall's plaza; here it is an argument by analogy, not a
                    // measurement -- the two differ in that this averages a dense
                    // binary depth test rather than a smooth height field, so a
                    // fixed kernel may band where the skylight one does not. If
                    // penumbra banding ever shows up on a rig capture, rotating
                    // this kernel is the first thing to try.
                    float radius = texel * u_shadowParams.w;
                    float total = shadow2D(s_shadowMap, vec3(coord.xy, depth));
                    for (int tap = 0; tap < 8; ++tap)
                    {
                        float fraction = (float(tap) + 0.5) * 0.125;
                        float spiral = float(tap) * 2.3999632;
                        vec2 offset = vec2(cos(spiral), sin(spiral)) *
                                      (radius * sqrt(fraction));
                        total += shadow2D(s_shadowMap, vec3(coord.xy + offset, depth));
                    }
                    sunlight = total / 9.0;
                }
                else
                {
                    sunlight = shadow2D(s_shadowMap, vec3(coord.xy, depth));
                }
            }
        }

        // Canonical map space is z-down, so "up" is -z. Skylight from above
        // and a dimmer bounce from below give every face a distinct tone even
        // where no direct light reaches, which is what stops interiors from
        // collapsing into one flat grey.
        // Bit-identical to the old -normal.z for every world pass: u_upAxis is
        // (0,0,-1) there, and 0*finite is exactly 0 while x+0 is exact in IEEE.
        float up01 = clamp(dot(normal, u_upAxis.xyz) * 0.5 + 0.5, 0.0, 1.0);
        vec3 ambient = mix(u_groundAmbient.rgb, u_skyAmbient.rgb, up01) * u_sunColor.w;

        // Skylight occlusion. Ambient here IS the sky, so a surface with no view
        // of the sky must not receive it: without this a bunker interior is lit
        // exactly as brightly as its own roof. The horizon texture stores the
        // topmost solid voxel per column, so anything below it is under cover.
        //
        // Sampled one step ALONG THE NORMAL rather than at the fragment itself.
        // A wall's own column has its horizon at the wall top, which would
        // wrongly darken the outside of every wall; stepping into the air the
        // face looks at asks the right question -- "can the sky see this?"
        // With the probe disabled, w is a caller-supplied constant scale rather
        // than a floor. That is how the viewmodel dims: it cannot look up the
        // horizon texture because its position is in view space, so the caller
        // samples the horizon at the camera and passes the answer here.
        float skylight = u_skylightParams.w;
        if (u_skylightParams.x > 0.5)
        {
            vec2 base = v_world.xy + normal.xy * 0.8;
            float falloff = max(u_skylightParams.z, 0.001);

            // Averaged over a disc of columns, not taken from one.
            //
            // The horizon texture is one value per column, so a single tap makes
            // cover a per-column property: the lateral transition is whatever the
            // bilinear filter gives across ONE block. Walking out from under a
            // bridge deck then switches from shaded to lit almost instantly, and
            // that hard line under bridges and walkways reads as a hard shadow --
            // even though the shadow map is not involved at all.
            //
            // Physically the sky is an enormous area light, so its occlusion
            // should be the SOFTEST term in the frame, not the sharpest. Sampling
            // several columns and averaging their occlusion gives a penumbra as
            // wide as the disc. The radius is tied to the vertical falloff depth
            // rather than being its own uniform: both describe how sharply cover
            // cuts the sky off, so one control for both keeps them consistent.
            // A FIXED hexagon plus the centre -- deliberately not rotated per
            // pixel the way the shadow filter is.
            //
            // What is being averaged here is a continuous function of world
            // position: the horizon texture is bilinear, so depth and the
            // smoothstep over it both vary smoothly. Averaging a smooth function
            // over a fixed kernel stays smooth. Randomising the kernel per pixel
            // instead makes neighbouring pixels average DIFFERENT sample sets,
            // which converts a smooth gradient into stochastic noise -- and with
            // no temporal filter in this renderer there is nothing to resolve
            // that noise, so it would simply look dirty. Measured: a rotated
            // 6-tap version of this produced obvious per-pixel speckle on
            // GreatWall's plaza where the fixed kernel is clean.
            float radius = falloff * 0.45;
            vec2 taps[6];
            taps[0] = vec2( 1.0,  0.0);
            taps[1] = vec2( 0.5,  0.866);
            taps[2] = vec2(-0.5,  0.866);
            taps[3] = vec2(-1.0,  0.0);
            taps[4] = vec2(-0.5, -0.866);
            taps[5] = vec2( 0.5, -0.866);

            // z grows downward, so positive depth means below the cover.
            float centre_depth = v_world.z - (texture2D(s_skylight,
                base * u_skylightParams.y).r * 255.0);
            float centre_enclosed = clamp(centre_depth / falloff, 0.0, 1.0);
            // Smoothstep so a doorway reads as a gradient rather than a hard
            // line across the floor.
            float total = centre_enclosed * centre_enclosed *
                          (3.0 - 2.0 * centre_enclosed);
            for (int tap = 0; tap < 6; ++tap)
            {
                vec2 probe = (base + taps[tap] * radius) * u_skylightParams.y;
                float horizon = texture2D(s_skylight, probe).r * 255.0;
                float depth = v_world.z - horizon;
                float enclosed = clamp(depth / falloff, 0.0, 1.0);
                total += enclosed * enclosed * (3.0 - 2.0 * enclosed);
            }
            skylight = mix(1.0, u_skylightParams.w, total / 7.0);
        }
        ambient *= skylight;

        // The key is gated by BOTH the shadow map and the skylight horizon. The
        // cascade only covers the near field, so beyond it an interior would
        // otherwise still receive full direct sun; the horizon has no such
        // range limit and correctly reports that the sky cannot see in.
        float direct = key * sunlight * skylight;
        lit = albedo * (ambient + u_sunColor.rgb * direct) * occlusion;

        // A narrow specular lobe catches the sun on block edges. Retail used
        // pow(spec, 10.0) * 0.065. Shadowed surfaces must not glint.
        vec3 view_dir = normalize(u_cameraPosition.xyz - v_world);
        vec3 half_vec = normalize(light_dir + view_dir);
        float specular = pow(max(dot(normal, half_vec), 0.0), u_lightParams.w);
        lit += u_sunColor.rgb * specular * u_lightParams.z * occlusion * sunlight * skylight;

        // Extended Reinhard roll-off. Without it a sunlit floor clips to flat
        // white and loses its ambient occlusion; this keeps midtones nearly
        // linear and compresses only the highlights, which is the cheap way
        // to get HDR behaviour before a real float framebuffer exists.
        // Self-illumination. Added AFTER every occlusion term and before the
        // tonemap, because a light source is not dimmed by shadow, by ambient
        // occlusion, or by being indoors -- that is what makes it a source. The
        // mesher packs the strength into the vertex colour's alpha byte.
        lit += albedo * v_color0.a * u_emissiveParams.x;

        // Light from placed blocks. Diffuse, so it tints by both the lamp's
        // colour and the receiving surface's albedo, and like emission it is
        // added after the occlusion terms: a lamp lights the inside of a bunker,
        // which is the entire point of carrying one.
        lit += albedo * v_placed * u_emissiveParams.y;

        // Light CAST BY emissive blocks. This is what makes a neon street feel
        // inhabited rather than decorated: a green sign bleeds green onto the
        // wall opposite, and a lantern pools light on the road below. Probed one
        // step along the normal, the same trick the skylight horizon uses, so a
        // wall's outward face samples the air it faces rather than its own cell.
        if (u_indirectParams.x > 0.0)
        {
            // The volume is NOT cubic: x and y span 512 blocks, z spans 240, so
            // the axes need separate scales. Using one scale for all three
            // samples the wrong slice entirely and the cast light vanishes.
            vec3 offset_world = v_world + normal * 2.0;
            vec3 probe = vec3(offset_world.x * u_indirectParams.z,
                              offset_world.y * u_indirectParams.z,
                              offset_world.z * u_indirectParams.w);
            vec3 cast_light = texture3D(s_emissiveVolume, probe).rgb;
            lit += albedo * cast_light * u_indirectParams.x;
        }

        // A crude single bounce of the key light, NOT gated by the shadow map.
        // Direct sun in a desert is so much stronger than the sky that shadowed
        // ground read as dead black; in reality it is filled by light bouncing
        // off the lit surfaces around it. Cheap, and it is what stops a shadow
        // from looking like a hole.
        lit += albedo * u_sunColor.rgb * u_sunDirection.w * u_indirectParams.y *
               occlusion * skylight;

        // Short-lived explosion and effect lights. These are deliberately
        // independent of skylight and the sun shadow map: a blast inside a
        // bunker must illuminate it. A wrapped diffuse term keeps the hard
        // voxel silhouettes readable while still giving each face direction.
        for (int point_index = 0; point_index < 8; ++point_index)
        {
            vec4 position_radius = u_pointLightPositionRadius[point_index];
            vec4 color_intensity = u_pointLightColorIntensity[point_index];
            vec3 to_light = position_radius.xyz - v_world;
            float distance_to_light = length(to_light);
            float radius = max(position_radius.w, 0.001);
            float range01 = clamp(1.0 - distance_to_light / radius, 0.0, 1.0);
            float attenuation = range01 * range01;
            vec3 direction = to_light / max(distance_to_light, 0.001);
            float facing = max(dot(normal, direction), 0.0);
            float wrapped_diffuse = 0.20 + facing * 0.80;
            lit += albedo * color_intensity.rgb * color_intensity.w *
                   attenuation * wrapped_diffuse * occlusion;
        }

        float white = u_fogCurve.z;
        lit = lit * (1.0 + lit / (white * white)) / (1.0 + lit);

        // Break up sub-byte lighting bands without changing the recovered
        // Legacy path. The hash is anchored in canonical world space so it
        // does not crawl across surfaces as the camera moves, and its
        // half-of-one-8-bit-step amplitude disappears under the normal fog
        // mix below rather than outlining chunk boundaries at draw distance.
        vec3 grain_cell = floor(v_world * 16.0);
        float grain = fract(sin(dot(grain_cell,
            vec3(12.9898, 78.233, 37.719))) * 43758.5453) - 0.5;
        lit = clamp(lit + grain * (0.5 / 255.0),
                    vec3(0.0, 0.0, 0.0), vec3(64.0, 64.0, 64.0));
    }

    // Radial fog from the eye, evaluated per pixel. Per-vertex fog bands
    // visibly across a 16-block chunk quad at grazing angles.
    vec3  offset   = v_world - u_cameraPosition.xyz;
    float distance = length(offset);
    float d        = clamp(distance / u_fogParams.w, 0.0, 1.0);

    float fog;
    vec3 fog_rgb;
    if (u_lightParams.x < 1.5)
    {
        // The preserved client configures GL_LINEAR fog with START at half
        // draw distance and END at draw distance. Legacy must retain that
        // late, straight fade and the server/map fog colour verbatim; feeding
        // it the enhanced exponential curve is what washed nearby Ancient
        // Egypt geometry orange.
        float start = clamp(u_fogCurve.y, 0.0, 0.999) * u_fogParams.w;
        float visibility = clamp(
            (u_fogParams.w - distance) / max(u_fogParams.w - start, 0.001),
            0.0,
            1.0);
        fog = 1.0 - visibility;
        fog_rgb = u_fogParams.rgb;
    }
    else
    {
        // Enhanced tiers use an exponential-squared atmosphere. The hard
        // clamp at d >= 1 is required because chunks past fog distance are
        // culled entirely and must already be fully hidden.
        float density = u_fogCurve.x;
        fog = 1.0 - exp2(-density * density * d * d * 2.885390);
        fog = (d >= 1.0) ? 1.0 : clamp(fog, 0.0, 1.0);

        // Fade toward what the enhanced sky is painting in that direction.
        vec3 view_dir = distance > 0.0001
                            ? (offset / distance)
                            : vec3(0.0, 0.0, -1.0);
        float sky01 = clamp(-view_dir.z * 0.5 + 0.5, 0.0, 1.0);
        fog_rgb = mix(u_fogHorizon.rgb, u_fogParams.rgb, sky01 * sky01);
    }

    // Alpha is a literal 1.0, not v_color0.a: that channel now carries the
    // mesher's self-illumination strength, and terrain is opaque regardless.
    gl_FragColor = vec4(mix(lit, fog_rgb, fog), u_modelOpacity.x);
}
