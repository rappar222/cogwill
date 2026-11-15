#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace cogwill {

namespace {

uint32_t xorshift32(uint32_t& state) {
    uint32_t x = state ? state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

float randFloat01(uint32_t& state) {
    return static_cast<float>(xorshift32(state) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

// Deterministic 2D lattice hash & coherent multi-octave fBm noise for seeded worlds
float hash2D(int ix, int iy, uint32_t seed) {
    uint32_t h = seed ^ 0x9E3779B9u;
    h ^= static_cast<uint32_t>(ix) * 374761393u;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(iy) * 668265263u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h ^= (h >> 16);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float smoothstep5(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float valueNoise2D(float x, float y, uint32_t seed) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    float fx = smoothstep5(x - static_cast<float>(x0));
    float fy = smoothstep5(y - static_cast<float>(y0));

    float v00 = hash2D(x0, y0, seed);
    float v10 = hash2D(x0 + 1, y0, seed);
    float v01 = hash2D(x0, y0 + 1, seed);
    float v11 = hash2D(x0 + 1, y0 + 1, seed);

    float nx0 = v00 + fx * (v10 - v00);
    float nx1 = v01 + fx * (v11 - v01);
    return nx0 + fy * (nx1 - nx0);
}

float fbm2D(float x, float y, uint32_t seed, int octaves = 4) {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 1013904223u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return norm > 0.0f ? (sum / norm) : 0.5f;
}

float ridgedFbm2D(float x, float y, uint32_t seed, int octaves = 4) {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = valueNoise2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 1664525u);
        float ridge = 1.0f - std::abs(2.0f * n - 1.0f);
        sum += amp * (ridge * ridge);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.07f;
    }
    return norm > 0.0f ? (sum / norm) : 0.0f;
}

float wrapAngle(float a) {
    float r = std::fmod(a, TWO_PI);
    if (r < 0.0f) {
        r += TWO_PI;
    }
    return r;
}

bool isDrillType(BuildingType t) {
    return t == BuildingType::IronDrill ||
           t == BuildingType::HeavyDrill ||
           t == BuildingType::ElectricDrill ||
           t == BuildingType::AetherExtractor;
}

bool isFactoryCrafter(BuildingType t) {
    return t == BuildingType::StoneSmelter ||
           t == BuildingType::ArcFurnace ||
           t == BuildingType::Assembler ||
           t == BuildingType::QuantumForge;
}

bool isDirectionalBelt(BuildingType t) {
    return t == BuildingType::ConveyorBelt ||
           t == BuildingType::FastBelt ||
           t == BuildingType::ExpressBelt ||
           t == BuildingType::UndergroundBelt;
}

bool isGeneratorType(BuildingType t) {
    return Catalog::getBuilding(t).isGenerator;
}

bool isPrimeMoverType(BuildingType t) {
    return t == BuildingType::HandCrank ||
           t == BuildingType::WindTurbine ||
           t == BuildingType::SteamEngine ||
           t == BuildingType::AetherDynamo;
}

bool isRelayType(BuildingType t) {
    return t == BuildingType::PowerPole || t == BuildingType::Substation;
}

bool isRailType(BuildingType t) {
    return t == BuildingType::RailTrack || t == BuildingType::TrainStation;
}

bool isElectricGridNode(BuildingType t) {
    return t == BuildingType::Alternator ||
           t == BuildingType::ElectricMotor ||
           t == BuildingType::SolarPanel ||
           t == BuildingType::Accumulator ||
           t == BuildingType::ElectricDrill ||
           t == BuildingType::AetherExtractor ||
           t == BuildingType::ArcFurnace ||
           t == BuildingType::Assembler ||
           t == BuildingType::QuantumForge ||
           t == BuildingType::OverclockBeacon ||
           t == BuildingType::NuclearReactor ||
           t == BuildingType::FusionReactor ||
           t == BuildingType::TrainStation;
}

bool canUpgradeBuildingInPlace(BuildingType existing, BuildingType replacement) {
    if (existing == replacement) return false;
    auto beltRank = [](BuildingType t) {
        if (t == BuildingType::ConveyorBelt) return 1;
        if (t == BuildingType::FastBelt) return 2;
        if (t == BuildingType::ExpressBelt) return 3;
        return 0;
    };
    if (beltRank(existing) > 0 && beltRank(replacement) > beltRank(existing)) {
        return true;
    }
    auto drillRank = [](BuildingType t) {
        if (t == BuildingType::IronDrill) return 1;
        if (t == BuildingType::HeavyDrill || t == BuildingType::ElectricDrill) return 2;
        if (t == BuildingType::AetherExtractor) return 3;
        return 0;
    };
    if (drillRank(existing) > 0 && drillRank(replacement) > drillRank(existing)) {
        return true;
    }
    return false;
}

int oreTierRequired(OreType ore) {
    switch (ore) {
    case OreType::Iron:
    case OreType::Copper:
    case OreType::Coal:     return 1;
    case OreType::Titanium:
    case OreType::Uranium:  return 2;
    case OreType::Aether:   return 3;
    default:                return 0;
    }
}

float footprintDistanceTiles(const BuildingEntity& a, int radiusA, int gxB, int gyB) {
    float best = 1e9f;
    for (int dy = -radiusA; dy <= radiusA; ++dy) {
        for (int dx = -radiusA; dx <= radiusA; ++dx) {
            float dist = std::hypot(static_cast<float>((a.gx + dx) - gxB),
                                    static_cast<float>((a.gy + dy) - gyB));
            best = std::min(best, dist);
        }
    }
    return best;
}

struct MechEdge {
    int toNode{-1};
    bool isInternalGearbox{false};
    float gearboxRatio{1.0f};
    int fromTeeth{12};
    int toTeeth{12};
    float alpha{0.0f};
};

} // namespace

World::World() {
    WorldGenSettings def;
    def.seedText = "1337";
    resetNewWorld(def, false);
}

bool World::isBeltLike(BuildingType t) {
    return isDirectionalBelt(t) || t == BuildingType::BeltSplitter;
}

void World::directionVector(int dir, int& dx, int& dy) {
    static constexpr int DX[4] = {1, 0, -1, 0};
    static constexpr int DY[4] = {0, 1, 0, -1};
    int d = ((dir % 4) + 4) % 4;
    dx = DX[d];
    dy = DY[d];
}

void World::resetNewMap(uint32_t seed) {
    uint32_t safeSeed = seed ? seed : 1u;
    genSettings_.seedText = std::to_string(safeSeed);
    resetNewWorld(genSettings_, sandboxMode_);
}

void World::resetNewWorld(const WorldGenSettings& gen, bool sandboxMode) {
    genSettings_ = gen;
    genSettings_.oreRichness = std::clamp(genSettings_.oreRichness, 0.4f, 4.0f);
    genSettings_.oreFrequency = std::clamp(genSettings_.oreFrequency, 0.3f, 3.0f);
    genSettings_.waterCoverage = std::clamp(genSettings_.waterCoverage, 0.0f, 2.5f);
    genSettings_.cliffCoverage = std::clamp(genSettings_.cliffCoverage, 0.0f, 2.5f);

    mapSeed_ = hashWorldSeed(genSettings_.seedText);
    sandboxMode_ = sandboxMode;

    tiles_.assign(GRID_W * GRID_H, Tile{});
    buildings_.clear();
    networks_.clear();
    mechHasPrimeMover_.clear();
    electricNetworks_.clear();
    powerLinks_.clear();
    trains_.clear();
    particles_.clear();
    popups_.clear();

    nextBuildingId_ = 1;
    nextTrainId_ = 1;

    upgradeLevels_.fill(0);
    couponLevels_.fill(0);
    spirePhase_ = 0;
    prestigeCount_ = 0;
    sinkPoints_ = 0.0;
    coupons_ = 0;
    totalCouponsMinted_ = 0;

    dayTime_ = 0.0f;
    solarRefreshTimer_ = 0.0f;

    stats_ = WorldStats{};
    rateTimer_ = 0.0f;
    minedThisWindow_.fill(0.0);
    smoothedRates_.fill(0.0);

    resources_ = ResourceBag{};
    if (sandboxMode_) {
        for (std::size_t i = 0; i < resources_.values.size(); ++i) {
            resources_.values[i] = 5000.0;
        }
        coupons_ = 25;
    } else {
        resources_[ResourceType::Iron] = 85.0;
        resources_[ResourceType::Copper] = 45.0;
        resources_[ResourceType::Coal] = 12.0;
    }

    generateMap(mapSeed_);

    // Place the permanent 3x3 Grand Clockwork Core at (HUB_GX, HUB_GY)
    BuildingEntity core;
    core.id = nextBuildingId_++;
    core.type = BuildingType::HubCore;
    core.gx = HUB_GX;
    core.gy = HUB_GY;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            Tile& t = tileMut(HUB_GX + dx, HUB_GY + dy);
            t.terrain = TerrainType::Ground;
            t.elevation = 0.24f;
            t.ore = OreType::None;
            t.occupantId = core.id;
        }
    }
    buildings_.push_back(core);

    recomputeNetworks();
}

void World::setSandboxMode(bool enabled) {
    sandboxMode_ = enabled;
    if (sandboxMode_) {
        for (BuildingEntity& b : buildings_) {
            const BuildingDef& def = Catalog::getBuilding(b.type);
            if (def.fuelType != ResourceType::Count && !b.hasFuel) {
                b.hasFuel = true;
                b.fuelTimer = std::max(b.fuelTimer, def.fuelInterval);
            }
        }
        for (TrainEntity& tr : trains_) {
            if (!tr.isElectric && !tr.hasFuel) {
                tr.hasFuel = true;
                tr.fuelTimer = std::max(tr.fuelTimer, 14.0f);
            }
        }
    }
    recomputeNetworks();
}

bool World::isBuildingUnlocked(BuildingType type) const {
    if (type == BuildingType::HubCore) return false;
    if (sandboxMode_) return true;
    return Catalog::isBuildingUnlocked(type, upgradeLevels_);
}

void World::unlockAllTechs() {
    for (int i = 0; i < static_cast<int>(UpgradeId::Count); ++i) {
        UpgradeId id = static_cast<UpgradeId>(i);
        upgradeLevels_[static_cast<std::size_t>(i)] = Catalog::getUpgrade(id).maxLevel;
    }
    stats_.hasUnlockedUpgrade = true;
    recomputeNetworks();
}

void World::grantSandboxResources(double amount) {
    for (std::size_t i = 0; i < resources_.values.size(); ++i) {
        resources_.values[i] += amount;
    }
    coupons_ += 20;
    addPopupText((HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY - 0.8f) * CELL_SIZE,
                 "+" + std::to_string(static_cast<int>(amount)) + " ALL RESOURCES!", 110, 245, 255);
}

void World::setLightingTimeMode(int mode) {
    int clamped = std::clamp(mode, 0, 3);
    if (lightingTimeMode_ != clamped) {
        lightingTimeMode_ = clamped;
        recomputeNetworks();
    }
}

float World::effectiveDayPhase() const {
    if (lightingTimeMode_ == 1) return 0.08f * TWO_PI; // Bright clear morning-noon sun
    if (lightingTimeMode_ == 2) return 0.50f * TWO_PI; // Deep neon midnight
    if (lightingTimeMode_ == 3) return 0.27f * TWO_PI; // Long-shadow golden dusk
    return (dayTime_ / DAY_CYCLE_SECONDS) * TWO_PI;
}

float World::solarFactor() const {
    if (lightingTimeMode_ == 1) return 1.0f;
    if (lightingTimeMode_ == 2) return 0.0f;
    if (lightingTimeMode_ == 3) return 0.42f;
    float phase = effectiveDayPhase();
    float raw = std::cos(phase) * 0.72f + 0.38f;
    return std::clamp(raw, 0.0f, 1.0f);
}

void World::spawnTerrainBlob(int cx, int cy, TerrainType terrain, int radius, uint32_t& rngState) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int gx = cx + dx;
            int gy = cy + dy;
            if (!inBounds(gx, gy)) continue;
            if (std::abs(gx - HUB_GX) <= 4 && std::abs(gy - HUB_GY) <= 4) continue;
            float dist = std::hypot(static_cast<float>(dx), static_cast<float>(dy));
            float chance = 1.0f - dist / (static_cast<float>(radius) + 0.65f);
            if ((dx == 0 && dy == 0) || randFloat01(rngState) < chance * 1.25f) {
                Tile& t = tileMut(gx, gy);
                if (t.ore == OreType::None && t.occupantId == -1) {
                    t.terrain = terrain;
                    if (terrain == TerrainType::Water) {
                        t.elevation = 0.0f;
                    } else if (terrain == TerrainType::Rock) {
                        t.elevation = std::clamp(0.72f + 0.24f * chance, 0.60f, 1.0f);
                    }
                }
            }
        }
    }
}

void World::spawnOreCluster(int cx, int cy, OreType ore, int radius, float baseRichness, uint32_t& rngState) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int gx = cx + dx;
            int gy = cy + dy;
            if (!inBounds(gx, gy)) continue;
            if (std::abs(gx - HUB_GX) <= 3 && std::abs(gy - HUB_GY) <= 3) continue;
            float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            float threshold = static_cast<float>(radius) + 0.35f;
            float chance = 1.0f - (dist / (threshold + 0.25f));
            if (dx == 0 && dy == 0) chance = 1.0f;
            if (randFloat01(rngState) < chance * 1.15f) {
                Tile& t = tileMut(gx, gy);
                if (t.ore == OreType::None) {
                    t.terrain = TerrainType::Ground;
                    t.elevation = std::clamp(t.elevation, 0.16f, 0.32f);
                    t.ore = ore;
                    t.richness = baseRichness;
                    if (dx == 0 && dy == 0 && baseRichness < 3.6f) {
                        t.richness = baseRichness + 0.25f * genSettings_.oreRichness;
                    }
                }
            }
        }
    }
}

