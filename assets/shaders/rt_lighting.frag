#version 120

uniform sampler2D u_albedo;
uniform sampler2D u_material;
uniform sampler2D u_emissive;

uniform vec2  u_texelSize;
uniform float u_time;
uniform vec2  u_sunDirUV;
uniform float u_sunSlope;
uniform float u_sunIntensity;
uniform vec3  u_sunColor;
uniform vec3  u_ambientColor;

uniform vec2  u_worldTopLeft;
uniform vec2  u_worldSize;
uniform vec2  u_mapWorldMax;
uniform float u_cellPx;

uniform int   u_rtQuality;      // 0=Off, 1=Med(8), 2=High(16), 3=Ultra(24)
uniform float u_bloomStrength;
uniform int   u_aoEnabled;
uniform int   u_waterReflect;
uniform int   u_postFxMode;     // 0=Clean, 1=Vignette, 2=CRT

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec3 acesFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec2 worldPos = u_worldTopLeft + vec2(uv.x, 1.0 - uv.y) * u_worldSize;

    // Mask void outside the 192x144 world so cliffs never cast shadows off map edges
    if (worldPos.x < 0.0 || worldPos.y < 0.0 ||
        worldPos.x > u_mapWorldMax.x || worldPos.y > u_mapWorldMax.y) {
        vec3 voidCol = vec3(0.036, 0.045, 0.064);
        if (u_postFxMode >= 1) {
            vec2 centered = uv * 2.0 - 1.0;
            float vig = 1.0 - dot(centered, centered) * 0.14;
            voidCol *= clamp(vig, 0.55, 1.0);
        }
        gl_FragColor = vec4(voidCol, 1.0);
        return;
    }

    vec4 albSample = texture2D(u_albedo, uv);
    vec3 albedo = albSample.rgb;
    vec4 mat = texture2D(u_material, uv);
    vec3 directEmissive = texture2D(u_emissive, uv).rgb;

    // Decode 2.5D surface normal, specular/water flag, and normalized height
    vec2 nXY = (mat.rg - vec2(0.50196)) * 2.4;
    bool isWater = (mat.b > 0.86);
    float metallic = isWater ? 0.85 : clamp(mat.b * 1.25, 0.0, 1.0);
    float hCenter = mat.a;
    float zoomScale = clamp(u_cellPx / 28.0, 0.18, 1.0);

    // World-space anchored water waves that smoothly calm down at strategic map zoom
    if (isWater) {
        float waveLod = clamp((u_cellPx - 7.0) / 12.0, 0.0, 1.0);
        float w1 = sin(worldPos.x * 0.098 + u_time * 2.6 + worldPos.y * 0.056);
        float w2 = cos(worldPos.y * 0.118 - u_time * 2.0 - worldPos.x * 0.068);
        nXY += vec2(w1, w2) * (0.14 * waveLod);
        metallic = mix(0.32, 0.85, waveLod);
    }
    vec3 N = normalize(vec3(nXY, 1.0));

    // Optional RT planar water reflection (marching North = +uv.y in FBO texture space)
    if (isWater && u_waterReflect != 0) {
        vec2 reflOffset = vec2(nXY.x * 6.0 * u_texelSize.x, 18.0 * u_texelSize.y) * zoomScale;
        vec3 reflCol = vec3(0.0);
        vec3 reflEmis = vec3(0.0);
        float reflWeight = 0.0;
        for (int s = 1; s <= 6; ++s) {
            float t = float(s) / 6.0;
            vec2 ruv = clamp(uv + reflOffset * t + vec2(0.0, float(s) * 4.5 * u_texelSize.y * zoomScale), vec2(0.002), vec2(0.998));
            vec2 rWorld = u_worldTopLeft + vec2(ruv.x, 1.0 - ruv.y) * u_worldSize;
            if (rWorld.y < 0.0) break;
            vec4 rMat = texture2D(u_material, ruv);
            if (rMat.a > 0.24) {
                reflCol = texture2D(u_albedo, ruv).rgb;
                reflEmis = texture2D(u_emissive, ruv).rgb;
                reflWeight = clamp((rMat.a - 0.20) * 1.8, 0.0, 0.52) * (1.0 - t * 0.45);
                break;
            }
        }
        albedo = mix(albedo, reflCol * vec3(0.65, 0.88, 1.05), reflWeight);
        directEmissive += reflEmis * (reflWeight * 0.75);
    }

    // 1. Heightfield Screen-Space Ambient Occlusion (zoom-aware contact creases)
    float ao = 1.0;
    if (u_aoEnabled != 0) {
        float occ = 0.0;
        float rNear = max(0.85, 2.5 * zoomScale);
        float rMid  = max(1.60, 6.0 * zoomScale);
        for (int k = 0; k < 8; ++k) {
            float ang = float(k) * 0.78539816;
            vec2 dir = vec2(cos(ang), sin(ang));
            float hNear = texture2D(u_material, clamp(uv + dir * u_texelSize * rNear, vec2(0.001), vec2(0.999))).a;
            float hMid  = texture2D(u_material, clamp(uv + dir * u_texelSize * rMid,  vec2(0.001), vec2(0.999))).a;
            occ += clamp((hNear - hCenter) * 3.2, 0.0, 0.35);
            occ += clamp((hMid  - hCenter) * 1.8, 0.0, 0.25);
        }
        float aoWeight = mix(0.08, 0.14, zoomScale);
        ao = clamp(1.0 - occ * aoWeight, 0.42, 1.0);
    }

    // 2. World-Consistent Ray-Marched 2.5D Heightfield Sun/Moon Shadows
    float sunShadow = 1.0;
    int shadowSteps = (u_rtQuality >= 2) ? 20 : 12;
    float baseStepPx = (u_rtQuality >= 2) ? 2.4 : 3.4;
    float stepPx = clamp(u_cellPx * 0.085, 0.55, baseStepPx);
    float slopePerTile = u_sunSlope * 32.0;
    float blocker = 0.0;
    for (int s = 1; s <= 20; ++s) {
        if (s > shadowSteps) break;
        float distPx = float(s) * stepPx;
        vec2 suv = uv + u_sunDirUV * (distPx * u_texelSize);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
        vec2 sWorld = u_worldTopLeft + vec2(suv.x, 1.0 - suv.y) * u_worldSize;
        if (sWorld.x < 0.0 || sWorld.y < 0.0 ||
            sWorld.x > u_mapWorldMax.x || sWorld.y > u_mapWorldMax.y) break;
        float sh = texture2D(u_material, suv).a;
        float distTiles = distPx / max(u_cellPx, 1.0);
        float rayH = hCenter + distTiles * slopePerTile;
        float diff = sh - rayH;
        if (diff > 0.012) {
            float penumbra = clamp(diff * 14.0 / (1.0 + float(s) * 0.18), 0.0, 1.0);
            blocker = max(blocker, penumbra);
        }
    }
    float shadowStrength = mix(0.60, 0.84, clamp((u_cellPx - 4.0) / 14.0, 0.0, 1.0));
    sunShadow = 1.0 - blocker * shadowStrength;

    // 3D directional lighting from Sun/Moon
    vec3 L = normalize(vec3(u_sunDirUV.x, -u_sunDirUV.y, max(0.32, u_sunSlope * 28.0)));
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 V = vec3(0.0, 0.22, 0.975);
    vec3 H = normalize(L + V);
    float spec = pow(clamp(dot(N, H), 0.0, 1.0), isWater ? 42.0 : 18.0) * metallic;

    vec3 directSun = u_sunColor * (NdotL * sunShadow * (0.45 + 0.95 * u_sunIntensity));
    vec3 specContrib = u_sunColor * (spec * sunShadow * (0.30 + 0.75 * u_sunIntensity));
    vec3 ambient = u_ambientColor * ao;

    // 3. Stratified Ray-Traced Global Illumination (RT GI) & Occluded Point-Light Shadows
    vec3 rtGI = vec3(0.0);
    if (u_rtQuality > 0) {
        int rayCount = (u_rtQuality == 1) ? 8 : ((u_rtQuality == 2) ? 16 : 24);
        float jitter = interleavedGradientNoise(gl_FragCoord.xy);
        float invRays = 1.0 / float(rayCount);
        float giScale = clamp(u_cellPx / 28.0, 0.24, 1.0);

        for (int r = 0; r < 24; ++r) {
            if (r >= rayCount) break;
            float angle = (float(r) + jitter) * invRays * 6.2831853;
            vec2 rayDirUV = vec2(cos(angle), sin(angle));
            vec2 rayDirWorld = vec2(rayDirUV.x, -rayDirUV.y);

            float maxHorizon = -0.05;
            vec3 rayAccum = vec3(0.0);

            for (int m = 1; m <= 6; ++m) {
                float dBase = 1.6 * float(m * m) + 1.0;
                float dPx = dBase * giScale;
                vec2 muv = uv + rayDirUV * (dPx * u_texelSize);
                if (muv.x < 0.001 || muv.x > 0.999 || muv.y < 0.001 || muv.y > 0.999) break;

                float mh = texture2D(u_material, muv).a;
                float slope = (mh - hCenter) / max(2.0, dBase * 0.045);

                vec3 em = texture2D(u_emissive, muv).rgb;
                float emMag = max(em.r, max(em.g, em.b));
                if (emMag > 0.02) {
                    float emitterTopSlope = (mh + 0.06 - hCenter) / max(2.0, dBase * 0.045);
                    float vis = smoothstep(maxHorizon - 0.10, maxHorizon + 0.06, emitterTopSlope);
                    float atten = 1.0 / (1.0 + 0.032 * dBase + 0.0016 * dBase * dBase);
                    vec3 lDir = normalize(vec3(rayDirWorld * 0.85, 0.55));
                    float nResp = clamp(dot(N, lDir), 0.25, 1.0);
                    rayAccum += em * (vis * atten * nResp * 1.55);
                } else if (u_rtQuality >= 3 && m <= 3 && mh > hCenter + 0.08) {
                    vec3 bounceAlb = texture2D(u_albedo, muv).rgb;
                    rayAccum += bounceAlb * u_sunColor * (u_sunIntensity * 0.08 / (1.0 + 0.08 * dBase));
                }

                maxHorizon = max(maxHorizon, slope);
            }
            rtGI += rayAccum;
        }
        rtGI *= invRays * 2.8;
    } else if (u_bloomStrength > 0.01) {
        rtGI = directEmissive * 0.35;
    }

    // 4. Multi-scale Emissive Bloom Halo
    vec3 bloom = vec3(0.0);
    if (u_bloomStrength > 0.01) {
        float bScale = clamp(u_cellPx / 24.0, 0.35, 1.0);
        bloom += directEmissive * 0.45;
        for (int k = 0; k < 8; ++k) {
            float ang = float(k) * 0.78539816;
            vec2 off1 = vec2(cos(ang), sin(ang)) * u_texelSize * (2.5 * bScale);
            vec2 off2 = vec2(cos(ang + 0.39), sin(ang + 0.39)) * u_texelSize * (6.5 * bScale);
            bloom += texture2D(u_emissive, clamp(uv + off1, vec2(0.001), vec2(0.999))).rgb * 0.11;
            bloom += texture2D(u_emissive, clamp(uv + off2, vec2(0.001), vec2(0.999))).rgb * 0.065;
        }
        bloom *= u_bloomStrength;
    }

    // Combine lighting equation
    vec3 lit = albedo * (ambient + directSun + rtGI) + specContrib + directEmissive * 1.10 + bloom;
    vec3 color = acesFilm(lit);
    color = pow(color, vec3(0.94));

    // 5. Optional Post-FX (Vignette or Retro CRT)
    if (u_postFxMode >= 1) {
        vec2 centered = uv * 2.0 - 1.0;
        float vig = 1.0 - dot(centered, centered) * (u_postFxMode == 2 ? 0.20 : 0.13);
        color *= clamp(vig, 0.55, 1.0);
    }
    if (u_postFxMode == 2) {
        float scan = 0.92 + 0.08 * sin(gl_FragCoord.y * 3.14159265);
        color *= scan;
    }

    gl_FragColor = vec4(color, albSample.a);
}
