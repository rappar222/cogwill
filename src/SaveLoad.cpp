#include "SaveLoad.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace cogwill {

using json = nlohmann::json;

namespace {

int hexDigitToInt(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

} // namespace

bool SaveLoad::saveToFile(const World& world, const std::string& path, std::string* errorOut) {
    try {
        json root;
        root["version"] = 4;
        root["seed"] = world.mapSeed();
        root["sandboxMode"] = world.isSandboxMode();

        const WorldGenSettings& wg = world.worldGen();
        root["worldGen"] = {
            {"seedText", wg.seedText},
            {"preset", static_cast<int>(wg.preset)},
            {"oreRichness", wg.oreRichness},
            {"oreFrequency", wg.oreFrequency},
            {"waterCoverage", wg.waterCoverage},
            {"cliffCoverage", wg.cliffCoverage}
        };

        root["nextBuildingId"] = world.nextBuildingId();
        root["nextTrainId"] = world.nextTrainId();
        root["resources"] = world.resources().values;
        root["upgrades"] = world.upgradeLevels();
        root["couponLevels"] = world.couponLevels();

        root["spirePhase"] = world.spirePhase();
        root["prestigeCount"] = world.prestigeCount();
        root["sinkPoints"] = world.sinkPoints();
        root["coupons"] = world.coupons();
        root["totalCouponsMinted"] = world.totalCouponsMinted();
        root["dayTime"] = world.dayTime();

        json statsJ;
        statsJ["totalMined"] = world.stats().totalMined;
        statsJ["playTimeSec"] = world.stats().playTimeSec;
        statsJ["hasUnlockedUpgrade"] = world.stats().hasUnlockedUpgrade;
        root["stats"] = statsJ;

        // Compact lossless biome + elevation strings + sparse terrain/ore tiles
        static constexpr char HEX[17] = "0123456789ABCDEF";
        std::string biomeStr;
        std::string elevStr;
        biomeStr.reserve(static_cast<std::size_t>(GRID_W * GRID_H));
        elevStr.reserve(static_cast<std::size_t>(GRID_W * GRID_H * 2));

        json terrainTiles = json::array();
        json oreTiles = json::array();
        const auto& tiles = world.tiles();
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                const Tile& t = tiles[static_cast<std::size_t>(y * GRID_W + x)];
                int b = std::clamp(static_cast<int>(t.biome), 0, 15);
                biomeStr.push_back(HEX[b]);
                int qElev = std::clamp(static_cast<int>(std::round(t.elevation * 255.0f)), 0, 255);
                elevStr.push_back(HEX[(qElev >> 4) & 0xF]);
                elevStr.push_back(HEX[qElev & 0xF]);

                if (t.terrain != TerrainType::Ground) {
                    terrainTiles.push_back({
                        {"x", x},
                        {"y", y},
                        {"terrain", static_cast<int>(t.terrain)}
                    });
                }
                if (t.ore != OreType::None) {
                    oreTiles.push_back({
                        {"x", x},
                        {"y", y},
                        {"ore", static_cast<int>(t.ore)},
                        {"richness", t.richness}
                    });
                }
            }
        }
        root["biomes"] = biomeStr;
        root["elevations"] = elevStr;
        root["terrainTiles"] = terrainTiles;
        root["oreTiles"] = oreTiles;

        json buildingsJ = json::array();
        for (const BuildingEntity& b : world.buildings()) {
            buildingsJ.push_back({
                {"id", b.id},
                {"type", static_cast<int>(b.type)},
                {"gx", b.gx},
                {"gy", b.gy},
                {"dir", b.dir},
                {"angle", b.angle},
                {"angleSecondary", b.angleSecondary},
                {"batteryCharge", b.batteryCharge},
                {"drillProgress", b.drillProgress},
                {"oreAccumulator", b.oreAccumulator},
                {"fuelTimer", b.fuelTimer},
                {"hasFuel", b.hasFuel},
                {"crankBoostTimer", b.crankBoostTimer},
                {"flywheelEnergy", b.flywheelEnergy},
                {"pistonPhase", b.pistonPhase},
                {"craftedType", static_cast<int>(b.craftedType)},
                {"craftedCount", b.craftedCount},
                {"beltItemA", static_cast<int>(b.beltItemA)},
                {"beltProgA", b.beltProgA},
                {"beltItemB", static_cast<int>(b.beltItemB)},
                {"beltProgB", b.beltProgB},
                {"splitterIdx", static_cast<int>(b.splitterIdx)},
                {"localStorage", b.localStorage.values}
            });
        }
        root["buildings"] = buildingsJ;

