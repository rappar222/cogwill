#include "Renderer.hpp"

#include <SFML/Graphics/BlendMode.hpp>
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace cogwill {

namespace {

constexpr const char* EMBEDDED_RT_FRAG_SHADER = R"GLSL(
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

uniform int   u_rtQuality;
uniform float u_bloomStrength;
uniform int   u_aoEnabled;
uniform int   u_waterReflect;
uniform int   u_postFxMode;

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

    vec2 nXY = (mat.rg - vec2(0.50196)) * 2.4;
    bool isWater = (mat.b > 0.86);
    float metallic = isWater ? 0.85 : clamp(mat.b * 1.25, 0.0, 1.0);
    float hCenter = mat.a;
    float zoomScale = clamp(u_cellPx / 28.0, 0.18, 1.0);

    if (isWater) {
        float waveLod = clamp((u_cellPx - 7.0) / 12.0, 0.0, 1.0);
        float w1 = sin(worldPos.x * 0.098 + u_time * 2.6 + worldPos.y * 0.056);
        float w2 = cos(worldPos.y * 0.118 - u_time * 2.0 - worldPos.x * 0.068);
        nXY += vec2(w1, w2) * (0.14 * waveLod);
        metallic = mix(0.32, 0.85, waveLod);
    }
    vec3 N = normalize(vec3(nXY, 1.0));

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

    vec3 L = normalize(vec3(u_sunDirUV.x, -u_sunDirUV.y, max(0.32, u_sunSlope * 28.0)));
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 V = vec3(0.0, 0.22, 0.975);
    vec3 H = normalize(L + V);
    float spec = pow(clamp(dot(N, H), 0.0, 1.0), isWater ? 42.0 : 18.0) * metallic;

    vec3 directSun = u_sunColor * (NdotL * sunShadow * (0.45 + 0.95 * u_sunIntensity));
    vec3 specContrib = u_sunColor * (spec * sunShadow * (0.30 + 0.75 * u_sunIntensity));
    vec3 ambient = u_ambientColor * ao;

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

    vec3 lit = albedo * (ambient + directSun + rtGI) + specContrib + directEmissive * 1.10 + bloom;
    vec3 color = acesFilm(lit);
    color = pow(color, vec3(0.94));

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
)GLSL";

void pushTri(sf::VertexArray& va, sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Color colA, sf::Color colB, sf::Color colC) {
    va.append(sf::Vertex{a, colA});
    va.append(sf::Vertex{b, colB});
    va.append(sf::Vertex{c, colC});
}

void pushQuad(sf::VertexArray& va, sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d, sf::Color col1, sf::Color col2) {
    pushTri(va, a, b, c, col1, col1, col2);
    pushTri(va, a, c, d, col1, col2, col2);
}

sf::Vector2f polar(float r, float ang) {
    return {r * std::cos(ang), r * std::sin(ang)};
}

std::uint8_t heightToByte(float hNorm) {
    return static_cast<std::uint8_t>(std::clamp(std::round(hNorm * 255.0f), 0.0f, 255.0f));
}

std::uint8_t pxHeightToByte(float heightPx, float baseGroundNorm = 0.16f) {
    float norm = baseGroundNorm + (heightPx / 64.0f) * 0.72f;
    return heightToByte(std::clamp(norm, 0.0f, 1.0f));
}

void stampMaterialRect(
    sf::RenderTarget* matTarget,
    sf::Vector2f pos,
    sf::Vector2f size,
    std::uint8_t normX,
    std::uint8_t normY,
    std::uint8_t spec,
    std::uint8_t heightByte,
    float rotDeg = 0.0f
) {
    if (!matTarget) return;
    sf::RectangleShape r(size);
    r.setOrigin({size.x * 0.5f, size.y * 0.5f});
    r.setPosition(pos);
    if (std::abs(rotDeg) > 0.01f) {
        r.setRotation(sf::degrees(rotDeg));
    }
    r.setFillColor(sf::Color(normX, normY, spec, heightByte));
    sf::RenderStates st(sf::BlendNone);
    matTarget->draw(r, st);
}

void stampMaterialCircle(
    sf::RenderTarget* matTarget,
    sf::Vector2f center,
    float radius,
    std::uint8_t normX,
    std::uint8_t normY,
    std::uint8_t spec,
    std::uint8_t heightByte
) {
    if (!matTarget) return;
    sf::CircleShape c(radius, 18);
    c.setOrigin({radius, radius});
    c.setPosition(center);
    c.setFillColor(sf::Color(normX, normY, spec, heightByte));
    sf::RenderStates st(sf::BlendNone);
    matTarget->draw(c, st);
}

void drawEmissiveGlow(
    sf::RenderTarget* emisTarget,
    sf::Vector2f center,
    float radius,
    sf::Color col,
    float intensity = 1.0f
) {
    if (!emisTarget || radius < 1.0f || intensity <= 0.01f) return;
    sf::Color inner(
        static_cast<std::uint8_t>(std::clamp(col.r * intensity, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(col.g * intensity, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(col.b * intensity, 0.0f, 255.0f)),
        255
    );
    sf::Color outer(0, 0, 0, 0);
    constexpr int SEGS = 16;
    sf::VertexArray fan(sf::PrimitiveType::TriangleFan, SEGS + 2);
    fan[0] = sf::Vertex{center, inner};
    for (int i = 0; i <= SEGS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(SEGS);
        fan[static_cast<std::size_t>(i + 1)] = sf::Vertex{center + polar(radius, a), outer};
    }
    sf::RenderStates st(sf::BlendAdd);
    emisTarget->draw(fan, st);
}

void drawEmissiveCone(
    sf::RenderTarget* emisTarget,
    sf::Vector2f origin,
    float angleRad,
    float length,
    float halfSpreadRad,
    sf::Color col,
    float intensity = 1.0f
) {
    if (!emisTarget || length < 2.0f) return;
    sf::Color inner(
        static_cast<std::uint8_t>(std::clamp(col.r * intensity, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(col.g * intensity, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(col.b * intensity, 0.0f, 255.0f)),
        255
    );
    sf::Color outer(0, 0, 0, 0);
    constexpr int SEGS = 10;
    sf::VertexArray fan(sf::PrimitiveType::TriangleFan, SEGS + 2);
    fan[0] = sf::Vertex{origin, inner};
    for (int i = 0; i <= SEGS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SEGS);
        float a = angleRad - halfSpreadRad + (2.0f * halfSpreadRad) * t;
        fan[static_cast<std::size_t>(i + 1)] = sf::Vertex{origin + polar(length, a), outer};
    }
    sf::RenderStates st(sf::BlendAdd);
    emisTarget->draw(fan, st);
}

void drawFilledArc(
    sf::RenderTarget& target,
    sf::Vector2f center,
    float innerR,
    float outerR,
    float startDeg,
    float sweepDeg,
    sf::Color color
) {
    if (sweepDeg <= 0.5f) return;
    int steps = std::max(4, static_cast<int>(sweepDeg / 15.0f));
    sf::VertexArray va(sf::PrimitiveType::Triangles);
    for (int i = 0; i < steps; ++i) {
        float a0 = (startDeg + sweepDeg * (static_cast<float>(i) / steps)) * PI / 180.0f;
        float a1 = (startDeg + sweepDeg * (static_cast<float>(i + 1) / steps)) * PI / 180.0f;
        sf::Vector2f p0 = center + polar(innerR, a0);
        sf::Vector2f p1 = center + polar(outerR, a0);
        sf::Vector2f p2 = center + polar(outerR, a1);
        sf::Vector2f p3 = center + polar(innerR, a1);
        pushQuad(va, p0, p1, p2, p3, color, color);
    }
    target.draw(va);
}

void drawThickLine(
    sf::RenderTarget& target,
    sf::Vector2f a,
    sf::Vector2f b,
    float thickness,
    sf::Color color,
    sf::BlendMode blend = sf::BlendAlpha
) {
    sf::Vector2f delta = b - a;
    float len = std::hypot(delta.x, delta.y);
    if (len < 0.5f) return;
    sf::Vector2f perp{-delta.y / len * (thickness * 0.5f), delta.x / len * (thickness * 0.5f)};
    sf::VertexArray va(sf::PrimitiveType::Triangles);
    pushQuad(va, a + perp, b + perp, b - perp, a - perp, color, color);
    sf::RenderStates st(blend);
    target.draw(va, st);
}

void drawCatenaryWire(
    sf::RenderTarget& target,
    sf::Vector2f a,
    sf::Vector2f b,
    float sagPx,
    float thickness,
    sf::Color color,
    sf::BlendMode blend = sf::BlendAlpha
) {
    constexpr int SEGS = 8;
    sf::Vector2f prev = a;
    for (int i = 1; i <= SEGS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SEGS);
        float sag = 4.0f * t * (1.0f - t) * sagPx;
        sf::Vector2f cur = a + (b - a) * t + sf::Vector2f{0.0f, sag};
        drawThickLine(target, prev, cur, thickness, color, blend);
        prev = cur;
    }
}

void drawDashedLine(
    sf::RenderTarget& target,
    sf::Vector2f a,
    sf::Vector2f b,
    float thickness,
    float dashLen,
    sf::Color color
) {
    sf::Vector2f delta = b - a;
    float len = std::hypot(delta.x, delta.y);
    if (len < 1.0f) return;
    sf::Vector2f dir = delta / len;
    int steps = std::max(1, static_cast<int>(len / (dashLen * 1.8f)));
    for (int i = 0; i <= steps; ++i) {
        float t0 = static_cast<float>(i) * dashLen * 1.8f;
        float t1 = std::min(len, t0 + dashLen);
        if (t1 > t0 + 1.0f) {
            drawThickLine(target, a + dir * t0, a + dir * t1, thickness, color);
        }
    }
}

void drawPixelCircleRing(
    sf::RenderTarget& target,
    sf::Vector2f center,
    float radius,
    float thickness,
    sf::Color color,
    int segments = 48
) {
    sf::VertexArray va(sf::PrimitiveType::Triangles);
    float inner = std::max(0.f, radius - thickness * 0.5f);
    float outer = radius + thickness * 0.5f;
    for (int i = 0; i < segments; ++i) {
        float a0 = TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
        float a1 = TWO_PI * static_cast<float>(i + 1) / static_cast<float>(segments);
        pushQuad(va,
                 center + polar(inner, a0),
                 center + polar(outer, a0),
                 center + polar(outer, a1),
                 center + polar(inner, a1),
                 color, color);
    }
    target.draw(va);
}

void drawWorldBar(
    sf::RenderTarget& target,
    sf::Vector2f center,
    float width,
    float height,
    float ratio,
    sf::Color fillCol,
    sf::Color bgCol = sf::Color(10, 14, 22, 225)
) {
    float r = std::clamp(ratio, 0.0f, 1.0f);
    sf::RectangleShape bg({width, height});
    bg.setPosition({center.x - width * 0.5f, center.y - height * 0.5f});
    bg.setFillColor(bgCol);
    bg.setOutlineColor(sf::Color(24, 32, 46, 230));
    bg.setOutlineThickness(1.5f);
    target.draw(bg);

    if (r > 0.01f) {
        sf::RectangleShape fg({(width - 2.0f) * r, height - 2.0f});
        fg.setPosition({center.x - width * 0.5f + 1.0f, center.y - height * 0.5f + 1.0f});
        fg.setFillColor(fillCol);
        target.draw(fg);
    }
}

sf::Color scaleAlpha(sf::Color c, float factor) {
    c.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(c.a) * factor, 0.0f, 255.0f));
    return c;
}

sf::Color darkenColor(sf::Color c, float mult) {
    return sf::Color(
        static_cast<std::uint8_t>(std::clamp(c.r * mult, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(c.g * mult, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(c.b * mult, 0.0f, 255.0f)),
        c.a
    );
}

// Draws a 2.5D extruded industrial chassis: ground shadow, South vertical wall, side bevels, and returns elevated top center!
sf::Vector2f drawChassis25D(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    sf::Vector2f baseCenter,
    float width,
    float depth,
    float heightPx,
    sf::Color topCol,
    sf::Color trimCol,
    std::uint8_t specByte,
    bool mode25D,
    bool isGhost
) {
    std::uint8_t hByte = pxHeightToByte(heightPx);
    if (!mode25D || heightPx < 1.0f) {
        sf::RectangleShape top({width, depth});
        top.setOrigin({width * 0.5f, depth * 0.5f});
        top.setPosition(baseCenter);
        top.setFillColor(topCol);
        top.setOutlineColor(trimCol);
        top.setOutlineThickness(3.0f);
        target.draw(top);
        if (!isGhost) {
            stampMaterialRect(matTarget, baseCenter, {width + 4.f, depth + 4.f}, 128, 128, specByte, hByte);
        }
        return baseCenter;
    }

    if (!isGhost) {
        sf::RectangleShape gShadow({width + 6.0f, depth + 5.0f});
        gShadow.setOrigin({(width + 6.0f) * 0.5f, (depth + 5.0f) * 0.5f});
        gShadow.setPosition(baseCenter + sf::Vector2f{2.5f, 3.5f});
        gShadow.setFillColor(sf::Color(6, 8, 14, 130));
        target.draw(gShadow);
    }

    sf::Vector2f topCenter = baseCenter - sf::Vector2f{0.0f, heightPx};
    float southBottomY = baseCenter.y + depth * 0.5f;
    float southTopY = topCenter.y + depth * 0.5f;
    float wallH = southBottomY - southTopY;

    // South vertical wall
    sf::Color frontCol = darkenColor(topCol, 0.62f);
    sf::Color darkTrim = darkenColor(trimCol, 0.68f);
    sf::RectangleShape frontWall({width, wallH});
    frontWall.setPosition({baseCenter.x - width * 0.5f, southTopY});
    frontWall.setFillColor(frontCol);
    frontWall.setOutlineColor(darkTrim);
    frontWall.setOutlineThickness(2.5f);
    target.draw(frontWall);

    // Subtle industrial vertical ribs on the 2.5D front face
    if (width >= 36.0f && wallH >= 8.0f) {
        int ribs = (width > 100.0f) ? 5 : 2;
        for (int i = 1; i <= ribs; ++i) {
            float rx = baseCenter.x - width * 0.5f + (width * static_cast<float>(i)) / static_cast<float>(ribs + 1);
            sf::RectangleShape rib({2.5f, wallH - 3.0f});
            rib.setPosition({rx, southTopY + 1.5f});
            rib.setFillColor(darkenColor(frontCol, 0.72f));
            target.draw(rib);
        }
    }

    if (!isGhost) {
        // Stamp South-facing normal (128, 226) on front wall & upward normal (128, 128) on roof cap!
        stampMaterialRect(
            matTarget,
            {baseCenter.x, southTopY + wallH * 0.5f},
            {width + 3.0f, wallH + 2.0f},
            128, 226, specByte, hByte
        );
        stampMaterialRect(
            matTarget,
            topCenter,
            {width + 3.0f, depth + 3.0f},
            128, 128, specByte, hByte
        );
    }

    // Elevated top deck
    sf::RectangleShape roof({width, depth});
    roof.setOrigin({width * 0.5f, depth * 0.5f});
    roof.setPosition(topCenter);
    roof.setFillColor(topCol);
    roof.setOutlineColor(trimCol);
    roof.setOutlineThickness(3.0f);
    target.draw(roof);

    return topCenter;
}

bool isRailOrStation(const World* world, int gx, int gy) {
    if (!world || !world->inBounds(gx, gy)) return false;
    const BuildingEntity* b = world->buildingAt(gx, gy);
    return b && (b->type == BuildingType::RailTrack || b->type == BuildingType::TrainStation);
}

bool isElectricNodeType(BuildingType t, const BuildingDef& def) {
    return def.powerRange > 0.0f ||
           std::abs(def.baseElectricKW) > 0.01f ||
           t == BuildingType::Accumulator ||
           t == BuildingType::TrainStation ||
           t == BuildingType::CoolingTower;
}

float wireAttachHeight25D(BuildingType t, float extrusion) {
    switch (t) {
    case BuildingType::PowerPole:      return 34.0f * extrusion;
    case BuildingType::Substation:     return 38.0f * extrusion;
    case BuildingType::HubCore:        return 46.0f * extrusion;
    case BuildingType::NuclearReactor: return 46.0f * extrusion;
    case BuildingType::FusionReactor:  return 48.0f * extrusion;
    default:                           return Catalog::getBuilding(t).height25D * extrusion * 0.85f;
    }
}

sf::Texture makePixelOreTexture(OreType ore, bool rich) {
    sf::Image img({16u, 16u}, sf::Color::Transparent);

    sf::Color outline(10, 12, 16);
    sf::Color dark, mid, hi, glint;
    switch (ore) {
    case OreType::Iron:
        dark  = {66, 82, 108};
        mid   = {118, 142, 174};
        hi    = {178, 204, 234};
        glint = {225, 240, 255};
        break;
    case OreType::Copper:
        dark  = {120, 56, 28};
        mid   = {194, 98, 48};
        hi    = {240, 148, 72};
        glint = {255, 206, 122};
        break;
    case OreType::Coal:
        dark  = {12, 13, 17};
        mid   = {28, 30, 38};
        hi    = {54, 58, 70};
        glint = {92, 98, 115};
        break;
    case OreType::Titanium:
        dark  = {58, 44, 102};
        mid   = {118, 94, 192};
        hi    = {186, 164, 248};
        glint = {240, 230, 255};
        break;
    case OreType::Uranium:
        dark  = {22, 82, 26};
        mid   = {52, 168, 48};
        hi    = {122, 242, 88};
        glint = {210, 255, 164};
        break;
    case OreType::Aether:
    default:
        dark  = {18, 92, 118};
        mid   = {38, 168, 204};
        hi    = {92, 232, 255};
        glint = {205, 255, 255};
        break;
    }

    static constexpr const char* PATTERN_STD[16] = {
        "................",
        "..ooo......ooo..",
        ".oMMHo....oMHHo.",
        ".oDDMo....oMMMo.",
        "..ooo......ooo..",
        "......oooo......",
        ".....oMGHHo.....",
        ".....oMMMMo.....",
        "..oo..oooo......",
        ".oMHo......ooo..",
        ".oDDo.....oGHMo.",
        "..oo..ooo.oDDMo.",
        ".....oMHHo.ooo..",
        ".....oDDMo......",
        "......ooo.......",
        "................"
    };
    static constexpr const char* PATTERN_RICH[16] = {
        "................",
        ".oooo......ooo..",
        "oMGHHo....oGHMo.",
        "oDMMMo....oDDMo.",
        ".oooo.oooo.ooo..",
        ".....oMGHHo.....",
        ".ooo.oMMMMo.ooo.",
        "oMHHo.oooo.oGHMo",
        "oDDMo......oMMMo",
        ".ooo..oooo..ooo.",
        ".....oMGHHo.....",
        ".ooo.oDMMMo.ooo.",
        "oGHMo.oooo.oMHHo",
        "oDDMo......oDDMo",
        ".ooo........ooo.",
        "................"
    };

    const char* const* pat = rich ? PATTERN_RICH : PATTERN_STD;
    for (unsigned int y = 0; y < 16; ++y) {
        for (unsigned int x = 0; x < 16; ++x) {
            char c = pat[y][x];
            if (c == '.') continue;
            sf::Color col = outline;
            if (c == 'D') col = dark;
            else if (c == 'M') col = mid;
            else if (c == 'H') col = hi;
            else if (c == 'G') col = glint;
            img.setPixel({x, y}, col);
        }
    }

    sf::Texture tex;
    (void)tex.loadFromImage(img);
    tex.setSmooth(false);
    return tex;
}

} // namespace

Renderer::Renderer() : iconRT_({24u, 24u}) {
    GearColors ironCol{
        {14, 18, 24}, {190, 202, 218}, {128, 140, 158}, {152, 164, 184}, {108, 118, 136}, {176, 188, 206}, {240, 200, 108}
    };
    GearColors copperCol{
        {22, 14, 10}, {238, 158, 92}, {168, 92, 46}, {204, 124, 66}, {152, 84, 44}, {224, 146, 82}, {252, 224, 145}
    };
    GearColors brassCol{
        {24, 18, 10}, {244, 204, 98}, {172, 130, 44}, {214, 170, 72}, {158, 120, 48}, {232, 190, 86}, {255, 238, 172}
    };
    GearColors collarCol{
        {12, 14, 20}, {132, 142, 160}, {88, 96, 112}, {106, 116, 134}, {82, 90, 106}, {122, 132, 150}, {220, 178, 92}
    };
    GearColors darkBrassCol{
        {14, 10, 8}, {138, 102, 40}, {96, 70, 26}, {118, 88, 34}, {90, 66, 24}, {112, 84, 32}, {148, 116, 52}
    };
    GearColors flywheelCol{
        {22, 14, 10}, {218, 144, 68}, {148, 86, 36}, {236, 188, 84}, {132, 82, 40}, {190, 132, 64}, {255, 236, 160}
    };
    GearColors shadowCol{
        {6, 8, 12, 125}, {6, 8, 12, 125}, {6, 8, 12, 125}, {6, 8, 12, 125}, {6, 8, 12, 125}, {6, 8, 12, 125}, {6, 8, 12, 125}
    };

    ironCog12_     = buildGearVertexArray(12, 32.0f, 27.2f, 36.2f, 20.5f, 4, ironCol);
    copperCog12_   = buildGearVertexArray(12, 32.0f, 27.2f, 36.2f, 20.5f, 4, copperCol);
    brassGear36_   = buildGearVertexArray(36, 96.0f, 91.2f, 100.2f, 76.0f, 6, brassCol);
    darkCollar12_  = buildGearVertexArray(12, 32.0f, 27.2f, 35.8f, 22.0f, 4, collarCol);
    darkGear36_    = buildGearVertexArray(36, 96.0f, 91.2f, 100.2f, 76.0f, 6, darkBrassCol);
    flywheelWheel_ = buildGearVertexArray(12, 32.0f, 27.2f, 35.8f, 17.5f, 6, flywheelCol);
    shadowCog12_   = buildGearVertexArray(12, 32.0f, 27.2f, 36.2f, 20.5f, 4, shadowCol);
    shadowGear36_  = buildGearVertexArray(36, 96.0f, 91.2f, 100.2f, 76.0f, 6, shadowCol);

    buildPixelTextures();
    loadShaders();
    iconRT_.setSmooth(false);
}

void Renderer::loadShaders() {
    shaderLoaded_ = false;
    if (!sf::Shader::isAvailable()) {
        return;
    }

    std::vector<std::filesystem::path> candidates = {
        "assets/shaders/rt_lighting.frag",
#ifdef COGWILL_ASSET_DIR
        std::filesystem::path(COGWILL_ASSET_DIR) / "shaders/rt_lighting.frag",
#endif
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            if (rtShader_.loadFromFile(p, sf::Shader::Type::Fragment)) {
                shaderLoaded_ = true;
                return;
            }
        }
    }

    if (rtShader_.loadFromMemory(EMBEDDED_RT_FRAG_SHADER, sf::Shader::Type::Fragment)) {
        shaderLoaded_ = true;
    }
}

void Renderer::ensureGBufferSize(unsigned int w, unsigned int h) const {
    unsigned int safeW = std::max(1u, w);
    unsigned int safeH = std::max(1u, h);
    if (gbufW_ == safeW && gbufH_ == safeH) {
        return;
    }
    (void)albedoRT_.resize({safeW, safeH});
    (void)materialRT_.resize({safeW, safeH});
    (void)emissiveRT_.resize({safeW, safeH});
    albedoRT_.setSmooth(false);
    materialRT_.setSmooth(false);
    emissiveRT_.setSmooth(false);
    gbufW_ = safeW;
    gbufH_ = safeH;
}

void Renderer::buildPixelTextures() {
    sf::Image bg({16u, 16u}, sf::Color(26, 31, 42));
    for (unsigned int y = 0; y < 16; ++y) {
        for (unsigned int x = 0; x < 16; ++x) {
            if (x == 0 || y == 0) {
                bg.setPixel({x, y}, sf::Color(35, 43, 56));
            } else if (x == 15 || y == 15) {
                bg.setPixel({x, y}, sf::Color(20, 24, 33));
            } else if (((x + y) & 1u) == 0u && ((x * 7u + y * 13u) % 11u == 0u)) {
                bg.setPixel({x, y}, sf::Color(31, 37, 49));
            } else if ((x == 4 && y == 11) || (x == 11 && y == 5)) {
                bg.setPixel({x, y}, sf::Color(22, 26, 36));
            }
        }
    }
    (void)bedrockTex_.loadFromImage(bg);
    bedrockTex_.setSmooth(false);

    // Animated 16x16 coolant lake water frames
    for (int f = 0; f < 2; ++f) {
        sf::Image wImg({16u, 16u}, sf::Color(18, 62, 106));
        for (unsigned int y = 0; y < 16; ++y) {
            for (unsigned int x = 0; x < 16; ++x) {
                unsigned int sx = (x + static_cast<unsigned int>(f * 4)) & 15u;
                if ((y == 3 && sx >= 2 && sx <= 6) || (y == 10 && sx >= 8 && sx <= 13)) {
                    wImg.setPixel({x, y}, sf::Color(64, 174, 236));
                } else if ((y == 4 && sx >= 3 && sx <= 5) || (y == 11 && sx >= 9 && sx <= 12)) {
                    wImg.setPixel({x, y}, sf::Color(128, 226, 255));
                } else if (((x + y + static_cast<unsigned int>(f)) % 5u) == 0u) {
                    wImg.setPixel({x, y}, sf::Color(26, 84, 136));
                }
            }
        }
        (void)waterTex_[static_cast<std::size_t>(f)].loadFromImage(wImg);
        waterTex_[static_cast<std::size_t>(f)].setSmooth(false);
    }

    // 16x16 jagged dark basalt cliff block
    sf::Image rImg({16u, 16u}, sf::Color(44, 48, 62));
    for (unsigned int y = 0; y < 16; ++y) {
        for (unsigned int x = 0; x < 16; ++x) {
            if (x == 0 || y == 0 || x == 15 || y == 15) {
                rImg.setPixel({x, y}, sf::Color(20, 22, 30));
            } else if (x <= 2 || y <= 2) {
                rImg.setPixel({x, y}, sf::Color(82, 90, 110));
            } else if (x >= 13 || y >= 13) {
                rImg.setPixel({x, y}, sf::Color(28, 30, 40));
            } else if ((x + y == 14) || (x == 7 && y >= 4 && y <= 11)) {
                rImg.setPixel({x, y}, sf::Color(24, 26, 36));
            } else if ((x + y * 3u) % 7u == 0u) {
                rImg.setPixel({x, y}, sf::Color(64, 70, 88));
            }
        }
    }
    (void)rockTex_.loadFromImage(rImg);
    rockTex_.setSmooth(false);

    oreTex_[0] = makePixelOreTexture(OreType::Iron, false);
    oreTex_[1] = makePixelOreTexture(OreType::Copper, false);
    oreTex_[2] = makePixelOreTexture(OreType::Coal, false);
    oreTex_[3] = makePixelOreTexture(OreType::Titanium, false);
    oreTex_[4] = makePixelOreTexture(OreType::Uranium, false);
    oreTex_[5] = makePixelOreTexture(OreType::Aether, false);

    oreRichTex_[0] = makePixelOreTexture(OreType::Iron, true);
    oreRichTex_[1] = makePixelOreTexture(OreType::Copper, true);
    oreRichTex_[2] = makePixelOreTexture(OreType::Coal, true);
    oreRichTex_[3] = makePixelOreTexture(OreType::Titanium, true);
    oreRichTex_[4] = makePixelOreTexture(OreType::Uranium, true);
    oreRichTex_[5] = makePixelOreTexture(OreType::Aether, true);
}

sf::VertexArray Renderer::buildGearVertexArray(
    int teeth,
    float /*pitchRadius*/,
    float rootRadius,
    float outerRadius,
    float innerRimRadius,
    int spokeCount,
    const GearColors& colors
) {
    sf::VertexArray va(sf::PrimitiveType::Triangles);
    const float toothStep = TWO_PI / static_cast<float>(teeth);

    for (int k = 0; k < teeth; ++k) {
        float c = static_cast<float>(k) * toothStep;
        float halfRoot = toothStep * 0.30f;
        float halfTip = toothStep * 0.185f;
        sf::Vector2f rL = polar(rootRadius - 2.0f, c - halfRoot);
        sf::Vector2f rR = polar(rootRadius - 2.0f, c + halfRoot);
        sf::Vector2f tL = polar(outerRadius + 1.4f, c - halfTip);
        sf::Vector2f tR = polar(outerRadius + 1.4f, c + halfTip);
        pushQuad(va, rL, tL, tR, rR, colors.outline, colors.outline);
    }

    for (int k = 0; k < teeth; ++k) {
        float c = static_cast<float>(k) * toothStep;
        float halfRoot = toothStep * 0.25f;
        float halfTip = toothStep * 0.14f;
        sf::Vector2f rL = polar(rootRadius - 0.5f, c - halfRoot);
        sf::Vector2f rR = polar(rootRadius - 0.5f, c + halfRoot);
        sf::Vector2f tL = polar(outerRadius - 0.4f, c - halfTip);
        sf::Vector2f tR = polar(outerRadius - 0.4f, c + halfTip);
        pushQuad(va, rL, tL, tR, rR, colors.toothDark, colors.toothLight);
    }

    int ringSegs = teeth * 2;
    for (int i = 0; i < ringSegs; ++i) {
        float a0 = TWO_PI * static_cast<float>(i) / static_cast<float>(ringSegs);
        float a1 = TWO_PI * static_cast<float>(i + 1) / static_cast<float>(ringSegs);
        pushQuad(va, polar(innerRimRadius - 1.5f, a0), polar(rootRadius + 1.5f, a0),
                 polar(rootRadius + 1.5f, a1), polar(innerRimRadius - 1.5f, a1), colors.outline, colors.outline);
        pushQuad(va, polar(innerRimRadius, a0), polar(rootRadius, a0),
                 polar(rootRadius, a1), polar(innerRimRadius, a1), colors.spoke, colors.rim);
    }

    float hubRadius = innerRimRadius * 0.44f;
    float spokeHalfAngle = 0.16f * (TWO_PI / static_cast<float>(std::max(3, spokeCount)));
    for (int s = 0; s < spokeCount; ++s) {
        float a = TWO_PI * static_cast<float>(s) / static_cast<float>(spokeCount);
        pushQuad(va,
                 polar(hubRadius * 0.7f, a - spokeHalfAngle * 1.35f),
                 polar(innerRimRadius + 1.0f, a - spokeHalfAngle * 1.05f),
                 polar(innerRimRadius + 1.0f, a + spokeHalfAngle * 1.05f),
                 polar(hubRadius * 0.7f, a + spokeHalfAngle * 1.35f),
                 colors.outline, colors.outline);
        pushQuad(va,
                 polar(hubRadius * 0.8f, a - spokeHalfAngle * 1.0f),
                 polar(innerRimRadius + 0.5f, a - spokeHalfAngle * 0.78f),
                 polar(innerRimRadius + 0.5f, a + spokeHalfAngle * 0.78f),
                 polar(hubRadius * 0.8f, a + spokeHalfAngle * 1.0f),
                 colors.hub, colors.spoke);
    }

    constexpr int HUB_SEGS = 12;
    for (int i = 0; i < HUB_SEGS; ++i) {
        float a0 = TWO_PI * static_cast<float>(i) / HUB_SEGS;
        float a1 = TWO_PI * static_cast<float>(i + 1) / HUB_SEGS;
        pushTri(va, {0.f, 0.f}, polar(hubRadius + 1.5f, a0), polar(hubRadius + 1.5f, a1), colors.outline, colors.outline, colors.outline);
        pushTri(va, {0.f, 0.f}, polar(hubRadius, a0), polar(hubRadius, a1), colors.hub, colors.spoke, colors.spoke);
    }

    float pinR = std::max(3.6f, hubRadius * 0.44f);
    for (int i = 0; i < 8; ++i) {
        float a0 = TWO_PI * static_cast<float>(i) / 8.f;
        float a1 = TWO_PI * static_cast<float>(i + 1) / 8.f;
        pushTri(va, {0.f, 0.f}, polar(pinR, a0), polar(pinR, a1), colors.pin, colors.pin, colors.pin);
    }
    sf::Vector2f markerPos = polar((innerRimRadius + rootRadius) * 0.5f, 0.0f);
    float mR = std::max(2.6f, (rootRadius - innerRimRadius) * 0.28f);
    pushQuad(va,
             markerPos + sf::Vector2f{-mR, -mR},
             markerPos + sf::Vector2f{mR, -mR},
             markerPos + sf::Vector2f{mR, mR},
             markerPos + sf::Vector2f{-mR, mR},
             colors.pin, colors.pin);

    return va;
}

void Renderer::renderScene(
    sf::RenderTarget& finalTarget,
    const sf::View& worldView,
    const World& world,
    const GameSettings& settings,
    float animTime,
    std::optional<BuildingType> ghostType,
    int hoverGx,
    int hoverGy,
    int ghostDir,
    const PlacementCheck& ghostCheck,
    int selectedGx,
    int selectedGy,
    const std::vector<BlueprintEntry>* blueprint,
    int bpRot,
    std::optional<std::pair<int, int>> copyBoxStart
) const {
    const sf::Vector2u winSz = finalTarget.getSize();
    const unsigned int pxScale = static_cast<unsigned int>(std::clamp(settings.pixelScale, 1, 4));
    const unsigned int rtW = std::max(1u, (winSz.x + pxScale - 1u) / pxScale);
    const unsigned int rtH = std::max(1u, (winSz.y + pxScale - 1u) / pxScale);
    ensureGBufferSize(rtW, rtH);

    const bool useShaders = settings.shadersEnabled && shaderLoaded_;

    albedoRT_.setView(worldView);
    albedoRT_.clear(sf::Color(14, 18, 26, 255));

    sf::RenderTarget* matPtr = nullptr;
    sf::RenderTarget* emisPtr = nullptr;
    if (useShaders) {
        materialRT_.setView(worldView);
        emissiveRT_.setView(worldView);
        materialRT_.clear(sf::Color(128, 128, 18, 40));
        emissiveRT_.clear(sf::Color(0, 0, 0, 255));
        matPtr = &materialRT_;
        emisPtr = &emissiveRT_;
    }

    renderWorld(
        albedoRT_,
        world,
        settings,
        animTime,
        ghostType,
        hoverGx,
        hoverGy,
        ghostDir,
        ghostCheck,
        selectedGx,
        selectedGy,
        blueprint,
        bpRot,
        copyBoxStart,
        matPtr,
        emisPtr
    );

    albedoRT_.display();
    if (useShaders) {
        materialRT_.display();
        emissiveRT_.display();
    }

    sf::View screenView(sf::FloatRect({0.f, 0.f}, {static_cast<float>(winSz.x), static_cast<float>(winSz.y)}));
    finalTarget.setView(screenView);

    sf::Sprite compositeSprite(albedoRT_.getTexture());
    compositeSprite.setScale({
        static_cast<float>(winSz.x) / static_cast<float>(rtW),
        static_cast<float>(winSz.y) / static_cast<float>(rtH)
    });

    if (useShaders) {
        float sun = world.solarFactor();
        float phase = world.effectiveDayPhase();

        // Orbiting Sun/Moon direction in world XY (pointing TOWARD the light source)
        // Sweeps East -> North-West -> West during the day so buildings cast crisp South/East/West shadows!
        float azimuth = -0.65f + 1.55f * std::sin(phase);
        sf::Vector2f sunDirWorld{-std::sin(azimuth) * 0.78f - 0.35f, -0.72f};
        float len = std::hypot(sunDirWorld.x, sunDirWorld.y);
        if (len > 0.001f) sunDirWorld /= len;

        // In SFML FBO UV space, +Y_uv is North (-Y_world), so flip Y!
        sf::Glsl::Vec2 sunDirUV{sunDirWorld.x, -sunDirWorld.y};
        float sunSlope = 0.014f + 0.020f * sun;

        // Sun & Ambient color grading across Day / Golden Dusk / Neon Night
        float duskWeight = std::clamp(1.0f - std::abs(sun - 0.42f) * 2.6f, 0.0f, 1.0f);
        sf::Glsl::Vec3 daySun{1.24f, 1.17f, 1.04f};
        sf::Glsl::Vec3 duskSun{1.30f, 0.72f, 0.40f};
        sf::Glsl::Vec3 nightMoon{0.22f, 0.32f, 0.58f};

        sf::Glsl::Vec3 sunCol{
            nightMoon.x + (daySun.x - nightMoon.x) * sun + (duskSun.x - daySun.x) * duskWeight * 0.65f,
            nightMoon.y + (daySun.y - nightMoon.y) * sun + (duskSun.y - daySun.y) * duskWeight * 0.65f,
            nightMoon.z + (daySun.z - nightMoon.z) * sun + (duskSun.z - daySun.z) * duskWeight * 0.65f
        };
        sf::Glsl::Vec3 ambCol{
            0.16f + 0.44f * sun + 0.08f * duskWeight,
            0.19f + 0.43f * sun + 0.02f * duskWeight,
            0.28f + 0.38f * sun - 0.03f * duskWeight
        };

        sf::Vector2f vSize = worldView.getSize();
        sf::Vector2f vTopLeft = worldView.getCenter() - vSize * 0.5f;
        float cellPx = (CELL_SIZE / std::max(1.0f, vSize.x)) * static_cast<float>(rtW);

        rtShader_.setUniform("u_albedo", sf::Shader::CurrentTexture);
        rtShader_.setUniform("u_material", materialRT_.getTexture());
        rtShader_.setUniform("u_emissive", emissiveRT_.getTexture());
        rtShader_.setUniform("u_texelSize", sf::Glsl::Vec2{1.0f / static_cast<float>(rtW), 1.0f / static_cast<float>(rtH)});
        rtShader_.setUniform("u_time", animTime);
        rtShader_.setUniform("u_sunDirUV", sunDirUV);
        rtShader_.setUniform("u_sunSlope", sunSlope);
        rtShader_.setUniform("u_sunIntensity", sun);
        rtShader_.setUniform("u_sunColor", sunCol);
        rtShader_.setUniform("u_ambientColor", ambCol);
        rtShader_.setUniform("u_worldTopLeft", sf::Glsl::Vec2{vTopLeft.x, vTopLeft.y});
        rtShader_.setUniform("u_worldSize", sf::Glsl::Vec2{vSize.x, vSize.y});
        rtShader_.setUniform("u_mapWorldMax", sf::Glsl::Vec2{GRID_W * CELL_SIZE, GRID_H * CELL_SIZE});
        rtShader_.setUniform("u_cellPx", cellPx);
        rtShader_.setUniform("u_rtQuality", std::clamp(settings.rtQuality, 0, 3));
        rtShader_.setUniform("u_bloomStrength", settings.bloomStrength);
        rtShader_.setUniform("u_aoEnabled", settings.ambientOcclusion ? 1 : 0);
        rtShader_.setUniform("u_waterReflect", settings.waterReflections ? 1 : 0);
        rtShader_.setUniform("u_postFxMode", std::clamp(settings.postFxMode, 0, 2));

        sf::RenderStates shaderStates(&rtShader_);
        finalTarget.draw(compositeSprite, shaderStates);
    } else {
        finalTarget.draw(compositeSprite);
    }
}

void Renderer::renderWorld(
    sf::RenderTarget& target,
    const World& world,
    const GameSettings& settings,
    float animTime,
    std::optional<BuildingType> ghostType,
    int hoverGx,
    int hoverGy,
    int ghostDir,
    const PlacementCheck& ghostCheck,
    int selectedGx,
    int selectedGy,
    const std::vector<BlueprintEntry>* blueprint,
    int bpRot,
    std::optional<std::pair<int, int>> copyBoxStart,
    sf::RenderTarget* matTarget,
    sf::RenderTarget* emisTarget
) const {
    const bool mode25D = settings.mode25D;
    const float extrusion = mode25D ? settings.extrusionDepth : 0.0f;
    const bool cpuSunTint = !(settings.shadersEnabled && shaderLoaded_ && matTarget != nullptr);

    // 1. Base Ground, Biome Stones, Sunken Coolant Lakes & Ore Sprites
    drawBaseTerrainAndOres(target, matTarget, emisTarget, world, settings, animTime);

    const sf::View& view = target.getView();
    sf::Vector2f vc = view.getCenter();
    sf::Vector2f vs = view.getSize();
    int minGx = std::max(0, static_cast<int>(std::floor((vc.x - vs.x * 0.5f) / CELL_SIZE)) - 3);
    int maxGx = std::min(GRID_W - 1, static_cast<int>(std::ceil((vc.x + vs.x * 0.5f) / CELL_SIZE)) + 3);
    int minGy = std::max(0, static_cast<int>(std::floor((vc.y - vs.y * 0.5f) / CELL_SIZE)) - 3);
    int maxGy = std::min(GRID_H - 1, static_cast<int>(std::ceil((vc.y + vs.y * 0.5f) / CELL_SIZE)) + 5);

    auto isVisible = [&](const BuildingEntity& b) {
        return b.gx >= minGx && b.gx <= maxGx && b.gy >= minGy && b.gy <= maxGy;
    };

    // 2A. Rails & Train Stations underneath machines
    for (const BuildingEntity& b : world.buildings()) {
        if ((b.type == BuildingType::RailTrack || b.type == BuildingType::TrainStation) && isVisible(b)) {
            drawRailBuilding(target, matTarget, emisTarget, &world, b, settings, animTime, false, true);
        }
    }

    // 2B. Conveyor Belt Tracks, Splitters & Tunnels
    for (const BuildingEntity& b : world.buildings()) {
        if (World::isBeltLike(b.type) && isVisible(b)) {
            drawBeltTrack(target, matTarget, emisTarget, &world, b, settings, animTime, false, true);
        }
    }
    // 2C. Physical Items riding on Belts
    for (const BuildingEntity& b : world.buildings()) {
        if (World::isBeltLike(b.type) && isVisible(b)) {
            drawBeltItems(target, matTarget, emisTarget, b, settings);
        }
    }

    // 3. Ground Auras (Lubricator & Overclock Beacon)
    const int ocRad = world.beaconAuraRadius();
    for (const BuildingEntity& b : world.buildings()) {
        if (b.type == BuildingType::Lubricator && isVisible(b) &&
            (b.status == NetworkStatus::Running || b.status == NetworkStatus::Buffering)) {
            sf::RectangleShape aura({CELL_SIZE * 3.0f - 6.0f, CELL_SIZE * 3.0f - 6.0f});
            aura.setPosition({(b.gx - 1) * CELL_SIZE + 3.0f, (b.gy - 1) * CELL_SIZE + 3.0f});
            aura.setFillColor(sf::Color(80, 210, 145, 26));
            aura.setOutlineColor(sf::Color(95, 225, 160, 145));
            aura.setOutlineThickness(2.5f);
            target.draw(aura);
        } else if (b.type == BuildingType::OverclockBeacon && b.electricPowered &&
                   b.gx >= minGx - 4 && b.gx <= maxGx + 4 && b.gy >= minGy - 4 && b.gy <= maxGy + 4) {
            float pulse = 0.8f + 0.2f * std::sin(animTime * 4.5f);
            float span = static_cast<float>(ocRad * 2 + 1) * CELL_SIZE - 6.0f;
            sf::RectangleShape aura({span, span});
            aura.setPosition({(b.gx - ocRad) * CELL_SIZE + 3.0f, (b.gy - ocRad) * CELL_SIZE + 3.0f});
            aura.setFillColor(sf::Color(245, 165, 65, 20));
            aura.setOutlineColor(sf::Color(255, 190, 92, static_cast<std::uint8_t>(145 * pulse)));
            aura.setOutlineThickness(2.5f);
            target.draw(aura);
        }
    }

    // 4. Y-Sorted 2.5D Extruded Pass (or 2D pass): Cliffs, Buildings & Trains sorted back-to-front by row gy!
    if (mode25D) {
        std::vector<std::vector<const BuildingEntity*>> rowBuildings(static_cast<std::size_t>(maxGy - minGy + 1));
        for (const BuildingEntity& b : world.buildings()) {
            if (b.type == BuildingType::RailTrack || b.type == BuildingType::TrainStation || World::isBeltLike(b.type)) {
                continue;
            }
            if (isVisible(b)) {
                rowBuildings[static_cast<std::size_t>(b.gy - minGy)].push_back(&b);
            }
        }

        std::vector<std::vector<const TrainEntity*>> rowTrains(static_cast<std::size_t>(maxGy - minGy + 1));
        for (const TrainEntity& tr : world.trains()) {
            int tgy = std::clamp(static_cast<int>(tr.wy / CELL_SIZE), 0, GRID_H - 1);
            if (tgy >= minGy && tgy <= maxGy) {
                rowTrains[static_cast<std::size_t>(tgy - minGy)].push_back(&tr);
            }
        }

        for (int gy = minGy; gy <= maxGy; ++gy) {
            for (int gx = minGx; gx <= maxGx; ++gx) {
                if (world.tile(gx, gy).terrain == TerrainType::Rock) {
                    drawRockCliff25D(target, matTarget, world, gx, gy, extrusion, cpuSunTint);
                }
            }
            for (const BuildingEntity* bp : rowBuildings[static_cast<std::size_t>(gy - minGy)]) {
                drawBuilding(target, matTarget, emisTarget, *bp, settings, animTime, false, true, &world);
            }
            for (const TrainEntity* tp : rowTrains[static_cast<std::size_t>(gy - minGy)]) {
                drawSingleTrain(target, matTarget, emisTarget, world, *tp, settings);
            }
        }
    } else {
        for (const BuildingEntity& b : world.buildings()) {
            if (b.type == BuildingType::RailTrack || b.type == BuildingType::TrainStation || World::isBeltLike(b.type)) {
                continue;
            }
            if (isVisible(b)) {
                drawBuilding(target, matTarget, emisTarget, b, settings, animTime, false, true, &world);
            }
        }
        for (const TrainEntity& tr : world.trains()) {
            drawSingleTrain(target, matTarget, emisTarget, world, tr, settings);
        }
    }

    // 5. Overhead Catenary Power Lines
    drawPowerLines(target, emisTarget, world, settings, animTime);

    // 6. Status badges
    for (const BuildingEntity& b : world.buildings()) {
        if (!isVisible(b)) continue;
        if (b.type == BuildingType::HubCore || World::isBeltLike(b.type) || b.type == BuildingType::StorageVault) {
            continue;
        }
        const BuildingDef& def = Catalog::getBuilding(b.type);
        float hOff = mode25D ? (def.height25D * extrusion * 0.7f) : 0.0f;
        sf::Vector2f c{(b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE - hOff};

        bool noFuel = (def.fuelType != ResourceType::Count) && !b.hasFuel;
        bool isHybrid = (b.type == BuildingType::Assembler || b.type == BuildingType::WaterPump);
        if (isHybrid) {
            bool running = b.electricPowered || b.status == NetworkStatus::Running || b.status == NetworkStatus::Buffering;
            if (!running && (b.status == NetworkStatus::Jammed || b.status == NetworkStatus::Overloaded)) {
                drawStatusBadge(target, c, sf::Color(248, 172, 55, 235));
            }
        } else if (def.isMechanical) {
            if (b.status == NetworkStatus::Jammed) {
                drawStatusBadge(target, c, sf::Color(240, 80, 75, 235));
            } else if (b.status == NetworkStatus::Overloaded && !noFuel) {
                drawStatusBadge(target, c, sf::Color(248, 172, 55, 235));
            } else if (noFuel) {
                drawStatusBadge(target, c, sf::Color(240, 80, 75, 235));
            }
        } else if (def.baseElectricKW < -0.1f) {
            const ElectricNetworkInfo* enet = world.electricNetworkById(b.electricNetId);
            if (!b.electricPowered) {
                sf::Color col = (enet && enet->overloaded) ? sf::Color(248, 172, 55, 235) : sf::Color(240, 80, 75, 235);
                drawStatusBadge(target, c, col);
            }
        } else if ((b.type == BuildingType::NuclearReactor || b.type == BuildingType::FusionReactor) && noFuel) {
            drawStatusBadge(target, c, sf::Color(240, 80, 75, 235));
        }
    }

    // 7. Selection highlight & range rings
    if (world.inBounds(selectedGx, selectedGy)) {
        if (const BuildingEntity* sel = world.buildingAt(selectedGx, selectedGy)) {
            const BuildingDef& def = Catalog::getBuilding(sel->type);
            int rad = def.footprintRadius;
            float size = (rad * 2 + 1) * CELL_SIZE;
            sf::RectangleShape box({size - 4.0f, size - 4.0f});
            box.setPosition({(sel->gx - rad) * CELL_SIZE + 2.0f, (sel->gy - rad) * CELL_SIZE + 2.0f});
            box.setFillColor(sf::Color::Transparent);
            box.setOutlineColor(sf::Color(248, 210, 96));
            box.setOutlineThickness(3.0f);
            target.draw(box);

            sf::Vector2f sc{(sel->gx + 0.5f) * CELL_SIZE, (sel->gy + 0.5f) * CELL_SIZE};
            drawEmissiveGlow(emisTarget, sc, size * 0.65f, {248, 210, 96}, 0.28f);

            if (def.powerRange > 0.f) {
                drawPixelCircleRing(target, sc, def.powerRange * CELL_SIZE, 3.0f, sf::Color(90, 225, 255, 135), 56);
            }
            if (sel->type == BuildingType::TrainStation) {
                drawPixelCircleRing(target, sc, static_cast<float>(STATION_PICKUP_RADIUS) * CELL_SIZE, 3.0f, sf::Color(245, 185, 75, 135), 40);
            } else if (sel->type == BuildingType::WaterPump) {
                drawPixelCircleRing(target, sc, 3.5f * CELL_SIZE, 3.0f, sf::Color(86, 195, 255, 145), 44);
            } else if (sel->type == BuildingType::UndergroundBelt) {
                int exitIdx = world.findUndergroundExitIndex(sel->gx, sel->gy, sel->dir);
                if (exitIdx >= 0 && exitIdx < static_cast<int>(world.buildings().size())) {
                    const BuildingEntity& ex = world.buildings()[static_cast<std::size_t>(exitIdx)];
                    drawDashedLine(target, sc, {(ex.gx + 0.5f) * CELL_SIZE, (ex.gy + 0.5f) * CELL_SIZE},
                                   4.0f, 14.0f, sf::Color(96, 238, 255, 215));
                }
            }
        }
    }

    // 8. Ctrl+C Blueprint selection rectangle / Ctrl+V stamp / Ghost preview
    if (copyBoxStart.has_value() && world.inBounds(hoverGx, hoverGy)) {
        int x0 = std::min(copyBoxStart->first, hoverGx);
        int x1 = std::max(copyBoxStart->first, hoverGx);
        int y0 = std::min(copyBoxStart->second, hoverGy);
        int y1 = std::max(copyBoxStart->second, hoverGy);
        sf::RectangleShape selBox({(x1 - x0 + 1) * CELL_SIZE, (y1 - y0 + 1) * CELL_SIZE});
        selBox.setPosition({x0 * CELL_SIZE, y0 * CELL_SIZE});
        selBox.setFillColor(sf::Color(85, 210, 255, 36));
        selBox.setOutlineColor(sf::Color(115, 235, 255, 225));
        selBox.setOutlineThickness(3.0f);
        target.draw(selBox);
    } else if (blueprint && !blueprint->empty() && world.inBounds(hoverGx, hoverGy)) {
        for (const BlueprintEntry& be : *blueprint) {
            int rdx = be.dx;
            int rdy = be.dy;
            for (int r = 0; r < (bpRot & 3); ++r) {
                int tmp = rdx;
                rdx = -rdy;
                rdy = tmp;
            }
            int gx = hoverGx + rdx;
            int gy = hoverGy + rdy;
            if (!world.inBounds(gx, gy)) continue;
            int dir = (be.dir + bpRot) & 3;
            if (be.type == BuildingType::TrainStation) dir = be.dir % 2;
            PlacementCheck chk = world.checkPlacement(be.type, gx, gy, dir);

            const BuildingDef& def = Catalog::getBuilding(be.type);
            int rad = def.footprintRadius;
            float size = (rad * 2 + 1) * CELL_SIZE;
            sf::RectangleShape fp({size - 4.0f, size - 4.0f});
            fp.setPosition({(gx - rad) * CELL_SIZE + 2.0f, (gy - rad) * CELL_SIZE + 2.0f});
            fp.setFillColor(chk.valid ? sf::Color(75, 215, 255, 42) : sf::Color(225, 70, 70, 52));
            fp.setOutlineColor(chk.valid ? sf::Color(105, 235, 255) : sf::Color(245, 90, 90));
            fp.setOutlineThickness(2.5f);
            target.draw(fp);

            BuildingEntity ghost;
            ghost.type = be.type;
            ghost.gx = gx;
            ghost.gy = gy;
            ghost.dir = dir;
            ghost.status = NetworkStatus::Running;
            ghost.electricPowered = true;
            ghost.hasFuel = true;
            ghost.angle = animTime * 0.9f;
            if (be.type == BuildingType::RailTrack || be.type == BuildingType::TrainStation) {
                drawRailBuilding(target, nullptr, emisTarget, &world, ghost, settings, animTime, true, chk.valid);
            } else if (World::isBeltLike(be.type)) {
                drawBeltTrack(target, nullptr, emisTarget, &world, ghost, settings, animTime, true, chk.valid);
            } else {
                drawBuilding(target, nullptr, emisTarget, ghost, settings, animTime, true, chk.valid, &world);
            }
        }
    } else if (ghostType.has_value() && world.inBounds(hoverGx, hoverGy)) {
        const BuildingDef& def = Catalog::getBuilding(*ghostType);
        int rad = def.footprintRadius;
        float size = (rad * 2 + 1) * CELL_SIZE;
        sf::RectangleShape footprint({size - 4.0f, size - 4.0f});
        footprint.setPosition({(hoverGx - rad) * CELL_SIZE + 2.0f, (hoverGy - rad) * CELL_SIZE + 2.0f});
        sf::Color glowCol = ghostCheck.valid ? sf::Color(95, 245, 145) : sf::Color(245, 90, 90);
        if (ghostCheck.valid && ghostCheck.isUpgradeReplace) {
            footprint.setFillColor(sf::Color(245, 195, 65, 58));
            footprint.setOutlineColor(sf::Color(255, 224, 96));
            glowCol = {255, 224, 96};
        } else {
            footprint.setFillColor(ghostCheck.valid ? sf::Color(70, 210, 120, 46) : sf::Color(225, 70, 70, 56));
            footprint.setOutlineColor(glowCol);
        }
        footprint.setOutlineThickness(3.0f);
        target.draw(footprint);

        sf::Vector2f hc{(hoverGx + 0.5f) * CELL_SIZE, (hoverGy + 0.5f) * CELL_SIZE};
        drawEmissiveGlow(emisTarget, hc, size * 0.72f, glowCol, 0.35f);

        if (*ghostType == BuildingType::Lubricator) {
            sf::RectangleShape aura({CELL_SIZE * 3.0f - 4.0f, CELL_SIZE * 3.0f - 4.0f});
            aura.setPosition({(hoverGx - 1) * CELL_SIZE + 2.0f, (hoverGy - 1) * CELL_SIZE + 2.0f});
            aura.setFillColor(sf::Color(80, 215, 145, 28));
            aura.setOutlineColor(sf::Color(95, 235, 165));
            aura.setOutlineThickness(2.5f);
            target.draw(aura);
        } else if (*ghostType == BuildingType::OverclockBeacon) {
            float span = static_cast<float>(ocRad * 2 + 1) * CELL_SIZE - 4.0f;
            sf::RectangleShape aura({span, span});
            aura.setPosition({(hoverGx - ocRad) * CELL_SIZE + 2.0f, (hoverGy - ocRad) * CELL_SIZE + 2.0f});
            aura.setFillColor(sf::Color(245, 175, 68, 26));
            aura.setOutlineColor(sf::Color(255, 205, 96));
            aura.setOutlineThickness(2.5f);
            target.draw(aura);
        } else if (*ghostType == BuildingType::WaterPump) {
            drawPixelCircleRing(target, hc, 3.5f * CELL_SIZE, 3.0f, sf::Color(86, 195, 255, 165), 44);
        } else if (def.powerRange > 0.f) {
            drawPixelCircleRing(target, hc, def.powerRange * CELL_SIZE, 3.0f, sf::Color(90, 225, 255, 165), 56);
        } else if (*ghostType == BuildingType::TrainStation) {
            drawPixelCircleRing(target, hc, static_cast<float>(STATION_PICKUP_RADIUS) * CELL_SIZE, 3.0f, sf::Color(245, 185, 75, 165), 40);
        }

        BuildingEntity ghostEntity;
        ghostEntity.type = *ghostType;
        ghostEntity.gx = hoverGx;
        ghostEntity.gy = hoverGy;
        ghostEntity.dir = (*ghostType == BuildingType::TrainStation && ghostDir < 0)
            ? (world.isInDirectHubZone(hoverGx, hoverGy) ? 1 : 0)
            : std::max(0, ghostDir);
        ghostEntity.status = NetworkStatus::Running;
        ghostEntity.electricPowered = true;
        ghostEntity.hasFuel = true;
        ghostEntity.angle = animTime * 0.9f;
        ghostEntity.angleSecondary = animTime * 1.8f;

        if (*ghostType == BuildingType::RailTrack || *ghostType == BuildingType::TrainStation) {
            drawRailBuilding(target, nullptr, emisTarget, &world, ghostEntity, settings, animTime, true, ghostCheck.valid);
        } else if (World::isBeltLike(*ghostType)) {
            drawBeltTrack(target, nullptr, emisTarget, &world, ghostEntity, settings, animTime, true, ghostCheck.valid);
        } else {
            drawBuilding(target, nullptr, emisTarget, ghostEntity, settings, animTime, true, ghostCheck.valid, &world);
        }

        drawGhostMeshingGuides(target, emisTarget, world, *ghostType, hoverGx, hoverGy, ghostEntity.dir, animTime);
    } else if (world.inBounds(hoverGx, hoverGy)) {
        sf::RectangleShape cursor({CELL_SIZE - 4.0f, CELL_SIZE - 4.0f});
        cursor.setPosition({hoverGx * CELL_SIZE + 2.0f, hoverGy * CELL_SIZE + 2.0f});
        cursor.setFillColor(sf::Color(255, 255, 255, 16));
        cursor.setOutlineColor(sf::Color(190, 210, 238, 130));
        cursor.setOutlineThickness(2.5f);
        target.draw(cursor);
    }

    drawParticles(target, emisTarget, world);
}

void Renderer::drawBaseTerrainAndOres(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    sf::RenderTarget* emisTarget,
    const World& world,
    const GameSettings& settings,
    float animTime
) const {
    const sf::View& view = target.getView();
    sf::Vector2f vc = view.getCenter();
    sf::Vector2f vs = view.getSize();
    const float cellPx = (CELL_SIZE / std::max(1.0f, vs.x)) * static_cast<float>(target.getSize().x);
    const float worldPerTexel = std::max(1.0f, vs.x / std::max(1.0f, static_cast<float>(target.getSize().x)));
    const bool useStrategicTerrain = (cellPx < 14.5f);
    const float checkerFade = (settings.gridStyle == 0) ? 0.0f : std::clamp((cellPx - 9.0f) / 7.5f, 0.0f, 1.0f);

    int minX = std::max(0, static_cast<int>(std::floor((vc.x - vs.x * 0.5f) / CELL_SIZE)) - 1);
    int maxX = std::min(GRID_W - 1, static_cast<int>(std::ceil((vc.x + vs.x * 0.5f) / CELL_SIZE)) + 1);
    int minY = std::max(0, static_cast<int>(std::floor((vc.y - vs.y * 0.5f) / CELL_SIZE)) - 1);
    int maxY = std::min(GRID_H - 1, static_cast<int>(std::ceil((vc.y + vs.y * 0.5f) / CELL_SIZE)) + 3);

    const bool useGpuLighting = settings.shadersEnabled && shaderLoaded_ && matTarget != nullptr;
    float sun = world.solarFactor();
    float ambR = useGpuLighting ? 1.0f : (0.68f + 0.32f * sun);
    float ambG = useGpuLighting ? 1.0f : (0.71f + 0.29f * sun);
    float ambB = useGpuLighting ? 1.0f : (0.82f + 0.18f * sun);
    auto applySun = [&](sf::Color c) -> sf::Color {
        if (useGpuLighting) return c;
        return sf::Color(
            static_cast<std::uint8_t>(std::clamp(c.r * ambR, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(c.g * ambG, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(c.b * ambB, 0.0f, 255.0f)),
            c.a
        );
    };

    sf::VertexArray tilesVa(sf::PrimitiveType::Triangles);
    sf::VertexArray solidTerrainVa(sf::PrimitiveType::Triangles);
    sf::VertexArray waterVa0(sf::PrimitiveType::Triangles);
    sf::VertexArray waterVa1(sf::PrimitiveType::Triangles);
    sf::VertexArray rockVa2D(sf::PrimitiveType::Triangles);
    sf::VertexArray matVa(sf::PrimitiveType::Triangles);
    int waterAnimStep = static_cast<int>(animTime * 2.2f);

    auto appendTexQuad = [](sf::VertexArray& va, float x0, float y0, sf::Color tint) {
        float x1 = x0 + CELL_SIZE;
        float y1 = y0 + CELL_SIZE;
        sf::Vector2f u0{0.f, 0.f}, u1{16.f, 0.f}, u2{16.f, 16.f}, u3{0.f, 16.f};
        va.append(sf::Vertex{{x0, y0}, tint, u0});
        va.append(sf::Vertex{{x1, y0}, tint, u1});
        va.append(sf::Vertex{{x1, y1}, tint, u2});
        va.append(sf::Vertex{{x0, y0}, tint, u0});
        va.append(sf::Vertex{{x1, y1}, tint, u2});
        va.append(sf::Vertex{{x0, y1}, tint, u3});
    };

    auto appendSolidQuad = [](sf::VertexArray& va, float x0, float y0, float w, float h, sf::Color col) {
        pushQuad(va, {x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}, col, col);
    };

    auto appendMatQuad = [&](float x0, float y0, float w, float h, sf::Color mc) {
        if (!matTarget) return;
        pushQuad(matVa, {x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h}, {x0, y0 + h}, mc, mc);
    };

    auto lerpCol = [](sf::Color a, sf::Color b, float t) -> sf::Color {
        return sf::Color(
            static_cast<std::uint8_t>(std::clamp(a.r + (b.r - a.r) * t, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(a.g + (b.g - a.g) * t, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(a.b + (b.b - a.b) * t, 0.0f, 255.0f)),
            a.a
        );
    };

    auto biomeGroundTint = [&](std::uint8_t biome, bool checker, float elev) -> sf::Color {
        float eBoost = 0.90f + 0.35f * elev;
        sf::Color darkBase, lightBase;
        switch (biome) {
        case 1:  darkBase = {228, 188, 162}; lightBase = {246, 206, 178}; break; // Rust Mesa
        case 2:  darkBase = {176, 218, 208}; lightBase = {192, 236, 224}; break; // Coolant Moss Basin
        case 3:  darkBase = {160, 154, 188}; lightBase = {178, 172, 206}; break; // Obsidian Rift
        case 4:  darkBase = {218, 226, 246}; lightBase = {238, 244, 255}; break; // Quartz Tundra
        default: darkBase = {206, 214, 232}; lightBase = {226, 234, 250}; break; // Clockwork Slate
        }
        sf::Color midBase = lerpCol(darkBase, lightBase, 0.5f);
        sf::Color base = lerpCol(midBase, checker ? lightBase : darkBase, checkerFade);
        return sf::Color(
            static_cast<std::uint8_t>(std::clamp(base.r * eBoost, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(base.g * eBoost, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(base.b * eBoost, 0.0f, 255.0f))
        );
    };

    const float bankDepth = settings.mode25D ? (10.0f * settings.extrusionDepth) : 0.0f;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const Tile& t = world.tile(x, y);
            float x0 = x * CELL_SIZE;
            float y0 = y * CELL_SIZE;
            bool checker = (((x + y) & 1) == 0);

            if (t.terrain == TerrainType::Water) {
                if (useStrategicTerrain) {
                    // Clean coastal-to-deep coolant gradient with zero Moire or white speckle blobs
                    bool shore =
                        (world.inBounds(x - 1, y) && world.tile(x - 1, y).terrain != TerrainType::Water) ||
                        (world.inBounds(x + 1, y) && world.tile(x + 1, y).terrain != TerrainType::Water) ||
                        (world.inBounds(x, y - 1) && world.tile(x, y - 1).terrain != TerrainType::Water) ||
                        (world.inBounds(x, y + 1) && world.tile(x, y + 1).terrain != TerrainType::Water);
                    bool midShallow = !shore && (
                        (world.inBounds(x - 2, y) && world.tile(x - 2, y).terrain != TerrainType::Water) ||
                        (world.inBounds(x + 2, y) && world.tile(x + 2, y).terrain != TerrainType::Water) ||
                        (world.inBounds(x, y - 2) && world.tile(x, y - 2).terrain != TerrainType::Water) ||
                        (world.inBounds(x, y + 2) && world.tile(x, y + 2).terrain != TerrainType::Water));
                    sf::Color wCol = shore ? sf::Color(36, 114, 172)
                                   : (midShallow ? sf::Color(25, 86, 142) : sf::Color(19, 68, 120));
                    appendSolidQuad(solidTerrainVa, x0, y0, CELL_SIZE, CELL_SIZE, applySun(wCol));
                } else {
                    sf::Color wTint = applySun(lerpCol({236, 245, 254}, checker ? sf::Color(245, 250, 255) : sf::Color(228, 240, 252), checkerFade));
                    if (((x + y + waterAnimStep) & 1) == 0) {
                        appendTexQuad(waterVa0, x0, y0, wTint);
                    } else {
                        appendTexQuad(waterVa1, x0, y0, wTint);
                    }
                }
                appendMatQuad(x0, y0, CELL_SIZE, CELL_SIZE, sf::Color(128, 128, 245, 0));
                continue;
            }

            if (t.terrain == TerrainType::Rock && !settings.mode25D) {
                if (useStrategicTerrain) {
                    float rShade = 0.85f + 0.25f * std::clamp(t.elevation, 0.55f, 1.0f);
                    sf::Color rCol(
                        static_cast<std::uint8_t>(std::clamp(44.0f * rShade, 0.0f, 255.0f)),
                        static_cast<std::uint8_t>(std::clamp(48.0f * rShade, 0.0f, 255.0f)),
                        static_cast<std::uint8_t>(std::clamp(62.0f * rShade, 0.0f, 255.0f))
                    );
                    appendSolidQuad(solidTerrainVa, x0, y0, CELL_SIZE, CELL_SIZE, applySun(rCol));
                } else {
                    sf::Color rTint = applySun(checker ? sf::Color(245, 245, 252) : sf::Color(222, 224, 235));
                    appendTexQuad(rockVa2D, x0, y0, rTint);
                }
                std::uint8_t hB = heightToByte(std::clamp(t.elevation, 0.65f, 1.0f));
                appendMatQuad(x0, y0, CELL_SIZE, CELL_SIZE, sf::Color(128, 128, 55, hB));
                continue;
            }

            // Ground (or base underneath 2.5D Rock cliffs so zero gaps ever appear!)
            sf::Color tint = biomeGroundTint(t.biome, checker, t.elevation);
            if (!useStrategicTerrain && t.ore != OreType::None) {
                switch (t.ore) {
                case OreType::Iron:     tint = sf::Color(200, 222, 255); break;
                case OreType::Copper:   tint = sf::Color(255, 206, 176); break;
                case OreType::Coal:     tint = sf::Color(170, 175, 188); break;
                case OreType::Titanium: tint = sf::Color(218, 198, 255); break;
                case OreType::Uranium:  tint = sf::Color(175, 252, 172); break;
                case OreType::Aether:   tint = sf::Color(165, 248, 255); break;
                default: break;
                }
            }
            if (useStrategicTerrain) {
                // Match exact average luminance of bedrockTex_ (26.2, 31.3, 42.3) with zero 16x16 seam Moire
                float micro = 0.98f + 0.04f * static_cast<float>((t.visualSeed >> 3u) & 3u) / 3.0f;
                sf::Color gSolid(
                    static_cast<std::uint8_t>(std::clamp(tint.r * (26.4f / 255.0f) * micro, 0.0f, 255.0f)),
                    static_cast<std::uint8_t>(std::clamp(tint.g * (31.5f / 255.0f) * micro, 0.0f, 255.0f)),
                    static_cast<std::uint8_t>(std::clamp(tint.b * (42.5f / 255.0f) * micro, 0.0f, 255.0f))
                );
                appendSolidQuad(solidTerrainVa, x0, y0, CELL_SIZE, CELL_SIZE, applySun(gSolid));
            } else {
                appendTexQuad(tilesVa, x0, y0, applySun(tint));
            }

            // Estimate gentle terrain normal from E/W & N/S elevation gradient
            float eL = world.inBounds(x - 1, y) ? world.tile(x - 1, y).elevation : t.elevation;
            float eR = world.inBounds(x + 1, y) ? world.tile(x + 1, y).elevation : t.elevation;
            float eN = world.inBounds(x, y - 1) ? world.tile(x, y - 1).elevation : t.elevation;
            float eS = world.inBounds(x, y + 1) ? world.tile(x, y + 1).elevation : t.elevation;
            float gradScale = useStrategicTerrain ? 55.0f : 110.0f;
            std::uint8_t nx = static_cast<std::uint8_t>(std::clamp(128.0f + (eL - eR) * gradScale, 80.0f, 176.0f));
            std::uint8_t ny = static_cast<std::uint8_t>(std::clamp(128.0f + (eN - eS) * gradScale, 80.0f, 176.0f));
            std::uint8_t spec = (t.ore == OreType::Titanium || t.ore == OreType::Aether || t.ore == OreType::Iron) ? 110 : 22;
            std::uint8_t hB = heightToByte(std::clamp(t.elevation * 0.55f, 0.08f, 0.24f));
            appendMatQuad(x0, y0, CELL_SIZE, CELL_SIZE, sf::Color(nx, ny, spec, hB));
        }
    }

    if (solidTerrainVa.getVertexCount() > 0) {
        target.draw(solidTerrainVa);
    }
    if (tilesVa.getVertexCount() > 0) {
        sf::RenderStates stGround;
        stGround.texture = &bedrockTex_;
        target.draw(tilesVa, stGround);
    }
    if (waterVa0.getVertexCount() > 0) {
        sf::RenderStates stW0;
        stW0.texture = &waterTex_[0];
        target.draw(waterVa0, stW0);
    }
    if (waterVa1.getVertexCount() > 0) {
        sf::RenderStates stW1;
        stW1.texture = &waterTex_[1];
        target.draw(waterVa1, stW1);
    }
    if (rockVa2D.getVertexCount() > 0) {
        sf::RenderStates stR;
        stR.texture = &rockTex_;
        target.draw(rockVa2D, stR);
    }
    if (matTarget && matVa.getVertexCount() > 0) {
        sf::RenderStates stM(sf::BlendNone);
        matTarget->draw(matVa, stM);
    }

    // Sunken 2.5D stone north banks along water shorelines
    if (settings.mode25D && bankDepth > 1.5f) {
        sf::VertexArray bankVa(sf::PrimitiveType::Triangles);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (world.tile(x, y).terrain != TerrainType::Water) continue;
                if (world.inBounds(x, y - 1) && world.tile(x, y - 1).terrain != TerrainType::Water) {
                    float x0 = x * CELL_SIZE;
                    float y0 = y * CELL_SIZE;
                    pushQuad(
                        bankVa,
                        {x0, y0},
                        {x0 + CELL_SIZE, y0},
                        {x0 + CELL_SIZE, y0 + bankDepth},
                        {x0, y0 + bankDepth},
                        applySun(sf::Color(54, 66, 86, 240)),
                        applySun(sf::Color(20, 42, 68, 220))
                    );
                }
            }
        }
        if (bankVa.getVertexCount() > 0) {
            target.draw(bankVa);
        }
    }

    // Strategic 16x16 macro-sector lines when zoomed out (pixel-locked thickness, zero Moire!)
    if (settings.gridStyle != 0 && cellPx < 16.0f) {
        float macroAlpha = std::clamp((16.0f - cellPx) / 6.0f, 0.0f, 1.0f) * (settings.gridStyle == 2 ? 48.0f : 24.0f);
        if (macroAlpha >= 2.0f) {
            sf::VertexArray sectorVa(sf::PrimitiveType::Triangles);
            sf::Color sCol(125, 152, 196, static_cast<std::uint8_t>(macroAlpha));
            float lineW = std::max(2.0f, worldPerTexel * 1.15f);
            int sxStart = ((minX + 15) / 16) * 16;
            for (int sx = sxStart; sx <= maxX; sx += 16) {
                if (sx <= 0 || sx >= GRID_W) continue;
                appendSolidQuad(sectorVa, sx * CELL_SIZE - lineW * 0.5f, minY * CELL_SIZE,
                                lineW, static_cast<float>(maxY - minY + 1) * CELL_SIZE, sCol);
            }
            int syStart = ((minY + 15) / 16) * 16;
            for (int sy = syStart; sy <= maxY; sy += 16) {
                if (sy <= 0 || sy >= GRID_H) continue;
                appendSolidQuad(sectorVa, minX * CELL_SIZE, sy * CELL_SIZE - lineW * 0.5f,
                                static_cast<float>(maxX - minX + 1) * CELL_SIZE, lineW, sCol);
            }
            if (sectorVa.getVertexCount() > 0) {
                target.draw(sectorVa);
            }
        }
    }

    // Optional bright 1x1 grid overlay when gridStyle == 2 (smoothly fades out below 17..9 px/tile)
    if (settings.gridStyle == 2 && cellPx > 9.0f) {
        float fineFade = std::clamp((cellPx - 9.0f) / 8.0f, 0.0f, 1.0f);
        sf::VertexArray gridLines(sf::PrimitiveType::Lines);
        sf::Color gCol(120, 145, 182, static_cast<std::uint8_t>(46.0f * fineFade));
        for (int x = minX; x <= maxX + 1; ++x) {
            gridLines.append(sf::Vertex{{x * CELL_SIZE, minY * CELL_SIZE}, gCol});
            gridLines.append(sf::Vertex{{x * CELL_SIZE, (maxY + 1) * CELL_SIZE}, gCol});
        }
        for (int y = minY; y <= maxY + 1; ++y) {
            gridLines.append(sf::Vertex{{minX * CELL_SIZE, y * CELL_SIZE}, gCol});
            gridLines.append(sf::Vertex{{(maxX + 1) * CELL_SIZE, y * CELL_SIZE}, gCol});
        }
        target.draw(gridLines);
    }

    // Crisp outer map frame around [0..GRID_W*CELL_SIZE, 0..GRID_H*CELL_SIZE]
    {
        sf::RectangleShape mapBorder({GRID_W * CELL_SIZE, GRID_H * CELL_SIZE});
        mapBorder.setPosition({0.0f, 0.0f});
        mapBorder.setFillColor(sf::Color::Transparent);
        mapBorder.setOutlineColor(sf::Color(92, 114, 148, 165));
        mapBorder.setOutlineThickness(std::max(3.0f, worldPerTexel * 1.6f));
        target.draw(mapBorder);
    }

    // Hub Core plaza & Direct Drone Radius ring
    sf::RectangleShape pad({CELL_SIZE * 5.0f, CELL_SIZE * 5.0f});
    pad.setPosition({(HUB_GX - 2) * CELL_SIZE, (HUB_GY - 2) * CELL_SIZE});
    pad.setFillColor(sf::Color(55, 85, 120, 26));
    pad.setOutlineColor(sf::Color(115, 168, 225, 98));
    pad.setOutlineThickness(std::max(2.5f, worldPerTexel * 1.1f));
    target.draw(pad);

    drawPixelCircleRing(
        target,
        {(HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY + 0.5f) * CELL_SIZE},
        static_cast<float>(world.directHubRadius()) * CELL_SIZE,
        std::max(4.0f, worldPerTexel * 1.3f),
        sf::Color(95, 165, 225, 55),
        96
    );

    // Multi-scale Mineral Veins: clean fused color-coded mineral pads when zoomed out,
    // smoothly cross-fading into detailed 16x16 pixel-art crystal sprites when zoomed in!
    const float spriteAlpha = std::clamp((cellPx - 11.5f) / 4.5f, 0.0f, 1.0f);
    if (spriteAlpha < 0.99f) {
        sf::VertexArray oreLodVa(sf::PrimitiveType::Triangles);
        auto oreColors = [](OreType o, sf::Color& rim, sf::Color& core, sf::Color& richCol) {
            switch (o) {
            case OreType::Iron:     rim = {42, 64, 92};   core = {138, 180, 228}; richCol = {212, 236, 255}; break;
            case OreType::Copper:   rim = {84, 42, 22};   core = {224, 126, 68};  richCol = {255, 192, 132}; break;
            case OreType::Coal:     rim = {12, 14, 20};   core = {38, 42, 54};    richCol = {78, 86, 108};   break;
            case OreType::Titanium: rim = {62, 48, 98};   core = {178, 156, 240}; richCol = {234, 222, 255}; break;
            case OreType::Uranium:  rim = {24, 78, 32};   core = {84, 224, 92};   richCol = {192, 255, 184}; break;
            case OreType::Aether:   rim = {20, 82, 108};  core = {74, 232, 255};  richCol = {210, 255, 255}; break;
            default:                rim = {40, 44, 54};   core = {120, 130, 145}; richCol = {180, 190, 205}; break;
            }
        };

        const float glowMul = std::clamp(cellPx / 18.0f, 0.35f, 1.0f);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const Tile& t = world.tile(x, y);
                if (t.ore == OreType::None) continue;

                sf::Color rimC, coreC, richC;
                oreColors(t.ore, rimC, coreC, richC);
                if (t.ore != OreType::Uranium && t.ore != OreType::Aether) {
                    rimC = applySun(rimC);
                    coreC = applySun(coreC);
                    richC = applySun(richC);
                }

                bool sameW = world.inBounds(x - 1, y) && world.tile(x - 1, y).ore == t.ore;
                bool sameE = world.inBounds(x + 1, y) && world.tile(x + 1, y).ore == t.ore;
                bool sameN = world.inBounds(x, y - 1) && world.tile(x, y - 1).ore == t.ore;
                bool sameS = world.inBounds(x, y + 1) && world.tile(x, y + 1).ore == t.ore;

                float x0 = x * CELL_SIZE + (sameW ? 0.0f : 6.0f);
                float x1 = (x + 1) * CELL_SIZE - (sameE ? 0.0f : 6.0f);
                float y0 = y * CELL_SIZE + (sameN ? 0.0f : 6.0f);
                float y1 = (y + 1) * CELL_SIZE - (sameS ? 0.0f : 6.0f);
                appendSolidQuad(oreLodVa, x0, y0, x1 - x0, y1 - y0, rimC);

                float cx0 = x * CELL_SIZE + (sameW ? 2.0f : 11.0f);
                float cx1 = (x + 1) * CELL_SIZE - (sameE ? 2.0f : 11.0f);
                float cy0 = y * CELL_SIZE + (sameN ? 2.0f : 11.0f);
                float cy1 = (y + 1) * CELL_SIZE - (sameS ? 2.0f : 11.0f);
                appendSolidQuad(oreLodVa, cx0, cy0, cx1 - cx0, cy1 - cy0, coreC);

                if (t.richness >= 1.5f) {
                    appendSolidQuad(oreLodVa, x * CELL_SIZE + 20.0f, y * CELL_SIZE + 20.0f, 24.0f, 24.0f, richC);
                }

                if (spriteAlpha <= 0.01f && t.occupantId == -1) {
                    sf::Vector2f oc{(x + 0.5f) * CELL_SIZE, (y + 0.5f) * CELL_SIZE};
                    if (t.ore == OreType::Aether) {
                        drawEmissiveGlow(emisTarget, oc, 34.0f, {72, 232, 255}, 0.34f * glowMul);
                    } else if (t.ore == OreType::Uranium) {
                        drawEmissiveGlow(emisTarget, oc, 30.0f, {88, 245, 92}, 0.28f * glowMul);
                    }
                }
            }
        }
        if (oreLodVa.getVertexCount() > 0) {
            target.draw(oreLodVa);
        }
    }

    if (spriteAlpha > 0.01f) {
        const std::uint8_t spA = static_cast<std::uint8_t>(std::clamp(spriteAlpha * 255.0f, 0.0f, 255.0f));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const Tile& t = world.tile(x, y);
                if (t.ore == OreType::None) continue;
                std::size_t idx = static_cast<std::size_t>(static_cast<int>(t.ore) - 1);
                if (idx >= oreTex_.size()) continue;
                const sf::Texture& tex = (t.richness >= 1.5f) ? oreRichTex_[idx] : oreTex_[idx];
                sf::Sprite sp(tex);
                sp.setPosition({x * CELL_SIZE, y * CELL_SIZE});
                sp.setScale({CELL_SIZE / 16.0f, CELL_SIZE / 16.0f});

                sf::Vector2f oc{(x + 0.5f) * CELL_SIZE, (y + 0.5f) * CELL_SIZE};
                if (t.ore == OreType::Aether) {
                    float pulse = 0.85f + 0.15f * std::sin(animTime * 3.0f + static_cast<float>(x + y));
                    sp.setColor(sf::Color(static_cast<std::uint8_t>(220 * pulse), 255, 255, spA));
                    if (t.occupantId == -1) {
                        drawEmissiveGlow(emisTarget, oc, 38.0f, {72, 232, 255}, 0.42f * pulse);
                    }
                } else if (t.ore == OreType::Uranium) {
                    float pulse = 0.85f + 0.15f * std::sin(animTime * 2.5f + static_cast<float>(x * 2 + y));
                    sp.setColor(sf::Color(220, 255, static_cast<std::uint8_t>(200 * pulse), spA));
                    if (t.occupantId == -1) {
                        drawEmissiveGlow(emisTarget, oc, 34.0f, {88, 245, 92}, 0.36f * pulse);
                    }
                } else {
                    sf::Color sc = applySun(sf::Color::White);
                    sc.a = spA;
                    sp.setColor(sc);
                }
                target.draw(sp);
            }
        }
    }
}

