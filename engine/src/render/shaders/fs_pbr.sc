$input v_worldPos, v_normalWS, v_tangentWS, v_bitangentWS, v_texcoord0, v_color0, v_shadowCoord

#include <bgfx_shader.sh>

SAMPLER2D(s_albedo,    0);
SAMPLER2D(s_normalMap, 1);
SAMPLER2D(s_mrMap,     2);
SAMPLER2DSHADOW(s_shadowMap, 3);

uniform vec4 u_baseColor;
uniform vec4 u_pbr0;         // x=metallic, y=roughness
uniform vec4 u_emissive;
uniform vec4 u_params;       // x=lit, y=hasAlbedo, z=hasNormal, w=hasMR
uniform vec4 u_camPos;
uniform vec4 u_ambient;
uniform vec4 u_lightCount;   // x = number of lights (float)
uniform vec4 u_shadowConfig; // x = shadow active (1.0 or 0.0)
uniform vec4 u_lightData0[8];
uniform vec4 u_lightData1[8];
uniform vec4 u_lightData2[8];
uniform vec4 u_lightData3[8];

#define PI 3.14159265359

float D_GGX(float NdotH, float alpha2) {
    float d = (NdotH * alpha2 - NdotH) * NdotH + 1.0;
    return alpha2 / (PI * d * d + 1e-5);
}

float G_SchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k + 1e-5);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    float p = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (vec3_splat(1.0) - F0) * p;
}

vec3 evalLight(vec3 N, vec3 V, vec3 fragPos,
               vec3 albedo, float metallic, float roughness, vec3 F0,
               vec4 d0, vec4 d1, vec4 d2, vec4 d3)
{
    vec3  lcolor = d1.rgb * d1.a;
    float lt     = d0.w;   // 0=dir, 1=point, 2=spot

    vec3  L;
    float attenuation = 1.0;

    if (lt < 0.5) {
        // Directional: d0.xyz is the light's forward (shining) direction.
        // L must point FROM surface TOWARD the light, so negate.
        L = -normalize(d0.xyz);
    } else {
        // Point or Spot: d0.xyz = position
        vec3  toLight = d0.xyz - fragPos;
        float dist    = length(toLight);
        L = toLight / (dist + 1e-5);

        float range  = max(d2.w, 0.001);
        float ratio  = dist / range;
        float atten  = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
        attenuation  = atten * atten / (dist * dist + 1.0);

        if (lt > 1.5) {
            // Spot angular attenuation
            float cosAngle = dot(-L, normalize(d2.xyz));
            float spotAtten = clamp((cosAngle - d3.y) / max(d3.x - d3.y, 0.001), 0.0, 1.0);
            attenuation *= spotAtten * spotAtten;
        }
    }

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3_splat(0.0);

    vec3  H     = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float alpha2  = roughness * roughness * roughness * roughness;
    float D = D_GGX(NdotH, alpha2);
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float G = G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
    vec3  F = F_Schlick(VdotH, F0);

    vec3 specular = D * G * F / (4.0 * NdotL * NdotV + 1e-5);
    vec3 kd = (vec3_splat(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / PI;

    return (diffuse + specular) * NdotL * lcolor * attenuation;
}

void main()
{
    // Albedo
    float hasAlbedo = u_params.y;
    vec4 albedoTex  = mix(vec4_splat(1.0), texture2D(s_albedo, v_texcoord0), hasAlbedo);
    vec4 baseCol    = u_baseColor * v_color0 * albedoTex;
    vec3 albedo     = baseCol.rgb;

    // Metallic/roughness
    float metallic  = clamp(u_pbr0.x, 0.0,  1.0);
    float roughness = clamp(u_pbr0.y, 0.04, 1.0);
    if (u_params.w > 0.5) {
        vec2 mr = texture2D(s_mrMap, v_texcoord0).gb;
        roughness = clamp(roughness * mr.x, 0.04, 1.0);
        metallic  = clamp(metallic  * mr.y, 0.0,  1.0);
    }

    // Normal
    vec3 N = normalize(v_normalWS);
    if (u_params.z > 0.5) {
        vec3 nm = texture2D(s_normalMap, v_texcoord0).xyz * 2.0 - 1.0;
        vec3 T  = normalize(v_tangentWS);
        vec3 B  = normalize(v_bitangentWS);
        N = normalize(T * nm.x + B * nm.y + N * nm.z);
    }

    if (u_params.x < 0.5) {
        gl_FragColor = vec4(albedo + u_emissive.rgb, baseCol.a);
        return;
    }

    // Shadow — 3×3 PCF for soft, anti-aliased edges
    float shadow = 1.0;
    if (u_shadowConfig.x > 0.5) {
        float ts = u_shadowConfig.y; // texel size (1.0 / shadow_map_resolution)
        shadow = 0.0;
        for (int xi = -1; xi <= 1; xi++) {
            for (int yi = -1; yi <= 1; yi++) {
                vec4 sc = v_shadowCoord;
                sc.xy += vec2(float(xi), float(yi)) * ts * sc.w;
                shadow += shadow2DProj(s_shadowMap, sc);
            }
        }
        shadow /= 9.0;
    }

    vec3 V  = normalize(u_camPos.xyz - v_worldPos);
    vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

    int count = int(u_lightCount.x);
    vec3 Lo = vec3_splat(0.0);

    // Unrolled light loop — avoids dynamic indexing issues in older GLSL
    if (0 < count) {
        vec3 c = evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                           u_lightData0[0], u_lightData1[0], u_lightData2[0], u_lightData3[0]);
        if (u_lightData0[0].w < 0.5) c *= shadow;
        Lo += c;
    }
    if (1 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[1], u_lightData1[1], u_lightData2[1], u_lightData3[1]);
    }
    if (2 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[2], u_lightData1[2], u_lightData2[2], u_lightData3[2]);
    }
    if (3 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[3], u_lightData1[3], u_lightData2[3], u_lightData3[3]);
    }
    if (4 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[4], u_lightData1[4], u_lightData2[4], u_lightData3[4]);
    }
    if (5 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[5], u_lightData1[5], u_lightData2[5], u_lightData3[5]);
    }
    if (6 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[6], u_lightData1[6], u_lightData2[6], u_lightData3[6]);
    }
    if (7 < count) {
        Lo += evalLight(N, V, v_worldPos, albedo, metallic, roughness, F0,
                        u_lightData0[7], u_lightData1[7], u_lightData2[7], u_lightData3[7]);
    }

    vec3 ambientColor = u_ambient.rgb * albedo * (1.0 - metallic * 0.7);
    vec3 finalColor   = Lo + ambientColor + u_emissive.rgb;

    // ACES tone mapping approximation
    finalColor = (finalColor * (2.51 * finalColor + 0.03)) / (finalColor * (2.43 * finalColor + 0.59) + 0.14);
    finalColor = clamp(finalColor, 0.0, 1.0);

    // Gamma correction
    finalColor = pow(finalColor, vec3_splat(1.0 / 2.2));

    gl_FragColor = vec4(finalColor, baseCol.a);
}
