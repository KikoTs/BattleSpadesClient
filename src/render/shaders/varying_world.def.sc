vec3 a_position : POSITION;
vec4 a_color0   : COLOR0;
vec4 a_color1   : COLOR1;
vec4 a_color2   : COLOR2;
vec4 a_texcoord0 : TEXCOORD0;

vec4 v_color0  : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec4 v_surface : TEXCOORD0 = vec4(0.0, 0.0, -1.0, 1.0);
vec3 v_world   : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
float v_shade  : TEXCOORD2 = 1.0;
vec4 v_shadow  : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 1.0);
vec3 v_placed  : TEXCOORD4 = vec3(0.0, 0.0, 0.0);
vec4 v_retail_uv   : TEXCOORD5 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_retail_meta : TEXCOORD6 = vec4(0.0, 0.0, 0.0, 0.0);