void World::generateMap(uint32_t seed) {
    // Mix preset into terrain noise seed so changing Preset on the same seed transforms geography deterministically
    const uint32_t presetSalt = static_cast<uint32_t>(genSettings_.preset) * 0x45D9F3Bu;
    const uint32_t geoSeed = seed ^ presetSalt ^ 0xA511E9B3u;
    uint32_t rng = geoSeed ? geoSeed : 0x9E3779B9u;

    float presetWaterScale = 1.0f;
    float presetCliffScale = 1.0f;
    float presetFreqScale = 1.0f;
    float presetRichScale = 1.0f;
    float noiseScale = 0.038f;
    float warpAmp = 10.5f;

    switch (genSettings_.preset) {
    case WorldPreset::BalancedValley:
        presetWaterScale = 1.0f;
        presetCliffScale = 1.0f;
        noiseScale = 0.038f;
        warpAmp = 10.5f;
        break;
    case WorldPreset::RiverArchipelago:
        presetWaterScale = 1.75f;
        presetCliffScale = 0.72f;
        noiseScale = 0.033f;
        warpAmp = 14.0f;
        break;
    case WorldPreset::BasaltCanyons:
        presetWaterScale = 0.75f;
        presetCliffScale = 1.75f;
        noiseScale = 0.044f;
        warpAmp = 12.5f;
        break;
    case WorldPreset::MegabasePlains:
        presetWaterScale = 0.42f;
        presetCliffScale = 0.30f;
        noiseScale = 0.028f;
        warpAmp = 7.0f;
        break;
    case WorldPreset::AetherBadlands:
        presetWaterScale = 0.65f;
        presetCliffScale = 1.25f;
        presetFreqScale = 1.25f;
        presetRichScale = 1.25f;
        noiseScale = 0.042f;
        warpAmp = 13.0f;
        break;
    default:
        break;
    }

    const float effWater = genSettings_.waterCoverage * presetWaterScale;
    const float effCliff = genSettings_.cliffCoverage * presetCliffScale;
    const float effOreFreq = genSettings_.oreFrequency * presetFreqScale;
    const float effOreRich = genSettings_.oreRichness * presetRichScale;

    const int cx = HUB_GX;
    const int cy = HUB_GY;

    // 1. Continuous Domain-Warped fBm Biomes, Elevation, Winding Rivers/Lakes & Ridged Basalt Mesas
    const float waterThreshold = (effWater <= 0.02f)
        ? -1.0f
        : std::clamp(0.24f + 0.105f * (effWater - 1.0f), 0.08f, 0.46f);
    const float cliffThreshold = (effCliff <= 0.02f)
        ? 2.0f
        : std::clamp(0.755f - 0.105f * (effCliff - 1.0f), 0.54f, 0.92f);

    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            Tile& t = tileMut(x, y);
            t.visualSeed = xorshift32(rng);

            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);

            // Domain warp coordinates for organic river bends and canyon walls
            float wx = fx + warpAmp * (fbm2D(fx * 0.022f, fy * 0.022f, geoSeed + 101u, 3) - 0.5f);
            float wy = fy + warpAmp * (fbm2D(fx * 0.022f + 37.3f, fy * 0.022f + 91.7f, geoSeed + 211u, 3) - 0.5f);

            float cont = fbm2D(wx * noiseScale, wy * noiseScale, geoSeed + 307u, 4);
            float ridge = ridgedFbm2D(wx * (noiseScale * 1.18f), wy * (noiseScale * 1.18f), geoSeed + 409u, 4);
            float temp = fbm2D(wx * 0.019f + 120.0f, wy * 0.019f - 45.0f, geoSeed + 503u, 3);
            float moist = fbm2D(wx * 0.024f - 65.0f, wy * 0.024f + 80.0f, geoSeed + 601u, 3);

            float distHub = std::hypot(fx - static_cast<float>(cx), fy - static_cast<float>(cy));
            // Smooth starter plateau sanctuary within 17 tiles of Grand Core
            float hubSafe = std::clamp((distHub - 11.0f) / 8.0f, 0.0f, 1.0f);

            // Biome assignment (0=Slate, 1=RustMesa, 2=MossBasin, 3=ObsidianRift, 4=QuartzTundra)
            uint8_t biome = 0;
            if (genSettings_.preset == WorldPreset::AetherBadlands && distHub > 20.0f && temp > 0.44f) {
                biome = (moist > 0.52f) ? 4 : 3;
            } else if (moist > 0.58f && cont < 0.48f) {
                biome = 2; // Coolant Moss Basin
            } else if (temp > 0.60f) {
                biome = (distHub > 46.0f && ridge > 0.62f) ? 3 : 1; // Obsidian Rift or Rust Mesa
            } else if (temp < 0.37f) {
                biome = 4; // Quartz Tundra
            } else {
                biome = 0; // Clockwork Slate
            }
            t.biome = biome;

            // River channel ribbon for RiverArchipelago / BalancedValley
            bool riverChannel = false;
            if (effWater > 0.25f && hubSafe > 0.65f) {
                float riverWave = std::abs(fbm2D(wx * 0.016f, wy * 0.016f, geoSeed + 709u, 3) - 0.5f);
                float riverWidth = (genSettings_.preset == WorldPreset::RiverArchipelago)
                    ? (0.032f * std::min(effWater, 1.8f))
                    : (0.016f * std::min(effWater, 1.5f));
                if (riverWave < riverWidth && cont < 0.58f) {
                    riverChannel = true;
                }
            }

            float cliffScore = 0.58f * ridge + 0.42f * cont;
            if (genSettings_.preset == WorldPreset::BasaltCanyons) {
                cliffScore = 0.74f * ridge + 0.26f * cont;
            }

            if (hubSafe > 0.55f && (cont < waterThreshold || riverChannel)) {
                t.terrain = TerrainType::Water;
                t.elevation = 0.0f;
                t.biome = 2;
            } else if (hubSafe > 0.75f && cliffScore > cliffThreshold) {
                t.terrain = TerrainType::Rock;
                float normCliff = std::clamp((cliffScore - cliffThreshold) / std::max(0.08f, 1.0f - cliffThreshold), 0.0f, 1.0f);
                t.elevation = std::clamp(0.60f + 0.40f * normCliff, 0.58f, 1.0f);
            } else {
                t.terrain = TerrainType::Ground;
                t.elevation = std::clamp(0.14f + 0.22f * cont, 0.12f, 0.36f);
            }
        }
    }

    // 2. Starter Ore Veins & Coolant Pond right near the 3x3 Grand Core (seed-rotated slightly for variety!)
    float angleOffset = randFloat01(rng) * TWO_PI;
    auto starterPos = [&](float baseAngle, float radius) -> std::pair<int, int> {
        float a = baseAngle + angleOffset;
        int gx = cx + static_cast<int>(std::round(std::cos(a) * radius));
        int gy = cy + static_cast<int>(std::round(std::sin(a) * radius));
        return {gx, gy};
    };

    auto [fe1x, fe1y] = starterPos(0.0f, 5.6f);
    auto [cu1x, cu1y] = starterPos(2.05f, 5.6f);
    auto [co1x, co1y] = starterPos(4.15f, 6.2f);
    auto [fe2x, fe2y] = starterPos(1.05f, 7.2f);
    auto [cu2x, cu2y] = starterPos(3.15f, 7.2f);

    spawnOreCluster(fe1x, fe1y, OreType::Iron, 2, 1.15f * effOreRich, rng);
    spawnOreCluster(cu1x, cu1y, OreType::Copper, 2, 1.15f * effOreRich, rng);
    spawnOreCluster(co1x, co1y, OreType::Coal, 2, 1.15f * effOreRich, rng);
    spawnOreCluster(fe2x, fe2y, OreType::Iron, 2, 1.30f * effOreRich, rng);
    spawnOreCluster(cu2x, cu2y, OreType::Copper, 2, 1.20f * effOreRich, rng);

    // Guaranteed Starter Coolant Pond near Hub when water is enabled
    if (effWater > 0.05f) {
        auto [wx, wy] = starterPos(5.20f, 9.8f);
        spawnTerrainBlob(wx, wy, TerrainType::Water, 2, rng);
    }

    // 3. Seeded Distance-Ring Ore Deposits scaled by Ore Frequency & Ore Richness
    auto scaledCount = [&](int baseCount) -> int {
        return std::max(1, static_cast<int>(std::round(static_cast<float>(baseCount) * effOreFreq)));
    };

    auto tryCluster = [&](OreType ore, float minDist, float maxDist, int radius, float richness, int count) {
        int placed = 0;
        int maxAttempts = std::max(400, count * 45);
        int attempts = 0;
        while (placed < count && attempts < maxAttempts) {
            ++attempts;
            int gx = 4 + static_cast<int>(xorshift32(rng) % static_cast<uint32_t>(GRID_W - 8));
            int gy = 4 + static_cast<int>(xorshift32(rng) % static_cast<uint32_t>(GRID_H - 8));
            float d = std::hypot(static_cast<float>(gx - cx), static_cast<float>(gy - cy));
            if (d < minDist || d > maxDist) continue;
            const Tile& center = tile(gx, gy);
            if (center.ore != OreType::None || center.terrain != TerrainType::Ground) continue;
            spawnOreCluster(gx, gy, ore, radius, richness * effOreRich, rng);
            ++placed;
        }
    };

    // Inner-plateau ring (10..26 tiles: Fe/Cu/Co + starter Titanium!)
    tryCluster(OreType::Iron, 10.f, 25.f, 2, 1.30f, scaledCount(7));
    tryCluster(OreType::Copper, 10.f, 25.f, 2, 1.30f, scaledCount(7));
    tryCluster(OreType::Coal, 10.f, 25.f, 2, 1.30f, scaledCount(6));
    tryCluster(OreType::Titanium, 14.f, 26.f, 2, 1.25f, scaledCount(4));

    // Mid-frontier outposts (27..58 tiles: rich Titanium, Uranium, Fe/Cu/Co, early Aether)
    tryCluster(OreType::Iron, 27.f, 58.f, 3, 1.75f, scaledCount(12));
    tryCluster(OreType::Copper, 27.f, 58.f, 3, 1.75f, scaledCount(12));
    tryCluster(OreType::Coal, 27.f, 58.f, 3, 1.75f, scaledCount(9));
    tryCluster(OreType::Titanium, 27.f, 58.f, 3, 1.70f, scaledCount(10));
    tryCluster(OreType::Uranium, 28.f, 58.f, 2, 1.60f, scaledCount(7));
    tryCluster(OreType::Aether, 33.f, 58.f, 2, 1.55f, scaledCount(5));

    // Deep frontier mega-deposits (59..114 tiles: massive 2.4x-3.0x veins!)
    tryCluster(OreType::Iron, 59.f, 114.f, 3, 2.40f, scaledCount(12));
    tryCluster(OreType::Copper, 59.f, 114.f, 3, 2.40f, scaledCount(12));
    tryCluster(OreType::Coal, 59.f, 114.f, 3, 2.40f, scaledCount(9));
    tryCluster(OreType::Titanium, 59.f, 114.f, 3, 2.50f, scaledCount(12));
    tryCluster(OreType::Uranium, 56.f, 114.f, 3, 2.45f, scaledCount(10));
    tryCluster(OreType::Aether, 55.f, 114.f, 3, 2.70f, scaledCount(12));
}

bool World::inBounds(int gx, int gy) const {
    return gx >= 0 && gx < GRID_W && gy >= 0 && gy < GRID_H;
}

const Tile& World::tile(int gx, int gy) const {
    return tiles_[static_cast<std::size_t>(tileIndex(gx, gy))];
}

Tile& World::tileMut(int gx, int gy) {
    return tiles_[static_cast<std::size_t>(tileIndex(gx, gy))];
}