void Renderer::drawRockCliff25D(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    const World& world,
    int gx,
    int gy,
    float extrusion,
    bool cpuSunTint
) const {
    const Tile& t = world.tile(gx, gy);
    const float cellPx = (CELL_SIZE / std::max(1.0f, target.getView().getSize().x)) * static_cast<float>(target.getSize().x);
    const float hPx = (18.0f + 30.0f * std::clamp(t.elevation, 0.55f, 1.0f)) * extrusion;
    const float x0 = gx * CELL_SIZE;
    const float y0 = gy * CELL_SIZE;
    const float y1 = y0 + CELL_SIZE;

    float amb = cpuSunTint ? (0.72f + 0.28f * world.solarFactor()) : 1.0f;
    auto tintC = [&](sf::Color c) {
        return sf::Color(
            static_cast<std::uint8_t>(std::clamp(c.r * amb, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(c.g * amb, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(c.b * amb, 0.0f, 255.0f)),
            c.a
        );
    };

    const std::uint8_t hByte = heightToByte(std::clamp(0.45f + 0.54f * t.elevation, 0.55f, 1.0f));
    const bool northOpen = !world.inBounds(gx, gy - 1) || world.tile(gx, gy - 1).terrain != TerrainType::Rock;
    const bool southOpen = !world.inBounds(gx, gy + 1) || world.tile(gx, gy + 1).terrain != TerrainType::Rock;
    const bool westOpen  = !world.inBounds(gx - 1, gy) || world.tile(gx - 1, gy).terrain != TerrainType::Rock;
    const bool eastOpen  = !world.inBounds(gx + 1, gy) || world.tile(gx + 1, gy).terrain != TerrainType::Rock;

    // 1. South vertical basalt cliff face
    sf::VertexArray faceVa(sf::PrimitiveType::Triangles);
    pushQuad(
        faceVa,
        {x0, y1 - hPx},
        {x0 + CELL_SIZE, y1 - hPx},
        {x0 + CELL_SIZE, y1},
        {x0, y1},
        tintC({42, 46, 62}),
        tintC({20, 22, 32})
    );
    if (cellPx >= 10.0f) {
        pushQuad(
            faceVa,
            {x0 + 2.f, y1 - hPx * 0.58f},
            {x0 + CELL_SIZE - 2.f, y1 - hPx * 0.58f},
            {x0 + CELL_SIZE - 2.f, y1 - hPx * 0.46f},
            {x0 + 2.f, y1 - hPx * 0.46f},
            tintC({58, 64, 84}),
            tintC({30, 34, 46})
        );
    }
    if (westOpen) {
        pushQuad(
            faceVa,
            {x0, y0 - hPx},
            {x0 + 5.f, y0 - hPx},
            {x0 + 5.f, y1},
            {x0, y1},
            tintC({78, 86, 108}),
            tintC({46, 52, 68})
        );
    }
    if (eastOpen) {
        pushQuad(
            faceVa,
            {x0 + CELL_SIZE - 5.f, y0 - hPx},
            {x0 + CELL_SIZE, y0 - hPx},
            {x0 + CELL_SIZE, y1},
            {x0 + CELL_SIZE - 5.f, y1},
            tintC({22, 24, 34}),
            tintC({14, 16, 24})
        );
    }
    target.draw(faceVa);

    if (matTarget) {
        if (southOpen) {
            stampMaterialRect(
                matTarget,
                {x0 + CELL_SIZE * 0.5f, y1 - hPx * 0.5f},
                {CELL_SIZE, hPx},
                128, 232, 48, hByte
            );
        }
        stampMaterialRect(
            matTarget,
            {x0 + CELL_SIZE * 0.5f, y0 + CELL_SIZE * 0.5f - hPx},
            {CELL_SIZE, CELL_SIZE},
            128, 128, 62, hByte
        );
    }

    // 2. Elevated basalt mesa cap (fused smooth mesa top when zoomed out, crisp 16x16 pixel rock when close!)
    sf::Color capCol = (t.biome == 1) ? sf::Color(255, 224, 204)
                     : (t.biome == 3) ? sf::Color(208, 198, 244)
                     : sf::Color(238, 242, 255);
    if (cellPx < 14.5f) {
        float eLift = 0.88f + 0.22f * std::clamp(t.elevation, 0.55f, 1.0f);
        sf::Color mesaTop(
            static_cast<std::uint8_t>(std::clamp(50.0f * (capCol.r / 255.0f) * eLift, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(55.0f * (capCol.g / 255.0f) * eLift, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(70.0f * (capCol.b / 255.0f) * eLift, 0.0f, 255.0f))
        );
        sf::VertexArray capVa(sf::PrimitiveType::Triangles);
        pushQuad(capVa, {x0, y0 - hPx}, {x0 + CELL_SIZE, y0 - hPx}, {x0 + CELL_SIZE, y1 - hPx}, {x0, y1 - hPx},
                 tintC(mesaTop), tintC(darkenColor(mesaTop, 0.90f)));
        if (northOpen) {
            sf::Color rimHi(
                static_cast<std::uint8_t>(std::min(255, mesaTop.r + 28)),
                static_cast<std::uint8_t>(std::min(255, mesaTop.g + 30)),
                static_cast<std::uint8_t>(std::min(255, mesaTop.b + 36))
            );
            pushQuad(capVa, {x0, y0 - hPx}, {x0 + CELL_SIZE, y0 - hPx}, {x0 + CELL_SIZE, y0 - hPx + 6.0f}, {x0, y0 - hPx + 6.0f},
                     tintC(rimHi), tintC(rimHi));
        }
        target.draw(capVa);
    } else {
        sf::Sprite cap(rockTex_);
        cap.setPosition({x0, y0 - hPx});
        cap.setScale({CELL_SIZE / 16.0f, CELL_SIZE / 16.0f});
        cap.setColor(tintC(capCol));
        target.draw(cap);
    }
}

void Renderer::drawBeltTrack(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    sf::RenderTarget* emisTarget,
    const World* /*world*/,
    const BuildingEntity& b,
    const GameSettings& settings,
    float animTime,
    bool isGhost,
    bool ghostValid
) const {
    const float lift = settings.mode25D ? (6.0f * settings.extrusionDepth) : 0.0f;
    const sf::Vector2f baseCenter{(b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE};
    const sf::Vector2f center = baseCenter - sf::Vector2f{0.0f, lift};

    float alphaMul = isGhost ? 0.60f : 1.0f;
    auto colA = [&](sf::Color c) {
        if (isGhost && !ghostValid) {
            c.r = static_cast<std::uint8_t>(std::min(255, c.r + 75));
            c.g = static_cast<std::uint8_t>(c.g * 0.45f);
            c.b = static_cast<std::uint8_t>(c.b * 0.45f);
        }
        return scaleAlpha(c, alphaMul);
    };

    if (b.type == BuildingType::BeltSplitter) {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter, 54.0f, 54.0f,
            settings.mode25D ? 12.0f * settings.extrusionDepth : 0.0f,
            colA({40, 46, 58}), colA({232, 182, 78}), 135, settings.mode25D, isGhost
        );

        int fdx = 0, fdy = 0;
        World::directionVector(b.dir, fdx, fdy);
        float dirDeg = static_cast<float>(b.dir) * 90.0f;

        sf::RectangleShape crossH({56.0f, 18.0f});
        crossH.setOrigin({28.0f, 9.0f});
        crossH.setPosition(topC);
        crossH.setFillColor(colA({26, 30, 38}));
        target.draw(crossH);

        sf::RectangleShape crossV({18.0f, 56.0f});
        crossV.setOrigin({9.0f, 28.0f});
        crossV.setPosition(topC);
        crossV.setFillColor(colA({26, 30, 38}));
        target.draw(crossV);

        sf::RectangleShape rotor({20.0f, 20.0f});
        rotor.setOrigin({10.0f, 10.0f});
        rotor.setPosition(topC);
        rotor.setRotation(sf::radians(animTime * 2.4f));
        rotor.setFillColor(colA({238, 190, 86}));
        rotor.setOutlineColor(colA({20, 16, 10}));
        rotor.setOutlineThickness(2.0f);
        target.draw(rotor);

        sf::RectangleShape fwd({12.0f, 8.0f});
        fwd.setOrigin({6.0f, 4.0f});
        fwd.setPosition(topC + sf::Vector2f{fdx * 19.0f, fdy * 19.0f});
        fwd.setRotation(sf::degrees(dirDeg));
        fwd.setFillColor(colA({105, 240, 175}));
        target.draw(fwd);
        return;
    }

    float dirDeg = static_cast<float>(b.dir) * 90.0f;
    sf::Color railCol{115, 128, 148};
    sf::Color chevronCol{242, 192, 68};
    float speedAnim = 2.8f;
    if (b.type == BuildingType::FastBelt) {
        railCol = {196, 78, 72};
        chevronCol = {255, 108, 86};
        speedAnim = 5.5f;
    } else if (b.type == BuildingType::ExpressBelt) {
        railCol = {62, 168, 224};
        chevronCol = {98, 242, 255};
        speedAnim = 9.5f;
    } else if (b.type == BuildingType::UndergroundBelt) {
        railCol = {178, 142, 82};
        chevronCol = {110, 236, 255};
        speedAnim = 5.0f;
    }

    // 2.5D steel belt frame skirt underneath raised rollers
    if (settings.mode25D && lift > 1.0f) {
        sf::RectangleShape skirt({(b.dir % 2 == 0) ? CELL_SIZE : 40.0f, (b.dir % 2 == 0) ? (38.0f + lift) : CELL_SIZE});
        skirt.setOrigin({skirt.getSize().x * 0.5f, skirt.getSize().y * 0.5f});
        skirt.setPosition(baseCenter - sf::Vector2f{0.0f, lift * 0.5f});
        skirt.setFillColor(colA(darkenColor(railCol, 0.42f)));
        target.draw(skirt);
    }

    if (!isGhost) {
        stampMaterialRect(
            matTarget, center, {CELL_SIZE, 40.0f}, 128, 128, 130,
            pxHeightToByte(lift), dirDeg
        );
    }

    sf::RectangleShape bed({CELL_SIZE, 38.0f});
    bed.setOrigin({CELL_SIZE * 0.5f, 19.0f});
    bed.setPosition(center);
    bed.setRotation(sf::degrees(dirDeg));
    bed.setFillColor(colA({26, 30, 38}));
    target.draw(bed);

    for (float sy : {-17.5f, 17.5f}) {
        sf::RectangleShape rail({CELL_SIZE, 5.0f});
        rail.setPosition(center);
        rail.setRotation(sf::degrees(dirDeg));
        rail.setOrigin({CELL_SIZE * 0.5f, 2.5f - sy});
        rail.setFillColor(colA(railCol));
        target.draw(rail);
    }

    float scroll = std::fmod(animTime * speedAnim, 1.0f);
    int fdx = 0, fdy = 0;
    World::directionVector(b.dir, fdx, fdy);
    sf::Vector2f fwd{static_cast<float>(fdx), static_cast<float>(fdy)};
    sf::Vector2f side{-fwd.y, fwd.x};

    for (int k = 0; k < 2; ++k) {
        float u = std::fmod((static_cast<float>(k) * 0.5f) + scroll, 1.0f) - 0.5f;
        sf::Vector2f tip = center + fwd * (u * (CELL_SIZE - 10.0f) + 5.0f);
        sf::Vector2f wL = tip - fwd * 8.0f + side * 9.5f;
        sf::Vector2f wR = tip - fwd * 8.0f - side * 9.5f;
        drawThickLine(target, wL, tip, 4.0f, colA(chevronCol));
        drawThickLine(target, wR, tip, 4.0f, colA(chevronCol));
    }

    if (!isGhost && b.type == BuildingType::ExpressBelt) {
        drawEmissiveGlow(emisTarget, center, 24.0f, {72, 224, 255}, 0.16f);
    }

    if (b.type == BuildingType::UndergroundBelt) {
        float hoodLift = settings.mode25D ? (12.0f * settings.extrusionDepth) : 0.0f;
        sf::RectangleShape hood({34.0f, 46.0f});
        hood.setOrigin({12.0f, 23.0f});
        hood.setPosition(center - sf::Vector2f{0.0f, hoodLift * 0.5f});
        hood.setRotation(sf::degrees(dirDeg));
        hood.setFillColor(colA({52, 58, 72}));
        hood.setOutlineColor(colA({228, 182, 82}));
        hood.setOutlineThickness(3.0f);
        target.draw(hood);

        sf::RectangleShape portal({12.0f, 34.0f});
        portal.setOrigin({-6.0f, 17.0f});
        portal.setPosition(center - sf::Vector2f{0.0f, hoodLift * 0.5f});
        portal.setRotation(sf::degrees(dirDeg));
        portal.setFillColor(colA({10, 14, 20}));
        target.draw(portal);

        if (!isGhost) {
            drawEmissiveGlow(emisTarget, center, 26.0f, {98, 232, 255}, 0.24f);
        }
    }
}

void Renderer::drawResourceItem(
    sf::RenderTarget& target,
    sf::RenderTarget* emisTarget,
    ResourceType type,
    sf::Vector2f pos,
    bool mode25D
) const {
    if (type == ResourceType::Count) return;

    sf::Color fill{180, 195, 220};
    sf::Color hi{235, 245, 255};
    sf::Color border{14, 18, 24};

    if (mode25D) {
        sf::RectangleShape dropShadow({16.0f, 12.0f});
        dropShadow.setOrigin({8.0f, 6.0f});
        dropShadow.setPosition(pos + sf::Vector2f{1.5f, 3.5f});
        dropShadow.setFillColor(sf::Color(6, 8, 14, 125));
        target.draw(dropShadow);
    }

    switch (type) {
    case ResourceType::Iron:
        fill = {138, 168, 206};
        hi   = {215, 234, 255};
        break;
    case ResourceType::Copper:
        fill = {218, 118, 56};
        hi   = {255, 196, 118};
        break;
    case ResourceType::Coal:
        fill = {36, 40, 50};
        hi   = {86, 92, 110};
        break;
    case ResourceType::Titanium:
        fill = {148, 122, 232};
        hi   = {228, 214, 255};
        break;
    case ResourceType::Uranium:
        fill = {68, 215, 64};
        hi   = {195, 255, 148};
        drawEmissiveGlow(emisTarget, pos, 22.0f, {82, 250, 92}, 0.45f);
        break;
    case ResourceType::Aether:
        fill = {52, 212, 248};
        hi   = {205, 255, 255};
        drawEmissiveGlow(emisTarget, pos, 24.0f, {72, 238, 255}, 0.50f);
        break;
    case ResourceType::Steel: {
        sf::RectangleShape bar({18.0f, 12.0f});
        bar.setOrigin({9.0f, 6.0f});
        bar.setPosition(pos);
        bar.setFillColor({192, 206, 226});
        bar.setOutlineColor(border);
        bar.setOutlineThickness(2.0f);
        target.draw(bar);
        sf::RectangleShape groove({12.0f, 4.0f});
        groove.setOrigin({6.0f, 2.0f});
        groove.setPosition(pos);
        groove.setFillColor({96, 110, 132});
        target.draw(groove);
        return;
    }
    case ResourceType::Circuit: {
        sf::RectangleShape pcb({16.0f, 16.0f});
        pcb.setOrigin({8.0f, 8.0f});
        pcb.setPosition(pos);
        pcb.setFillColor({38, 168, 92});
        pcb.setOutlineColor(border);
        pcb.setOutlineThickness(2.0f);
        target.draw(pcb);
        sf::RectangleShape trace({10.0f, 4.0f});
        trace.setOrigin({5.0f, 2.0f});
        trace.setPosition(pos);
        trace.setFillColor({250, 210, 82});
        target.draw(trace);
        sf::RectangleShape trace2({4.0f, 10.0f});
        trace2.setOrigin({2.0f, 5.0f});
        trace2.setPosition(pos);
        trace2.setFillColor({250, 210, 82});
        target.draw(trace2);
        drawEmissiveGlow(emisTarget, pos, 16.0f, {92, 240, 152}, 0.22f);
        return;
    }
    case ResourceType::QuantumCore: {
        sf::RectangleShape qOuter({17.0f, 17.0f});
        qOuter.setOrigin({8.5f, 8.5f});
        qOuter.setPosition(pos);
        qOuter.setRotation(sf::degrees(45.0f));
        qOuter.setFillColor({216, 86, 255});
        qOuter.setOutlineColor({18, 12, 28});
        qOuter.setOutlineThickness(2.0f);
        target.draw(qOuter);
        sf::RectangleShape qInner({8.0f, 8.0f});
        qInner.setOrigin({4.0f, 4.0f});
        qInner.setPosition(pos);
        qInner.setFillColor({125, 252, 255});
        target.draw(qInner);
        drawEmissiveGlow(emisTarget, pos, 28.0f, {225, 105, 255}, 0.58f);
        return;
    }
    default:
        break;
    }

    if (mode25D) {
        sf::RectangleShape side({14.0f, 5.0f});
        side.setOrigin({7.0f, 0.0f});
        side.setPosition(pos + sf::Vector2f{0.0f, 4.0f});
        side.setFillColor(darkenColor(fill, 0.58f));
        target.draw(side);
    }

    sf::RectangleShape chunk({14.0f, 14.0f});
    chunk.setOrigin({7.0f, 7.0f});
    chunk.setPosition(pos);
    chunk.setFillColor(fill);
    chunk.setOutlineColor(border);
    chunk.setOutlineThickness(2.0f);
    target.draw(chunk);

    sf::RectangleShape glint({5.0f, 5.0f});
    glint.setPosition(pos + sf::Vector2f{-4.5f, -4.5f});
    glint.setFillColor(hi);
    target.draw(glint);
}

void Renderer::drawBeltItems(
    sf::RenderTarget& target,
    sf::RenderTarget* /*matTarget*/,
    sf::RenderTarget* emisTarget,
    const BuildingEntity& b,
    const GameSettings& settings
) const {
    if (b.beltItemA == ResourceType::Count && b.beltItemB == ResourceType::Count) {
        return;
    }
    const float lift = settings.mode25D ? (8.5f * settings.extrusionDepth) : 0.0f;
    const sf::Vector2f center{(b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE - lift};
    int fdx = 0, fdy = 0;
    World::directionVector(b.dir, fdx, fdy);
    sf::Vector2f fwd{static_cast<float>(fdx), static_cast<float>(fdy)};

    if (b.beltItemB != ResourceType::Count) {
        float u = std::clamp(b.beltProgB, 0.0f, 1.0f) - 0.5f;
        drawResourceItem(target, emisTarget, b.beltItemB, center + fwd * (u * CELL_SIZE), settings.mode25D);
    }
    if (b.beltItemA != ResourceType::Count) {
        float u = std::clamp(b.beltProgA, 0.0f, 1.0f) - 0.5f;
        drawResourceItem(target, emisTarget, b.beltItemA, center + fwd * (u * CELL_SIZE), settings.mode25D);
    }
}

void Renderer::drawRailBuilding(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    sf::RenderTarget* emisTarget,
    const World* world,
    const BuildingEntity& b,
    const GameSettings& settings,
    float /*animTime*/,
    bool isGhost,
    bool ghostValid
) const {
    const sf::Vector2f center{(b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE};
    float alphaMul = isGhost ? 0.62f : 1.0f;
    auto colA = [&](sf::Color c) {
        if (isGhost && !ghostValid) {
            c.r = static_cast<std::uint8_t>(std::min(255, c.r + 80));
            c.g = static_cast<std::uint8_t>(c.g * 0.45f);
            c.b = static_cast<std::uint8_t>(c.b * 0.45f);
        }
        return scaleAlpha(c, alphaMul);
    };

    bool connE = isRailOrStation(world, b.gx + 1, b.gy);
    bool connW = isRailOrStation(world, b.gx - 1, b.gy);
    bool connN = isRailOrStation(world, b.gx, b.gy - 1);
    bool connS = isRailOrStation(world, b.gx, b.gy + 1);
    if (!connE && !connW && !connN && !connS) {
        connE = connW = true;
    }

    auto drawArm = [&](float dx, float dy) {
        sf::Vector2f end = center + sf::Vector2f{dx * (CELL_SIZE * 0.5f), dy * (CELL_SIZE * 0.5f)};
        drawThickLine(target, center - sf::Vector2f{dx * 8.f, dy * 8.f}, end, 38.f, colA({26, 30, 38}));
        if (std::abs(dx) > 0.1f) {
            for (int k = 1; k <= 3; ++k) {
                float ox = dx * static_cast<float>(k) * 10.0f;
                sf::RectangleShape tie({6.0f, 34.0f});
                tie.setOrigin({3.0f, 17.0f});
                tie.setPosition(center + sf::Vector2f{ox, 0.f});
                tie.setFillColor(colA({92, 62, 42}));
                tie.setOutlineColor(colA({24, 16, 12}));
                tie.setOutlineThickness(1.5f);
                target.draw(tie);
            }
            drawThickLine(target, center + sf::Vector2f{0.f, -11.f}, end + sf::Vector2f{0.f, -11.f}, 5.f, colA({176, 192, 214}));
            drawThickLine(target, center + sf::Vector2f{0.f, 11.f}, end + sf::Vector2f{0.f, 11.f}, 5.f, colA({176, 192, 214}));
        } else {
            for (int k = 1; k <= 3; ++k) {
                float oy = dy * static_cast<float>(k) * 10.0f;
                sf::RectangleShape tie({34.0f, 6.0f});
                tie.setOrigin({17.0f, 3.0f});
                tie.setPosition(center + sf::Vector2f{0.f, oy});
                tie.setFillColor(colA({92, 62, 42}));
                tie.setOutlineColor(colA({24, 16, 12}));
                tie.setOutlineThickness(1.5f);
                target.draw(tie);
            }
            drawThickLine(target, center + sf::Vector2f{-11.f, 0.f}, end + sf::Vector2f{-11.f, 0.f}, 5.f, colA({176, 192, 214}));
            drawThickLine(target, center + sf::Vector2f{11.f, 0.f}, end + sf::Vector2f{11.f, 0.f}, 5.f, colA({176, 192, 214}));
        }
    };

    sf::RectangleShape midTie({26.f, 26.f});
    midTie.setOrigin({13.f, 13.f});
    midTie.setPosition(center);
    midTie.setFillColor(colA({78, 56, 38}));
    midTie.setOutlineColor(colA({158, 174, 198}));
    midTie.setOutlineThickness(3.f);
    target.draw(midTie);

    if (connE) drawArm(1.f, 0.f);
    if (connW) drawArm(-1.f, 0.f);
    if (connN) drawArm(0.f, -1.f);
    if (connS) drawArm(0.f, 1.f);

    if (!isGhost) {
        stampMaterialRect(matTarget, center, {52.0f, 44.0f}, 128, 128, 185, heightToByte(0.18f));
    }

    if (b.type == BuildingType::TrainStation) {
        bool isUnload = (b.dir % 2 == 1);
        sf::Color accent = isUnload ? sf::Color(86, 232, 162) : sf::Color(246, 168, 66);
        float pLift = settings.mode25D ? (14.0f * settings.extrusionDepth) : 0.0f;

        sf::Vector2f platCenter = drawChassis25D(
            target, isGhost ? nullptr : matTarget,
            center + sf::Vector2f{0.f, -21.f},
            56.0f, 16.0f, pLift,
            colA({56, 64, 80}), colA(accent), 145, settings.mode25D, isGhost
        );

        sf::RectangleShape banner({18.f, 8.f});
        banner.setOrigin({9.f, 4.f});
        banner.setPosition(platCenter);
        banner.setFillColor(colA(accent));
        target.draw(banner);

        sf::RectangleShape dock({54.f, 12.f});
        dock.setOrigin({27.f, 6.f});
        dock.setPosition(center + sf::Vector2f{0.f, 22.f});
        dock.setFillColor(colA({36, 42, 54}));
        dock.setOutlineColor(colA({120, 135, 160}));
        dock.setOutlineThickness(2.0f);
        target.draw(dock);

        if (!isGhost) {
            drawEmissiveGlow(emisTarget, platCenter, 36.0f, accent, 0.42f);
        }

        if (!isGhost && world) {
            double fillRatio = std::clamp(
                b.localStorage.totalAmount() / std::max(1.0, world->stationBufferCapacity()), 0.0, 1.0);
            if (fillRatio > 0.01) {
                sf::RectangleShape bar({static_cast<float>(48.0 * fillRatio), 6.f});
                bar.setPosition(center + sf::Vector2f{-24.f, 19.f});
                bar.setFillColor(accent);
                target.draw(bar);
            }
            if (b.electricPowered) {
                sf::RectangleShape elecDot({8.f, 8.f});
                elecDot.setOrigin({4.f, 4.f});
                elecDot.setPosition(platCenter + sf::Vector2f{20.f, 0.f});
                elecDot.setFillColor(sf::Color(92, 238, 255));
                target.draw(elecDot);
                drawEmissiveGlow(emisTarget, platCenter + sf::Vector2f{20.f, 0.f}, 24.0f, {92, 238, 255}, 0.50f);
            }
        }
    }
}

void Renderer::drawBuilding(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    sf::RenderTarget* emisTarget,
    const BuildingEntity& b,
    const GameSettings& settings,
    float animTime,
    bool isGhost,
    bool ghostValid,
    const World* world
) const {
    const sf::Vector2f baseCenter{(b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE};
    const bool mode25D = settings.mode25D;
    const float ext = mode25D ? settings.extrusionDepth : 0.0f;
    const BuildingDef& def = Catalog::getBuilding(b.type);
    const float h25 = def.height25D * ext;

    auto drawMesh = [&](const sf::VertexArray& va, sf::Vector2f pos, float angleRad, float scale = 1.0f) {
        sf::RenderStates st;
        st.transform.translate(pos);
        st.transform.rotate(sf::radians(angleRad));
        if (scale != 1.0f) {
            st.transform.scale({scale, scale});
        }
        if (isGhost) {
            sf::VertexArray copy = va;
            for (std::size_t i = 0; i < copy.getVertexCount(); ++i) {
                copy[i].color = scaleAlpha(copy[i].color, 0.58f);
                if (!ghostValid) {
                    copy[i].color.r = static_cast<std::uint8_t>(std::min(255, copy[i].color.r + 95));
                    copy[i].color.g = static_cast<std::uint8_t>(copy[i].color.g * 0.4f);
                    copy[i].color.b = static_cast<std::uint8_t>(copy[i].color.b * 0.4f);
                }
            }
            target.draw(copy, st);
        } else {
            target.draw(va, st);
        }
    };

    // Helper for 2.5D stacked cylindrical gears
    auto drawGear3D = [&](const sf::VertexArray& topGear, const sf::VertexArray& sideGear,
                          sf::Vector2f gBase, float angleRad, float gearLift, float radiusPx, float scale = 1.0f) -> sf::Vector2f {
        if (!mode25D || gearLift < 1.0f) {
            if (!isGhost) {
                drawMesh(
                    (&topGear == &brassGear36_) ? shadowGear36_ : shadowCog12_,
                    gBase + sf::Vector2f{4.f, 4.f}, angleRad, scale
                );
                stampMaterialCircle(matTarget, gBase, radiusPx * scale, 128, 128, 195, pxHeightToByte(10.0f));
            }
            drawMesh(topGear, gBase, angleRad, scale);
            return gBase;
        }

        if (!isGhost) {
            drawMesh(
                (&topGear == &brassGear36_) ? shadowGear36_ : shadowCog12_,
                gBase + sf::Vector2f{3.f, 4.f}, angleRad, scale
            );
            // Axle pedestal on ground
            sf::CircleShape ped(10.0f * scale, 10);
            ped.setOrigin({10.0f * scale, 10.0f * scale});
            ped.setPosition(gBase);
            ped.setFillColor(sf::Color(28, 34, 46));
            target.draw(ped);
        }

        int layers = std::clamp(static_cast<int>(gearLift / 3.5f), 2, 5);
        for (int i = 1; i < layers; ++i) {
            float ly = gearLift * (static_cast<float>(i) / static_cast<float>(layers));
            drawMesh(sideGear, gBase - sf::Vector2f{0.0f, ly}, angleRad, scale);
        }

        sf::Vector2f topPos = gBase - sf::Vector2f{0.0f, gearLift};
        drawMesh(topGear, topPos, angleRad, scale);

        if (!isGhost) {
            stampMaterialCircle(matTarget, topPos, radiusPx * scale, 128, 128, 210, pxHeightToByte(gearLift));
        }
        return topPos;
    };

    float alphaMul = isGhost ? 0.58f : 1.0f;
    auto colA = [&](sf::Color c) {
        if (isGhost && !ghostValid) {
            c.r = static_cast<std::uint8_t>(std::min(255, c.r + 70));
            c.g = static_cast<std::uint8_t>(c.g * 0.5f);
            c.b = static_cast<std::uint8_t>(c.b * 0.5f);
        }
        return scaleAlpha(c, alphaMul);
    };

    switch (b.type) {
    case BuildingType::HubCore: {
        // Monumental 3x3 Stepped Grand Clockwork Core & Orbital Spire
        float tier1H = mode25D ? (22.0f * ext) : 0.0f;
        float tier2H = mode25D ? (42.0f * ext) : 0.0f;

        sf::Vector2f t1Center = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            CELL_SIZE * 2.86f, CELL_SIZE * 2.86f, tier1H,
            colA({32, 40, 54}), colA({228, 184, 86}), 165, mode25D, isGhost
        );

        // 4 cardinal belt intake chutes
        sf::RectangleShape chuteH({CELL_SIZE * 2.94f, 36.0f});
        chuteH.setOrigin({CELL_SIZE * 1.47f, 18.0f});
        chuteH.setPosition(t1Center);
        chuteH.setFillColor(colA({24, 30, 42}));
        target.draw(chuteH);

        sf::RectangleShape chuteV({36.0f, CELL_SIZE * 2.94f});
        chuteV.setOrigin({18.0f, CELL_SIZE * 1.47f});
        chuteV.setPosition(t1Center);
        chuteV.setFillColor(colA({24, 30, 42}));
        target.draw(chuteV);

        for (int sy : {-1, 1}) {
            for (int sx : {-1, 1}) {
                sf::Vector2f bPos = baseCenter + sf::Vector2f{sx * 64.f, sy * 64.f};
                sf::Vector2f bTop = drawChassis25D(
                    target, isGhost ? nullptr : matTarget, bPos,
                    42.f, 42.f, mode25D ? (32.0f * ext) : 0.0f,
                    colA({56, 66, 84}), colA({218, 172, 74}), 180, mode25D, isGhost
                );
                if (!isGhost) {
                    drawEmissiveGlow(emisTarget, bTop, 32.0f, {255, 192, 86}, 0.42f);
                }
            }
        }

        // Upper ziggurat clockwork deck
        sf::Vector2f spireTop = t1Center;
        if (mode25D) {
            spireTop = drawChassis25D(
                target, isGhost ? nullptr : matTarget, baseCenter,
                114.0f, 114.0f, tier2H,
                colA({38, 48, 66}), colA({245, 202, 96}), 195, true, isGhost
            );
        }

        drawMesh(brassGear36_, spireTop, animTime * 0.45f, 0.72f);
        drawMesh(copperCog12_, spireTop, -animTime * 1.35f, 1.15f);

        int phase = world ? world->spirePhase() : 0;
        int prestige = world ? world->prestigeCount() : 0;
        float pulse = 0.86f + 0.14f * std::sin(animTime * 3.8f);

        if (phase >= 1) {
            drawPixelCircleRing(target, spireTop, 48.0f, 4.5f, colA({92, 232, 255}), 36);
        }
        if (phase >= 3) {
            drawPixelCircleRing(target, spireTop, 68.0f, 4.0f, colA({248, 204, 88}), 48);
        }
        if (phase >= 5 || prestige > 0) {
            drawPixelCircleRing(target, spireTop, 82.0f, 5.0f, colA({222, 105, 255}), 56);
        }

        float coreR = (22.0f + static_cast<float>(phase) * 2.5f) * pulse;
        sf::CircleShape jewel(coreR, 12);
        jewel.setOrigin({coreR, coreR});
        jewel.setPosition(spireTop);
        sf::Color jCol = (phase >= 4) ? sf::Color(218, 110, 255) : (phase >= 2 ? sf::Color(92, 242, 255) : sf::Color(252, 208, 96));
        jewel.setFillColor(colA(jCol));
        jewel.setOutlineColor(colA({255, 252, 235}));
        jewel.setOutlineThickness(3.0f);
        target.draw(jewel);

        if (!isGhost) {
            drawEmissiveGlow(emisTarget, spireTop, 138.0f, jCol, 0.92f * pulse);
        }
        break;
    }
    case BuildingType::SmallCog: {
        drawGear3D(ironCog12_, darkCollar12_, baseCenter, b.angle, h25, 32.0f);
        break;
    }
    case BuildingType::LargeGear: {
        drawGear3D(brassGear36_, darkGear36_, baseCenter, b.angle, h25, 94.0f);
        break;
    }
    case BuildingType::SpeedGearbox: {
        int fdx = 0, fdy = 0;
        World::directionVector(b.dir, fdx, fdy);
        sf::Vector2f fVec{static_cast<float>(fdx), static_cast<float>(fdy)};
        float gLift = mode25D ? (10.0f * ext) : 0.0f;
        drawMesh(darkCollar12_, baseCenter - fVec * 12.0f - sf::Vector2f{0.f, gLift}, b.angle, 0.72f);
        drawMesh(copperCog12_, baseCenter + fVec * 12.0f - sf::Vector2f{0.f, gLift}, b.angleSecondary, 0.72f);

        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            40.0f, 38.0f, h25,
            colA({52, 60, 74}), colA({224, 178, 78}), 165, mode25D, isGhost
        );

        float dirDeg = static_cast<float>(b.dir) * 90.0f;
        sf::Color arrowCol = colA(b.status == NetworkStatus::Running ? sf::Color(100, 236, 165) : sf::Color(225, 190, 95));
        sf::RectangleShape stem({10.0f, 10.0f});
        stem.setOrigin({5.0f, 5.0f});
        stem.setPosition(topC - fVec * 4.0f);
        stem.setRotation(sf::degrees(dirDeg));
        stem.setFillColor(arrowCol);
        target.draw(stem);

        sf::RectangleShape tip({8.0f, 18.0f});
        tip.setOrigin({4.0f, 9.0f});
        tip.setPosition(topC + fVec * 5.0f);
        tip.setRotation(sf::degrees(dirDeg));
        tip.setFillColor(arrowCol);
        target.draw(tip);
        break;
    }
    case BuildingType::AxleBridge: {
        sf::Vector2f lowC = baseCenter - sf::Vector2f{0.f, mode25D ? (6.0f * ext) : 0.f};
        drawMesh(darkCollar12_, lowC, b.angleSecondary, 0.92f);
        sf::RectangleShape vBar({14.0f, 58.0f});
        vBar.setOrigin({7.0f, 29.0f});
        vBar.setPosition(lowC);
        vBar.setFillColor(colA(sf::Color(62, 72, 90)));
        vBar.setOutlineColor(colA(sf::Color(18, 22, 30)));
        vBar.setOutlineThickness(2.5f);
        target.draw(vBar);

        sf::Vector2f hiC = baseCenter - sf::Vector2f{0.f, h25};
        drawMesh(copperCog12_, hiC, b.angle, 0.88f);
        sf::RectangleShape hBar({58.0f, 16.0f});
        hBar.setOrigin({29.0f, 8.0f});
        hBar.setPosition(hiC);
        hBar.setFillColor(colA(sf::Color(196, 126, 66)));
        hBar.setOutlineColor(colA(sf::Color(24, 16, 10)));
        hBar.setOutlineThickness(2.5f);
        target.draw(hBar);

        sf::RectangleShape cap({10.0f, 10.0f});
        cap.setOrigin({5.0f, 5.0f});
        cap.setPosition(hiC);
        cap.setFillColor(colA(sf::Color(245, 206, 118)));
        target.draw(cap);
        if (!isGhost) stampMaterialRect(matTarget, hiC, {56.f, 24.f}, 128, 128, 175, pxHeightToByte(h25));
        break;
    }
    case BuildingType::HandCrank: {
        sf::Vector2f topC = drawGear3D(darkCollar12_, darkCollar12_, baseCenter, b.angle, h25 * 0.7f, 32.0f);
        if (!isGhost && b.crankBoostTimer > 0.0f) {
            sf::RectangleShape boostGlow({46.0f, 46.0f});
            boostGlow.setOrigin({23.0f, 23.0f});
            boostGlow.setPosition(topC);
            boostGlow.setFillColor(sf::Color(255, 200, 75, 45));
            boostGlow.setOutlineColor(sf::Color(255, 215, 95));
            boostGlow.setOutlineThickness(3.0f);
            target.draw(boostGlow);
            drawEmissiveGlow(emisTarget, topC, 42.0f, {255, 205, 86}, 0.55f);
        }

        sf::RectangleShape arm({24.0f, 7.0f});
        arm.setOrigin({3.0f, 3.5f});
        arm.setPosition(topC);
        arm.setRotation(sf::radians(b.angle));
        arm.setFillColor(colA(sf::Color(232, 190, 92)));
        arm.setOutlineColor(colA(sf::Color(20, 16, 10)));
        arm.setOutlineThickness(2.0f);
        target.draw(arm);

        sf::Vector2f knobPos = topC + polar(20.0f, b.angle);
        sf::RectangleShape knob({11.0f, 11.0f});
        knob.setOrigin({5.5f, 5.5f});
        knob.setPosition(knobPos - sf::Vector2f{0.f, mode25D ? (4.0f * ext) : 0.f});
        knob.setFillColor(colA(sf::Color(208, 72, 52)));
        knob.setOutlineColor(colA(sf::Color(24, 14, 12)));
        knob.setOutlineThickness(2.0f);
        target.draw(knob);
        break;
    }
    case BuildingType::WindTurbine: {
        drawGear3D(ironCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (10.0f * ext) : 0.f, 32.0f);

        sf::Vector2f rotorCenter = baseCenter - sf::Vector2f{0.0f, h25};
        if (mode25D) {
            // Tall steel lattice tower rising from base cog to elevated wind rotor
            drawThickLine(target, baseCenter + sf::Vector2f{-7.f, 4.f}, rotorCenter, 5.0f, colA({84, 96, 118}));
            drawThickLine(target, baseCenter + sf::Vector2f{7.f, 4.f}, rotorCenter, 5.0f, colA({64, 74, 94}));
        }

        for (int s = 0; s < 4; ++s) {
            float bladeAng = b.angle + static_cast<float>(s) * (PI * 0.5f);
            sf::RectangleShape sail({26.0f, 9.5f});
            sail.setOrigin({-4.0f, 4.75f});
            sail.setPosition(rotorCenter);
            sail.setRotation(sf::radians(bladeAng));
            sail.setFillColor(colA(sf::Color(238, 228, 200)));
            sail.setOutlineColor(colA(sf::Color(52, 42, 28)));
            sail.setOutlineThickness(2.0f);
            target.draw(sail);
        }

        sf::RectangleShape nose({12.0f, 12.0f});
        nose.setOrigin({6.0f, 6.0f});
        nose.setPosition(rotorCenter);
        nose.setFillColor(colA(sf::Color(196, 152, 70)));
        nose.setOutlineColor(colA(sf::Color(24, 18, 10)));
        nose.setOutlineThickness(2.0f);
        target.draw(nose);

        if (!isGhost) {
            stampMaterialCircle(matTarget, rotorCenter, 24.0f, 128, 128, 120, pxHeightToByte(h25));
        }
        break;
    }
    case BuildingType::SteamEngine: {
        drawGear3D(copperCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (10.0f * ext) : 0.f, 32.0f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            38.0f, 34.0f, h25,
            colA({48, 54, 66}), colA({214, 152, 74}), 175, mode25D, isGhost
        );

        sf::RectangleShape firebox({14.0f, 14.0f});
        firebox.setOrigin({7.0f, 7.0f});
        firebox.setPosition(topC + sf::Vector2f{-7.0f, 0.0f});
        firebox.setFillColor(colA(b.hasFuel ? sf::Color(255, 142, 42) : sf::Color(68, 54, 54)));
        firebox.setOutlineColor(colA(sf::Color(20, 14, 10)));
        firebox.setOutlineThickness(2.0f);
        target.draw(firebox);

        float stroke = std::round(std::sin(b.pistonPhase) * 2.0f) * 3.0f;
        sf::RectangleShape piston({11.0f, 11.0f});
        piston.setOrigin({5.5f, 5.5f});
        piston.setPosition(topC + sf::Vector2f{8.0f, stroke});
        piston.setFillColor(colA(sf::Color(238, 194, 92)));
        piston.setOutlineColor(colA(sf::Color(22, 16, 10)));
        piston.setOutlineThickness(2.0f);
        target.draw(piston);

        if (!isGhost && b.hasFuel) {
            drawEmissiveGlow(emisTarget, topC + sf::Vector2f{-7.f, 4.f}, 54.0f, {255, 140, 46}, 0.68f);
        }
        break;
    }
    case BuildingType::SolarPanel: {
        float sun = world ? world->solarFactor() : 1.0f;
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            54.0f, 54.0f, h25,
            colA({128, 142, 164}), colA({24, 30, 40}), 220, mode25D, isGhost
        );
        // Tilted photovoltaic normal (tilted slightly South-West to catch the sun!)
        if (!isGhost) {
            stampMaterialRect(matTarget, topC, {50.f, 50.f}, 116, 152, 225, pxHeightToByte(h25));
        }

        sf::Color cellCol(
            static_cast<std::uint8_t>(22 + 34 * sun),
            static_cast<std::uint8_t>(56 + 78 * sun),
            static_cast<std::uint8_t>(118 + 115 * sun)
        );
        for (int cy = -1; cy <= 1; ++cy) {
            for (int cx = -1; cx <= 1; ++cx) {
                sf::RectangleShape cell({13.0f, 13.0f});
                cell.setOrigin({6.5f, 6.5f});
                cell.setPosition(topC + sf::Vector2f{cx * 15.0f, cy * 15.0f});
                cell.setFillColor(colA(cellCol));
                target.draw(cell);
            }
        }
        if (sun > 0.35f) {
            sf::RectangleShape led({8.0f, 8.0f});
            led.setOrigin({4.0f, 4.0f});
            led.setPosition(topC);
            led.setFillColor(colA({255, 224, 98}));
            target.draw(led);
            if (!isGhost) drawEmissiveGlow(emisTarget, topC, 22.0f, {255, 224, 98}, 0.25f * sun);
        }
        break;
    }
    case BuildingType::Accumulator: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            48.0f, 50.0f, h25,
            colA({40, 48, 62}), colA({158, 178, 204}), 175, mode25D, isGhost
        );

        for (int sx : {-1, 1}) {
            sf::RectangleShape post({10.0f, 10.0f});
            post.setOrigin({5.0f, 5.0f});
            post.setPosition(topC + sf::Vector2f{sx * 12.0f, -16.0f});
            post.setFillColor(colA({228, 152, 74}));
            target.draw(post);
        }

        float charge = isGhost ? 0.7f : std::clamp(b.batteryCharge, 0.0f, 1.0f);
        sf::RectangleShape slot({28.0f, 22.0f});
        slot.setOrigin({14.0f, 11.0f});
        slot.setPosition(topC + sf::Vector2f{0.0f, 6.0f});
        slot.setFillColor(colA({18, 24, 34}));
        target.draw(slot);

        if (charge > 0.02f) {
            sf::Color cCol = (charge > 0.35f) ? sf::Color(86, 242, 205) : sf::Color(248, 182, 68);
            sf::RectangleShape bar({24.0f * charge, 18.0f});
            bar.setPosition(topC + sf::Vector2f{-12.0f, -3.0f});
            bar.setFillColor(colA(cCol));
            target.draw(bar);
            if (!isGhost) {
                drawEmissiveGlow(emisTarget, topC + sf::Vector2f{0.f, 6.f}, 42.0f, cCol, 0.48f * charge);
            }
        }
        break;
    }
    case BuildingType::AetherDynamo: {
        drawGear3D(copperCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (10.0f * ext) : 0.f, 32.0f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            38.0f, 38.0f, h25,
            colA({22, 40, 56}), colA({88, 228, 255}), 190, mode25D, isGhost
        );

        float pulse = 1.0f + 0.15f * std::sin(animTime * 4.5f);
        float cSz = std::round(16.0f * pulse);
        sf::RectangleShape core({cSz, cSz});
        core.setOrigin({cSz * 0.5f, cSz * 0.5f});
        core.setPosition(topC);
        core.setFillColor(colA(b.hasFuel ? sf::Color(115, 248, 255) : sf::Color(65, 85, 95)));
        target.draw(core);
        if (!isGhost && b.hasFuel) {
            drawEmissiveGlow(emisTarget, topC, 68.0f, {86, 238, 255}, 0.80f);
        }
        break;
    }
    case BuildingType::IronDrill:
    case BuildingType::HeavyDrill:
    case BuildingType::AetherExtractor:
    case BuildingType::ElectricDrill: {
        bool isElec = (b.type == BuildingType::ElectricDrill);
        if (!isElec) {
            drawGear3D(
                (b.type == BuildingType::IronDrill) ? darkCollar12_ : copperCog12_,
                darkCollar12_, baseCenter, b.angle, mode25D ? (9.0f * ext) : 0.f, 32.0f
            );
        }

        sf::Color frameCol(52, 58, 72);
        sf::Color trimCol(186, 198, 218);
        sf::Color bitCol(222, 232, 245);
        if (b.type == BuildingType::HeavyDrill) {
            frameCol = sf::Color(68, 50, 42);
            trimCol = sf::Color(238, 166, 72);
            bitCol = sf::Color(250, 206, 112);
        } else if (b.type == BuildingType::AetherExtractor) {
            frameCol = sf::Color(22, 46, 62);
            trimCol = sf::Color(92, 236, 255);
            bitCol = sf::Color(165, 252, 255);
        } else if (isElec) {
            frameCol = sf::Color(24, 48, 66);
            trimCol = b.electricPowered ? sf::Color(86, 236, 255) : sf::Color(92, 130, 150);
            bitCol = sf::Color(200, 246, 255);
        }

        float fSz = isElec ? 42.0f : 34.0f;
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            fSz, fSz, h25,
            colA(frameCol), colA(trimCol), 170, mode25D, isGhost
        );

        if (isElec) {
            for (int side : {-1, 1}) {
                sf::RectangleShape coil({10.f, 22.f});
                coil.setOrigin({5.f, 11.f});
                coil.setPosition(topC + sf::Vector2f{side * 20.f, 0.f});
                coil.setFillColor(colA(sf::Color(196, 120, 62)));
                coil.setOutlineColor(colA(sf::Color(18, 22, 30)));
                coil.setOutlineThickness(2.0f);
                target.draw(coil);
            }
        }

        sf::RectangleShape b1({20.0f, 6.0f});
        b1.setOrigin({10.0f, 3.0f});
        b1.setPosition(topC);
        b1.setRotation(sf::radians(b.angle * 2.0f));
        b1.setFillColor(colA(bitCol));
        target.draw(b1);

        sf::RectangleShape b2({6.0f, 20.0f});
        b2.setOrigin({3.0f, 10.0f});
        b2.setPosition(topC);
        b2.setRotation(sf::radians(b.angle * 2.0f));
        b2.setFillColor(colA(bitCol));
        target.draw(b2);

        if (!isGhost) {
            if (b.type == BuildingType::AetherExtractor && b.electricPowered) {
                drawEmissiveGlow(emisTarget, topC, 66.0f, {86, 238, 255}, 0.78f);
            } else if (isElec && b.electricPowered) {
                drawEmissiveGlow(emisTarget, topC, 42.0f, {86, 220, 255}, 0.38f);
            }
        }

        if (!isGhost && b.drillProgress > 0.01f) {
            drawFilledArc(target, topC, 17.5f, 21.0f, -90.0f, 360.0f * std::clamp(b.drillProgress, 0.0f, 1.0f), trimCol);
        }
        if (!isGhost && b.lubricated) {
            sf::RectangleShape lubeDot({7.0f, 7.0f});
            lubeDot.setOrigin({3.5f, 3.5f});
            lubeDot.setPosition(topC + sf::Vector2f{14.0f, -14.0f});
            lubeDot.setFillColor(sf::Color(95, 240, 165));
            target.draw(lubeDot);
        }
        if (!isGhost && b.overclocked) {
            sf::RectangleShape oDot({7.0f, 7.0f});
            oDot.setOrigin({3.5f, 3.5f});
            oDot.setPosition(topC + sf::Vector2f{14.0f, 14.0f});
            oDot.setFillColor(sf::Color(255, 198, 72));
            target.draw(oDot);
        }
        if (!isGhost && world && b.localStorage.totalAmount() >= 1.0) {
            double ratio = std::clamp(b.localStorage.totalAmount() / std::max(1.0, world->remoteDrillBufferCapacity()), 0.0, 1.0);
            drawWorldBar(target, topC + sf::Vector2f{0.f, 21.f}, 32.f, 5.f, static_cast<float>(ratio), sf::Color(246, 178, 72));
        }
        break;
    }
    case BuildingType::WaterPump: {
        drawGear3D(copperCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (8.0f * ext) : 0.f, 29.0f, 0.92f);
        sf::Vector2f topC = baseCenter - sf::Vector2f{0.f, h25};

        sf::CircleShape tank(20.0f, 12);
        tank.setOrigin({20.0f, 20.0f});
        tank.setPosition(topC);
        tank.setFillColor(colA({22, 64, 108}));
        tank.setOutlineColor(colA({96, 208, 255}));
        tank.setOutlineThickness(3.0f);
        target.draw(tank);

        float spin = (b.electricPowered || b.status == NetworkStatus::Running) ? animTime * 4.5f : b.angle;
        for (int k = 0; k < 3; ++k) {
            sf::RectangleShape blade({22.0f, 5.0f});
            blade.setOrigin({11.0f, 2.5f});
            blade.setPosition(topC);
            blade.setRotation(sf::radians(spin + k * (TWO_PI / 3.0f)));
            blade.setFillColor(colA({185, 240, 255}));
            target.draw(blade);
        }
        if (!isGhost) {
            stampMaterialCircle(matTarget, topC, 21.0f, 128, 128, 165, pxHeightToByte(h25));
            drawEmissiveGlow(emisTarget, topC, 34.0f, {82, 198, 255}, 0.28f);
        }
        break;
    }
    case BuildingType::StoneSmelter: {
        drawGear3D(darkCollar12_, darkCollar12_, baseCenter, b.angle, mode25D ? (8.0f * ext) : 0.f, 30.0f, 0.96f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            42.0f, 42.0f, h25,
            colA({78, 52, 44}), colA({198, 138, 92}), 120, mode25D, isGhost
        );

        bool active = isGhost || (b.drillProgress > 0.01f) || (b.localStorage[ResourceType::Coal] >= 1.0 && b.status == NetworkStatus::Running);
        // Molten firebox viewport on 2.5D south wall as well!
        if (mode25D && h25 > 8.0f) {
            sf::RectangleShape frontArch({16.0f, std::min(12.0f, h25 * 0.55f)});
            frontArch.setOrigin({8.0f, frontArch.getSize().y});
            frontArch.setPosition({baseCenter.x, baseCenter.y + 21.0f - 2.0f});
            frontArch.setFillColor(colA(active ? sf::Color(255, 146, 42) : sf::Color(28, 20, 20)));
            target.draw(frontArch);
        }

        sf::RectangleShape hearth({20.0f, 18.0f});
        hearth.setOrigin({10.0f, 9.0f});
        hearth.setPosition(topC + sf::Vector2f{0.f, 3.f});
        hearth.setFillColor(colA(active ? sf::Color(255, 150, 48) : sf::Color(36, 28, 28)));
        hearth.setOutlineColor(colA({18, 14, 12}));
        hearth.setOutlineThickness(2.0f);
        target.draw(hearth);

        if (!isGhost && active) {
            drawEmissiveGlow(emisTarget, topC + sf::Vector2f{0.f, 6.f}, 64.0f, {255, 142, 44}, 0.78f);
        }
        if (!isGhost && b.drillProgress > 0.01f) {
            drawWorldBar(target, topC + sf::Vector2f{0.f, -15.f}, 30.f, 5.f, b.drillProgress, sf::Color(255, 188, 72));
        }
        break;
    }
    case BuildingType::ArcFurnace: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            48.0f, 48.0f, h25,
            colA({38, 44, 62}),
            colA(b.electricPowered ? sf::Color(110, 220, 255) : sf::Color(120, 135, 158)),
            185, mode25D, isGhost
        );

        bool active = isGhost || b.electricPowered;
        float pulse = 0.82f + 0.18f * std::sin(animTime * 9.0f);
        sf::CircleShape crucible(14.0f, 10);
        crucible.setOrigin({14.0f, 14.0f});
        crucible.setPosition(topC);
        crucible.setFillColor(colA(active ? sf::Color(125, static_cast<std::uint8_t>(215 * pulse), 255) : sf::Color(28, 34, 46)));
        target.draw(crucible);

        for (int sx : {-1, 1}) {
            sf::RectangleShape rod({8.0f, 22.0f});
            rod.setOrigin({4.0f, 11.0f});
            rod.setPosition(topC + sf::Vector2f{sx * 13.0f, -6.0f});
            rod.setFillColor(colA({210, 145, 78}));
            target.draw(rod);
        }
        if (!isGhost && b.electricPowered) {
            float boost = (b.drillProgress > 0.01f) ? 0.92f : 0.48f;
            drawEmissiveGlow(emisTarget, topC, 76.0f, {92, 215, 255}, boost * pulse);
        }
        if (!isGhost && b.drillProgress > 0.01f) {
            drawWorldBar(target, topC + sf::Vector2f{0.f, 18.f}, 34.f, 5.f, b.drillProgress, sf::Color(96, 238, 255));
        }
        break;
    }
    case BuildingType::Assembler: {
        drawGear3D(ironCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (8.0f * ext) : 0.f, 29.0f, 0.92f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            44.0f, 44.0f, h25,
            colA({34, 58, 52}), colA({92, 226, 152}), 165, mode25D, isGhost
        );

        sf::RectangleShape bed({24.0f, 18.0f});
        bed.setOrigin({12.0f, 9.0f});
        bed.setPosition(topC);
        bed.setFillColor(colA({22, 34, 32}));
        target.draw(bed);

        float sweep = (b.drillProgress > 0.01f || isGhost) ? std::sin(animTime * 6.0f) * 7.0f : 0.0f;
        sf::RectangleShape gantry({8.0f, 28.0f});
        gantry.setOrigin({4.0f, 14.0f});
        gantry.setPosition(topC + sf::Vector2f{sweep, 0.0f});
        gantry.setFillColor(colA({246, 198, 86}));
        target.draw(gantry);

        if (!isGhost && (b.electricPowered || b.status == NetworkStatus::Running)) {
            drawEmissiveGlow(emisTarget, topC, 50.0f, {92, 240, 158}, 0.48f);
        }
        if (!isGhost && b.drillProgress > 0.01f) {
            drawWorldBar(target, topC + sf::Vector2f{0.f, 17.f}, 32.f, 5.f, b.drillProgress, sf::Color(96, 245, 162));
        }
        break;
    }
    case BuildingType::QuantumForge: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            52.0f, 52.0f, h25,
            colA({30, 24, 52}),
            colA(b.electricPowered ? sf::Color(218, 108, 255) : sf::Color(118, 96, 152)),
            205, mode25D, isGhost
        );

        for (int sy : {-1, 1}) {
            for (int sx : {-1, 1}) {
                sf::RectangleShape coil({11.0f, 11.0f});
                coil.setOrigin({5.5f, 5.5f});
                coil.setPosition(topC + sf::Vector2f{sx * 16.0f, sy * 16.0f});
                coil.setFillColor(colA({92, 232, 255}));
                target.draw(coil);
            }
        }

        sf::RectangleShape core({18.0f, 18.0f});
        core.setOrigin({9.0f, 9.0f});
        core.setPosition(topC);
        core.setRotation(sf::radians(animTime * 2.6f));
        core.setFillColor(colA(b.electricPowered ? sf::Color(235, 115, 255) : sf::Color(86, 68, 110)));
        core.setOutlineColor(colA({140, 252, 255}));
        core.setOutlineThickness(2.0f);
        target.draw(core);

        if (!isGhost && b.electricPowered) {
            drawEmissiveGlow(emisTarget, topC, 84.0f, {228, 102, 255}, 0.88f);
        }
        if (!isGhost && b.drillProgress > 0.01f) {
            drawWorldBar(target, topC + sf::Vector2f{0.f, 20.f}, 38.f, 5.f, b.drillProgress, sf::Color(228, 115, 255));
        }
        break;
    }
    case BuildingType::OverclockBeacon: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            44.0f, 44.0f, h25,
            colA({48, 36, 42}), colA({248, 190, 82}), 190, mode25D, isGhost
        );

        float spin = b.electricPowered ? animTime * 3.5f : 0.0f;
        sf::RectangleShape ring({24.0f, 24.0f});
        ring.setOrigin({12.0f, 12.0f});
        ring.setPosition(topC);
        ring.setRotation(sf::radians(spin));
        ring.setFillColor(colA(b.electricPowered ? sf::Color(255, 198, 76) : sf::Color(110, 92, 68)));
        target.draw(ring);

        sf::RectangleShape gem({10.0f, 10.0f});
        gem.setOrigin({5.0f, 5.0f});
        gem.setPosition(topC);
        gem.setFillColor(colA(b.electricPowered ? sf::Color(255, 115, 232) : sf::Color(82, 64, 82)));
        target.draw(gem);

        if (!isGhost && b.electricPowered) {
            float pulse = 0.85f + 0.15f * std::sin(animTime * 4.5f);
            drawEmissiveGlow(emisTarget, topC, 88.0f, {255, 178, 76}, 0.82f * pulse);
        }
        break;
    }
    case BuildingType::MatterSink: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            52.0f, 52.0f, h25,
            colA({38, 30, 54}), colA({246, 178, 68}), 175, mode25D, isGhost
        );

        sf::CircleShape maw(18.0f, 12);
        maw.setOrigin({18.0f, 18.0f});
        maw.setPosition(topC);
        maw.setFillColor(colA({14, 10, 24}));
        maw.setOutlineColor(colA({198, 96, 255}));
        maw.setOutlineThickness(3.0f);
        target.draw(maw);

        for (int k = 0; k < 3; ++k) {
            sf::RectangleShape swirl({18.0f, 4.0f});
            swirl.setOrigin({2.0f, 2.0f});
            swirl.setPosition(topC);
            swirl.setRotation(sf::radians(-animTime * 3.2f + k * (TWO_PI / 3.0f)));
            swirl.setFillColor(colA({125, 242, 255}));
            target.draw(swirl);
        }
        if (!isGhost) {
            drawEmissiveGlow(emisTarget, topC, 62.0f, {195, 105, 255}, 0.62f);
        }
        break;
    }
    case BuildingType::StorageVault: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            50.0f, 50.0f, h25,
            colA({56, 64, 78}), colA({222, 178, 86}), 160, mode25D, isGhost
        );

        sf::RectangleShape band({50.0f, 10.0f});
        band.setOrigin({25.0f, 5.0f});
        band.setPosition(topC);
        band.setFillColor(colA({198, 152, 68}));
        target.draw(band);

        if (!isGhost && world) {
            float ratio = static_cast<float>(std::clamp(
                b.localStorage.totalAmount() / std::max(1.0, world->vaultBufferCapacity()), 0.0, 1.0));
            drawWorldBar(target, topC + sf::Vector2f{0.f, 15.f}, 38.f, 6.f, ratio, sf::Color(102, 238, 175));
        }
        break;
    }
    case BuildingType::Lubricator: {
        drawGear3D(darkCollar12_, darkCollar12_, baseCenter, b.angle, mode25D ? (8.0f * ext) : 0.f, 30.0f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            32.0f, 32.0f, h25,
            colA({32, 68, 56}), colA({95, 228, 162}), 165, mode25D, isGhost
        );

        sf::RectangleShape drop({12.0f, 12.0f});
        drop.setOrigin({6.0f, 6.0f});
        drop.setPosition(topC);
        drop.setFillColor(colA(b.status == NetworkStatus::Running ? sf::Color(115, 250, 178) : sf::Color(70, 120, 95)));
        target.draw(drop);

        if (!isGhost && b.status == NetworkStatus::Running) {
            drawEmissiveGlow(emisTarget, topC, 42.0f, {96, 240, 168}, 0.42f);
        }
        break;
    }
    case BuildingType::Flywheel: {
        sf::Vector2f topC = drawGear3D(flywheelWheel_, darkCollar12_, baseCenter, b.angle, h25, 34.0f);
        if (!isGhost && b.flywheelEnergy > 0.01f) {
            sf::Color fCol = (b.status == NetworkStatus::Buffering) ? sf::Color(105, 238, 255) : sf::Color(250, 198, 72);
            drawFilledArc(target, topC, 19.0f, 23.5f, -90.0f, 360.0f * b.flywheelEnergy, fCol);
            drawEmissiveGlow(emisTarget, topC, 36.0f, fCol, 0.35f * b.flywheelEnergy);
        }
        break;
    }
    case BuildingType::Alternator: {
        drawGear3D(copperCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (9.0f * ext) : 0.f, 32.0f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            38.0f, 34.0f, h25,
            colA({38, 44, 68}), colA({234, 152, 74}), 180, mode25D, isGhost
        );

        sf::RectangleShape coil({20.0f, 16.0f});
        coil.setOrigin({10.0f, 8.0f});
        coil.setPosition(topC);
        coil.setFillColor(colA(b.status == NetworkStatus::Running ? sf::Color(86, 224, 255) : sf::Color(76, 98, 122)));
        coil.setOutlineColor(colA({16, 22, 32}));
        coil.setOutlineThickness(2.0f);
        target.draw(coil);

        if (!isGhost && b.status == NetworkStatus::Running) {
            drawEmissiveGlow(emisTarget, topC, 50.0f, {86, 224, 255}, 0.52f);
        }
        break;
    }
    case BuildingType::ElectricMotor: {
        drawGear3D(ironCog12_, darkCollar12_, baseCenter, b.angle, mode25D ? (9.0f * ext) : 0.f, 32.0f);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            38.0f, 34.0f, h25,
            colA({34, 52, 68}),
            colA(b.electricPowered ? sf::Color(92, 238, 255) : sf::Color(115, 130, 148)),
            180, mode25D, isGhost
        );

        sf::RectangleShape rotor({16.0f, 16.0f});
        rotor.setOrigin({8.0f, 8.0f});
        rotor.setPosition(topC);
        rotor.setRotation(sf::radians(b.angle));
        rotor.setFillColor(colA(b.electricPowered ? sf::Color(250, 212, 92) : sf::Color(92, 98, 112)));
        target.draw(rotor);

        if (!isGhost && b.electricPowered) {
            drawEmissiveGlow(emisTarget, topC, 46.0f, {92, 238, 255}, 0.45f);
        }
        break;
    }
    case BuildingType::PowerPole: {
        sf::RectangleShape base({18.0f, 18.0f});
        base.setOrigin({9.0f, 9.0f});
        base.setPosition(baseCenter + sf::Vector2f{0.f, 8.f});
        base.setFillColor(colA({74, 56, 42}));
        base.setOutlineColor(colA({18, 20, 26}));
        base.setOutlineThickness(2.5f);
        target.draw(base);

        float armLift = mode25D ? (34.0f * ext) : 8.0f;
        sf::Vector2f armCenter = baseCenter - sf::Vector2f{0.0f, armLift};

        if (mode25D) {
            drawThickLine(target, baseCenter + sf::Vector2f{0.f, 8.f}, armCenter, 7.0f, colA({98, 70, 48}));
        }

        sf::RectangleShape crossArm({38.0f, 9.0f});
        crossArm.setOrigin({19.0f, 4.5f});
        crossArm.setPosition(armCenter);
        crossArm.setFillColor(colA({128, 92, 60}));
        crossArm.setOutlineColor(colA({20, 16, 12}));
        crossArm.setOutlineThickness(2.0f);
        target.draw(crossArm);

        for (int s : {-1, 1}) {
            sf::RectangleShape ins({8.0f, 10.0f});
            ins.setOrigin({4.0f, 5.0f});
            ins.setPosition(armCenter + sf::Vector2f{s * 14.0f, 0.f});
            ins.setFillColor(colA(b.electricPowered ? sf::Color(92, 238, 255) : sf::Color(145, 165, 180)));
            target.draw(ins);
        }
        if (!isGhost) {
            stampMaterialRect(matTarget, armCenter, {38.f, 12.f}, 128, 128, 120, pxHeightToByte(armLift));
            if (b.electricPowered) {
                drawEmissiveGlow(emisTarget, armCenter, 36.0f, {92, 238, 255}, 0.42f);
            }
        }
        break;
    }
    case BuildingType::Substation: {
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            48.0f, 46.0f, mode25D ? (20.0f * ext) : 0.0f,
            colA({44, 52, 68}), colA({180, 198, 224}), 185, mode25D, isGhost
        );

        float pylonExtra = mode25D ? (18.0f * ext) : 2.0f;
        for (int s : {-1, 1}) {
            sf::RectangleShape tower({12.0f, 22.0f + pylonExtra * 0.5f});
            tower.setOrigin({6.0f, tower.getSize().y * 0.5f});
            tower.setPosition(topC + sf::Vector2f{s * 12.0f, -pylonExtra * 0.5f});
            tower.setFillColor(colA({188, 126, 72}));
            tower.setOutlineColor(colA({22, 18, 14}));
            tower.setOutlineThickness(2.0f);
            target.draw(tower);
        }
        sf::RectangleShape led({12.0f, 10.0f});
        led.setOrigin({6.0f, 5.0f});
        led.setPosition(topC + sf::Vector2f{0.f, 12.f});
        led.setFillColor(colA(b.electricPowered ? sf::Color(86, 245, 175) : sf::Color(240, 170, 65)));
        target.draw(led);

        if (!isGhost && b.electricPowered) {
            drawEmissiveGlow(emisTarget, topC - sf::Vector2f{0.f, pylonExtra}, 64.0f, {86, 238, 225}, 0.62f);
        }
        break;
    }
    case BuildingType::NuclearReactor: {
        float tier1H = mode25D ? (26.0f * ext) : 0.0f;
        float tier2H = mode25D ? (46.0f * ext) : 0.0f;

        drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            CELL_SIZE * 2.75f, CELL_SIZE * 2.75f, tier1H,
            colA({42, 48, 58}), colA({158, 176, 196}), 170, mode25D, isGhost
        );

        for (int sy : {-1, 1}) {
            for (int sx : {-1, 1}) {
                drawChassis25D(
                    target, isGhost ? nullptr : matTarget,
                    baseCenter + sf::Vector2f{sx * 64.f, sy * 64.f},
                    38.f, 38.f, mode25D ? (34.0f * ext) : 0.0f,
                    colA({58, 66, 80}), colA({20, 24, 32}), 160, mode25D, isGhost
                );
            }
        }

        sf::Vector2f domeTop = baseCenter - sf::Vector2f{0.0f, tier2H};
        if (mode25D && !isGhost) {
            stampMaterialCircle(matTarget, domeTop, 62.0f, 128, 128, 190, pxHeightToByte(tier2H));
        }

        sf::CircleShape ring(58.0f, 16);
        ring.setOrigin({58.0f, 58.0f});
        ring.setPosition(domeTop);
        ring.setFillColor(colA({26, 36, 44}));
        ring.setOutlineColor(colA(b.hasFuel ? sf::Color(115, 250, 125) : sf::Color(90, 108, 120)));
        ring.setOutlineThickness(5.0f);
        target.draw(ring);

        float pulse = 0.88f + 0.12f * std::sin(animTime * 4.5f);
        float coreR = 34.0f * (b.hasFuel ? pulse : 0.85f);
        sf::CircleShape core(coreR, 12);
        core.setOrigin({coreR, coreR});
        core.setPosition(domeTop);
        core.setFillColor(colA(b.hasFuel ? sf::Color(64, 245, 160) : sf::Color(62, 74, 82)));
        core.setOutlineColor(colA(b.hasFuel ? sf::Color(185, 255, 225) : sf::Color(40, 48, 56)));
        core.setOutlineThickness(3.0f);
        target.draw(core);

        sf::RectangleShape r1({36.f, 8.f});
        r1.setOrigin({18.f, 4.f});
        r1.setPosition(domeTop);
        r1.setFillColor(colA({24, 42, 46}));
        target.draw(r1);
        sf::RectangleShape r2({8.f, 36.f});
        r2.setOrigin({4.f, 18.f});
        r2.setPosition(domeTop);
        r2.setFillColor(colA({24, 42, 46}));
        target.draw(r2);

        if (!isGhost && b.hasFuel) {
            drawEmissiveGlow(emisTarget, domeTop, 148.0f, {72, 250, 138}, 0.98f * pulse);
        }
        break;
    }
    case BuildingType::CoolingTower: {
        sf::Vector2f t1 = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            50.0f, 50.0f, mode25D ? (18.0f * ext) : 0.0f,
            colA({64, 72, 86}), colA({158, 172, 192}), 135, mode25D, isGhost
        );
        sf::Vector2f t2 = mode25D
            ? drawChassis25D(target, isGhost ? nullptr : matTarget, baseCenter, 36.0f, 36.0f, h25,
                             colA({84, 94, 112}), colA({26, 30, 38}), 145, true, isGhost)
            : t1;

        if (!mode25D) {
            sf::RectangleShape mid({36.0f, 36.0f});
            mid.setOrigin({18.0f, 18.0f});
            mid.setPosition(t2);
            mid.setFillColor(colA({84, 94, 112}));
            mid.setOutlineColor(colA({26, 30, 38}));
            mid.setOutlineThickness(2.5f);
            target.draw(mid);
        }

        sf::RectangleShape stack({22.0f, 22.0f});
        stack.setOrigin({11.0f, 11.0f});
        stack.setPosition(t2);
        stack.setFillColor(colA(b.electricPowered ? sf::Color(145, 220, 245) : sf::Color(28, 34, 44)));
        stack.setOutlineColor(colA({18, 22, 28}));
        stack.setOutlineThickness(2.5f);
        target.draw(stack);

        if (!isGhost && b.electricPowered) {
            drawEmissiveGlow(emisTarget, t2, 46.0f, {125, 218, 255}, 0.38f);
        }
        break;
    }
    case BuildingType::FusionReactor: {
        float tier1H = mode25D ? (28.0f * ext) : 0.0f;
        float tier2H = mode25D ? (50.0f * ext) : 0.0f;

        drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            CELL_SIZE * 2.82f, CELL_SIZE * 2.82f, tier1H,
            colA({28, 34, 56}), colA({125, 232, 255}), 205, mode25D, isGhost
        );

        sf::Vector2f torusTop = baseCenter - sf::Vector2f{0.0f, tier2H};
        if (mode25D && !isGhost) {
            stampMaterialCircle(matTarget, torusTop, 68.0f, 128, 128, 220, pxHeightToByte(tier2H));
        }

        sf::CircleShape chamber(66.0f, 24);
        chamber.setOrigin({66.0f, 66.0f});
        chamber.setPosition(torusTop);
        chamber.setFillColor(colA({16, 18, 34}));
        chamber.setOutlineColor(colA({190, 175, 255}));
        chamber.setOutlineThickness(4.0f);
        target.draw(chamber);

        float pulse = 0.88f + 0.12f * std::sin(animTime * 6.0f);
        if (b.hasFuel || isGhost) {
            drawPixelCircleRing(target, torusTop, 44.0f * pulse, 14.0f, colA({228, 102, 255}), 36);
            drawPixelCircleRing(target, torusTop, 44.0f * pulse, 6.0f, colA({135, 252, 255}), 36);
        }

        for (int k = 0; k < 8; ++k) {
            float a = static_cast<float>(k) * (PI * 0.25f);
            sf::RectangleShape mag({24.0f, 12.0f});
            mag.setOrigin({-28.0f, 6.0f});
            mag.setPosition(torusTop);
            mag.setRotation(sf::radians(a));
            mag.setFillColor(colA({195, 210, 238}));
            mag.setOutlineColor(colA({22, 26, 38}));
            mag.setOutlineThickness(2.0f);
            target.draw(mag);
        }

        sf::CircleShape hub(16.0f, 12);
        hub.setOrigin({16.0f, 16.0f});
        hub.setPosition(torusTop);
        hub.setFillColor(colA({44, 56, 82}));
        hub.setOutlineColor(colA({125, 245, 255}));
        hub.setOutlineThickness(3.0f);
        target.draw(hub);

        if (!isGhost && b.hasFuel) {
            drawEmissiveGlow(emisTarget, torusTop, 170.0f, {220, 96, 255}, 1.0f * pulse);
            drawEmissiveGlow(emisTarget, torusTop, 100.0f, {98, 245, 255}, 0.85f * pulse);
        }
        break;
    }
    case BuildingType::ConveyorBelt:
    case BuildingType::FastBelt:
    case BuildingType::ExpressBelt:
    case BuildingType::BeltSplitter:
    case BuildingType::UndergroundBelt:
        drawBeltTrack(target, matTarget, emisTarget, world, b, settings, animTime, isGhost, ghostValid);
        break;
    case BuildingType::RailTrack:
    case BuildingType::TrainStation:
        drawRailBuilding(target, matTarget, emisTarget, world, b, settings, animTime, isGhost, ghostValid);
        break;
    case BuildingType::SteamTrain:
    case BuildingType::ElectricTrain: {
        bool isElec = (b.type == BuildingType::ElectricTrain);
        sf::Color bodyCol = isElec ? sf::Color(56, 134, 232) : sf::Color(178, 58, 52);
        sf::Color trimCol = isElec ? sf::Color(160, 236, 255) : sf::Color(238, 194, 88);
        sf::Vector2f topC = drawChassis25D(
            target, isGhost ? nullptr : matTarget, baseCenter,
            46.0f, 24.0f, mode25D ? (14.0f * ext) : 0.0f,
            colA(bodyCol), colA(trimCol), 185, mode25D, isGhost
        );

        sf::RectangleShape cab({16.0f, 20.0f});
        cab.setOrigin({8.0f, 10.0f});
        cab.setPosition(topC + sf::Vector2f{-10.f, 0.f});
        cab.setFillColor(colA({38, 44, 56}));
        target.draw(cab);

        sf::RectangleShape nose({12.0f, 12.0f});
        nose.setOrigin({6.0f, 6.0f});
        nose.setPosition(topC + sf::Vector2f{12.f, 0.f});
        nose.setFillColor(colA(trimCol));
        target.draw(nose);
        break;
    }
    default:
        break;
    }
}

