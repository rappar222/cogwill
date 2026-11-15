#pragma once

#include "Catalog.hpp"
#include "Types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cogwill {

struct PlacementCheck {
    bool valid{false};
    bool isUpgradeReplace{false};
    std::string reason;
};

struct WorldStats {
    std::array<double, static_cast<std::size_t>(ResourceType::Count)> totalMined{};
    std::array<double, static_cast<std::size_t>(ResourceType::Count)> ratePerSec{};
    double playTimeSec{0.0};
    int cogCount{0};
    int drillCount{0};
    int generatorCount{0};
    int factoryCount{0};
    int beltCount{0};
    int electricCount{0};
    int trainCount{0};
    bool hasUnlockedUpgrade{false};
};

class World {
public:
    World();

    void resetNewWorld(const WorldGenSettings& gen, bool sandboxMode = false);
    void resetNewMap(uint32_t seed);
    void step(float dt);

    // Sandbox & World Generation Controls
    const WorldGenSettings& worldGen() const { return genSettings_; }
    uint32_t mapSeed() const { return mapSeed_; }
    bool isSandboxMode() const { return sandboxMode_; }
    void setSandboxMode(bool enabled);
    void toggleSandboxMode() { setSandboxMode(!sandboxMode_); }
    bool isBuildingUnlocked(BuildingType type) const;
    void unlockAllTechs();
    void grantSandboxResources(double amount = 10000.0);

    // Atmosphere & Solar Lighting Mode (0=Dynamic, 1=Daylight, 2=Neon Night, 3=Golden Dusk)
    int lightingTimeMode() const { return lightingTimeMode_; }
    void setLightingTimeMode(int mode);
    float dayTime() const { return dayTime_; }
    void setDayTime(float t) { dayTime_ = t; }
    float effectiveDayPhase() const;
    float solarFactor() const;

    // Placement, In-Place Upgrade & Interaction
    PlacementCheck checkPlacement(BuildingType type, int gx, int gy, int dir = 0) const;
    bool placeBuilding(BuildingType type, int gx, int gy, int dir = 0, std::string* failReason = nullptr);
    bool removeBuildingAt(int gx, int gy, ResourceBag* refundedOut = nullptr);
    bool removeTrainNear(float wx, float wy, float maxDistPx = 30.0f, ResourceBag* refundedOut = nullptr);
    bool rotateBuildingAt(int gx, int gy);
    bool crankAt(int gx, int gy, float boostDuration = 1.6f);
    bool manualFeedMachineAt(int gx, int gy);

    // Upgrades, Spire Milestones & Matter Sink Coupons
    bool canBuyUpgrade(UpgradeId id) const;
    bool buyUpgrade(UpgradeId id);
    int getUpgradeLevel(UpgradeId id) const;
    void setUpgradeLevelForTest(UpgradeId id, int level);

    int spirePhase() const { return spirePhase_; }
    int prestigeCount() const { return prestigeCount_; }
    bool canCompleteSpirePhase() const;
    bool completeSpirePhase();

    double sinkPoints() const { return sinkPoints_; }
    double nextCouponCost() const;
    int coupons() const { return coupons_; }
    int couponLevel(CouponItem item) const { return couponLevels_[static_cast<std::size_t>(item)]; }
    int couponItemCost(CouponItem item) const;
    bool spendCoupon(CouponItem item);

    // Queries
    bool inBounds(int gx, int gy) const;
    const Tile& tile(int gx, int gy) const;
    Tile& tileMut(int gx, int gy);

    const BuildingEntity* buildingAt(int gx, int gy) const;
    BuildingEntity* buildingAtMut(int gx, int gy);
    const BuildingEntity* buildingById(int id) const;
    BuildingEntity* buildingByIdMut(int id);
    const std::vector<BuildingEntity>& buildings() const { return buildings_; }
    const std::vector<NetworkInfo>& networks() const { return networks_; }
    const NetworkInfo* networkById(int id) const;

    const std::vector<ElectricNetworkInfo>& electricNetworks() const { return electricNetworks_; }
    const ElectricNetworkInfo* electricNetworkById(int id) const;
    const std::vector<PowerLineLink>& powerLinks() const { return powerLinks_; }
    const std::vector<TrainEntity>& trains() const { return trains_; }

    const ResourceBag& resources() const { return resources_; }
    ResourceBag& resourcesMut() { return resources_; }
    const std::array<int, static_cast<std::size_t>(UpgradeId::Count)>& upgradeLevels() const {
        return upgradeLevels_;
    }
    const std::array<int, static_cast<std::size_t>(CouponItem::Count)>& couponLevels() const {
        return couponLevels_;
    }

    const std::vector<Particle>& particles() const { return particles_; }
    const std::vector<FloatingPopup>& popups() const { return popups_; }
    const WorldStats& stats() const { return stats_; }

    void setParticleDensity(int density) { particleDensity_ = std::clamp(density, 0, 2); }
    void setPopupsEnabled(bool enabled) { popupsEnabled_ = enabled; }