int World::findBuildingIndexById(int id) const {
    if (id <= 0) return -1;
    for (std::size_t i = 0; i < buildings_.size(); ++i) {
        if (buildings_[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const BuildingEntity* World::buildingById(int id) const {
    int idx = findBuildingIndexById(id);
    return idx >= 0 ? &buildings_[static_cast<std::size_t>(idx)] : nullptr;
}

BuildingEntity* World::buildingByIdMut(int id) {
    int idx = findBuildingIndexById(id);
    return idx >= 0 ? &buildings_[static_cast<std::size_t>(idx)] : nullptr;
}

const BuildingEntity* World::buildingAt(int gx, int gy) const {
    if (!inBounds(gx, gy)) return nullptr;
    return buildingById(tile(gx, gy).occupantId);
}

BuildingEntity* World::buildingAtMut(int gx, int gy) {
    if (!inBounds(gx, gy)) return nullptr;
    int idx = findBuildingIndexById(tile(gx, gy).occupantId);
    return idx >= 0 ? &buildings_[static_cast<std::size_t>(idx)] : nullptr;
}

const NetworkInfo* World::networkById(int id) const {
    if (id < 0 || id >= static_cast<int>(networks_.size())) return nullptr;
    return &networks_[static_cast<std::size_t>(id)];
}

const ElectricNetworkInfo* World::electricNetworkById(int id) const {
    if (id < 0 || id >= static_cast<int>(electricNetworks_.size())) return nullptr;
    return &electricNetworks_[static_cast<std::size_t>(id)];
}

PlacementCheck World::checkPlacement(BuildingType type, int gx, int gy, int /*dir*/) const {
    if (type == BuildingType::HubCore) {
        return {false, false, "The Grand Core is unique"};
    }
    if (!isBuildingUnlocked(type)) {
        return {false, false, Catalog::buildingUnlockHint(type)};
    }
    const BuildingDef& def = Catalog::getBuilding(type);

    if (def.isTrainSpawner) {
        if (!inBounds(gx, gy)) {
            return {false, false, "Out of map bounds"};
        }
        const BuildingEntity* occ = buildingAt(gx, gy);
        if (!occ || !isRailType(occ->type)) {
            return {false, false, "Trains must be placed on Rail Track or Train Station"};
        }
        float wx = (static_cast<float>(gx) + 0.5f) * CELL_SIZE;
        float wy = (static_cast<float>(gy) + 0.5f) * CELL_SIZE;
        for (const TrainEntity& tr : trains_) {
            if (std::hypot(tr.wx - wx, tr.wy - wy) < CELL_SIZE * 0.45f) {
                return {false, false, "Another train is parked on this tile"};
            }
        }
        if (!sandboxMode_ && !resources_.canAfford(def.cost)) {
            return {false, false, "Not enough resources"};
        }
        return {true, false, ""};
    }

    // Check in-place upgrade for 1x1 structures (Belts & Drills)
    bool upgradingInPlace = false;
    ResourceBag effectiveCost = def.cost;
    if (def.footprintRadius == 0 && inBounds(gx, gy)) {
        if (const BuildingEntity* existing = buildingAt(gx, gy)) {
            if (existing->gx == gx && existing->gy == gy && canUpgradeBuildingInPlace(existing->type, type)) {
                upgradingInPlace = true;
                const ResourceBag& oldCost = Catalog::getBuilding(existing->type).cost;
                for (std::size_t i = 0; i < effectiveCost.values.size(); ++i) {
                    effectiveCost.values[i] = std::max(0.0, effectiveCost.values[i] - oldCost.values[i]);
                }
            }
        }
    }

    const int rad = def.footprintRadius;
    for (int dy = -rad; dy <= rad; ++dy) {
        for (int dx = -rad; dx <= rad; ++dx) {
            int cx = gx + dx;
            int cy = gy + dy;
            if (!inBounds(cx, cy)) {
                return {false, false, "Out of map bounds"};
            }
            const Tile& tl = tile(cx, cy);
            if (tl.terrain == TerrainType::Rock) {
                return {false, false, "Basalt cliff! Tunnel underneath with Underground Belts or span with Power Poles"};
            }
            if (tl.terrain == TerrainType::Water) {
                bool canCrossWater = (type == BuildingType::WaterPump ||
                                      type == BuildingType::RailTrack ||
                                      type == BuildingType::AxleBridge);
                if (!canCrossWater) {
                    return {false, false, "Coolant Water! Only Hydro Pumps, Rails & Axle Bridges can build on water"};
                }
            }
            if (!upgradingInPlace && tl.occupantId != -1) {
                return {false, false, "Tile is occupied"};
            }
        }
    }

    if (def.requiresWater) {
        bool nearWater = false;
        for (int dy = -1; dy <= 1 && !nearWater; ++dy) {
            for (int dx = -1; dx <= 1 && !nearWater; ++dx) {
                if (inBounds(gx + dx, gy + dy) && tile(gx + dx, gy + dy).terrain == TerrainType::Water) {
                    nearWater = true;
                }
            }
        }
        if (!nearWater) {
            return {false, false, "Hydro Pump must be placed on or adjacent to Water"};
        }
    }

    if (def.requiresOre) {
        const OreType ore = tile(gx, gy).ore;
        if (ore == OreType::None) {
            return {false, false, "Drills must be placed on an Ore Vein"};
        }
        if (def.minDrillTier < oreTierRequired(ore)) {
            int req = oreTierRequired(ore);
            return {false, false, oreName(ore) + (req >= 3 ? " requires Tier 3 Aether Extractor" : " requires Tier 2+ Drill")};
        }
    }

    if (!sandboxMode_ && !resources_.canAfford(effectiveCost)) {
        return {false, false, "Not enough resources"};
    }

    return {true, upgradingInPlace, ""};
}

bool World::placeBuilding(BuildingType type, int gx, int gy, int dir, std::string* failReason) {
    PlacementCheck check = checkPlacement(type, gx, gy, dir);
    if (!check.valid) {
        if (failReason) *failReason = check.reason;
        return false;
    }

    const BuildingDef& def = Catalog::getBuilding(type);

    if (check.isUpgradeReplace) {
        BuildingEntity* existing = buildingAtMut(gx, gy);
        if (existing) {
            if (!sandboxMode_) {
                const BuildingDef& oldDef = Catalog::getBuilding(existing->type);
                resources_.add(oldDef.cost);
                resources_.subtract(def.cost);
            }
            existing->type = type;
            if (def.rotatable && dir >= 0) {
                existing->dir = ((dir % 4) + 4) % 4;
            }
            recomputeNetworks();
            return true;
        }
    }

    if (!sandboxMode_) {
        resources_.subtract(def.cost);
    }

    if (def.isTrainSpawner) {
        TrainEntity tr;
        tr.id = nextTrainId_++;
        tr.isElectric = (type == BuildingType::ElectricTrain);
        tr.wx = (static_cast<float>(gx) + 0.5f) * CELL_SIZE;
        tr.wy = (static_cast<float>(gy) + 0.5f) * CELL_SIZE;
        tr.state = TrainState::Idle;
        tr.waitTimer = 0.1f;
        if (!tr.isElectric) {
            if (sandboxMode_) {
                tr.fuelTimer = def.fuelInterval;
                tr.hasFuel = true;
            } else if (resources_[ResourceType::Coal] >= 1.0) {
                resources_[ResourceType::Coal] -= 1.0;
                tr.fuelTimer = def.fuelInterval;
                tr.hasFuel = true;
            } else {
                tr.fuelTimer = 0.0f;
                tr.hasFuel = false;
            }
        } else {
            tr.hasFuel = true;
        }
        trains_.push_back(tr);
        stats_.trainCount = static_cast<int>(trains_.size());
        return true;
    }

    BuildingEntity entity;
    entity.id = nextBuildingId_++;
    entity.type = type;
    entity.gx = gx;
    entity.gy = gy;

    if (type == BuildingType::TrainStation) {
        entity.dir = (dir < 0) ? (isInDirectHubZone(gx, gy) ? 1 : 0) : (dir % 2);
    } else {
        entity.dir = def.rotatable ? (((dir % 4) + 4) % 4) : 0;
    }

    if (def.fuelType != ResourceType::Count) {
        float fuelMult = 1.0f;
        if (type == BuildingType::SteamEngine) fuelMult = boilerFuelMultiplier();
        else if (type == BuildingType::NuclearReactor || type == BuildingType::FusionReactor) fuelMult = nuclearFuelMultiplier();

        if (sandboxMode_) {
            entity.fuelTimer = def.fuelInterval * fuelMult;
            entity.hasFuel = true;
        } else if (resources_[def.fuelType] >= 1.0) {
            resources_[def.fuelType] -= 1.0;
            entity.fuelTimer = def.fuelInterval * fuelMult;
            entity.hasFuel = true;
        } else {
            entity.fuelTimer = 0.0f;
            entity.hasFuel = false;
        }
    }

    const int rad = def.footprintRadius;
    for (int dy = -rad; dy <= rad; ++dy) {
        for (int dx = -rad; dx <= rad; ++dx) {
            tileMut(gx + dx, gy + dy).occupantId = entity.id;
        }
    }

    buildings_.push_back(entity);
    recomputeNetworks();
    return true;
}

bool World::removeTrainNear(float wx, float wy, float maxDistPx, ResourceBag* refundedOut) {
    int bestIdx = -1;
    float bestDist = maxDistPx;
    for (std::size_t i = 0; i < trains_.size(); ++i) {
        float d = std::hypot(trains_[i].wx - wx, trains_[i].wy - wy);
        if (d <= bestDist) {
            bestDist = d;
            bestIdx = static_cast<int>(i);
        }
    }
    if (bestIdx < 0) return false;
    const TrainEntity& tr = trains_[static_cast<std::size_t>(bestIdx)];
    const BuildingDef& def = Catalog::getBuilding(
        tr.isElectric ? BuildingType::ElectricTrain : BuildingType::SteamTrain);
    if (!sandboxMode_) {
        resources_.add(def.cost);
    }
    resources_.add(tr.cargo);
    if (refundedOut) *refundedOut = def.cost;
    trains_.erase(trains_.begin() + bestIdx);
    stats_.trainCount = static_cast<int>(trains_.size());
    return true;
}

bool World::removeBuildingAt(int gx, int gy, ResourceBag* refundedOut) {
    const BuildingEntity* existing = buildingAt(gx, gy);
    if (!existing || existing->type == BuildingType::HubCore) {
        return false;
    }
    const int targetId = existing->id;
    const BuildingType targetType = existing->type;
    const BuildingDef& def = Catalog::getBuilding(targetType);
    const int cx = existing->gx;
    const int cy = existing->gy;
    const int rad = def.footprintRadius;

    for (int dy = -rad; dy <= rad; ++dy) {
        for (int dx = -rad; dx <= rad; ++dx) {
            if (inBounds(cx + dx, cy + dy)) {
                tileMut(cx + dx, cy + dy).occupantId = -1;
            }
        }
    }

    if (!sandboxMode_) {
        resources_.add(def.cost);
    }
    resources_.add(existing->localStorage);
    if (existing->craftedCount > 0 && existing->craftedType != ResourceType::Count) {
        resources_[existing->craftedType] += static_cast<double>(existing->craftedCount);
    }
    if (existing->beltItemA != ResourceType::Count) {
        resources_[existing->beltItemA] += 1.0;
    }
    if (existing->beltItemB != ResourceType::Count) {
        resources_[existing->beltItemB] += 1.0;
    }
    if (refundedOut) *refundedOut = def.cost;

    int idx = findBuildingIndexById(targetId);
    if (idx >= 0) {
        buildings_.erase(buildings_.begin() + idx);
    }

    if (isRailType(targetType)) {
        for (TrainEntity& tr : trains_) {
            tr.path.clear();
            tr.pathIndex = 0;
            if (tr.targetStationId == targetId ||
                tr.state == TrainState::MovingToLoad ||
                tr.state == TrainState::MovingToUnload) {
                tr.targetStationId = -1;
                tr.state = TrainState::Idle;
                tr.waitTimer = 0.2f;
            }
        }
    }

    recomputeNetworks();
    return true;
}

bool World::rotateBuildingAt(int gx, int gy) {
    BuildingEntity* entity = buildingAtMut(gx, gy);
    if (!entity) return false;
    const BuildingDef& def = Catalog::getBuilding(entity->type);
    if (!def.rotatable) return false;
    if (entity->type == BuildingType::TrainStation) {
        entity->dir = (entity->dir + 1) % 2;
    } else {
        entity->dir = (entity->dir + 1) % 4;
    }
    recomputeNetworks();
    return true;
}

bool World::crankAt(int gx, int gy, float boostDuration) {
    BuildingEntity* entity = buildingAtMut(gx, gy);
    if (!entity || entity->type != BuildingType::HandCrank) return false;
    bool wasUnboosted = entity->crankBoostTimer <= 0.05f;
    entity->crankBoostTimer = std::max(entity->crankBoostTimer, boostDuration);
    if (wasUnboosted) {
        recomputeNetworks();
    }
    return true;
}

bool World::manualFeedMachineAt(int gx, int gy) {
    BuildingEntity* b = buildingAtMut(gx, gy);
    if (!b) return false;
    bool fed = false;
    auto pull = [&](ResourceType rt, double maxBuf, double batch) {
        double room = std::max(0.0, maxBuf - b->localStorage[rt]);
        double amt = sandboxMode_
            ? std::min(room, batch * 2.0)
            : std::min({room, batch, std::floor(resources_[rt])});
        if (amt >= 1.0) {
            if (!sandboxMode_) {
                resources_[rt] -= amt;
            }
            b->localStorage[rt] += amt;
            fed = true;
        }
    };
    switch (b->type) {
    case BuildingType::StoneSmelter:
        pull(ResourceType::Iron, 16.0, 6.0);
        pull(ResourceType::Coal, 8.0, 3.0);
        break;
    case BuildingType::ArcFurnace:
        pull(ResourceType::Iron, 20.0, 8.0);
        break;
    case BuildingType::Assembler:
        pull(ResourceType::Copper, 16.0, 6.0);
        pull(ResourceType::Iron, 12.0, 4.0);
        break;
    case BuildingType::QuantumForge:
        pull(ResourceType::Steel, 12.0, 4.0);
        pull(ResourceType::Circuit, 12.0, 4.0);
        pull(ResourceType::Titanium, 10.0, 2.0);
        pull(ResourceType::Aether, 10.0, 2.0);
        break;
    case BuildingType::SteamEngine:
        pull(ResourceType::Coal, 10.0, 4.0);
        break;
    case BuildingType::NuclearReactor:
        pull(ResourceType::Uranium, 10.0, 3.0);
        break;
    case BuildingType::FusionReactor:
    case BuildingType::AetherDynamo:
        pull(ResourceType::Aether, 10.0, 3.0);
        break;
    default:
        break;
    }
    return fed;
}

bool World::canBuyUpgrade(UpgradeId id) const {
    const UpgradeDef& def = Catalog::getUpgrade(id);
    int cur = getUpgradeLevel(id);
    if (cur >= def.maxLevel) return false;
    if (sandboxMode_) return true;
    if (!Catalog::canMeetPrerequisites(id, upgradeLevels_)) return false;
    return resources_.canAfford(Catalog::upgradeCost(id, cur));
}

bool World::buyUpgrade(UpgradeId id) {
    if (!canBuyUpgrade(id)) return false;
    int cur = getUpgradeLevel(id);
    if (!sandboxMode_) {
        resources_.subtract(Catalog::upgradeCost(id, cur));
    }
    upgradeLevels_[static_cast<std::size_t>(id)] = cur + 1;
    stats_.hasUnlockedUpgrade = true;
    recomputeNetworks();
    return true;
}

int World::getUpgradeLevel(UpgradeId id) const {
    if (id == UpgradeId::Count) return 0;
    return upgradeLevels_[static_cast<std::size_t>(id)];
}

void World::setUpgradeLevelForTest(UpgradeId id, int level) {
    if (id != UpgradeId::Count) {
        upgradeLevels_[static_cast<std::size_t>(id)] = level;
        recomputeNetworks();
    }
}

bool World::canCompleteSpirePhase() const {
    if (sandboxMode_) return true;
    int idx = std::min(spirePhase_, Catalog::spirePhaseCount() - 1);
    const SpirePhaseDef& sp = Catalog::getSpirePhase(idx);
    ResourceBag req = sp.requirement;
    if (spirePhase_ >= Catalog::spirePhaseCount()) {
        double scale = std::pow(1.45, static_cast<double>(prestigeCount_ + 1));
        for (double& v : req.values) {
            if (v > 0.0) v = std::round(v * scale);
        }
    }
    return resources_.canAfford(req);
}

bool World::completeSpirePhase() {
    if (!canCompleteSpirePhase()) return false;
    int idx = std::min(spirePhase_, Catalog::spirePhaseCount() - 1);
    const SpirePhaseDef& sp = Catalog::getSpirePhase(idx);
    ResourceBag req = sp.requirement;
    if (spirePhase_ >= Catalog::spirePhaseCount()) {
        double scale = std::pow(1.45, static_cast<double>(prestigeCount_ + 1));
        for (double& v : req.values) {
            if (v > 0.0) v = std::round(v * scale);
        }
        if (!sandboxMode_) resources_.subtract(req);
        ++prestigeCount_;
        coupons_ += 8;
        addPopupText((HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY - 1.0f) * CELL_SIZE,
                     "PRESTIGE ASCENSION #" + std::to_string(prestigeCount_) + "!", 255, 224, 96);
    } else {
        if (!sandboxMode_) resources_.subtract(req);
        ++spirePhase_;
        static constexpr int BONUS_COUPONS[5] = {2, 3, 4, 6, 10};
        coupons_ += BONUS_COUPONS[idx];
        addPopupText((HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY - 1.0f) * CELL_SIZE,
                     "SPIRE PHASE " + std::to_string(spirePhase_) + " COMPLETE!", 255, 218, 92);
    }
    addSparkBurst((HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY + 0.5f) * CELL_SIZE, 255, 225, 110, 28);
    recomputeNetworks();
    return true;
}

double World::nextCouponCost() const {
    return 100.0 + 30.0 * static_cast<double>(totalCouponsMinted_);
}

int World::couponItemCost(CouponItem item) const {
    int lv = couponLevel(item);
    switch (item) {
    case CouponItem::BeltAndCraftSpeed: return 1 + lv;
    case CouponItem::GridAndShaftPower: return 1 + lv;
    case CouponItem::MiningYield:       return 1 + lv;
    case CouponItem::CarePackage:       return 2;
    default:                            return 1;
    }
}

bool World::spendCoupon(CouponItem item) {
    int cost = couponItemCost(item);
    if (!sandboxMode_ && coupons_ < cost) return false;
    if (!sandboxMode_) {
        coupons_ -= cost;
    }
    if (item == CouponItem::CarePackage) {
        resources_[ResourceType::Steel] += 90.0;
        resources_[ResourceType::Circuit] += 55.0;
        resources_[ResourceType::Titanium] += 45.0;
        resources_[ResourceType::Uranium] += 15.0;
        resources_[ResourceType::Aether] += 10.0;
        addPopupText((HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY + 0.5f) * CELL_SIZE,
                     "+ORBITAL CARE PACKAGE!", 110, 242, 255);
    } else {
        couponLevels_[static_cast<std::size_t>(item)] += 1;
    }
    recomputeNetworks();
    return true;
}

void World::addSinkItem(ResourceType item, double count) {
    double pts = resourceSinkValue(item) * count * sinkPointMultiplier();
    sinkPoints_ += pts;
    while (sinkPoints_ >= nextCouponCost()) {
        sinkPoints_ -= nextCouponCost();
        ++coupons_;
        ++totalCouponsMinted_;
        addPopupText((HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY - 0.5f) * CELL_SIZE,
                     "+1 SINK COUPON!", 255, 212, 88);
    }
}

float World::globalPrestigeMultiplier() const {
    float phase5Bonus = (spirePhase_ >= 5) ? 0.20f : 0.0f;
    float planetaryBonus = 0.25f * static_cast<float>(getUpgradeLevel(UpgradeId::PlanetaryLogistics));
    return 1.0f + phase5Bonus + 0.20f * static_cast<float>(prestigeCount_) + planetaryBonus;
}

float World::drillSpeedMultiplier() const {
    float spireBonus = (spirePhase_ >= 1) ? 1.15f : 1.0f;
    return (1.0f + 0.25f * static_cast<float>(getUpgradeLevel(UpgradeId::DrillSpeed))
                 + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::CoreFracking)))
           * spireBonus * globalPrestigeMultiplier();
}

float World::beltedMineBonusMultiplier() const {
    return 1.25f + 0.15f * static_cast<float>(getUpgradeLevel(UpgradeId::SmartLoaders));
}

float World::oreYieldMultiplier() const {
    float spireBonus = (spirePhase_ >= 4) ? 1.30f : 1.0f;
    float couponBonus = 1.0f + 0.12f * static_cast<float>(couponLevel(CouponItem::MiningYield));
    return (1.0f + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::OreYield)))
           * spireBonus * couponBonus * globalPrestigeMultiplier();
}

float World::richOreBonusMultiplier() const {
    return 1.0f + 0.35f * static_cast<float>(getUpgradeLevel(UpgradeId::CoreFracking));
}

float World::craftingSpeedMultiplier() const {
    float spireBonus = (spirePhase_ >= 2) ? 1.20f : 1.0f;
    float couponBonus = 1.0f + 0.12f * static_cast<float>(couponLevel(CouponItem::BeltAndCraftSpeed));
    return (1.0f + 0.22f * static_cast<float>(getUpgradeLevel(UpgradeId::SmeltingMastery))
                 + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::SmartLoaders))
                 + 0.15f * static_cast<float>(getUpgradeLevel(UpgradeId::CoreFracking)))
           * spireBonus * couponBonus * globalPrestigeMultiplier();
}

float World::beltSpeedMultiplier() const {
    float spireBonus = (spirePhase_ >= 3) ? 1.25f : 1.0f;
    float couponBonus = 1.0f + 0.12f * static_cast<float>(couponLevel(CouponItem::BeltAndCraftSpeed));
    float magRollerBonus = 1.0f + 0.18f * static_cast<float>(getUpgradeLevel(UpgradeId::BeltCompression));
    return spireBonus * couponBonus * magRollerBonus;
}

float World::stressMultiplier() const {
    float reduction = 0.12f * static_cast<float>(getUpgradeLevel(UpgradeId::PrecisionBearings))
                    + 0.08f * static_cast<float>(getUpgradeLevel(UpgradeId::MagneticBearings));
    return std::max(0.22f, 1.0f - reduction);
}

float World::mechanicalCapMultiplier() const {
    float couponBonus = 1.0f + 0.15f * static_cast<float>(couponLevel(CouponItem::GridAndShaftPower));
    return (1.0f + 0.25f * static_cast<float>(getUpgradeLevel(UpgradeId::MagneticBearings)))
           * couponBonus * globalPrestigeMultiplier();
}

float World::springPowerMultiplier() const {
    return (1.0f + 0.22f * static_cast<float>(getUpgradeLevel(UpgradeId::SpringTension))) * mechanicalCapMultiplier();
}

float World::windPowerMultiplier() const {
    return springPowerMultiplier()
         * (1.0f + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::SolarEfficiency)));
}

float World::solarPowerMultiplier() const {
    return 1.0f + 0.25f * static_cast<float>(getUpgradeLevel(UpgradeId::SolarEfficiency));
}

float World::hydroBoostMultiplier() const {
    return 1.35f + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::HydroThermalMastery));
}

float World::coolingTowerBonusMultiplier() const {
    return 1.0f + 0.25f * static_cast<float>(getUpgradeLevel(UpgradeId::HydroThermalMastery));
}

float World::superconductingMult() const {
    return 1.0f + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::SuperconductingGrid));
}

float World::accumulatorCapacityKJ() const {
    float subMult = (getUpgradeLevel(UpgradeId::UnlockSubstations) > 0) ? 1.25f : 1.0f;
    float scMult = 1.0f + 0.30f * static_cast<float>(getUpgradeLevel(UpgradeId::SuperconductingGrid));
    return 1800.0f * subMult * scMult;
}

float World::beaconSpeedMultiplier() const {
    return 1.50f + 0.20f * static_cast<float>(getUpgradeLevel(UpgradeId::BeaconAmplification));
}

int World::beaconAuraRadius() const {
    return (getUpgradeLevel(UpgradeId::BeaconAmplification) > 0) ? 3 : 2;
}

float World::boilerPowerMultiplier() const {
    return (1.0f + 0.35f * static_cast<float>(getUpgradeLevel(UpgradeId::BoilerPressure))) * mechanicalCapMultiplier();
}

float World::boilerFuelMultiplier() const {
    return 1.0f + 0.25f * static_cast<float>(getUpgradeLevel(UpgradeId::BoilerPressure));
}