void Renderer::drawPowerLines(
    sf::RenderTarget& target,
    sf::RenderTarget* emisTarget,
    const World& world,
    const GameSettings& settings,
    float animTime
) const {
    const sf::View& view = target.getView();
    sf::Vector2f vc = view.getCenter();
    sf::Vector2f vs = view.getSize();
    float minX = vc.x - vs.x * 0.6f;
    float maxX = vc.x + vs.x * 0.6f;
    float minY = vc.y - vs.y * 0.6f;
    float maxY = vc.y + vs.y * 0.65f;

    const bool mode25D = settings.mode25D;
    const float ext = mode25D ? settings.extrusionDepth : 0.0f;
    float pulse = 0.78f + 0.22f * std::sin(animTime * 5.0f);

    for (const PowerLineLink& link : world.powerLinks()) {
        sf::Vector2f gA{(link.ax + 0.5f) * CELL_SIZE, (link.ay + 0.5f) * CELL_SIZE};
        sf::Vector2f gB{(link.bx + 0.5f) * CELL_SIZE, (link.by + 0.5f) * CELL_SIZE};
        if (std::max(gA.x, gB.x) < minX || std::min(gA.x, gB.x) > maxX ||
            std::max(gA.y, gB.y) < minY || std::min(gA.y, gB.y) > maxY) {
            continue;
        }

        float liftA = 8.0f;
        float liftB = 8.0f;
        if (mode25D) {
            if (const BuildingEntity* ba = world.buildingAt(link.ax, link.ay)) {
                liftA = wireAttachHeight25D(ba->type, ext);
            }
            if (const BuildingEntity* bb = world.buildingAt(link.bx, link.by)) {
                liftB = wireAttachHeight25D(bb->type, ext);
            }
            // Subtle ground wire shadow for 2.5D depth read!
            drawCatenaryWire(target, gA + sf::Vector2f{4.f, 4.f}, gB + sf::Vector2f{4.f, 4.f},
                             6.0f, 2.5f, sf::Color(6, 8, 14, 72));
        }

        sf::Vector2f a = gA - sf::Vector2f{0.0f, liftA};
        sf::Vector2f b = gB - sf::Vector2f{0.0f, liftB};

        sf::Color wireCol(175, 118, 66, 215);
        if (link.overloaded) {
            wireCol = sf::Color(246, 120, 62, static_cast<std::uint8_t>(230 * pulse));
        } else if (link.powered) {
            wireCol = sf::Color(92, 232, 255, static_cast<std::uint8_t>(235 * pulse));
        }
        float sag = mode25D ? (8.5f * ext) : 3.0f;
        drawCatenaryWire(target, a, b, sag, 3.4f, wireCol);

        if (emisTarget && (link.powered || link.overloaded)) {
            sf::Color eCol = link.overloaded ? sf::Color(95, 38, 16) : sf::Color(26, 78, 96);
            drawCatenaryWire(*emisTarget, a, b, sag, 7.0f, eCol, sf::BlendAdd);
        }
    }
}

