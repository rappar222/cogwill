#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cogwill {

inline constexpr int GRID_W = 192;
inline constexpr int GRID_H = 144;
inline constexpr int HUB_GX = GRID_W / 2;
inline constexpr int HUB_GY = GRID_H / 2;
inline constexpr int BASE_DIRECT_RADIUS = 8;
inline constexpr int STATION_PICKUP_RADIUS = 5;
inline constexpr int UNDERGROUND_MAX_DIST = 5;
inline constexpr float CELL_SIZE = 64.0f;
inline constexpr float DAY_CYCLE_SECONDS = 150.0f;
inline constexpr float TILT_25D_Y = 0.80f;
inline constexpr float PI = 3.14159265358979323846f;
inline constexpr float TWO_PI = 2.0f * PI;

enum class TerrainType : uint8_t {
    Ground = 0,
    Water = 1,
    Rock = 2
};

enum class WorldPreset : int {
    BalancedValley = 0,
    RiverArchipelago = 1,
    BasaltCanyons = 2,
    MegabasePlains = 3,
    AetherBadlands = 4,
    Count = 5
};

inline const char* worldPresetName(WorldPreset p) {
    switch (p) {
    case WorldPreset::BalancedValley:   return "Balanced Valley";
    case WorldPreset::RiverArchipelago: return "Coolant Archipelago";
    case WorldPreset::BasaltCanyons:    return "Basalt Canyons";
    case WorldPreset::MegabasePlains:   return "Megabase Plains";
    case WorldPreset::AetherBadlands:   return "Aether Rift Badlands";
    default:                            return "Balanced Valley";
    }
}

struct WorldGenSettings {
    std::string seedText{"COGWILL-25D"};
    WorldPreset preset{WorldPreset::BalancedValley};
    float oreRichness{1.0f};   // 0.5 .. 3.0
    float oreFrequency{1.0f};  // 0.5 .. 2.5
    float waterCoverage{1.0f}; // 0.0 .. 2.0
    float cliffCoverage{1.0f}; // 0.0 .. 2.0
};

inline uint32_t hashWorldSeed(const std::string& raw) {
    std::string trimmed;
    for (char c : raw) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            trimmed.push_back(c);
        }
    }
    if (trimmed.empty()) {
        return 1337u;
    }
    bool allDigits = true;
    for (char c : trimmed) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits && trimmed.size() <= 9) {
        uint32_t val = static_cast<uint32_t>(std::stoul(trimmed));
        return val == 0u ? 1u : val;
    }
    // FNV-1a 32-bit deterministic string hash
    uint32_t h = 2166136261u;
    for (unsigned char c : trimmed) {
        h ^= static_cast<uint32_t>(c);
        h *= 16777619u;
    }
    return h == 0u ? 1337u : h;
}

struct GameSettings {
    // 2.5D, Ray-Tracing & Shader Pipeline
    bool mode25D{true};
    bool shadersEnabled{true};
    int rtQuality{2};          // 0=Off, 1=Medium (8 rays), 2=High (16 rays), 3=Ultra (24 rays + GI bounce)
    float extrusionDepth{1.0f}; // 0.6f, 1.0f, 1.4f
    float bloomStrength{0.85f}; // 0.0f, 0.45f, 0.85f, 1.30f
    bool ambientOcclusion{true};
    bool waterReflections{true};
    int postFxMode{1};         // 0=Clean, 1=Cinematic Vignette, 2=Retro CRT

    // Display & Performance
    int pixelScale{2};         // 1=Native HD, 2=Fine Pixel, 3=Classic Pixel, 4=Chunky Retro
    bool vsync{true};
    int fpsLimit{60};          // 0=Unlimited, 60, 120, 144
    bool fullscreen{false};
    bool showFps{true};

    // Audio, Atmosphere & Gameplay
    float masterVolume{0.80f}; // 0.0 .. 1.0
    bool muted{false};
    int lightingTimeMode{0};   // 0=Dynamic Cycle, 1=Always Daylight, 2=Always Neon Night, 3=Golden Dusk
    int particleDensity{1};    // 0=Off, 1=Normal, 2=Lush (2x)
    bool showPopups{true};
    int gridStyle{1};          // 0=Hidden, 1=Subtle, 2=Bright
    int autoSaveSec{120};      // 0=Off, 60, 120, 300
    float cameraSpeed{1.0f};   // 0.6f, 1.0f, 1.5f, 2.0f
};