float World::nuclearPowerMultiplier() const {
    float spireGrid = (spirePhase_ >= 2) ? 1.15f : 1.0f;
    float couponGrid = 1.0f + 0.15f * static_cast<float>(couponLevel(CouponItem::GridAndShaftPower));
    int fuseLv = getUpgradeLevel(UpgradeId::UnlockFusionPower);
    return (1.0f + 0.30f * static_cast<float>(std::max(0, fuseLv - 1)))
           * spireGrid * couponGrid * globalPrestigeMultiplier();
}

float World::nuclearFuelMultiplier() const {
    int fuseLv = getUpgradeLevel(UpgradeId::UnlockFusionPower);
    return 1.0f + 0.25f * static_cast<float>(fuseLv);
}

int World::directHubRadius() const {
    return BASE_DIRECT_RADIUS + 5 * getUpgradeLevel(UpgradeId::HubLogistics);
}

bool World::isInDirectHubZone(int gx, int gy) const {
    int dx = gx - HUB_GX;
    int dy = gy - HUB_GY;
    int r = directHubRadius();
    return dx * dx + dy * dy <= r * r;
}

double World::remoteDrillBufferCapacity() const {
    return 60.0 + 50.0 * static_cast<double>(getUpgradeLevel(UpgradeId::HubLogistics));
}

double World::vaultBufferCapacity() const {
    double base = (getUpgradeLevel(UpgradeId::UnlockFastBelts) > 0) ? 375.0 : 250.0;
    return base + 150.0 * static_cast<double>(getUpgradeLevel(UpgradeId::MatterCompression));
}

double World::stationBufferCapacity() const {
    return 320.0 * (1.0 + 0.25 * static_cast<double>(getUpgradeLevel(UpgradeId::HeavyFreight)));
}

double World::trainCargoCapacity() const {
    double spireMult = (spirePhase_ >= 3) ? 1.25 : 1.0;
    return (90.0 + 55.0 * static_cast<double>(getUpgradeLevel(UpgradeId::HeavyFreight))) * spireMult;
}

double World::freightBonusMultiplier() const {
    return 1.0 + 0.20 * static_cast<double>(getUpgradeLevel(UpgradeId::FreightBonus));
}

double World::sinkPointMultiplier() const {
    return (1.0 + 0.25 * static_cast<double>(getUpgradeLevel(UpgradeId::FreightBonus)))
         * (1.0 + 0.35 * static_cast<double>(getUpgradeLevel(UpgradeId::MatterCompression)));
}

int World::findUndergroundExitIndex(int gx, int gy, int dir) const {
    int dx = 0, dy = 0;
    directionVector(dir, dx, dy);
    for (int step = 2; step <= UNDERGROUND_MAX_DIST; ++step) {
        int nx = gx + dx * step;
        int ny = gy + dy * step;
        if (!inBounds(nx, ny)) break;
        int occId = tile(nx, ny).occupantId;
        if (occId > 0) {
            int idx = findBuildingIndexById(occId);
            if (idx >= 0) {
                const BuildingEntity& nb = buildings_[static_cast<std::size_t>(idx)];
                if (nb.type == BuildingType::UndergroundBelt && nb.dir == dir) {
                    return idx;
                }
            }
        }
    }
    return -1;
}

void World::recomputeAurasAndLubrication() {
    for (BuildingEntity& b : buildings_) {
        b.lubricated = false;
        b.overclocked = false;
        b.waterBoosted = false;

        int rad = Catalog::getBuilding(b.type).footprintRadius;
        for (int dy = -rad - 1; dy <= rad + 1 && !b.waterBoosted; ++dy) {
            for (int dx = -rad - 1; dx <= rad + 1 && !b.waterBoosted; ++dx) {
                if (inBounds(b.gx + dx, b.gy + dy) && tile(b.gx + dx, b.gy + dy).terrain == TerrainType::Water) {
                    b.waterBoosted = true;
                }
            }
        }
    }

    const int ocRad = beaconAuraRadius();
    for (const BuildingEntity& src : buildings_) {
        if (src.type == BuildingType::WaterPump) {
            for (BuildingEntity& b : buildings_) {
                if (std::hypot(static_cast<float>(b.gx - src.gx), static_cast<float>(b.gy - src.gy)) <= 3.5f) {
                    b.waterBoosted = true;
                }
            }
        } else if (src.type == BuildingType::Lubricator &&
                   (src.status == NetworkStatus::Running || src.status == NetworkStatus::Buffering)) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    BuildingEntity* target = buildingAtMut(src.gx + dx, src.gy + dy);
                    if (target && (isDrillType(target->type) || target->type == BuildingType::StoneSmelter)) {
                        target->lubricated = true;
                    }
                }
            }
        } else if (src.type == BuildingType::OverclockBeacon && src.electricPowered) {
            for (int dy = -ocRad; dy <= ocRad; ++dy) {
                for (int dx = -ocRad; dx <= ocRad; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    BuildingEntity* target = buildingAtMut(src.gx + dx, src.gy + dy);
                    if (target && (isDrillType(target->type) || isFactoryCrafter(target->type))) {
                        target->overclocked = true;
                    }
                }
            }
        }
    }
}

void World::solveMechanicalPass() {
    const int nEntities = static_cast<int>(buildings_.size());
    const int nNodes = nEntities * 2;
    networks_.clear();
    mechHasPrimeMover_.clear();

    if (nEntities == 0) return;

    std::vector<bool> nodeActive(static_cast<std::size_t>(nNodes), false);
    std::vector<float> prevRpm(static_cast<std::size_t>(nNodes), 0.0f);
    std::vector<float> prevAngle(static_cast<std::size_t>(nNodes), 0.0f);
    std::vector<int> nodeTeeth(static_cast<std::size_t>(nNodes), 12);

    for (int i = 0; i < nEntities; ++i) {
        const BuildingEntity& b = buildings_[static_cast<std::size_t>(i)];
        const BuildingDef& def = Catalog::getBuilding(b.type);
        if (!def.isMechanical) continue;
        int u0 = 2 * i;
        nodeActive[static_cast<std::size_t>(u0)] = true;
        prevRpm[static_cast<std::size_t>(u0)] = b.rpm;
        prevAngle[static_cast<std::size_t>(u0)] = b.angle;
        nodeTeeth[static_cast<std::size_t>(u0)] = def.teeth;

        if (b.type == BuildingType::AxleBridge || b.type == BuildingType::SpeedGearbox) {
            int u1 = 2 * i + 1;
            nodeActive[static_cast<std::size_t>(u1)] = true;
            prevRpm[static_cast<std::size_t>(u1)] = b.rpmSecondary;
            prevAngle[static_cast<std::size_t>(u1)] = b.angleSecondary;
            nodeTeeth[static_cast<std::size_t>(u1)] = 12;
        }
    }

    auto resolvePort = [&](int nx, int ny, int stepDx, int stepDy) -> int {
        if (!inBounds(nx, ny)) return -1;
        int occId = tile(nx, ny).occupantId;
        if (occId <= 0) return -1;
        int bIdx = findBuildingIndexById(occId);
        if (bIdx < 0) return -1;
        const BuildingEntity& nb = buildings_[static_cast<std::size_t>(bIdx)];
        if (!Catalog::getBuilding(nb.type).isMechanical) return -1;
        if (nb.type == BuildingType::LargeGear) {
            return (nb.gx == nx + stepDx && nb.gy == ny + stepDy) ? (2 * bIdx) : -1;
        }
        if (nb.type == BuildingType::AxleBridge) {
            return (stepDx != 0) ? (2 * bIdx) : (2 * bIdx + 1);
        }
        if (nb.type == BuildingType::SpeedGearbox) {
            int fdx = 0, fdy = 0;
            directionVector(nb.dir, fdx, fdy);
            if (stepDx == fdx && stepDy == fdy) return 2 * bIdx;
            if (stepDx == -fdx && stepDy == -fdy) return 2 * bIdx + 1;
            return -1;
        }
        return 2 * bIdx;
    };

    std::vector<std::vector<MechEdge>> adj(static_cast<std::size_t>(nNodes));
    auto addExternalDirEdge = [&](int fromNode, int edgeX, int edgeY, int dx, int dy) {
        int nx = edgeX + dx;
        int ny = edgeY + dy;
        int toNode = resolvePort(nx, ny, dx, dy);
        if (toNode < 0) return;
        float alpha = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
        adj[static_cast<std::size_t>(fromNode)].push_back(MechEdge{
            toNode, false, 1.0f,
            nodeTeeth[static_cast<std::size_t>(fromNode)],
            nodeTeeth[static_cast<std::size_t>(toNode)],
            alpha
        });
    };

    for (int i = 0; i < nEntities; ++i) {
        const BuildingEntity& b = buildings_[static_cast<std::size_t>(i)];
        if (!Catalog::getBuilding(b.type).isMechanical) continue;
        if (b.type == BuildingType::LargeGear) {
            static constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
            for (const auto& d : DIRS) {
                addExternalDirEdge(2 * i, b.gx + d[0], b.gy + d[1], d[0], d[1]);
            }
        } else if (b.type == BuildingType::AxleBridge) {
            addExternalDirEdge(2 * i, b.gx, b.gy, 1, 0);
            addExternalDirEdge(2 * i, b.gx, b.gy, -1, 0);
            addExternalDirEdge(2 * i + 1, b.gx, b.gy, 0, 1);
            addExternalDirEdge(2 * i + 1, b.gx, b.gy, 0, -1);
        } else if (b.type == BuildingType::SpeedGearbox) {
            int fdx = 0, fdy = 0;
            directionVector(b.dir, fdx, fdy);
            addExternalDirEdge(2 * i, b.gx, b.gy, -fdx, -fdy);
            addExternalDirEdge(2 * i + 1, b.gx, b.gy, fdx, fdy);
            adj[static_cast<std::size_t>(2 * i)].push_back(MechEdge{2 * i + 1, true, 2.0f, 12, 12, 0.f});
            adj[static_cast<std::size_t>(2 * i + 1)].push_back(MechEdge{2 * i, true, 0.5f, 12, 12, 0.f});
        } else {
            static constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
            for (const auto& d : DIRS) {
                addExternalDirEdge(2 * i, b.gx, b.gy, d[0], d[1]);
            }
        }
    }

    const float wetBoost = hydroBoostMultiplier();
    const float scMult = superconductingMult();

    auto generatorOutput = [&](const BuildingEntity& b, float& outRpm, float& outCap) -> bool {
        const BuildingDef& def = Catalog::getBuilding(b.type);
        if (!def.isGenerator) return false;
        if (b.type == BuildingType::HandCrank) {
            bool boosted = b.crankBoostTimer > 0.0f;
            outRpm = boosted ? 24.0f : def.baseRpm;
            outCap = (boosted ? 48.0f : def.basePowerCapacity) * springPowerMultiplier();
            return true;
        }
        if (b.type == BuildingType::WindTurbine) {
            outRpm = def.baseRpm;
            outCap = def.basePowerCapacity * windPowerMultiplier();
            return true;
        }
        if (b.type == BuildingType::SteamEngine) {
            if (!b.hasFuel) return false;
            outRpm = def.baseRpm;
            float wetMult = b.waterBoosted ? wetBoost : 1.0f;
            float beltFedMult = (b.localStorage[ResourceType::Coal] >= 1.0) ? 1.25f : 1.0f;
            outCap = def.basePowerCapacity * boilerPowerMultiplier() * wetMult * beltFedMult;
            return true;
        }
        if (b.type == BuildingType::AetherDynamo) {
            if (!b.hasFuel) return false;
            outRpm = def.baseRpm;
            outCap = def.basePowerCapacity * mechanicalCapMultiplier();
            return true;
        }
        if (b.type == BuildingType::ElectricMotor) {
            if (!b.electricPowered) return false;
            outRpm = def.baseRpm;
            outCap = def.basePowerCapacity * mechanicalCapMultiplier() * scMult;
            return true;
        }
        return false;
    };

    std::vector<bool> visited(static_cast<std::size_t>(nNodes), false);
    std::vector<float> solvedRpm(static_cast<std::size_t>(nNodes), 0.0f);
    std::vector<float> solvedAngle = prevAngle;
    std::vector<int> nodeNetwork(static_cast<std::size_t>(nNodes), -1);

    for (int start = 0; start < nNodes; ++start) {
        if (!nodeActive[static_cast<std::size_t>(start)] || visited[static_cast<std::size_t>(start)]) {
            continue;
        }

        std::vector<int> compNodes;
        std::queue<int> q;
        visited[static_cast<std::size_t>(start)] = true;
        q.push(start);
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            compNodes.push_back(u);
            for (const MechEdge& e : adj[static_cast<std::size_t>(u)]) {
                if (!visited[static_cast<std::size_t>(e.toNode)]) {
                    visited[static_cast<std::size_t>(e.toNode)] = true;
                    q.push(e.toNode);
                }
            }
        }

        int netId = static_cast<int>(networks_.size());
        NetworkInfo net;
        net.id = netId;
        net.nodeCount = static_cast<int>(compNodes.size());

        bool hasPrimeMover = false;
        for (int u : compNodes) {
            if (isPrimeMoverType(buildings_[static_cast<std::size_t>(u / 2)].type)) {
                hasPrimeMover = true;
                break;
            }
        }

        int rootNode = -1;
        float rootTargetRpm = 0.0f;
        float rootCap = 0.0f;
        bool usingFlywheelBuffer = false;
        float bestFlywheelEnergy = 0.0f;

        for (int u : compNodes) {
            const BuildingEntity& b = buildings_[static_cast<std::size_t>(u / 2)];
            float gRpm = 0.f, gCap = 0.f;
            if (generatorOutput(b, gRpm, gCap) && !Catalog::getBuilding(b.type).isRatchetGenerator) {
                rootNode = u;
                rootTargetRpm = gRpm;
                rootCap = gCap;
                break;
            }
        }
        if (rootNode < 0) {
            for (int u : compNodes) {
                const BuildingEntity& b = buildings_[static_cast<std::size_t>(u / 2)];
                float gRpm = 0.f, gCap = 0.f;
                if (generatorOutput(b, gRpm, gCap)) {
                    rootNode = u;
                    rootTargetRpm = gRpm;
                    rootCap = gCap;
                    break;
                }
            }
        }
        if (rootNode < 0) {
            bool hasAnyGenerator = false;
            for (int u : compNodes) {
                if (isGeneratorType(buildings_[static_cast<std::size_t>(u / 2)].type)) {
                    hasAnyGenerator = true;
                    break;
                }
            }
            if (hasAnyGenerator) {
                for (int u : compNodes) {
                    const BuildingEntity& b = buildings_[static_cast<std::size_t>(u / 2)];
                    if (b.type == BuildingType::Flywheel && b.flywheelEnergy > 0.01f && std::abs(b.pistonPhase) > 0.1f) {
                        if (b.flywheelEnergy > bestFlywheelEnergy) {
                            bestFlywheelEnergy = b.flywheelEnergy;
                            rootNode = u;
                            rootTargetRpm = std::abs(b.pistonPhase);
                        }
                    }
                }
                if (rootNode >= 0) usingFlywheelBuffer = true;
            }
        }

        if (rootNode < 0) {
            net.status = NetworkStatus::Unpowered;
            net.diagnostic = "Connect Crank, Wind, Steam, or Electric Motor";
            for (int u : compNodes) {
                nodeNetwork[static_cast<std::size_t>(u)] = netId;
                solvedRpm[static_cast<std::size_t>(u)] = 0.0f;
            }
            networks_.push_back(net);
            mechHasPrimeMover_.push_back(hasPrimeMover);
            continue;
        }

        std::unordered_set<int> solvedSet;
        std::queue<int> bfs;
        float sign = (prevRpm[static_cast<std::size_t>(rootNode)] < -0.05f) ? -1.0f : 1.0f;
        solvedRpm[static_cast<std::size_t>(rootNode)] = sign * rootTargetRpm;
        solvedAngle[static_cast<std::size_t>(rootNode)] = prevAngle[static_cast<std::size_t>(rootNode)];
        solvedSet.insert(rootNode);
        bfs.push(rootNode);

        bool jammed = false;
        std::string jamReason;
        float totalCapacity = usingFlywheelBuffer ? 0.0f : rootCap;

        while (!bfs.empty()) {
            int u = bfs.front();
            bfs.pop();
            for (const MechEdge& e : adj[static_cast<std::size_t>(u)]) {
                int v = e.toNode;
                float expectedRpm = e.isInternalGearbox
                    ? (solvedRpm[static_cast<std::size_t>(u)] * e.gearboxRatio)
                    : (-solvedRpm[static_cast<std::size_t>(u)] * (static_cast<float>(e.fromTeeth) / static_cast<float>(e.toTeeth)));

                if (solvedSet.count(v)) {
                    if (std::abs(expectedRpm - solvedRpm[static_cast<std::size_t>(v)]) > 0.15f) {
                        jammed = true;
                        std::ostringstream oss;
                        oss << "Gear Jam: conflicting speed/direction ("
                            << static_cast<int>(std::round(expectedRpm)) << " vs "
                            << static_cast<int>(std::round(solvedRpm[static_cast<std::size_t>(v)])) << " RPM)";
                        jamReason = oss.str();
                    }
                    continue;
                }

                solvedRpm[static_cast<std::size_t>(v)] = expectedRpm;
                if (!e.isInternalGearbox) {
                    solvedAngle[static_cast<std::size_t>(v)] = wrapAngle(
                        e.alpha + PI + (static_cast<float>(e.fromTeeth) * (e.alpha - solvedAngle[static_cast<std::size_t>(u)]) - PI)
                                           / static_cast<float>(e.toTeeth));
                } else {
                    solvedAngle[static_cast<std::size_t>(v)] = prevAngle[static_cast<std::size_t>(v)];
                }

                const BuildingEntity& vb = buildings_[static_cast<std::size_t>(v / 2)];
                float gRpm = 0.f, gCap = 0.f;
                if (v != rootNode && generatorOutput(vb, gRpm, gCap)) {
                    const BuildingDef& gDef = Catalog::getBuilding(vb.type);
                    float actualMag = std::abs(expectedRpm);
                    if (gDef.isRatchetGenerator) {
                        if (std::abs(actualMag - gRpm) <= 0.25f) totalCapacity += gCap;
                    } else {
                        if (std::abs(actualMag - gRpm) > 0.25f) {
                            jammed = true;
                            std::ostringstream oss;
                            oss << "Speed Conflict: " << gDef.name << " wants "
                                << static_cast<int>(gRpm) << " RPM, network is "
                                << static_cast<int>(std::round(actualMag)) << " RPM";
                            jamReason = oss.str();
                        } else {
                            totalCapacity += gCap;
                        }
                    }
                }

                solvedSet.insert(v);
                bfs.push(v);
            }
        }

        std::unordered_set<int> countedBuildings;
        float totalLoad = 0.0f;
        int flywheelCount = 0;

        for (int u : compNodes) {
            int bIdx = u / 2;
            const BuildingEntity& b = buildings_[static_cast<std::size_t>(bIdx)];
            const BuildingDef& def = Catalog::getBuilding(b.type);

            if (b.type == BuildingType::Flywheel && !countedBuildings.count(bIdx)) {
                ++flywheelCount;
                totalCapacity += usingFlywheelBuffer ? 160.0f : def.basePowerCapacity;
            }
            if (countedBuildings.count(bIdx)) continue;
            countedBuildings.insert(bIdx);

            // Hybrid machines already powered by electric grid draw 0 mechanical stress!
            if (def.baseElectricKW < 0.0f && b.electricPowered) {
                continue;
            }

            if (isDrillType(b.type) || b.type == BuildingType::Alternator ||
                b.type == BuildingType::StoneSmelter || b.type == BuildingType::Assembler) {
                float speedScale = std::abs(solvedRpm[static_cast<std::size_t>(u)]) / 12.0f;
                float lubeMult = ((isDrillType(b.type) || b.type == BuildingType::StoneSmelter) && b.lubricated) ? 0.75f : 1.0f;
                totalLoad += def.baseStressLoad * speedScale * stressMultiplier() * lubeMult;
            } else if (!def.isGenerator && b.type != BuildingType::Flywheel) {
                totalLoad += def.baseStressLoad * stressMultiplier();
            }
        }

        net.capacityPU = totalCapacity;
        net.loadPU = totalLoad;
        net.referenceRpm = std::abs(solvedRpm[static_cast<std::size_t>(rootNode)]);
        net.bufferRatio = usingFlywheelBuffer ? bestFlywheelEnergy : (flywheelCount > 0 ? 1.0f : 0.0f);

        if (jammed) {
            net.status = NetworkStatus::Jammed;
            net.diagnostic = jamReason;
            for (int u : compNodes) solvedRpm[static_cast<std::size_t>(u)] = 0.0f;
        } else if (totalLoad > totalCapacity + 0.05f) {
            net.status = NetworkStatus::Overloaded;
            net.diagnostic = "Overloaded! Add power, reduce speed, or upgrade bearings.";
            for (int u : compNodes) solvedRpm[static_cast<std::size_t>(u)] = 0.0f;
        } else if (usingFlywheelBuffer) {
            net.status = NetworkStatus::Buffering;
            net.diagnostic = "Sustained by Kinetic Flywheel buffer";
        } else {
            net.status = NetworkStatus::Running;
            net.diagnostic = "Running smoothly";
        }

        for (int u : compNodes) {
            nodeNetwork[static_cast<std::size_t>(u)] = netId;
        }
        networks_.push_back(net);
        mechHasPrimeMover_.push_back(hasPrimeMover);
    }

    int cogs = 0, drills = 0, gens = 0, factories = 0, belts = 0;
    for (int i = 0; i < nEntities; ++i) {
        BuildingEntity& b = buildings_[static_cast<std::size_t>(i)];
        const BuildingDef& def = Catalog::getBuilding(b.type);
        if (def.isMechanical) {
            int u0 = 2 * i;
            b.rpm = solvedRpm[static_cast<std::size_t>(u0)];
            b.angle = solvedAngle[static_cast<std::size_t>(u0)];
            b.networkId = nodeNetwork[static_cast<std::size_t>(u0)];
            b.status = (b.networkId >= 0) ? networks_[static_cast<std::size_t>(b.networkId)].status : NetworkStatus::Unpowered;

            if (b.type == BuildingType::AxleBridge || b.type == BuildingType::SpeedGearbox) {
                int u1 = 2 * i + 1;
                b.rpmSecondary = solvedRpm[static_cast<std::size_t>(u1)];
                b.angleSecondary = solvedAngle[static_cast<std::size_t>(u1)];
                b.networkIdSecondary = nodeNetwork[static_cast<std::size_t>(u1)];
                b.statusSecondary = (b.networkIdSecondary >= 0)
                    ? networks_[static_cast<std::size_t>(b.networkIdSecondary)].status
                    : NetworkStatus::Unpowered;
            }
        } else {
            b.rpm = 0.0f;
            b.networkId = -1;
            b.status = NetworkStatus::Unpowered;
        }

        if (def.category == BuildingCategory::Transmission) ++cogs;
        else if (def.category == BuildingCategory::Drill) ++drills;
        else if (def.category == BuildingCategory::Generator) ++gens;
        else if (def.category == BuildingCategory::Factory && b.type != BuildingType::HubCore) ++factories;
        else if (def.category == BuildingCategory::Belt) ++belts;
    }
    stats_.cogCount = cogs;
    stats_.drillCount = drills;
    stats_.generatorCount = gens;
    stats_.factoryCount = factories;
    stats_.beltCount = belts;
}