void Renderer::drawSingleTrain(
    sf::RenderTarget& target,
    sf::RenderTarget* matTarget,
    sf::RenderTarget* emisTarget,
    const World& world,
    const TrainEntity& tr,
    const GameSettings& settings
) const {
    auto trailPointAtDist = [](const TrainEntity& train, float targetDist, sf::Vector2f& outPos, float& outAng) -> bool {
        if (train.trail.size() < 2) return false;
        float accum = 0.0f;
        sf::Vector2f prev{train.wx, train.wy};
        for (const auto& pt : train.trail) {
            sf::Vector2f p{pt.first, pt.second};
            float seg = std::hypot(p.x - prev.x, p.y - prev.y);
            if (accum + seg >= targetDist && seg > 0.01f) {
                float t = (targetDist - accum) / seg;
                outPos = prev + (p - prev) * t;
                outAng = std::atan2(prev.y - p.y, prev.x - p.x);
                return true;
            }
            accum += seg;
            prev = p;
        }
        return false;
    };

    const bool mode25D = settings.mode25D;
    const float lift = mode25D ? (12.0f * settings.extrusionDepth) : 0.0f;
    const double cap = std::max(1.0, world.trainCargoCapacity());
    double cargoAmt = tr.cargo.totalAmount();
    float fill = static_cast<float>(std::clamp(cargoAmt / cap, 0.0, 1.0));

    for (int car = 2; car >= 1; --car) {
        sf::Vector2f carPos;
        float carAng = tr.angle;
        if (!trailPointAtDist(tr, static_cast<float>(car) * 40.0f, carPos, carAng)) {
            carPos = sf::Vector2f{tr.wx, tr.wy} - polar(static_cast<float>(car) * 40.0f, tr.angle);
        }

        sf::RectangleShape wShadow({34.0f, 22.0f});
        wShadow.setOrigin({17.0f, 11.0f});
        wShadow.setPosition(carPos + sf::Vector2f{3.f, 4.f});
        wShadow.setRotation(sf::radians(carAng));
        wShadow.setFillColor(sf::Color(6, 8, 12, 125));
        target.draw(wShadow);

        if (mode25D) {
            sf::RectangleShape chassis({32.0f, 20.0f});
            chassis.setOrigin({16.0f, 10.0f});
            chassis.setPosition(carPos - sf::Vector2f{0.f, lift * 0.45f});
            chassis.setRotation(sf::radians(carAng));
            chassis.setFillColor(sf::Color(34, 38, 48));
            target.draw(chassis);
        }

        sf::Vector2f wTop = carPos - sf::Vector2f{0.f, lift * 0.85f};
        sf::RectangleShape wagon({32.0f, 20.0f});
        wagon.setOrigin({16.0f, 10.0f});
        wagon.setPosition(wTop);
        wagon.setRotation(sf::radians(carAng));
        wagon.setFillColor(sf::Color(58, 66, 78));
        wagon.setOutlineColor(sf::Color(16, 20, 26));
        wagon.setOutlineThickness(2.5f);
        target.draw(wagon);

        if (fill > 0.05f) {
            sf::RectangleShape heap({22.0f * std::max(0.35f, fill), 12.0f});
            heap.setOrigin({11.0f * std::max(0.35f, fill), 6.0f});
            heap.setPosition(wTop);
            heap.setRotation(sf::radians(carAng));
            heap.setFillColor(sf::Color(206, 158, 92));
            target.draw(heap);
        }
        stampMaterialCircle(matTarget, wTop, 14.0f, 128, 128, 165, pxHeightToByte(lift * 0.85f));
    }

    sf::Color bodyCol = tr.isElectric ? sf::Color(56, 134, 232) : sf::Color(178, 58, 52);
    sf::Color trimCol = tr.isElectric ? sf::Color(160, 236, 255) : sf::Color(238, 194, 88);
    if (!tr.hasFuel) {
        trimCol = sf::Color(240, 82, 75);
    }

    sf::RectangleShape shadow({44.0f, 24.0f});
    shadow.setOrigin({22.0f, 12.0f});
    shadow.setPosition({tr.wx + 3.5f, tr.wy + 4.5f});
    shadow.setRotation(sf::radians(tr.angle));
    shadow.setFillColor(sf::Color(6, 8, 12, 135));
    target.draw(shadow);

    if (mode25D) {
        sf::RectangleShape lowerBody({44.0f, 24.0f});
        lowerBody.setOrigin({22.0f, 12.0f});
        lowerBody.setPosition({tr.wx, tr.wy - lift * 0.5f});
        lowerBody.setRotation(sf::radians(tr.angle));
        lowerBody.setFillColor(darkenColor(bodyCol, 0.56f));
        target.draw(lowerBody);
    }

    sf::Vector2f locoTop{tr.wx, tr.wy - lift};
    sf::RectangleShape body({44.0f, 24.0f});
    body.setOrigin({22.0f, 12.0f});
    body.setPosition(locoTop);
    body.setRotation(sf::radians(tr.angle));
    body.setFillColor(bodyCol);
    body.setOutlineColor(trimCol);
    body.setOutlineThickness(2.5f);
    target.draw(body);

    sf::Vector2f nosePos = locoTop + polar(14.0f, tr.angle);
    sf::RectangleShape nose({10.0f, 14.0f});
    nose.setOrigin({5.0f, 7.0f});
    nose.setPosition(nosePos);
    nose.setRotation(sf::radians(tr.angle));
    nose.setFillColor(trimCol);
    target.draw(nose);

    sf::RectangleShape cab({14.0f, 18.0f});
    cab.setOrigin({7.0f, 9.0f});
    cab.setPosition(locoTop - polar(10.0f, tr.angle) - sf::Vector2f{0.f, mode25D ? 3.5f : 0.f});
    cab.setRotation(sf::radians(tr.angle));
    cab.setFillColor(sf::Color(34, 40, 52));
    target.draw(cab);

    stampMaterialCircle(matTarget, locoTop, 18.0f, 128, 128, 200, pxHeightToByte(lift + 4.0f));

    if (tr.hasFuel) {
        sf::Color beamCol = tr.isElectric ? sf::Color(115, 240, 255) : sf::Color(255, 214, 118);
        drawEmissiveGlow(emisTarget, nosePos, 40.0f, beamCol, 0.72f);
        drawEmissiveCone(emisTarget, nosePos, tr.angle, 130.0f, 0.42f, beamCol, 0.75f);
    } else {
        drawStatusBadge(target, locoTop, sf::Color(240, 82, 75, 240));
    }
}

