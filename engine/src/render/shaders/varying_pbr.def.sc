vec3 v_worldPos     : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normalWS     : TEXCOORD1 = vec3(0.0, 1.0, 0.0);
vec3 v_tangentWS    : TEXCOORD2 = vec3(1.0, 0.0, 0.0);
vec3 v_bitangentWS  : TEXCOORD3 = vec3(0.0, 0.0, 1.0);
vec2 v_texcoord0    : TEXCOORD4 = vec2(0.0, 0.0);
vec4 v_color0       : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec4 v_shadowCoord  : TEXCOORD5 = vec4(0.0, 0.0, 0.0, 1.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec4 a_tangent   : TANGENT;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_color0    : COLOR0;