void World::solveElectricGrid() {
    electricNetworks_.clear();
    powerLinks_.clear();

    const int nEntities = static_cast<int>(buildings_.size());
    int elecCount = 0;

    for (int i = 0; i < nEntities; ++i) {
        BuildingEntity& b = buildings_[static_cast<std::size_t>(i)];
        b.electricNetId = -1;
        b.electricPowered = false;
        b.adjacentCoolers = 0;
        if (Catalog::getBuilding(b.type).category == BuildingCategory::Electric) {
            ++elecCount;
        }
    }
    stats_.electricCount = elecCount;

    // 1. CoolingTower adjacency for NuclearReactor & FusionReactor
    for (int i = 0; i < nEntities; ++i) {
        BuildingType rt = buildings_[static_cast<std::size_t>(i)].type;
        if (rt != BuildingType::NuclearReactor && rt != BuildingType::FusionReactor) continue;
        const BuildingEntity& r = buildings_[static_cast<std::size_t>(i)];
        int count = 0;
        for (int j = 0; j < nEntities; ++j) {
            if (buildings_[static_cast<std::size_t>(j)].type != BuildingType::CoolingTower) continue;
            const BuildingEntity& c = buildings_[static_cast<std::size_t>(j)];
            if (std::abs(c.gx - r.gx) <= 2 && std::abs(c.gy - r.gy) <= 2) {
                ++count;
                if (r.hasFuel) {
                    buildings_[static_cast<std::size_t>(j)].electricPowered = true;
                }
            }
        }
        buildings_[static_cast<std::size_t>(i)].adjacentCoolers = count;
    }

    // 2. Relays MST
    std::vector<int> relayEntityIdx;
    for (int i = 0; i < nEntities; ++i) {
        if (isRelayType(buildings_[static_cast<std::size_t>(i)].type)) {
            relayEntityIdx.push_back(i);
        }
    }

    const int nRelays = static_cast<int>(relayEntityIdx.size());
    if (nRelays == 0) return;

    struct RelayEdge { int rA; int rB; float dist; };
    std::vector<RelayEdge> candidateEdges;
    for (int i = 0; i < nRelays; ++i) {
        const BuildingEntity& a = buildings_[static_cast<std::size_t>(relayEntityIdx[static_cast<std::size_t>(i)])];
        float rangeA = Catalog::getBuilding(a.type).powerRange;
        for (int j = i + 1; j < nRelays; ++j) {
            const BuildingEntity& b = buildings_[static_cast<std::size_t>(relayEntityIdx[static_cast<std::size_t>(j)])];
            float rangeB = Catalog::getBuilding(b.type).powerRange;
            float dist = std::hypot(static_cast<float>(a.gx - b.gx), static_cast<float>(a.gy - b.gy));
            if (dist <= std::max(rangeA, rangeB) + 0.05f) {
                candidateEdges.push_back({i, j, dist});
            }
        }
    }
    std::sort(candidateEdges.begin(), candidateEdges.end(),
              [](const RelayEdge& lhs, const RelayEdge& rhs) { return lhs.dist < rhs.dist; });

    std::vector<int> dsu(static_cast<std::size_t>(nRelays));
    std::iota(dsu.begin(), dsu.end(), 0);
    auto findRoot = [&](int x) {
        while (dsu[static_cast<std::size_t>(x)] != x) {
            dsu[static_cast<std::size_t>(x)] = dsu[static_cast<std::size_t>(dsu[static_cast<std::size_t>(x)])];
            x = dsu[static_cast<std::size_t>(x)];
        }
        return x;
    };

    struct PendingLink { int entityA; int entityB; int netId; };
    std::vector<PendingLink> pendingLinks;
    std::vector<RelayEdge> mstEdges;

    for (const RelayEdge& e : candidateEdges) {
        int ra = findRoot(e.rA);
        int rb = findRoot(e.rB);
        if (ra != rb) {
            dsu[static_cast<std::size_t>(rb)] = ra;
            mstEdges.push_back(e);
        }
    }

    std::vector<int> rootToNet(static_cast<std::size_t>(nRelays), -1);
    for (int i = 0; i < nRelays; ++i) {
        int root = findRoot(i);
        if (rootToNet[static_cast<std::size_t>(root)] < 0) {
            int netId = static_cast<int>(electricNetworks_.size());
            rootToNet[static_cast<std::size_t>(root)] = netId;
            ElectricNetworkInfo info;
            info.id = netId;
            electricNetworks_.push_back(info);
        }
        int netId = rootToNet[static_cast<std::size_t>(root)];
        int eIdx = relayEntityIdx[static_cast<std::size_t>(i)];
        buildings_[static_cast<std::size_t>(eIdx)].electricNetId = netId;
        electricNetworks_[static_cast<std::size_t>(netId)].nodeCount++;
    }

    for (const RelayEdge& e : mstEdges) {
        int netId = buildings_[static_cast<std::size_t>(relayEntityIdx[static_cast<std::size_t>(e.rA)])].electricNetId;
        pendingLinks.push_back({
            relayEntityIdx[static_cast<std::size_t>(e.rA)],
            relayEntityIdx[static_cast<std::size_t>(e.rB)],
            netId
        });
    }

    std::vector<float> maxBattKW(electricNetworks_.size(), 0.0f);
    const float sun = solarFactor();
    const float couponGrid = 1.0f + 0.15f * static_cast<float>(couponLevel(CouponItem::GridAndShaftPower));
    const float gridMult = ((spirePhase_ >= 2) ? 1.15f : 1.0f) * couponGrid * globalPrestigeMultiplier();
    const float scMult = superconductingMult();
    const float solarMult = solarPowerMultiplier();
    const float coolerMult = coolingTowerBonusMultiplier();

    // 3. Attach electric machines/producers/batteries to nearest relay in range
    for (int i = 0; i < nEntities; ++i) {
        BuildingEntity& m = buildings_[static_cast<std::size_t>(i)];
        if (!isElectricGridNode(m.type)) continue;
        const BuildingDef& mDef = Catalog::getBuilding(m.type);

        int bestRelayEntity = -1;
        float bestDist = 1e9f;
        for (int rIdx : relayEntityIdx) {
            const BuildingEntity& r = buildings_[static_cast<std::size_t>(rIdx)];
            float rRange = Catalog::getBuilding(r.type).powerRange;
            float dist = footprintDistanceTiles(m, mDef.footprintRadius, r.gx, r.gy);
            if (dist <= rRange + 0.05f && dist < bestDist) {
                bestDist = dist;
                bestRelayEntity = rIdx;
            }
        }

        if (bestRelayEntity >= 0) {
            int netId = buildings_[static_cast<std::size_t>(bestRelayEntity)].electricNetId;
            m.electricNetId = netId;
            ElectricNetworkInfo& net = electricNetworks_[static_cast<std::size_t>(netId)];
            net.nodeCount++;
            pendingLinks.push_back({bestRelayEntity, i, netId});

            if (m.type == BuildingType::Alternator) {
                bool mechOk = (m.status == NetworkStatus::Running || m.status == NetworkStatus::Buffering)
                    && m.networkId >= 0
                    && m.networkId < static_cast<int>(mechHasPrimeMover_.size())
                    && mechHasPrimeMover_[static_cast<std::size_t>(m.networkId)];
                if (mechOk && std::abs(m.rpm) > 0.1f) {
                    net.supplyKW += mDef.baseElectricKW * (std::abs(m.rpm) / 24.0f) * gridMult * scMult;
                }
            } else if (m.type == BuildingType::SolarPanel) {
                net.supplyKW += mDef.baseElectricKW * sun * solarMult * gridMult;
            } else if (m.type == BuildingType::Accumulator) {
                net.batteryCount++;
                net.avgBatteryRatio += m.batteryCharge;
                if (m.batteryCharge > 0.005f) {
                    maxBattKW[static_cast<std::size_t>(netId)] += mDef.baseElectricKW;
                }
            } else if (m.type == BuildingType::NuclearReactor || m.type == BuildingType::FusionReactor) {
                if (m.hasFuel) {
                    float coolerPer = ((m.type == BuildingType::FusionReactor) ? 400.0f : 300.0f) * coolerMult;
                    float coolerBonus = 0.0f;
                    for (const auto& cb : buildings_) {
                        if (cb.type == BuildingType::CoolingTower &&
                            std::abs(cb.gx - m.gx) <= 2 && std::abs(cb.gy - m.gy) <= 2) {
                            coolerBonus += coolerPer * (cb.waterBoosted ? 1.40f : 1.0f);
                        }
                    }
                    ResourceType fType = mDef.fuelType;
                    float beltFed = (fType != ResourceType::Count && m.localStorage[fType] >= 1.0) ? 1.20f : 1.0f;
                    net.supplyKW += (mDef.baseElectricKW + coolerBonus) * nuclearPowerMultiplier() * beltFed;
                }
            } else if (mDef.baseElectricKW < 0.0f) {
                net.demandKW += -mDef.baseElectricKW;
            }
        }
    }

    // 4. Solve supply + battery discharge vs demand
    for (std::size_t n = 0; n < electricNetworks_.size(); ++n) {
        ElectricNetworkInfo& net = electricNetworks_[n];
        if (net.batteryCount > 0) {
            net.avgBatteryRatio /= static_cast<float>(net.batteryCount);
        }
        float rawSupply = net.supplyKW;
        float availBatt = maxBattKW[n];
        if (rawSupply > 0.05f && rawSupply + 0.05f >= net.demandKW) {
            net.powered = true;
            net.overloaded = false;
            net.batteryDischargeKW = 0.0f;
        } else if ((rawSupply + availBatt) > 0.05f && (rawSupply + availBatt + 0.05f) >= net.demandKW) {
            net.powered = true;
            net.overloaded = false;
            net.batteryDischargeKW = std::max(0.0f, net.demandKW - rawSupply);
            net.supplyKW = rawSupply + availBatt;
        } else if ((rawSupply + availBatt) > 0.05f && net.demandKW > rawSupply + availBatt + 0.05f) {
            net.powered = false;
            net.overloaded = true;
            net.supplyKW = rawSupply + availBatt;
        } else {
            net.powered = false;
            net.overloaded = false;
        }
    }

    for (BuildingEntity& b : buildings_) {
        if (b.electricNetId >= 0 && b.electricNetId < static_cast<int>(electricNetworks_.size())) {
            b.electricPowered = electricNetworks_[static_cast<std::size_t>(b.electricNetId)].powered;
        }
    }

    for (const PendingLink& pl : pendingLinks) {
        const BuildingEntity& a = buildings_[static_cast<std::size_t>(pl.entityA)];
        const BuildingEntity& b = buildings_[static_cast<std::size_t>(pl.entityB)];
        const ElectricNetworkInfo& net = electricNetworks_[static_cast<std::size_t>(pl.netId)];
        powerLinks_.push_back(PowerLineLink{a.gx, a.gy, b.gx, b.gy, net.powered, net.overloaded});
    }
}