void Renderer::drawGhostMeshingGuides(
    sf::RenderTarget& target,
    sf::RenderTarget* emisTarget,
    const World& world,
    BuildingType type,
    int gx,
    int gy,
    int dir,
    float animTime
) const {
    const BuildingDef& def = Catalog::getBuilding(type);
    float pulse = 0.7f + 0.3f * std::sin(animTime * 6.0f);
    sf::Vector2f c{(gx + 0.5f) * CELL_SIZE, (gy + 0.5f) * CELL_SIZE};

    if (type == BuildingType::UndergroundBelt) {
        int exitIdx = world.findUndergroundExitIndex(gx, gy, dir);
        if (exitIdx >= 0 && exitIdx < static_cast<int>(world.buildings().size())) {
            const BuildingEntity& ex = world.buildings()[static_cast<std::size_t>(exitIdx)];
            drawDashedLine(target, c, {(ex.gx + 0.5f) * CELL_SIZE, (ex.gy + 0.5f) * CELL_SIZE},
                           4.0f, 14.0f, sf::Color(96, 245, 190, static_cast<std::uint8_t>(225 * pulse)));
        } else {
            int dx = 0, dy = 0;
            World::directionVector(dir, dx, dy);
            for (int step = 1; step <= UNDERGROUND_MAX_DIST; ++step) {
                int ux = gx - dx * step;
                int uy = gy - dy * step;
                if (!world.inBounds(ux, uy)) break;
                const BuildingEntity* ub = world.buildingAt(ux, uy);
                if (ub && ub->type == BuildingType::UndergroundBelt && ub->dir == dir) {
                    drawDashedLine(target, {(ub->gx + 0.5f) * CELL_SIZE, (ub->gy + 0.5f) * CELL_SIZE}, c,
                                   4.0f, 14.0f, sf::Color(96, 245, 190, static_cast<std::uint8_t>(225 * pulse)));
                    break;
                }
            }
        }
        return;
    }

    if (def.powerRange > 0.f) {
        for (const BuildingEntity& nb : world.buildings()) {
            const BuildingDef& ndef = Catalog::getBuilding(nb.type);
            if (!isElectricNodeType(nb.type, ndef)) continue;
            float maxR = std::max(def.powerRange, ndef.powerRange);
            float d = std::hypot(static_cast<float>(nb.gx - gx), static_cast<float>(nb.gy - gy));
            if (ndef.footprintRadius > 0) d = std::max(0.f, d - 1.0f);
            if (d <= maxR + 0.05f) {
                sf::Vector2f nbPos{(nb.gx + 0.5f) * CELL_SIZE, (nb.gy + 0.5f) * CELL_SIZE};
                drawThickLine(target, c, nbPos, 3.0f, sf::Color(92, 236, 255, static_cast<std::uint8_t>(170 * pulse)));
                if (emisTarget) drawThickLine(*emisTarget, c, nbPos, 5.0f, sf::Color(20, 60, 75), sf::BlendAdd);
            }
        }
        return;
    }

    if (isElectricNodeType(type, def)) {
        for (const BuildingEntity& nb : world.buildings()) {
            const BuildingDef& ndef = Catalog::getBuilding(nb.type);
            if (ndef.powerRange <= 0.f) continue;
            float d = std::hypot(static_cast<float>(nb.gx - gx), static_cast<float>(nb.gy - gy));
            if (def.footprintRadius > 0) d = std::max(0.f, d - 1.0f);
            if (d <= ndef.powerRange + 0.05f) {
                sf::Vector2f nbPos{(nb.gx + 0.5f) * CELL_SIZE, (nb.gy + 0.5f) * CELL_SIZE};
                drawThickLine(target, c, nbPos, 3.0f, sf::Color(92, 236, 255, static_cast<std::uint8_t>(185 * pulse)));
            }
        }
    }

    if (!def.isMechanical) return;

    sf::Color portCol(90, 235, 255, static_cast<std::uint8_t>(225 * pulse));
    auto drawContactDot = [&](sf::Vector2f pos, sf::Color col) {
        sf::RectangleShape dot({10.0f, 10.0f});
        dot.setOrigin({5.0f, 5.0f});
        dot.setPosition(pos);
        dot.setFillColor(col);
        dot.setOutlineColor(sf::Color(12, 18, 26));
        dot.setOutlineThickness(2.0f);
        target.draw(dot);
    };

    if (type == BuildingType::LargeGear) {
        static constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (const auto& d : DIRS) {
            sf::Vector2f portPos{
                (gx + d[0] * 1.5f + 0.5f) * CELL_SIZE,
                (gy + d[1] * 1.5f + 0.5f) * CELL_SIZE
            };
            int nx = gx + d[0] * 2;
            int ny = gy + d[1] * 2;
            const BuildingEntity* nb = world.inBounds(nx, ny) ? world.buildingAt(nx, ny) : nullptr;
            bool hasNeighbor = nb && Catalog::getBuilding(nb->type).isMechanical;
            drawContactDot(portPos, hasNeighbor ? portCol : sf::Color(240, 200, 95, 160));
        }
        return;
    }

    if (type == BuildingType::SpeedGearbox) {
        int fdx = 0, fdy = 0;
        World::directionVector(dir, fdx, fdy);
        drawContactDot({(gx + 0.5f + fdx * 0.5f) * CELL_SIZE, (gy + 0.5f + fdy * 0.5f) * CELL_SIZE}, sf::Color(100, 245, 170));
        drawContactDot({(gx + 0.5f - fdx * 0.5f) * CELL_SIZE, (gy + 0.5f - fdy * 0.5f) * CELL_SIZE}, sf::Color(245, 195, 85));
        return;
    }

    static constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (const auto& d : DIRS) {
        int nx = gx + d[0];
        int ny = gy + d[1];
        if (!world.inBounds(nx, ny)) continue;
        const BuildingEntity* nb = world.buildingAt(nx, ny);
        if (!nb || !Catalog::getBuilding(nb->type).isMechanical) continue;
        bool connects = true;
        if (nb->type == BuildingType::LargeGear) {
            connects = (nb->gx == nx + d[0] && nb->gy == ny + d[1]);
        } else if (nb->type == BuildingType::SpeedGearbox) {
            int fdx = 0, fdy = 0;
            World::directionVector(nb->dir, fdx, fdy);
            connects = (d[0] == fdx && d[1] == fdy) || (d[0] == -fdx && d[1] == -fdy);
        }
        if (connects) {
            sf::Vector2f mid{
                (gx + 0.5f + d[0] * 0.5f) * CELL_SIZE,
                (gy + 0.5f + d[1] * 0.5f) * CELL_SIZE
            };
            drawContactDot(mid, portCol);
        }
    }
}