enum class ResourceType : int {
    Iron = 0,
    Copper = 1,
    Coal = 2,
    Titanium = 3,
    Uranium = 4,
    Aether = 5,
    Steel = 6,
    Circuit = 7,
    QuantumCore = 8,
    Count = 9
};

enum class OreType : int {
    None = 0,
    Iron = 1,
    Copper = 2,
    Coal = 3,
    Titanium = 4,
    Uranium = 5,
    Aether = 6
};

struct ResourceBag {
    std::array<double, static_cast<std::size_t>(ResourceType::Count)> values{};

    double& operator[](ResourceType type) {
        return values[static_cast<std::size_t>(type)];
    }

    double operator[](ResourceType type) const {
        return values[static_cast<std::size_t>(type)];
    }

    double totalAmount() const {
        double sum = 0.0;
        for (double v : values) sum += v;
        return sum;
    }

    bool canAfford(const ResourceBag& cost) const {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i] + 1e-6 < cost.values[i]) {
                return false;
            }
        }
        return true;
    }

    void add(const ResourceBag& other) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] += other.values[i];
        }
    }

    void subtract(const ResourceBag& cost) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = std::max(0.0, values[i] - cost.values[i]);
        }
    }
};

enum class BuildingType : int {
    HubCore = 0,

    // F1: Mechanical Transmission & Utility
    SmallCog,
    LargeGear,
    SpeedGearbox,
    AxleBridge,
    Lubricator,
    Flywheel,

    // F2: Power & Storage
    HandCrank,
    WindTurbine,
    SteamEngine,
    SolarPanel,
    Accumulator,
    AetherDynamo,

    // F3: Mining & Extraction
    IronDrill,
    HeavyDrill,
    ElectricDrill,
    AetherExtractor,
    WaterPump,

    // F4: Refining, Assembly & Beacons
    StoneSmelter,
    ArcFurnace,
    Assembler,
    QuantumForge,
    OverclockBeacon,
    MatterSink,

    // F5: Conveyor Logistics
    ConveyorBelt,
    FastBelt,
    ExpressBelt,
    BeltSplitter,
    UndergroundBelt,
    StorageVault,

    // F6: Electrical Grid, Nuclear & Fusion
    PowerPole,
    Substation,
    Alternator,
    ElectricMotor,
    NuclearReactor,
    CoolingTower,
    FusionReactor,

    // F7: Railways & Trains
    RailTrack,
    TrainStation,
    SteamTrain,
    ElectricTrain,

    Count
};

enum class BuildingCategory : int {
    Transmission = 0,
    Generator = 1,
    Drill = 2,
    Factory = 3,
    Belt = 4,
    Electric = 5,
    Rail = 6,
    Count = 7
};

enum class NetworkStatus {
    Unpowered = 0,
    Running,
    Overloaded,
    Jammed,
    Buffering
};

enum class UpgradeId : int {
    // Row 0: Mechanical & Factory (cols 0..7)
    UnlockLargeGear = 0,
    UnlockGearboxBridge,
    UnlockAssembler,
    PrecisionBearings,
    UnlockUtilityMachines,
    UnlockArcFurnace,
    MagneticBearings,
    UnlockOverclockers,

    // Row 1: Power, Solar & Atomics (cols 0..7)
    SpringTension,
    UnlockSteamEngine,
    BoilerPressure,
    UnlockElectricalGrid,
    UnlockSolarBattery,
    UnlockSubstations,
    UnlockNuclearPower,
    UnlockFusionPower,

    // Row 2: Mining, Titanium & Quantum (cols 0..7)
    DrillSpeed,
    OreYield,
    UnlockHeavyDrill,
    SmeltingMastery,
    UnlockElectricDrill,
    UnlockAetherTier,
    UnlockQuantumForge,
    CoreFracking,

    // Row 3: Belts, Logistics & Railways (cols 0..7)
    UnlockSplittersTunnels,
    UnlockFastBelts,
    HubLogistics,
    UnlockRailways,
    HeavyFreight,
    UnlockExpressBelts,
    UnlockElectricTrains,
    FreightBonus,

    // Row 4: Megastructures & Optimization (cols 0..7)
    SolarEfficiency,
    HydroThermalMastery,
    BeltCompression,
    SuperconductingGrid,
    SmartLoaders,
    BeaconAmplification,
    MatterCompression,
    PlanetaryLogistics,

    Count
};

enum class CouponItem : int {
    BeltAndCraftSpeed = 0,
    GridAndShaftPower = 1,
    MiningYield = 2,
    CarePackage = 3,
    Count = 4
};