        json trainsJ = json::array();
        for (const TrainEntity& tr : world.trains()) {
            trainsJ.push_back({
                {"id", tr.id},
                {"isElectric", tr.isElectric},
                {"wx", tr.wx},
                {"wy", tr.wy},
                {"angle", tr.angle},
                {"state", static_cast<int>(tr.state)},
                {"waitTimer", tr.waitTimer},
                {"fuelTimer", tr.fuelTimer},
                {"hasFuel", tr.hasFuel},
                {"targetStationId", tr.targetStationId},
                {"cargo", tr.cargo.values}
            });
        }
        root["trains"] = trainsJ;

        std::ofstream out(path);
        if (!out) {
            if (errorOut) *errorOut = "Could not open file for writing: " + path;
            return false;
        }
        out << root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        if (errorOut) *errorOut = ex.what();
        return false;
    }
}

bool SaveLoad::loadFromFile(World& world, const std::string& path, std::string* errorOut) {
    try {
        std::ifstream in(path);
        if (!in) {
            if (errorOut) *errorOut = "Save file not found: " + path;
            return false;
        }
        json root;
        in >> root;

        uint32_t seed = root.value("seed", 1337u);
        bool sandboxMode = root.value("sandboxMode", false);

        WorldGenSettings wg;
        wg.seedText = std::to_string(seed);
        if (root.contains("worldGen") && root["worldGen"].is_object()) {
            const auto& wj = root["worldGen"];
            wg.seedText = wj.value("seedText", wg.seedText);
            int pVal = std::clamp(wj.value("preset", 0), 0, static_cast<int>(WorldPreset::Count) - 1);
            wg.preset = static_cast<WorldPreset>(pVal);
            wg.oreRichness = wj.value("oreRichness", 1.0f);
            wg.oreFrequency = wj.value("oreFrequency", 1.0f);
            wg.waterCoverage = wj.value("waterCoverage", 1.0f);
            wg.cliffCoverage = wj.value("cliffCoverage", 1.0f);
        }

        int nextId = root.value("nextBuildingId", 1);
        int nextTrainId = root.value("nextTrainId", 1);

        int spirePhase = root.value("spirePhase", 0);
        int prestigeCount = root.value("prestigeCount", 0);
        double sinkPoints = root.value("sinkPoints", 0.0);
        int coupons = root.value("coupons", 0);
        int totalCouponsMinted = root.value("totalCouponsMinted", 0);
        float dayTime = root.value("dayTime", 0.0f);

        ResourceBag resources;
        if (root.contains("resources") && root["resources"].is_array()) {
            const auto& arr = root["resources"];
            for (std::size_t i = 0; i < resources.values.size() && i < arr.size(); ++i) {
                resources.values[i] = arr[i].get<double>();
            }
        }

        std::array<int, static_cast<std::size_t>(UpgradeId::Count)> upgrades{};
        if (root.contains("upgrades") && root["upgrades"].is_array()) {
            const auto& arr = root["upgrades"];
            for (std::size_t i = 0; i < upgrades.size() && i < arr.size(); ++i) {
                upgrades[i] = arr[i].get<int>();
            }
        }

        std::array<int, static_cast<std::size_t>(CouponItem::Count)> couponLevels{};
        if (root.contains("couponLevels") && root["couponLevels"].is_array()) {
            const auto& arr = root["couponLevels"];
            for (std::size_t i = 0; i < couponLevels.size() && i < arr.size(); ++i) {
                couponLevels[i] = arr[i].get<int>();
            }
        }

        WorldStats stats{};
        if (root.contains("stats")) {
            const auto& sj = root["stats"];
            stats.playTimeSec = sj.value("playTimeSec", 0.0);
            stats.hasUnlockedUpgrade = sj.value("hasUnlockedUpgrade", false);
            if (sj.contains("totalMined") && sj["totalMined"].is_array()) {
                for (std::size_t i = 0; i < stats.totalMined.size() && i < sj["totalMined"].size(); ++i) {
                    stats.totalMined[i] = sj["totalMined"][i].get<double>();
                }
            }
        }

        std::vector<Tile> tiles(static_cast<std::size_t>(GRID_W * GRID_H), Tile{});
        uint32_t rng = seed ^ 0xA511E9B3u;
        for (int i = 0; i < GRID_W * GRID_H; ++i) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            tiles[static_cast<std::size_t>(i)].visualSeed = rng;
        }

        bool hasElevStr = false;
        if (root.contains("biomes") && root["biomes"].is_string()) {
            const std::string bStr = root["biomes"].get<std::string>();
            if (bStr.size() >= static_cast<std::size_t>(GRID_W * GRID_H)) {
                for (std::size_t i = 0; i < tiles.size(); ++i) {
                    tiles[i].biome = static_cast<uint8_t>(std::clamp(hexDigitToInt(bStr[i]), 0, 4));
                }
            }
        }
        if (root.contains("elevations") && root["elevations"].is_string()) {
            const std::string eStr = root["elevations"].get<std::string>();
            if (eStr.size() >= static_cast<std::size_t>(GRID_W * GRID_H * 2)) {
                hasElevStr = true;
                for (std::size_t i = 0; i < tiles.size(); ++i) {
                    int hi = hexDigitToInt(eStr[2 * i]);
                    int lo = hexDigitToInt(eStr[2 * i + 1]);
                    tiles[i].elevation = static_cast<float>((hi << 4) | lo) / 255.0f;
                }
            }
        }

        if (root.contains("terrainTiles") && root["terrainTiles"].is_array()) {
            for (const auto& tt : root["terrainTiles"]) {
                int x = tt.value("x", -1);
                int y = tt.value("y", -1);
                int tv = tt.value("terrain", 0);
                if (x >= 0 && x < GRID_W && y >= 0 && y < GRID_H &&
                    tv >= 0 && tv <= static_cast<int>(TerrainType::Rock)) {
                    Tile& t = tiles[static_cast<std::size_t>(y * GRID_W + x)];
                    t.terrain = static_cast<TerrainType>(tv);
                    if (!hasElevStr) {
                        if (t.terrain == TerrainType::Water) t.elevation = 0.0f;
                        else if (t.terrain == TerrainType::Rock) t.elevation = 0.78f;
                    }
                }
            }
        }

        if (root.contains("oreTiles") && root["oreTiles"].is_array()) {
            for (const auto& ot : root["oreTiles"]) {
                int x = ot.value("x", -1);
                int y = ot.value("y", -1);
                if (x >= 0 && x < GRID_W && y >= 0 && y < GRID_H) {
                    Tile& t = tiles[static_cast<std::size_t>(y * GRID_W + x)];
                    int oVal = ot.value("ore", 0);
                    if (oVal >= 0 && oVal <= static_cast<int>(OreType::Aether)) {
                        t.ore = static_cast<OreType>(oVal);
                    }
                    t.richness = ot.value("richness", 1.0f);
                }
            }
        }

        auto toResourceType = [](int val) -> ResourceType {
            if (val >= 0 && val < static_cast<int>(ResourceType::Count)) {
                return static_cast<ResourceType>(val);
            }
            return ResourceType::Count;
        };

        std::vector<BuildingEntity> buildings;
        if (root.contains("buildings") && root["buildings"].is_array()) {
            for (const auto& bj : root["buildings"]) {
                int tVal = bj.value("type", 0);
                if (tVal < 0 || tVal >= static_cast<int>(BuildingType::Count)) {
                    continue;
                }
                BuildingEntity b;
                b.id = bj.value("id", 1);
                b.type = static_cast<BuildingType>(tVal);
                b.gx = bj.value("gx", 0);
                b.gy = bj.value("gy", 0);
                b.dir = bj.value("dir", 0);

                const BuildingDef& def = Catalog::getBuilding(b.type);
                b.angle = bj.value("angle", 0.0f);
                b.angleSecondary = bj.value("angleSecondary", 0.0f);
                b.batteryCharge = bj.value("batteryCharge", 0.0f);
                b.drillProgress = bj.value("drillProgress", 0.0f);
                b.oreAccumulator = bj.value("oreAccumulator", 0.0f);
                b.fuelTimer = bj.value("fuelTimer", 0.0f);
                b.hasFuel = bj.value("hasFuel", true);
                b.crankBoostTimer = bj.value("crankBoostTimer", 0.0f);
                b.flywheelEnergy = bj.value("flywheelEnergy", 0.0f);
                b.pistonPhase = bj.value("pistonPhase", 0.0f);

                b.craftedType = toResourceType(bj.value("craftedType", static_cast<int>(ResourceType::Count)));
                b.craftedCount = bj.value("craftedCount", 0);
                b.beltItemA = toResourceType(bj.value("beltItemA", static_cast<int>(ResourceType::Count)));
                b.beltProgA = bj.value("beltProgA", 0.0f);
                b.beltItemB = toResourceType(bj.value("beltItemB", static_cast<int>(ResourceType::Count)));
                b.beltProgB = bj.value("beltProgB", 0.0f);
                b.splitterIdx = static_cast<uint8_t>(bj.value("splitterIdx", 0) & 3);

                if (bj.contains("localStorage") && bj["localStorage"].is_array()) {
                    const auto& arr = bj["localStorage"];
                    for (std::size_t i = 0; i < b.localStorage.values.size() && i < arr.size(); ++i) {
                        b.localStorage.values[i] = arr[i].get<double>();
                    }
                }

                const int rad = def.footprintRadius;
                for (int dy = -rad; dy <= rad; ++dy) {
                    for (int dx = -rad; dx <= rad; ++dx) {
                        int cx = b.gx + dx;
                        int cy = b.gy + dy;
                        if (cx >= 0 && cx < GRID_W && cy >= 0 && cy < GRID_H) {
                            tiles[static_cast<std::size_t>(cy * GRID_W + cx)].occupantId = b.id;
                        }
                    }
                }
                buildings.push_back(b);
            }
        }

        std::vector<TrainEntity> trains;
        if (root.contains("trains") && root["trains"].is_array()) {
            for (const auto& tj : root["trains"]) {
                TrainEntity tr;
                tr.id = tj.value("id", 1);
                tr.isElectric = tj.value("isElectric", false);
                tr.wx = tj.value("wx", 0.0f);
                tr.wy = tj.value("wy", 0.0f);
                tr.angle = tj.value("angle", 0.0f);
                int stVal = tj.value("state", 0);
                if (stVal >= 0 && stVal <= static_cast<int>(TrainState::Unloading)) {
                    tr.state = static_cast<TrainState>(stVal);
                }
                tr.waitTimer = tj.value("waitTimer", 0.0f);
                tr.fuelTimer = tj.value("fuelTimer", 0.0f);
                tr.hasFuel = tj.value("hasFuel", true);
                tr.targetStationId = tj.value("targetStationId", -1);
                if (tj.contains("cargo") && tj["cargo"].is_array()) {
                    const auto& arr = tj["cargo"];
                    for (std::size_t i = 0; i < tr.cargo.values.size() && i < arr.size(); ++i) {
                        tr.cargo.values[i] = arr[i].get<double>();
                    }
                }
                trains.push_back(tr);
            }
        }

        world.loadState(
            seed,
            wg,
            sandboxMode,
            tiles,
            resources,
            upgrades,
            couponLevels,
            spirePhase,
            prestigeCount,
            sinkPoints,
            coupons,
            totalCouponsMinted,
            dayTime,
            buildings,
            trains,
            nextId,
            nextTrainId,
            stats
        );
        return true;
    } catch (const std::exception& ex) {
        if (errorOut) *errorOut = ex.what();
        return false;
    }
}