void Renderer::drawStatusBadge(
    sf::RenderTarget& target,
    sf::Vector2f pos,
    sf::Color ringCol
) const {
    sf::RectangleShape box({54.0f, 54.0f});
    box.setOrigin({27.0f, 27.0f});
    box.setPosition(pos);
    box.setFillColor(sf::Color::Transparent);
    box.setOutlineColor(ringCol);
    box.setOutlineThickness(3.0f);
    target.draw(box);

    sf::RectangleShape badge({12.0f, 12.0f});
    badge.setOrigin({6.0f, 6.0f});
    badge.setPosition(pos + sf::Vector2f{18.0f, -18.0f});
    badge.setFillColor(ringCol);
    target.draw(badge);
}

void Renderer::drawParticles(
    sf::RenderTarget& target,
    sf::RenderTarget* emisTarget,
    const World& world
) const {
    for (const Particle& p : world.particles()) {
        float ratio = std::clamp(p.life / std::max(0.01f, p.maxLife), 0.0f, 1.0f);
        float sz = std::max(3.0f, std::round(p.size * 2.0f));
        sf::RectangleShape sq({sz, sz});
        sq.setOrigin({sz * 0.5f, sz * 0.5f});
        sq.setPosition({p.x, p.y});
        sq.setFillColor(sf::Color(p.r, p.g, p.b, static_cast<std::uint8_t>(p.a * ratio)));
        target.draw(sq);

        if (emisTarget && p.size < 4.0f) {
            drawEmissiveGlow(emisTarget, {p.x, p.y}, 16.0f, {p.r, p.g, p.b}, 0.35f * ratio);
        }
    }
}