    // Effective upgrade, Spire & Coupon multipliers
    float globalPrestigeMultiplier() const;
    float drillSpeedMultiplier() const;
    float beltedMineBonusMultiplier() const;
    float oreYieldMultiplier() const;
    float richOreBonusMultiplier() const;
    float craftingSpeedMultiplier() const;
    float beltSpeedMultiplier() const;
    float stressMultiplier() const;
    float mechanicalCapMultiplier() const;
    float springPowerMultiplier() const;
    float windPowerMultiplier() const;
    float solarPowerMultiplier() const;
    float hydroBoostMultiplier() const;
    float coolingTowerBonusMultiplier() const;
    float superconductingMult() const;
    float accumulatorCapacityKJ() const;
    float beaconSpeedMultiplier() const;
    int beaconAuraRadius() const;
    float boilerPowerMultiplier() const;
    float boilerFuelMultiplier() const;
    float nuclearPowerMultiplier() const;
    float nuclearFuelMultiplier() const;
    int directHubRadius() const;
    bool isInDirectHubZone(int gx, int gy) const;
    double remoteDrillBufferCapacity() const;
    double vaultBufferCapacity() const;
    double stationBufferCapacity() const;
    double trainCargoCapacity() const;
    double freightBonusMultiplier() const;
    double sinkPointMultiplier() const;

    // Underground belt pair lookup (for simulation & renderer guide)
    int findUndergroundExitIndex(int gx, int gy, int dir) const;

    // Network recomputation
    void recomputeNetworks();

    // Serialization helpers
    void loadState(
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
    );
    const std::vector<Tile>& tiles() const { return tiles_; }
    int nextBuildingId() const { return nextBuildingId_; }
    int nextTrainId() const { return nextTrainId_; }
    int totalCouponsMinted() const { return totalCouponsMinted_; }

    // Geometry helper
    static void directionVector(int dir, int& dx, int& dy);
    static bool isBeltLike(BuildingType t);

private:
    void generateMap(uint32_t seed);
    void spawnOreCluster(int cx, int cy, OreType ore, int radius, float baseRichness, uint32_t& rngState);
    void spawnTerrainBlob(int cx, int cy, TerrainType terrain, int radius, uint32_t& rngState);
    void recomputeAurasAndLubrication();
    void solveMechanicalPass();
    void solveElectricGrid();
    bool findRailPath(
        int startGx,
        int startGy,
        int targetGx,
        int targetGy,
        std::vector<std::pair<int, int>>& outPath
    ) const;

    bool tryAcceptItem(BuildingEntity& target, ResourceType item);
    bool tryPushFromBuilding(const BuildingEntity& src, ResourceType item);
    void addSinkItem(ResourceType item, double count = 1.0);

    void updateMachinesAndMining(float dt);
    void updateBeltsAndLogistics(float dt);
    void updateTrains(float dt);
    void updateParticlesAndPopups(float dt);

    void addMiningBurst(float wx, float wy, ResourceType res, int amount, const std::string& suffix = "");
    void addPopupText(float wx, float wy, const std::string& text, uint8_t r, uint8_t g, uint8_t b);
    void addSteamPuff(float wx, float wy);
    void addSparkBurst(float wx, float wy, uint8_t r, uint8_t g, uint8_t b, int count);

    int tileIndex(int gx, int gy) const { return gy * GRID_W + gx; }
    int findBuildingIndexById(int id) const;

    WorldGenSettings genSettings_{};
    uint32_t mapSeed_{1337u};
    bool sandboxMode_{false};
    int lightingTimeMode_{0};
    int particleDensity_{1};
    bool popupsEnabled_{true};

    uint32_t particleRng_{0xCAFEBABEu};
    std::vector<Tile> tiles_;
    std::vector<BuildingEntity> buildings_;
    std::vector<NetworkInfo> networks_;
    std::vector<bool> mechHasPrimeMover_;
    std::vector<ElectricNetworkInfo> electricNetworks_;
    std::vector<PowerLineLink> powerLinks_;
    std::vector<TrainEntity> trains_;
    float solarRefreshTimer_{0.0f};

    ResourceBag resources_{};
    std::array<int, static_cast<std::size_t>(UpgradeId::Count)> upgradeLevels_{};
    std::array<int, static_cast<std::size_t>(CouponItem::Count)> couponLevels_{};
    int spirePhase_{0};
    int prestigeCount_{0};
    double sinkPoints_{0.0};
    int coupons_{0};
    int totalCouponsMinted_{0};

    float dayTime_{0.0f};

    int nextBuildingId_{1};
    int nextTrainId_{1};

    std::vector<Particle> particles_;
    std::vector<FloatingPopup> popups_;
    WorldStats stats_{};

    float rateTimer_{0.0f};
    std::array<double, static_cast<std::size_t>(ResourceType::Count)> minedThisWindow_{};
    std::array<double, static_cast<std::size_t>(ResourceType::Count)> smoothedRates_{};
};

} // namespace cogwill