void World::recomputeNetworks() {
    std::vector<bool> prevElec(buildings_.size(), false);
    for (std::size_t i = 0; i < buildings_.size(); ++i) {
        prevElec[i] = buildings_[i].electricPowered;
    }

    auto runMechAndAuras = [&]() {
        recomputeAurasAndLubrication();
        solveMechanicalPass();
        recomputeAurasAndLubrication();
        solveMechanicalPass();
    };

    runMechAndAuras();
    solveElectricGrid();
    recomputeAurasAndLubrication();

    bool elecChanged = false;
    for (std::size_t i = 0; i < buildings_.size(); ++i) {
        if (buildings_[i].electricPowered != prevElec[i]) {
            elecChanged = true;
            break;
        }
    }
    if (elecChanged) {
        runMechAndAuras();
        solveElectricGrid();
        recomputeAurasAndLubrication();
    }
    stats_.trainCount = static_cast<int>(trains_.size());
}

bool World::tryAcceptItem(BuildingEntity& target, ResourceType item) {
    if (item == ResourceType::Count) return false;

    switch (target.type) {
    case BuildingType::HubCore:
        resources_[item] += 1.0;
        stats_.totalMined[static_cast<std::size_t>(item)] += 1.0;
        minedThisWindow_[static_cast<std::size_t>(item)] += 1.0;
        return true;

    case BuildingType::MatterSink:
        addSinkItem(item, 1.0);
        if (particles_.size() < 460) {
            addSparkBurst((target.gx + 0.5f) * CELL_SIZE, (target.gy + 0.5f) * CELL_SIZE, 210, 120, 255, 2);
        }
        return true;

    case BuildingType::ConveyorBelt:
    case BuildingType::FastBelt:
    case BuildingType::ExpressBelt:
    case BuildingType::UndergroundBelt:
    case BuildingType::BeltSplitter:
        if (target.beltItemB == ResourceType::Count) {
            if (target.beltItemA == ResourceType::Count) {
                target.beltItemA = item;
                target.beltProgA = 0.0f;
            } else {
                target.beltItemB = item;
                target.beltProgB = 0.0f;
            }
            return true;
        }
        return false;

    case BuildingType::StorageVault:
        if (target.localStorage.totalAmount() + 1.0 <= vaultBufferCapacity() + 1e-4) {
            target.localStorage[item] += 1.0;
            return true;
        }
        return false;

    case BuildingType::TrainStation:
        if (target.dir == 0 && target.localStorage.totalAmount() + 1.0 <= stationBufferCapacity() + 1e-4) {
            target.localStorage[item] += 1.0;
            return true;
        }
        return false;

    case BuildingType::StoneSmelter:
        if (item == ResourceType::Iron && target.localStorage[ResourceType::Iron] < 16.0) {
            target.localStorage[ResourceType::Iron] += 1.0;
            return true;
        }
        if (item == ResourceType::Coal && target.localStorage[ResourceType::Coal] < 10.0) {
            target.localStorage[ResourceType::Coal] += 1.0;
            return true;
        }
        return false;

    case BuildingType::ArcFurnace:
        if (item == ResourceType::Iron && target.localStorage[ResourceType::Iron] < 20.0) {
            target.localStorage[ResourceType::Iron] += 1.0;
            return true;
        }
        return false;

    case BuildingType::Assembler:
        if (item == ResourceType::Copper && target.localStorage[ResourceType::Copper] < 16.0) {
            target.localStorage[ResourceType::Copper] += 1.0;
            return true;
        }
        if (item == ResourceType::Iron && target.localStorage[ResourceType::Iron] < 12.0) {
            target.localStorage[ResourceType::Iron] += 1.0;
            return true;
        }
        return false;

    case BuildingType::QuantumForge:
        if ((item == ResourceType::Steel || item == ResourceType::Circuit ||
             item == ResourceType::Titanium || item == ResourceType::Aether) &&
            target.localStorage[item] < 12.0) {
            target.localStorage[item] += 1.0;
            return true;
        }
        return false;

    case BuildingType::SteamEngine:
        if (item == ResourceType::Coal && target.localStorage[ResourceType::Coal] < 10.0) {
            target.localStorage[ResourceType::Coal] += 1.0;
            return true;
        }
        return false;

    case BuildingType::NuclearReactor:
        if (item == ResourceType::Uranium && target.localStorage[ResourceType::Uranium] < 10.0) {
            target.localStorage[ResourceType::Uranium] += 1.0;
            return true;
        }
        return false;

    case BuildingType::FusionReactor:
    case BuildingType::AetherDynamo:
        if (item == ResourceType::Aether && target.localStorage[ResourceType::Aether] < 10.0) {
            target.localStorage[ResourceType::Aether] += 1.0;
            return true;
        }
        return false;

    default:
        return false;
    }
}

bool World::tryPushFromBuilding(const BuildingEntity& src, ResourceType item) {
    bool isVaultOrUnload = (src.type == BuildingType::StorageVault || src.type == BuildingType::TrainStation);

    for (int k = 0; k < 4; ++k) {
        int d = (src.dir + k) % 4;
        int dx = 0, dy = 0;
        directionVector(d, dx, dy);
        int nx = src.gx + dx;
        int ny = src.gy + dy;
        if (!inBounds(nx, ny)) continue;
        BuildingEntity* nb = buildingAtMut(nx, ny);
        if (!nb || nb->id == src.id) continue;

        if (isDirectionalBelt(nb->type)) {
            int bdx = 0, bdy = 0;
            directionVector(nb->dir, bdx, bdy);
            if (inBounds(nb->gx + bdx, nb->gy + bdy) &&
                tile(nb->gx + bdx, nb->gy + bdy).occupantId == src.id) {
                continue;
            }
            if (tryAcceptItem(*nb, item)) {
                return true;
            }
        } else if (!isVaultOrUnload) {
            if (tryAcceptItem(*nb, item)) {
                return true;
            }
        }
    }
    return false;
}

void World::step(float dt) {
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    stats_.playTimeSec += dt;

    dayTime_ = std::fmod(dayTime_ + dt, DAY_CYCLE_SECONDS);
    if (lightingTimeMode_ == 0) {
        solarRefreshTimer_ += dt;
        if (solarRefreshTimer_ >= 1.25f) {
            solarRefreshTimer_ = 0.0f;
            bool hasSolar = false;
            for (const auto& b : buildings_) {
                if (b.type == BuildingType::SolarPanel) {
                    hasSolar = true;
                    break;
                }
            }
            if (hasSolar) {
                recomputeNetworks();
            }
        }
    }

    updateMachinesAndMining(dt);
    updateBeltsAndLogistics(dt);
    updateTrains(dt);
    updateParticlesAndPopups(dt);

    rateTimer_ += dt;
    if (rateTimer_ >= 1.0f) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(ResourceType::Count); ++i) {
            double instantRate = minedThisWindow_[i] / static_cast<double>(rateTimer_);
            smoothedRates_[i] = 0.55 * instantRate + 0.45 * smoothedRates_[i];
            stats_.ratePerSec[i] = smoothedRates_[i];
            minedThisWindow_[i] = 0.0;
        }
        rateTimer_ = 0.0f;
    }
}