struct SpirePhaseDef {
    int phaseIndex{0}; // 1..5
    std::string title;
    std::string subtitle;
    std::string rewardText;
    ResourceBag requirement{};
};

struct BuildingDef {
    BuildingType type{};
    BuildingCategory category{};
    std::string id;
    std::string name;
    std::string shortLabel;
    std::string description;
    ResourceBag cost{};
    int footprintRadius{0}; // 0 => 1x1, 1 => 3x3
    int teeth{12};
    float baseRpm{0.0f};
    float basePowerCapacity{0.0f}; // PU (mechanical)
    float baseStressLoad{0.0f};    // PU (mechanical)
    float baseElectricKW{0.0f};    // +supply or -demand in kW
    float powerRange{0.0f};        // tile connection radius for poles/substations
    float baseMineRate{0.0f};      // cycles/sec (or craft speed base)
    int minDrillTier{0};           // 1=Fe/Cu/Co, 2=+Ti/Ur, 3=+Aether
    float beltSpeed{0.0f};         // items/sec for belts/splitters/tunnels
    float height25D{16.0f};        // 2.5D extrusion height in world pixels
    bool rotatable{false};
    bool requiresOre{false};
    bool requiresWater{false};
    bool isGenerator{false};
    bool isRatchetGenerator{false};
    bool isMechanical{true};
    bool isRailPlaceable{false};
    bool isTrainSpawner{false};
    ResourceType fuelType{ResourceType::Count};
    float fuelInterval{0.0f};
};

struct UpgradeDef {
    UpgradeId id{};
    std::string key;
    std::string name;
    std::string branch;
    std::string description;
    int maxLevel{1};
    ResourceBag baseCost{};
    double costGrowth{1.65};
    UpgradeId prerequisite{UpgradeId::Count};
    UpgradeId prerequisite2{UpgradeId::Count};
    float treeX{0.0f};
    float treeY{1.0f};
    BuildingType iconType{BuildingType::SmallCog};
};

struct Tile {
    TerrainType terrain{TerrainType::Ground};
    uint8_t biome{0};       // 0=Slate, 1=RustMesa, 2=MossBasin, 3=ObsidianRift, 4=QuartzTundra
    float elevation{0.20f}; // 0.0=water, 0.1..0.35=ground relief, 0.55..1.0=cliff height
    OreType ore{OreType::None};
    float richness{1.0f};
    int occupantId{-1};
    uint32_t visualSeed{0};
};

struct BuildingEntity {
    int id{0};
    BuildingType type{BuildingType::SmallCog};
    int gx{0};
    int gy{0};
    int dir{0}; // 0=E, 1=S, 2=W, 3=N (TrainStation: 0=LOAD, 1=UNLOAD)

    // Mechanical nodes
    float rpm{0.0f};
    float angle{0.0f};
    int networkId{-1};
    NetworkStatus status{NetworkStatus::Unpowered};

    float rpmSecondary{0.0f};
    float angleSecondary{0.0f};
    int networkIdSecondary{-1};
    NetworkStatus statusSecondary{NetworkStatus::Unpowered};

    // Electrical grid & aura state
    int electricNetId{-1};
    bool electricPowered{false};
    int adjacentCoolers{0};
    bool overclocked{false};
    bool waterBoosted{false};
    float batteryCharge{0.0f}; // 0..1 for Accumulator

    // Machine, crafting & buffer state
    float drillProgress{0.0f};
    float oreAccumulator{0.0f};
    float fuelTimer{0.0f};
    bool hasFuel{true};
    float crankBoostTimer{0.0f};
    float flywheelEnergy{0.0f};
    bool lubricated{false};
    float pistonPhase{0.0f};

    // Crafted product output buffer
    ResourceType craftedType{ResourceType::Count};
    int craftedCount{0};

    // Conveyor belt / splitter / tunnel 2-slot physical item state
    ResourceType beltItemA{ResourceType::Count}; // leading slot [0.5 .. 1.0]
    float beltProgA{0.0f};
    ResourceType beltItemB{ResourceType::Count}; // trailing slot [0.0 .. 0.5]
    float beltProgB{0.0f};
    uint8_t splitterIdx{0};

    // Input / local storage (recipe inputs, remote drills, vaults, stations)
    ResourceBag localStorage{};
};

struct NetworkInfo {
    int id{0};
    NetworkStatus status{NetworkStatus::Unpowered};
    float capacityPU{0.0f};
    float loadPU{0.0f};
    float referenceRpm{0.0f};
    float bufferRatio{0.0f};
    int nodeCount{0};
    std::string diagnostic;
};