void Renderer::renderPopups(
    sf::RenderTarget& target,
    const World& world,
    const GameSettings& settings,
    const sf::Font& font,
    const sf::View& worldView
) const {
    if (!settings.showPopups) return;
    sf::Vector2u sz = target.getSize();
    sf::Vector2f viewCenter = worldView.getCenter();
    sf::Vector2f viewSize = worldView.getSize();
    float worldToScreen = static_cast<float>(sz.x) / std::max(1.0f, viewSize.x);
    float zoomFade = std::clamp((worldToScreen - 0.38f) / 0.20f, 0.0f, 1.0f);
    if (zoomFade <= 0.02f) return;

    float liftY = settings.mode25D ? (16.0f * settings.extrusionDepth) : 0.0f;

    for (const FloatingPopup& f : world.popups()) {
        float nx = (f.x - (viewCenter.x - viewSize.x * 0.5f)) / viewSize.x;
        float ny = ((f.y - liftY) - (viewCenter.y - viewSize.y * 0.5f)) / viewSize.y;
        sf::Vector2f screenPos{
            std::round(nx * static_cast<float>(sz.x)),
            std::round(ny * static_cast<float>(sz.y))
        };

        float ratio = std::clamp(f.life / std::max(0.01f, f.maxLife), 0.0f, 1.0f) * zoomFade;
        sf::Text txt(font, f.text, 14);
        txt.setStyle(sf::Text::Bold);
        txt.setFillColor(sf::Color(f.r, f.g, f.b, static_cast<std::uint8_t>(255 * ratio)));
        txt.setOutlineColor(sf::Color(10, 14, 20, static_cast<std::uint8_t>(230 * ratio)));
        txt.setOutlineThickness(2.0f);
        auto lb = txt.getLocalBounds();
        txt.setOrigin({std::round(lb.position.x + lb.size.x * 0.5f), std::round(lb.position.y + lb.size.y * 0.5f)});
        txt.setPosition(screenPos);
        target.draw(txt);
    }
}