void World::updateMachinesAndMining(float dt) {
    bool needResolve = false;
    const float maxBattKJ = accumulatorCapacityKJ();
    const float ocSpeedVal = beaconSpeedMultiplier();
    const float wetBoost = hydroBoostMultiplier();
    const float beltedBonus = beltedMineBonusMultiplier();

    for (BuildingEntity& b : buildings_) {
        const BuildingDef& def = Catalog::getBuilding(b.type);

        // Visual rotations
        if (def.isMechanical) {
            if (b.status == NetworkStatus::Running || b.status == NetworkStatus::Buffering) {
                b.angle = wrapAngle(b.angle + (b.rpm * TWO_PI / 60.0f) * dt);
            } else if ((b.type == BuildingType::Assembler || b.type == BuildingType::AetherExtractor) && b.electricPowered) {
                b.angle = wrapAngle(b.angle + (30.0f * TWO_PI / 60.0f) * dt);
            }
            if (b.type == BuildingType::AxleBridge || b.type == BuildingType::SpeedGearbox) {
                if (b.statusSecondary == NetworkStatus::Running || b.statusSecondary == NetworkStatus::Buffering) {
                    b.angleSecondary = wrapAngle(b.angleSecondary + (b.rpmSecondary * TWO_PI / 60.0f) * dt);
                }
            }
        } else if (b.type == BuildingType::ElectricDrill && b.electricPowered) {
            b.angle = wrapAngle(b.angle + (28.0f * TWO_PI / 60.0f) * dt);
        }

        // HandCrank boost decay
        if (b.type == BuildingType::HandCrank && b.crankBoostTimer > 0.0f) {
            b.crankBoostTimer = std::max(0.0f, b.crankBoostTimer - dt);
            if (b.crankBoostTimer <= 0.0f) needResolve = true;
        }

        // Accumulator battery charge & discharge
        if (b.type == BuildingType::Accumulator && b.electricNetId >= 0) {
            const ElectricNetworkInfo* enet = electricNetworkById(b.electricNetId);
            if (enet) {
                float prevCharge = b.batteryCharge;
                if (enet->powered && enet->batteryDischargeKW <= 0.05f && enet->batteryCount > 0) {
                    float surplusKW = std::max(0.0f, enet->supplyKW - enet->demandKW);
                    float chargeKW = std::min(220.0f, surplusKW / static_cast<float>(enet->batteryCount));
                    b.batteryCharge = std::min(1.0f, b.batteryCharge + (chargeKW * dt) / maxBattKJ);
                    if (prevCharge <= 0.005f && b.batteryCharge > 0.01f) needResolve = true;
                } else if (enet->powered && enet->batteryDischargeKW > 0.05f && b.batteryCharge > 0.001f) {
                    float activeCount = std::max(1.0f, static_cast<float>(enet->batteryCount));
                    float drainKW = enet->batteryDischargeKW / activeCount;
                    b.batteryCharge = std::max(0.0f, b.batteryCharge - (drainKW * dt) / maxBattKJ);
                    if (b.batteryCharge <= 0.001f) {
                        b.batteryCharge = 0.0f;
                        needResolve = true;
                    }
                }
            }
        }

        // Fuel consumption (belt-fed localStorage first, then Core resources_, or free in Sandbox!)
        if (def.fuelType != ResourceType::Count) {
            float idleFactor = 0.2f;
            float fuelDurationMult = 1.0f;
            if (b.type == BuildingType::NuclearReactor || b.type == BuildingType::FusionReactor) {
                const ElectricNetworkInfo* enet = electricNetworkById(b.electricNetId);
                idleFactor = (enet && enet->demandKW > 1.0f) ? 1.0f : 0.15f;
                fuelDurationMult = nuclearFuelMultiplier();
            } else {
                const NetworkInfo* net = networkById(b.networkId);
                idleFactor = (net && net->loadPU > 1.0f) ? 1.0f : 0.2f;
                if (b.type == BuildingType::SteamEngine) fuelDurationMult = boilerFuelMultiplier();
            }

            auto consumeOneFuel = [&]() -> bool {
                if (b.localStorage[def.fuelType] >= 1.0) {
                    b.localStorage[def.fuelType] -= 1.0;
                    return true;
                }
                if (resources_[def.fuelType] >= 1.0) {
                    resources_[def.fuelType] -= 1.0;
                    return true;
                }
                if (sandboxMode_) {
                    return true;
                }
                return false;
            };

            if (b.hasFuel) {
                b.fuelTimer -= dt * idleFactor;
                if (b.fuelTimer <= 0.0f) {
                    if (consumeOneFuel()) {
                        b.fuelTimer += def.fuelInterval * fuelDurationMult;
                    } else {
                        b.fuelTimer = 0.0f;
                        b.hasFuel = false;
                        needResolve = true;
                    }
                }
            } else if (consumeOneFuel()) {
                b.fuelTimer = def.fuelInterval * fuelDurationMult;
                b.hasFuel = true;
                needResolve = true;
            }

            if (def.isMechanical && b.status == NetworkStatus::Running) {
                b.pistonPhase = wrapAngle(b.pistonPhase + (std::abs(b.rpm) * TWO_PI / 60.0f) * dt);
                if (b.type == BuildingType::SteamEngine && static_cast<int>((b.pistonPhase * 10.f)) % 7 == 0) {
                    if (randFloat01(particleRng_) < 0.12f) {
                        addSteamPuff((b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.25f) * CELL_SIZE);
                    }
                }
            }
        }

        if (b.type == BuildingType::CoolingTower && b.electricPowered) {
            if (randFloat01(particleRng_) < 0.08f) {
                addSteamPuff((b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.2f) * CELL_SIZE);
            }
        }

        // Flywheel
        if (b.type == BuildingType::Flywheel) {
            if (b.status == NetworkStatus::Running && std::abs(b.rpm) > 0.1f) {
                b.flywheelEnergy = std::min(1.0f, b.flywheelEnergy + dt / 4.0f);
                b.pistonPhase = std::abs(b.rpm);
            } else if (b.status == NetworkStatus::Buffering) {
                b.flywheelEnergy = std::max(0.0f, b.flywheelEnergy - dt / 6.0f);
                if (b.flywheelEnergy <= 0.01f) {
                    b.flywheelEnergy = 0.0f;
                    needResolve = true;
                }
            } else if (b.status == NetworkStatus::Unpowered || b.status == NetworkStatus::Jammed) {
                b.flywheelEnergy = std::max(0.0f, b.flywheelEnergy - dt / 2.0f);
            }
        }

        // ==================== FACTORY CRAFTING ====================
        if (isFactoryCrafter(b.type) && b.craftedCount < 16) {
            float powerSpeed = 0.0f;
            if (b.type == BuildingType::StoneSmelter) {
                if ((b.status == NetworkStatus::Running || b.status == NetworkStatus::Buffering) && std::abs(b.rpm) > 0.1f) {
                    powerSpeed = (std::abs(b.rpm) / 12.0f)
                               * (b.lubricated ? 1.35f : 1.0f)
                               * (b.waterBoosted ? wetBoost : 1.0f);
                }
            } else if (b.type == BuildingType::ArcFurnace || b.type == BuildingType::QuantumForge) {
                if (b.electricPowered) powerSpeed = 1.0f;
            } else if (b.type == BuildingType::Assembler) {
                if (b.electricPowered) {
                    powerSpeed = 1.45f;
                } else if ((b.status == NetworkStatus::Running || b.status == NetworkStatus::Buffering) && std::abs(b.rpm) > 0.1f) {
                    powerSpeed = std::abs(b.rpm) / 12.0f;
                }
            }

            if (powerSpeed > 0.01f) {
                bool hasIngredients = false;
                ResourceType outProduct = ResourceType::Count;
                if (b.type == BuildingType::StoneSmelter) {
                    hasIngredients = (b.localStorage[ResourceType::Iron] >= 2.0 - 1e-4 &&
                                      b.localStorage[ResourceType::Coal] >= 1.0 - 1e-4);
                    outProduct = ResourceType::Steel;
                } else if (b.type == BuildingType::ArcFurnace) {
                    hasIngredients = (b.localStorage[ResourceType::Iron] >= 2.0 - 1e-4);
                    outProduct = ResourceType::Steel;
                } else if (b.type == BuildingType::Assembler) {
                    hasIngredients = (b.localStorage[ResourceType::Copper] >= 2.0 - 1e-4 &&
                                      b.localStorage[ResourceType::Iron] >= 1.0 - 1e-4);
                    outProduct = ResourceType::Circuit;
                } else if (b.type == BuildingType::QuantumForge) {
                    hasIngredients = (b.localStorage[ResourceType::Steel] >= 2.0 - 1e-4 &&
                                      b.localStorage[ResourceType::Circuit] >= 2.0 - 1e-4 &&
                                      b.localStorage[ResourceType::Titanium] >= 1.0 - 1e-4 &&
                                      b.localStorage[ResourceType::Aether] >= 1.0 - 1e-4);
                    outProduct = ResourceType::QuantumCore;
                }

                if (hasIngredients) {
                    float ocSpeed = b.overclocked ? ocSpeedVal : 1.0f;
                    b.drillProgress += def.baseMineRate * powerSpeed * craftingSpeedMultiplier() * ocSpeed * dt;
                    if (randFloat01(particleRng_) < 0.07f) {
                        addSparkBurst((b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE, 255, 185, 85, 1);
                    }
                    while (b.drillProgress >= 1.0f && hasIngredients && b.craftedCount < 16) {
                        b.drillProgress -= 1.0f;
                        if (b.type == BuildingType::StoneSmelter) {
                            b.localStorage[ResourceType::Iron] -= 2.0;
                            b.localStorage[ResourceType::Coal] -= 1.0;
                            hasIngredients = (b.localStorage[ResourceType::Iron] >= 2.0 - 1e-4 && b.localStorage[ResourceType::Coal] >= 1e-4 + 0.999);
                        } else if (b.type == BuildingType::ArcFurnace) {
                            b.localStorage[ResourceType::Iron] -= 2.0;
                            hasIngredients = (b.localStorage[ResourceType::Iron] >= 2.0 - 1e-4);
                        } else if (b.type == BuildingType::Assembler) {
                            b.localStorage[ResourceType::Copper] -= 2.0;
                            b.localStorage[ResourceType::Iron] -= 1.0;
                            hasIngredients = (b.localStorage[ResourceType::Copper] >= 2.0 - 1e-4 && b.localStorage[ResourceType::Iron] >= 1.0 - 1e-4);
                        } else if (b.type == BuildingType::QuantumForge) {
                            b.localStorage[ResourceType::Steel] -= 2.0;
                            b.localStorage[ResourceType::Circuit] -= 2.0;
                            b.localStorage[ResourceType::Titanium] -= 1.0;
                            b.localStorage[ResourceType::Aether] -= 1.0;
                            hasIngredients = false;
                        }
                        int made = 1;
                        if (b.overclocked && randFloat01(particleRng_) < 0.25f) {
                            made = 2;
                        }
                        b.craftedType = outProduct;
                        b.craftedCount += made;
                    }
                }
            }
        }

        // ==================== DRILLS MINING ====================
        bool drillRunning = false;
        float rpmRatio = 0.0f;
        if (b.type == BuildingType::ElectricDrill) {
            if (b.electricPowered) {
                drillRunning = true;
                rpmRatio = 2.0f;
            }
        } else if (b.type == BuildingType::AetherExtractor && b.electricPowered) {
            drillRunning = true;
            rpmRatio = 2.4f;
        } else if (isDrillType(b.type) &&
                   (b.status == NetworkStatus::Running || b.status == NetworkStatus::Buffering) &&
                   std::abs(b.rpm) > 0.1f) {
            drillRunning = true;
            rpmRatio = std::abs(b.rpm) / 12.0f;
        }

        if (drillRunning) {
            const Tile& t = tile(b.gx, b.gy);
            if (t.ore != OreType::None) {
                bool hasAdjacentBeltOrConsumer = false;
                static constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                for (const auto& d : DIRS) {
                    if (const BuildingEntity* nb = buildingAt(b.gx + d[0], b.gy + d[1])) {
                        if (isBeltLike(nb->type) || isFactoryCrafter(nb->type) ||
                            nb->type == BuildingType::HubCore || nb->type == BuildingType::StorageVault) {
                            hasAdjacentBeltOrConsumer = true;
                            break;
                        }
                    }
                }

                float lubeSpeed = b.lubricated ? 1.35f : 1.0f;
                float ocSpeed = b.overclocked ? ocSpeedVal : 1.0f;
                float beltExtractBonus = hasAdjacentBeltOrConsumer ? beltedBonus : 1.0f;
                float cyclesPerSec = def.baseMineRate * rpmRatio * drillSpeedMultiplier() * lubeSpeed * ocSpeed * beltExtractBonus;
                b.drillProgress += cyclesPerSec * dt;

                if (randFloat01(particleRng_) < 0.08f * std::min(rpmRatio, 3.0f)) {
                    float wx = (b.gx + 0.5f) * CELL_SIZE;
                    float wy = (b.gy + 0.5f) * CELL_SIZE;
                    if (t.ore == OreType::Aether) addSparkBurst(wx, wy, 90, 235, 255, 1);
                    else if (t.ore == OreType::Uranium) addSparkBurst(wx, wy, 120, 250, 105, 1);
                    else if (t.ore == OreType::Titanium) addSparkBurst(wx, wy, 185, 145, 255, 1);
                    else if (t.ore == OreType::Copper) addSparkBurst(wx, wy, 245, 155, 75, 1);
                    else addSparkBurst(wx, wy, 255, 210, 120, 1);
                }

                if (b.drillProgress >= 1.0f) {
                    int cycles = static_cast<int>(b.drillProgress);
                    b.drillProgress -= static_cast<float>(cycles);

                    float extractorBonus = (b.type == BuildingType::AetherExtractor) ? 1.45f : 1.0f;
                    float richBonus = (t.richness > 1.25f) ? richOreBonusMultiplier() : 1.0f;
                    float ocYield = b.overclocked ? 1.25f : 1.0f;
                    float perCycleYield = t.richness * richBonus * oreYieldMultiplier() * extractorBonus * ocYield;
                    b.oreAccumulator += static_cast<float>(cycles) * perCycleYield;

                    int gained = static_cast<int>(b.oreAccumulator + 1e-4f);
                    if (gained > 0) {
                        b.oreAccumulator -= static_cast<float>(gained);
                        ResourceType rType = oreToResource(t.ore);

                        int remaining = gained;
                        while (remaining > 0 && hasAdjacentBeltOrConsumer) {
                            if (tryPushFromBuilding(b, rType)) {
                                --remaining;
                            } else {
                                break;
                            }
                        }

                        if (remaining > 0) {
                            if (!hasAdjacentBeltOrConsumer && isInDirectHubZone(b.gx, b.gy)) {
                                resources_[rType] += static_cast<double>(remaining);
                                stats_.totalMined[static_cast<std::size_t>(rType)] += static_cast<double>(remaining);
                                minedThisWindow_[static_cast<std::size_t>(rType)] += static_cast<double>(remaining);
                                addMiningBurst((b.gx + 0.5f) * CELL_SIZE, (b.gy + 0.5f) * CELL_SIZE, rType, remaining);
                            } else {
                                double cap = remoteDrillBufferCapacity();
                                double room = std::max(0.0, cap - b.localStorage.totalAmount());
                                double stored = std::min(static_cast<double>(remaining), room);
                                if (stored > 0.0) {
                                    b.localStorage[rType] += stored;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Transfer buffered ore from Drills to nearby LOAD TrainStations
    const double stationCap = stationBufferCapacity();
    for (BuildingEntity& drill : buildings_) {
        if (!isDrillType(drill.type) || drill.localStorage.totalAmount() < 1e-3) continue;
        BuildingEntity* bestStation = nullptr;
        float bestDist = static_cast<float>(STATION_PICKUP_RADIUS) + 0.05f;
        for (BuildingEntity& st : buildings_) {
            if (st.type != BuildingType::TrainStation || st.dir != 0) continue;
            if (st.localStorage.totalAmount() + 1e-3 >= stationCap) continue;
            float d = std::hypot(static_cast<float>(st.gx - drill.gx), static_cast<float>(st.gy - drill.gy));
            if (d <= bestDist) {
                bestDist = d;
                bestStation = &st;
            }
        }
        if (bestStation) {
            double room = std::max(0.0, stationCap - bestStation->localStorage.totalAmount());
            for (std::size_t r = 0; r < static_cast<std::size_t>(ResourceType::Count); ++r) {
                double moveAmt = std::min(drill.localStorage.values[r], room);
                if (moveAmt > 0.0) {
                    drill.localStorage.values[r] -= moveAmt;
                    bestStation->localStorage.values[r] += moveAmt;
                    room -= moveAmt;
                }
            }
        }
    }

    if (needResolve) {
        recomputeNetworks();
    }
}

void World::updateBeltsAndLogistics(float dt) {
    const float beltMult = beltSpeedMultiplier();

    // 1. Eject buffered outputs from Drills, Factory Crafters, StorageVaults, and UNLOAD Stations onto Belts/Consumers
    for (BuildingEntity& b : buildings_) {
        if (isFactoryCrafter(b.type) && b.craftedCount > 0 && b.craftedType != ResourceType::Count) {
            if (tryPushFromBuilding(b, b.craftedType)) {
                b.craftedCount--;
                if (b.craftedCount == 0) b.craftedType = ResourceType::Count;
            }
        } else if (isDrillType(b.type) || b.type == BuildingType::StorageVault ||
                   (b.type == BuildingType::TrainStation && b.dir == 1)) {
            if (b.localStorage.totalAmount() >= 1.0) {
                for (std::size_t r = 0; r < static_cast<std::size_t>(ResourceType::Count); ++r) {
                    if (b.localStorage.values[r] >= 1.0) {
                        ResourceType rt = static_cast<ResourceType>(r);
                        if (tryPushFromBuilding(b, rt)) {
                            b.localStorage.values[r] -= 1.0;
                            break;
                        }
                    }
                }
            }
        }
    }

    // 2. Advance items along Conveyor Belts, Underground Tunnels, and Splitters
    for (std::size_t i = 0; i < buildings_.size(); ++i) {
        BuildingEntity& b = buildings_[i];
        if (!isBeltLike(b.type)) continue;
        float speed = Catalog::getBuilding(b.type).beltSpeed * beltMult;

        if (b.beltItemA != ResourceType::Count) {
            b.beltProgA = std::min(1.0f, b.beltProgA + speed * dt);
            if (b.beltProgA >= 1.0f) {
                bool transferred = false;

                if (b.type == BuildingType::BeltSplitter) {
                    for (int k = 0; k < 4; ++k) {
                        int d = (static_cast<int>(b.splitterIdx) + k) % 4;
                        int dx = 0, dy = 0;
                        directionVector(d, dx, dy);
                        BuildingEntity* nb = buildingAtMut(b.gx + dx, b.gy + dy);
                        if (!nb || nb->id == b.id) continue;
                        if (isDirectionalBelt(nb->type)) {
                            int bdx = 0, bdy = 0;
                            directionVector(nb->dir, bdx, bdy);
                            if (nb->gx + bdx == b.gx && nb->gy + bdy == b.gy) continue;
                        }
                        if (tryAcceptItem(*nb, b.beltItemA)) {
                            b.splitterIdx = static_cast<uint8_t>((d + 1) % 4);
                            transferred = true;
                            break;
                        }
                    }
                } else {
                    if (b.type == BuildingType::UndergroundBelt) {
                        int exitIdx = findUndergroundExitIndex(b.gx, b.gy, b.dir);
                        if (exitIdx >= 0) {
                            BuildingEntity& exitTunnel = buildings_[static_cast<std::size_t>(exitIdx)];
                            if (tryAcceptItem(exitTunnel, b.beltItemA)) {
                                transferred = true;
                            }
                        }
                    }
                    if (!transferred) {
                        int dx = 0, dy = 0;
                        directionVector(b.dir, dx, dy);
                        if (BuildingEntity* nb = buildingAtMut(b.gx + dx, b.gy + dy)) {
                            if (tryAcceptItem(*nb, b.beltItemA)) {
                                transferred = true;
                            }
                        }
                    }
                }

                if (transferred) {
                    b.beltItemA = ResourceType::Count;
                    b.beltProgA = 0.0f;
                }
            }
        }

        if (b.beltItemB != ResourceType::Count) {
            b.beltProgB += speed * dt;
            if (b.beltItemA == ResourceType::Count) {
                if (b.beltProgB >= 0.5f) {
                    b.beltItemA = b.beltItemB;
                    b.beltProgA = std::min(1.0f, b.beltProgB);
                    b.beltItemB = ResourceType::Count;
                    b.beltProgB = 0.0f;
                }
            } else {
                b.beltProgB = std::min(0.5f, b.beltProgB);
            }
        }
    }
}

bool World::findRailPath(
    int startGx,
    int startGy,
    int targetGx,
    int targetGy,
    std::vector<std::pair<int, int>>& outPath
) const {
    outPath.clear();
    if (!inBounds(startGx, startGy) || !inBounds(targetGx, targetGy)) return false;
    const BuildingEntity* sB = buildingAt(startGx, startGy);
    const BuildingEntity* tB = buildingAt(targetGx, targetGy);
    if (!sB || !tB || !isRailType(sB->type) || !isRailType(tB->type)) return false;
    if (startGx == targetGx && startGy == targetGy) {
        outPath.push_back({startGx, startGy});
        return true;
    }

    std::vector<int> parent(GRID_W * GRID_H, -2);
    std::queue<int> q;
    int sIdx = tileIndex(startGx, startGy);
    int tIdx = tileIndex(targetGx, targetGy);
    parent[static_cast<std::size_t>(sIdx)] = -1;
    q.push(sIdx);

    static constexpr int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    bool found = false;
    while (!q.empty()) {
        int cur = q.front();
        q.pop();
        if (cur == tIdx) {
            found = true;
            break;
        }
        int cx = cur % GRID_W;
        int cy = cur / GRID_W;
        for (const auto& d : DIRS) {
            int nx = cx + d[0];
            int ny = cy + d[1];
            if (!inBounds(nx, ny)) continue;
            int nIdx = tileIndex(nx, ny);
            if (parent[static_cast<std::size_t>(nIdx)] != -2) continue;
            const BuildingEntity* nb = buildingAt(nx, ny);
            if (!nb || !isRailType(nb->type)) continue;
            parent[static_cast<std::size_t>(nIdx)] = cur;
            q.push(nIdx);
        }
    }

    if (!found) return false;
    for (int cur = tIdx; cur != -1; cur = parent[static_cast<std::size_t>(cur)]) {
        outPath.push_back({cur % GRID_W, cur / GRID_W});
    }
    std::reverse(outPath.begin(), outPath.end());
    return true;
}

void World::updateTrains(float dt) {
    if (trains_.empty()) return;

    auto hasElectrifiedStationOnRail = [&](int startGx, int startGy) -> bool {
        if (!inBounds(startGx, startGy)) return false;
        std::vector<uint8_t> vis(GRID_W * GRID_H, 0);
        std::queue<int> q;
        int sIdx = tileIndex(startGx, startGy);
        const BuildingEntity* sb = buildingAt(startGx, startGy);
        if (!sb || !isRailType(sb->type)) return false;
        vis[static_cast<std::size_t>(sIdx)] = 1;
        q.push(sIdx);
        static constexpr int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!q.empty()) {
            int cur = q.front();
            q.pop();
            int cx = cur % GRID_W;
            int cy = cur / GRID_W;
            const BuildingEntity* cb = buildingAt(cx, cy);
            if (cb && cb->type == BuildingType::TrainStation && cb->electricPowered) return true;
            for (const auto& d : DIRS) {
                int nx = cx + d[0];
                int ny = cy + d[1];
                if (!inBounds(nx, ny)) continue;
                int nIdx = tileIndex(nx, ny);
                if (vis[static_cast<std::size_t>(nIdx)]) continue;
                const BuildingEntity* nb = buildingAt(nx, ny);
                if (nb && isRailType(nb->type)) {
                    vis[static_cast<std::size_t>(nIdx)] = 1;
                    q.push(nIdx);
                }
            }
        }
        return false;
    };

    const double cap = trainCargoCapacity();
    const double bonusMult = freightBonusMultiplier();
    const float speedBonus = (spirePhase_ >= 3) ? 1.25f : 1.0f;

    for (TrainEntity& tr : trains_) {
        int gx = std::clamp(static_cast<int>(tr.wx / CELL_SIZE), 0, GRID_W - 1);
        int gy = std::clamp(static_cast<int>(tr.wy / CELL_SIZE), 0, GRID_H - 1);

        if (!tr.isElectric) {
            bool moving = (tr.state == TrainState::MovingToLoad || tr.state == TrainState::MovingToUnload);
            float burnScale = moving ? 1.0f : 0.15f;
            if (tr.hasFuel) {
                tr.fuelTimer -= dt * burnScale;
                if (tr.fuelTimer <= 0.0f) {
                    if (resources_[ResourceType::Coal] >= 1.0) {
                        resources_[ResourceType::Coal] -= 1.0;
                        tr.fuelTimer += Catalog::getBuilding(BuildingType::SteamTrain).fuelInterval;
                    } else if (sandboxMode_) {
                        tr.fuelTimer += Catalog::getBuilding(BuildingType::SteamTrain).fuelInterval;
                    } else {
                        tr.fuelTimer = 0.0f;
                        tr.hasFuel = false;
                    }
                }
            } else if (resources_[ResourceType::Coal] >= 1.0) {
                resources_[ResourceType::Coal] -= 1.0;
                tr.fuelTimer = Catalog::getBuilding(BuildingType::SteamTrain).fuelInterval;
                tr.hasFuel = true;
            } else if (sandboxMode_) {
                tr.fuelTimer = Catalog::getBuilding(BuildingType::SteamTrain).fuelInterval;
                tr.hasFuel = true;
            }
        } else {
            tr.hasFuel = sandboxMode_ || hasElectrifiedStationOnRail(gx, gy);
        }

        if (!tr.hasFuel) continue;

        if (tr.state == TrainState::Idle) {
            tr.waitTimer -= dt;
            if (tr.waitTimer > 0.0f) continue;
            tr.waitTimer = 0.35f;

            if (tr.cargo.totalAmount() >= cap * 0.5) {
                std::vector<std::pair<int, int>> bestPath;
                int bestStId = -1;
                for (const BuildingEntity& st : buildings_) {
                    if (st.type != BuildingType::TrainStation || st.dir != 1) continue;
                    std::vector<std::pair<int, int>> candidate;
                    if (findRailPath(gx, gy, st.gx, st.gy, candidate)) {
                        if (bestStId < 0 || candidate.size() < bestPath.size()) {
                            bestPath = std::move(candidate);
                            bestStId = st.id;
                        }
                    }
                }
                if (bestStId > 0) {
                    tr.targetStationId = bestStId;
                    tr.path = std::move(bestPath);
                    tr.pathIndex = (tr.path.size() > 1) ? 1 : 0;
                    tr.state = TrainState::MovingToUnload;
                    continue;
                }
            }

            int bestStId = -1;
            double bestScore = -1.0;
            std::vector<std::pair<int, int>> bestPath;
            for (const BuildingEntity& st : buildings_) {
                if (st.type != BuildingType::TrainStation || st.dir != 0) continue;
                std::vector<std::pair<int, int>> candidate;
                if (findRailPath(gx, gy, st.gx, st.gy, candidate)) {
                    double amt = st.localStorage.totalAmount();
                    if (amt < 0.5 && tr.cargo.totalAmount() > 0.5) continue;
                    double score = amt * 100.0 - static_cast<double>(candidate.size());
                    if (bestStId < 0 || score > bestScore) {
                        bestScore = score;
                        bestStId = st.id;
                        bestPath = std::move(candidate);
                    }
                }
            }

            if (bestStId > 0) {
                const BuildingEntity* chosen = buildingById(bestStId);
                bool chosenHasOre = chosen && chosen->localStorage.totalAmount() >= 1.0;
                if (!chosenHasOre && tr.cargo.totalAmount() >= 1.0) {
                    for (const BuildingEntity& st : buildings_) {
                        if (st.type != BuildingType::TrainStation || st.dir != 1) continue;
                        std::vector<std::pair<int, int>> candidate;
                        if (findRailPath(gx, gy, st.gx, st.gy, candidate)) {
                            tr.targetStationId = st.id;
                            tr.path = std::move(candidate);
                            tr.pathIndex = (tr.path.size() > 1) ? 1 : 0;
                            tr.state = TrainState::MovingToUnload;
                            break;
                        }
                    }
                    if (tr.state == TrainState::MovingToUnload) continue;
                }
                if (chosenHasOre) {
                    tr.targetStationId = bestStId;
                    tr.path = std::move(bestPath);
                    tr.pathIndex = (tr.path.size() > 1) ? 1 : 0;
                    tr.state = TrainState::MovingToLoad;
                    continue;
                }
            }

            if (tr.cargo.totalAmount() >= 1.0) {
                for (const BuildingEntity& st : buildings_) {
                    if (st.type != BuildingType::TrainStation || st.dir != 1) continue;
                    std::vector<std::pair<int, int>> candidate;
                    if (findRailPath(gx, gy, st.gx, st.gy, candidate)) {
                        tr.targetStationId = st.id;
                        tr.path = std::move(candidate);
                        tr.pathIndex = (tr.path.size() > 1) ? 1 : 0;
                        tr.state = TrainState::MovingToUnload;
                        break;
                    }
                }
            }
        } else if (tr.state == TrainState::MovingToLoad || tr.state == TrainState::MovingToUnload) {
            const BuildingEntity* target = buildingById(tr.targetStationId);
            int expectedDir = (tr.state == TrainState::MovingToLoad) ? 0 : 1;
            if (!target || target->type != BuildingType::TrainStation || target->dir != expectedDir || tr.path.empty()) {
                tr.state = TrainState::Idle;
                tr.waitTimer = 0.2f;
                tr.path.clear();
                continue;
            }

            float speed = (tr.isElectric ? 340.0f : 205.0f) * speedBonus;
            float remainingMove = speed * dt;
            while (remainingMove > 0.01f && tr.pathIndex < static_cast<int>(tr.path.size())) {
                auto [tx, ty] = tr.path[static_cast<std::size_t>(tr.pathIndex)];
                if (!buildingAt(tx, ty) || !isRailType(buildingAt(tx, ty)->type)) {
                    tr.state = TrainState::Idle;
                    tr.waitTimer = 0.2f;
                    tr.path.clear();
                    break;
                }
                float targetWx = (static_cast<float>(tx) + 0.5f) * CELL_SIZE;
                float targetWy = (static_cast<float>(ty) + 0.5f) * CELL_SIZE;
                float dx = targetWx - tr.wx;
                float dy = targetWy - tr.wy;
                float dist = std::hypot(dx, dy);
                if (dist > 0.5f) tr.angle = std::atan2(dy, dx);
                if (dist <= remainingMove) {
                    tr.wx = targetWx;
                    tr.wy = targetWy;
                    remainingMove -= dist;
                    ++tr.pathIndex;
                } else {
                    tr.wx += (dx / dist) * remainingMove;
                    tr.wy += (dy / dist) * remainingMove;
                    remainingMove = 0.0f;
                }

                if (tr.trail.empty() ||
                    std::hypot(tr.wx - tr.trail.front().first, tr.wy - tr.trail.front().second) >= 5.0f) {
                    tr.trail.insert(tr.trail.begin(), {tr.wx, tr.wy});
                    if (tr.trail.size() > 64) tr.trail.pop_back();
                }
            }

            if (!tr.isElectric && randFloat01(particleRng_) < 0.25f) {
                addSteamPuff(tr.wx, tr.wy - 10.0f);
            }

            if (tr.state != TrainState::Idle && tr.pathIndex >= static_cast<int>(tr.path.size())) {
                tr.waitTimer = 0.65f;
                tr.state = (tr.state == TrainState::MovingToLoad) ? TrainState::Loading : TrainState::Unloading;
            }
        } else if (tr.state == TrainState::Loading) {
            BuildingEntity* st = buildingByIdMut(tr.targetStationId);
            if (st && st->type == BuildingType::TrainStation && st->dir == 0) {
                double room = std::max(0.0, cap - tr.cargo.totalAmount());
                for (std::size_t r = 0; r < static_cast<std::size_t>(ResourceType::Count); ++r) {
                    double take = std::min(st->localStorage.values[r], room);
                    if (take > 0.0) {
                        st->localStorage.values[r] -= take;
                        tr.cargo.values[r] += take;
                        room -= take;
                    }
                }
            }
            tr.waitTimer -= dt;
            if (tr.waitTimer <= 0.0f || tr.cargo.totalAmount() + 0.1 >= cap) {
                std::vector<std::pair<int, int>> bestPath;
                int bestUnloadId = -1;
                for (const BuildingEntity& ust : buildings_) {
                    if (ust.type != BuildingType::TrainStation || ust.dir != 1) continue;
                    std::vector<std::pair<int, int>> candidate;
                    if (findRailPath(gx, gy, ust.gx, ust.gy, candidate)) {
                        if (bestUnloadId < 0 || candidate.size() < bestPath.size()) {
                            bestPath = std::move(candidate);
                            bestUnloadId = ust.id;
                        }
                    }
                }
                if (bestUnloadId > 0 && tr.cargo.totalAmount() > 0.5) {
                    tr.targetStationId = bestUnloadId;
                    tr.path = std::move(bestPath);
                    tr.pathIndex = (tr.path.size() > 1) ? 1 : 0;
                    tr.state = TrainState::MovingToUnload;
                } else {
                    tr.state = TrainState::Idle;
                    tr.waitTimer = 0.4f;
                }
            }
        } else if (tr.state == TrainState::Unloading) {
            BuildingEntity* ust = buildingByIdMut(tr.targetStationId);
            bool hasOutgoingBelt = false;
            if (ust) {
                static constexpr int DIRS[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                for (const auto& d : DIRS) {
                    if (const BuildingEntity* nb = buildingAt(ust->gx + d[0], ust->gy + d[1])) {
                        if (isDirectionalBelt(nb->type)) {
                            int bdx = 0, bdy = 0;
                            directionVector(nb->dir, bdx, bdy);
                            if (!(nb->gx + bdx == ust->gx && nb->gy + bdy == ust->gy)) {
                                hasOutgoingBelt = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (tr.cargo.totalAmount() > 0.0) {
                int totalDelivered = 0;
                ResourceType dominantRes = ResourceType::Iron;
                double bestAmt = 0.0;
                for (std::size_t r = 0; r < static_cast<std::size_t>(ResourceType::Count); ++r) {
                    double amt = tr.cargo.values[r];
                    if (amt > 0.0) {
                        double boosted = amt * bonusMult;
                        if (ust && hasOutgoingBelt) {
                            double stCap = stationBufferCapacity();
                            double room = std::max(0.0, stCap - ust->localStorage.totalAmount());
                            double toBeltBuf = std::min(boosted, room);
                            ust->localStorage.values[r] += toBeltBuf;
                            double overflow = boosted - toBeltBuf;
                            if (overflow > 0.0) {
                                resources_.values[r] += overflow;
                                stats_.totalMined[r] += overflow;
                                minedThisWindow_[r] += overflow;
                            }
                        } else {
                            resources_.values[r] += boosted;
                            stats_.totalMined[r] += boosted;
                            minedThisWindow_[r] += boosted;
                        }
                        totalDelivered += static_cast<int>(std::round(boosted));
                        if (amt > bestAmt) {
                            bestAmt = amt;
                            dominantRes = static_cast<ResourceType>(r);
                        }
                        tr.cargo.values[r] = 0.0;
                    }
                }
                if (totalDelivered > 0) {
                    addMiningBurst(tr.wx, tr.wy, dominantRes, totalDelivered, " (Freight)");
                }
            }
            tr.waitTimer -= dt;
            if (tr.waitTimer <= 0.0f) {
                tr.state = TrainState::Idle;
                tr.waitTimer = 0.25f;
            }
        }
    }
}

void World::addMiningBurst(float wx, float wy, ResourceType res, int amount, const std::string& suffix) {
    uint8_t r = 210, g = 215, b = 225;
    switch (res) {
    case ResourceType::Iron:        r = 195; g = 210; b = 230; break;
    case ResourceType::Copper:      r = 240; g = 148; b = 78;  break;
    case ResourceType::Coal:        r = 175; g = 155; b = 125; break;
    case ResourceType::Titanium:    r = 185; g = 152; b = 255; break;
    case ResourceType::Uranium:     r = 115; g = 250; b = 100; break;
    case ResourceType::Aether:      r = 90;  g = 238; b = 255; break;
    case ResourceType::Steel:       r = 220; g = 232; b = 245; break;
    case ResourceType::Circuit:     r = 98;  g = 242; b = 168; break;
    case ResourceType::QuantumCore: r = 250; g = 135; b = 255; break;
    default: break;
    }
    addPopupText(wx, wy - 18.0f, "+" + std::to_string(amount) + " " + resourceName(res) + suffix, r, g, b);
    addSparkBurst(wx, wy, r, g, b, 4);
}

void World::addPopupText(float wx, float wy, const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
    if (!popupsEnabled_) return;
    if (popups_.size() < 96) {
        FloatingPopup p;
        p.x = wx;
        p.y = wy;
        p.vy = -30.0f;
        p.life = 1.15f;
        p.maxLife = 1.15f;
        p.text = text;
        p.r = r;
        p.g = g;
        p.b = b;
        popups_.push_back(p);
    }
}

void World::addSteamPuff(float wx, float wy) {
    if (particleDensity_ <= 0) return;
    const std::size_t cap = (particleDensity_ >= 2) ? 760u : 450u;
    int copies = (particleDensity_ >= 2) ? 2 : 1;
    for (int i = 0; i < copies && particles_.size() < cap; ++i) {
        Particle p;
        p.x = wx + (static_cast<float>((particles_.size() + static_cast<std::size_t>(i)) % 7) - 3.0f) * 2.0f;
        p.y = wy;
        p.vx = (static_cast<float>(particles_.size() % 5) - 2.0f) * 4.0f;
        p.vy = -26.0f - static_cast<float>(particles_.size() % 4) * 5.0f;
        p.life = 0.85f;
        p.maxLife = 0.85f;
        p.size = 5.5f;
        p.r = 215; p.g = 222; p.b = 232; p.a = 150;
        particles_.push_back(p);
    }
}

void World::addSparkBurst(float wx, float wy, uint8_t r, uint8_t g, uint8_t b, int count) {
    if (particleDensity_ <= 0) return;
    const std::size_t cap = (particleDensity_ >= 2) ? 840u : 520u;
    int effCount = (particleDensity_ >= 2) ? (count * 2) : count;
    uint32_t seed = static_cast<uint32_t>(particles_.size() * 2654435761u + static_cast<int>(wx * 13.f));
    for (int i = 0; i < effCount && particles_.size() < cap; ++i) {
        float ang = randFloat01(seed) * TWO_PI;
        float spd = 25.0f + randFloat01(seed) * 55.0f;
        Particle p;
        p.x = wx;
        p.y = wy;
        p.vx = std::cos(ang) * spd;
        p.vy = std::sin(ang) * spd;
        p.life = 0.42f + randFloat01(seed) * 0.25f;
        p.maxLife = p.life;
        p.size = 2.6f;
        p.r = r; p.g = g; p.b = b; p.a = 235;
        particles_.push_back(p);
    }
}

void World::updateParticlesAndPopups(float dt) {
    for (std::size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vx *= (1.0f - 2.0f * dt);
        p.vy *= (1.0f - 1.2f * dt);
        ++i;
    }

    for (std::size_t i = 0; i < popups_.size();) {
        FloatingPopup& f = popups_[i];
        f.life -= dt;
        if (f.life <= 0.0f) {
            popups_[i] = popups_.back();
            popups_.pop_back();
            continue;
        }
        f.y += f.vy * dt;
        ++i;
    }
}

void World::loadState(
    uint32_t seed,
    const WorldGenSettings& genSettings,
    bool sandboxMode,
    const std::vector<Tile>& tiles,
    const ResourceBag& resources,
    const std::array<int, static_cast<std::size_t>(UpgradeId::Count)>& upgrades,
    const std::array<int, static_cast<std::size_t>(CouponItem::Count)>& couponLevels,
    int spirePhase,
    int prestigeCount,
    double sinkPoints,
    int coupons,
    int totalCouponsMinted,
    float dayTime,
    const std::vector<BuildingEntity>& buildings,
    const std::vector<TrainEntity>& trains,
    int nextId,
    int nextTrainId,
    const WorldStats& stats
) {
    mapSeed_ = seed ? seed : 1337u;
    genSettings_ = genSettings;
    if (genSettings_.seedText.empty()) {
        genSettings_.seedText = std::to_string(mapSeed_);
    }
    sandboxMode_ = sandboxMode;

    if (tiles.size() == static_cast<std::size_t>(GRID_W * GRID_H)) {
        tiles_ = tiles;
    } else {
        tiles_.assign(GRID_W * GRID_H, Tile{});
        generateMap(mapSeed_);
    }
    resources_ = resources;
    upgradeLevels_ = upgrades;
    couponLevels_ = couponLevels;
    spirePhase_ = spirePhase;
    prestigeCount_ = prestigeCount;
    sinkPoints_ = sinkPoints;
    coupons_ = coupons;
    totalCouponsMinted_ = totalCouponsMinted;
    dayTime_ = dayTime;
    buildings_ = buildings;
    trains_ = trains;
    nextBuildingId_ = std::max(1, nextId);
    nextTrainId_ = std::max(1, nextTrainId);
    stats_ = stats;
    particles_.clear();
    popups_.clear();

    // Ensure HubCore always exists even if loading an older save file!
    bool hasCore = false;
    for (const auto& b : buildings_) {
        if (b.type == BuildingType::HubCore) {
            hasCore = true;
            break;
        }
    }
    if (!hasCore) {
        BuildingEntity core;
        core.id = nextBuildingId_++;
        core.type = BuildingType::HubCore;
        core.gx = HUB_GX;
        core.gy = HUB_GY;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (inBounds(HUB_GX + dx, HUB_GY + dy) && tile(HUB_GX + dx, HUB_GY + dy).occupantId == -1) {
                    tileMut(HUB_GX + dx, HUB_GY + dy).occupantId = core.id;
                }
            }
        }
        buildings_.push_back(core);
    }

    recomputeNetworks();
}

} // namespace cogwill