bool SaveLoad::saveSettings(const GameSettings& settings, const std::string& path, std::string* errorOut) {
    try {
        json j = {
            {"version", 1},
            {"mode25D", settings.mode25D},
            {"shadersEnabled", settings.shadersEnabled},
            {"rtQuality", settings.rtQuality},
            {"extrusionDepth", settings.extrusionDepth},
            {"bloomStrength", settings.bloomStrength},
            {"ambientOcclusion", settings.ambientOcclusion},
            {"waterReflections", settings.waterReflections},
            {"postFxMode", settings.postFxMode},
            {"pixelScale", settings.pixelScale},
            {"vsync", settings.vsync},
            {"fpsLimit", settings.fpsLimit},
            {"fullscreen", settings.fullscreen},
            {"showFps", settings.showFps},
            {"masterVolume", settings.masterVolume},
            {"muted", settings.muted},
            {"lightingTimeMode", settings.lightingTimeMode},
            {"particleDensity", settings.particleDensity},
            {"showPopups", settings.showPopups},
            {"gridStyle", settings.gridStyle},
            {"autoSaveSec", settings.autoSaveSec},
            {"cameraSpeed", settings.cameraSpeed}
        };
        std::ofstream out(path);
        if (!out) {
            if (errorOut) *errorOut = "Could not write settings to: " + path;
            return false;
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception& ex) {
        if (errorOut) *errorOut = ex.what();
        return false;
    }
}

bool SaveLoad::loadSettings(GameSettings& settings, const std::string& path, std::string* errorOut) {
    try {
        std::ifstream in(path);
        if (!in) {
            if (errorOut) *errorOut = "Settings file not found: " + path;
            return false;
        }
        json j;
        in >> j;

        settings.mode25D = j.value("mode25D", settings.mode25D);
        settings.shadersEnabled = j.value("shadersEnabled", settings.shadersEnabled);
        settings.rtQuality = std::clamp(j.value("rtQuality", settings.rtQuality), 0, 3);
        settings.extrusionDepth = std::clamp(j.value("extrusionDepth", settings.extrusionDepth), 0.4f, 1.8f);
        settings.bloomStrength = std::clamp(j.value("bloomStrength", settings.bloomStrength), 0.0f, 2.0f);
        settings.ambientOcclusion = j.value("ambientOcclusion", settings.ambientOcclusion);
        settings.waterReflections = j.value("waterReflections", settings.waterReflections);
        settings.postFxMode = std::clamp(j.value("postFxMode", settings.postFxMode), 0, 2);

        settings.pixelScale = std::clamp(j.value("pixelScale", settings.pixelScale), 1, 4);
        settings.vsync = j.value("vsync", settings.vsync);
        settings.fpsLimit = std::clamp(j.value("fpsLimit", settings.fpsLimit), 0, 240);
        settings.fullscreen = j.value("fullscreen", settings.fullscreen);
        settings.showFps = j.value("showFps", settings.showFps);

        settings.masterVolume = std::clamp(j.value("masterVolume", settings.masterVolume), 0.0f, 1.0f);
        settings.muted = j.value("muted", settings.muted);
        settings.lightingTimeMode = std::clamp(j.value("lightingTimeMode", settings.lightingTimeMode), 0, 3);
        settings.particleDensity = std::clamp(j.value("particleDensity", settings.particleDensity), 0, 2);
        settings.showPopups = j.value("showPopups", settings.showPopups);
        settings.gridStyle = std::clamp(j.value("gridStyle", settings.gridStyle), 0, 2);
        settings.autoSaveSec = std::clamp(j.value("autoSaveSec", settings.autoSaveSec), 0, 600);
        settings.cameraSpeed = std::clamp(j.value("cameraSpeed", settings.cameraSpeed), 0.4f, 3.0f);
        return true;
    } catch (const std::exception& ex) {
        if (errorOut) *errorOut = ex.what();
        return false;
    }
}

} // namespace cogwill