void Renderer::drawBuildingPreviewIcon(
    sf::RenderTarget& target,
    BuildingType type,
    sf::Vector2f center,
    float scale,
    float angle
) const {
    iconRT_.clear(sf::Color::Transparent);
    sf::View iconView;
    iconView.setCenter({CELL_SIZE * 0.5f, CELL_SIZE * 0.5f});
    const BuildingDef& def = Catalog::getBuilding(type);
    if (def.footprintRadius > 0) {
        iconView.setSize({CELL_SIZE * 3.35f, CELL_SIZE * 3.35f});
    } else {
        iconView.setSize({CELL_SIZE * 1.25f, CELL_SIZE * 1.25f});
    }
    iconRT_.setView(iconView);

    GameSettings flatPreviewSettings;
    flatPreviewSettings.mode25D = false;
    flatPreviewSettings.shadersEnabled = false;

    BuildingEntity dummy;
    dummy.type = type;
    dummy.gx = 0;
    dummy.gy = 0;
    dummy.angle = angle;
    dummy.angleSecondary = angle * 2.0f;
    dummy.status = NetworkStatus::Running;
    dummy.electricPowered = true;
    dummy.hasFuel = true;
    dummy.flywheelEnergy = 0.75f;
    dummy.batteryCharge = 0.75f;
    dummy.drillProgress = 0.45f;
    drawBuilding(iconRT_, nullptr, nullptr, dummy, flatPreviewSettings, angle, false, true, nullptr);
    iconRT_.display();

    sf::Sprite sp(iconRT_.getTexture());
    sp.setOrigin({12.0f, 12.0f});
    sp.setPosition({std::round(center.x), std::round(center.y)});
    float drawScale = scale * 3.0f;
    sp.setScale({drawScale, drawScale});
    target.draw(sp);
}

} // namespace cogwill