struct ElectricNetworkInfo {
    int id{0};
    float supplyKW{0.0f};
    float demandKW{0.0f};
    float batteryDischargeKW{0.0f};
    float avgBatteryRatio{0.0f};
    int batteryCount{0};
    bool powered{false};
    bool overloaded{false};
    int nodeCount{0};
};

struct PowerLineLink {
    int ax{0}, ay{0};
    int bx{0}, by{0};
    bool powered{false};
    bool overloaded{false};
};

enum class TrainState {
    Idle = 0,
    MovingToLoad,
    Loading,
    MovingToUnload,
    Unloading
};

struct TrainEntity {
    int id{0};
    bool isElectric{false};
    float wx{0.0f};
    float wy{0.0f};
    float angle{0.0f};
    TrainState state{TrainState::Idle};
    float waitTimer{0.0f};
    float fuelTimer{0.0f};
    bool hasFuel{true};
    int targetStationId{-1};
    std::vector<std::pair<int, int>> path;
    int pathIndex{0};
    ResourceBag cargo{};
    std::vector<std::pair<float, float>> trail;
};

struct BlueprintEntry {
    BuildingType type{BuildingType::SmallCog};
    int dx{0};
    int dy{0};
    int dir{0};
};

struct Particle {
    float x{0.0f};
    float y{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float life{0.0f};
    float maxLife{1.0f};
    float size{3.0f};
    uint8_t r{255}, g{255}, b{255}, a{255};
};

struct FloatingPopup {
    float x{0.0f};
    float y{0.0f};
    float vy{-28.0f};
    float life{0.0f};
    float maxLife{1.2f};
    std::string text;
    uint8_t r{255}, g{255}, b{255};
};

inline std::string resourceName(ResourceType type) {
    switch (type) {
    case ResourceType::Iron:        return "Iron";
    case ResourceType::Copper:      return "Copper";
    case ResourceType::Coal:        return "Coal";
    case ResourceType::Titanium:    return "Titanium";
    case ResourceType::Uranium:     return "Uranium";
    case ResourceType::Aether:      return "Aether";
    case ResourceType::Steel:       return "Steel";
    case ResourceType::Circuit:     return "Circuits";
    case ResourceType::QuantumCore: return "Q-Cores";
    default:                        return "None";
    }
}

inline const char* resourceTag(ResourceType type) {
    switch (type) {
    case ResourceType::Iron:        return "Fe";
    case ResourceType::Copper:      return "Cu";
    case ResourceType::Coal:        return "Co";
    case ResourceType::Titanium:    return "Ti";
    case ResourceType::Uranium:     return "Ur";
    case ResourceType::Aether:      return "Ae";
    case ResourceType::Steel:       return "St";
    case ResourceType::Circuit:     return "Ci";
    case ResourceType::QuantumCore: return "Qc";
    default:                        return "??";
    }
}

inline double resourceSinkValue(ResourceType type) {
    switch (type) {
    case ResourceType::Iron:        return 1.0;
    case ResourceType::Copper:      return 1.0;
    case ResourceType::Coal:        return 1.2;
    case ResourceType::Titanium:    return 3.0;
    case ResourceType::Steel:       return 4.0;
    case ResourceType::Circuit:     return 6.0;
    case ResourceType::Uranium:     return 8.0;
    case ResourceType::Aether:      return 12.0;
    case ResourceType::QuantumCore: return 45.0;
    default:                        return 1.0;
    }
}

inline ResourceType oreToResource(OreType ore) {
    switch (ore) {
    case OreType::Iron:     return ResourceType::Iron;
    case OreType::Copper:   return ResourceType::Copper;
    case OreType::Coal:     return ResourceType::Coal;
    case OreType::Titanium: return ResourceType::Titanium;
    case OreType::Uranium:  return ResourceType::Uranium;
    case OreType::Aether:   return ResourceType::Aether;
    default:                return ResourceType::Count;
    }
}

inline std::string oreName(OreType ore) {
    switch (ore) {
    case OreType::Iron:     return "Iron Vein";
    case OreType::Copper:   return "Copper Vein";
    case OreType::Coal:     return "Coal Seam";
    case OreType::Titanium: return "Titanium Vein";
    case OreType::Uranium:  return "Uranium Deposit";
    case OreType::Aether:   return "Aether Crystal";
    default:                return "Bedrock";
    }
}

} // namespace cogwill
