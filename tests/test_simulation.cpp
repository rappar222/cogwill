#include "Catalog.hpp"
#include "SaveLoad.hpp"
#include "World.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace cogwill;

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__                    \
                      << " -> " << #cond << "\n";                                             \
            std::exit(1);                                                                     \
        }                                                                                     \
    } while (false)

namespace {

bool approx(float a, float b, float eps = 0.05f) {
    return std::abs(a - b) <= eps;
}

float wrapAngle(float a) {
    float r = std::fmod(a, TWO_PI);
    if (r < 0.0f) r += TWO_PI;
    return r;
}

void grantAllResources(World& w, double amt = 5000.0) {
    for (int i = 0; i < static_cast<int>(ResourceType::Count); ++i) {
        w.resourcesMut().values[static_cast<std::size_t>(i)] = amt;
    }
}

void clearGroundPatch(World& w, int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!w.inBounds(x, y)) continue;
            if (std::abs(x - HUB_GX) <= 1 && std::abs(y - HUB_GY) <= 1) continue;
            w.tileMut(x, y).terrain = TerrainType::Ground;
        }
    }
}

void assertToothValleyMesh(const BuildingEntity& a, const BuildingEntity& b, int teethA, int teethB) {
    float dx = static_cast<float>(b.gx - a.gx);
    float dy = static_cast<float>(b.gy - a.gy);
    float alpha = std::atan2(dy, dx);
    float sum = static_cast<float>(teethA) * (alpha - a.angle)
              + static_cast<float>(teethB) * ((alpha + PI) - b.angle);
    float diff = wrapAngle(sum - PI);
    if (diff > PI) diff -= TWO_PI;
    CHECK(std::abs(diff) < 0.01f);
}

void testCogChainAndPhase() {
    World w;
    grantAllResources(w);
    clearGroundPatch(w, 80, 65, 92, 70);

    CHECK(w.placeBuilding(BuildingType::HandCrank, 84, 68));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 85, 68));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 86, 68));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 87, 68));

    const auto* g0 = w.buildingAt(84, 68);
    const auto* c1 = w.buildingAt(85, 68);
    const auto* c2 = w.buildingAt(86, 68);
    const auto* c3 = w.buildingAt(87, 68);
    CHECK(g0 && c1 && c2 && c3);
    CHECK(g0->status == NetworkStatus::Running);
    CHECK(approx(g0->rpm, 12.0f));
    CHECK(approx(c1->rpm, -12.0f));
    CHECK(approx(c2->rpm, 12.0f));
    CHECK(approx(c3->rpm, -12.0f));

    assertToothValleyMesh(*g0, *c1, 12, 12);
    assertToothValleyMesh(*c1, *c2, 12, 12);
    assertToothValleyMesh(*c2, *c3, 12, 12);

    for (int i = 0; i < 30; ++i) {
        w.step(1.0f / 60.0f);
    }
    assertToothValleyMesh(*g0, *c1, 12, 12);
    assertToothValleyMesh(*c1, *c2, 12, 12);
}

void testLargeGear3to1Ratio() {
    World w;
    grantAllResources(w);
    clearGroundPatch(w, 80, 64, 92, 72);
    w.setUpgradeLevelForTest(UpgradeId::UnlockLargeGear, 1);

    CHECK(w.placeBuilding(BuildingType::WindTurbine, 84, 68));
    CHECK(w.placeBuilding(BuildingType::LargeGear, 86, 68));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 88, 68));

    const auto* turbine = w.buildingAt(84, 68);
    const auto* big = w.buildingAt(86, 68);
    const auto* outCog = w.buildingAt(88, 68);
    CHECK(turbine && big && outCog);
    CHECK(approx(turbine->rpm, 24.0f));
    CHECK(approx(big->rpm, -8.0f));
    CHECK(approx(outCog->rpm, 24.0f));

    assertToothValleyMesh(*turbine, *big, 12, 36);
    assertToothValleyMesh(*big, *outCog, 36, 12);

    CHECK(w.placeBuilding(BuildingType::SmallCog, 84, 67));
    const auto* cornerCog = w.buildingAt(84, 67);
    CHECK(cornerCog && cornerCog->status == NetworkStatus::Running);
    CHECK(approx(cornerCog->rpm, -24.0f));
}

void testSpeedGearboxAndSynchronizedEngines() {
    World w;
    grantAllResources(w);
    clearGroundPatch(w, 80, 64, 92, 72);
    w.setUpgradeLevelForTest(UpgradeId::UnlockLargeGear, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockGearboxBridge, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSteamEngine, 1);

    CHECK(w.placeBuilding(BuildingType::WindTurbine, 84, 68));
    CHECK(w.placeBuilding(BuildingType::SpeedGearbox, 85, 68, 0));
    CHECK(w.placeBuilding(BuildingType::SteamEngine, 86, 68));

    const auto* wt = w.buildingAt(84, 68);
    const auto* gb = w.buildingAt(85, 68);
    const auto* st = w.buildingAt(86, 68);
    CHECK(wt && gb && st);
    CHECK(st->status == NetworkStatus::Running);
    CHECK(approx(std::abs(wt->rpm), 24.0f));
    CHECK(approx(std::abs(st->rpm), 48.0f));

    const NetworkInfo* net = w.networkById(st->networkId);
    CHECK(net != nullptr);
    CHECK(approx(net->capacityPU, 210.0f, 1.0f));

    CHECK(w.placeBuilding(BuildingType::SmallCog, 85, 67));
    CHECK(w.buildingAt(85, 67)->status == NetworkStatus::Unpowered);

    CHECK(w.removeBuildingAt(85, 68));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 85, 68));
    CHECK(w.buildingAt(84, 68)->status == NetworkStatus::Jammed);
    CHECK(w.buildingAt(86, 68)->status == NetworkStatus::Jammed);
}

void testAxleBridgeIndependentCrossings() {
    World w;
    grantAllResources(w);
    clearGroundPatch(w, 80, 64, 92, 72);
    w.setUpgradeLevelForTest(UpgradeId::UnlockGearboxBridge, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSteamEngine, 1);

    w.tileMut(85, 68).terrain = TerrainType::Water;

    CHECK(w.placeBuilding(BuildingType::HandCrank, 84, 68));
    CHECK(w.placeBuilding(BuildingType::AxleBridge, 85, 68));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 86, 68));
    CHECK(w.placeBuilding(BuildingType::SteamEngine, 85, 67));
    CHECK(w.placeBuilding(BuildingType::SmallCog, 85, 69));

    const auto* hOut = w.buildingAt(86, 68);
    const auto* vOut = w.buildingAt(85, 69);
    CHECK(hOut && vOut);
    CHECK(hOut->status == NetworkStatus::Running);
    CHECK(vOut->status == NetworkStatus::Running);
    CHECK(approx(std::abs(hOut->rpm), 12.0f));
    CHECK(approx(std::abs(vOut->rpm), 48.0f));
}

void testDrillingOverloadLubricatorAndFlywheel() {
    World w;
    grantAllResources(w, 500.0);
    w.setUpgradeLevelForTest(UpgradeId::UnlockUtilityMachines, 1);

    int oreX = 91, oreY = 72;
    clearGroundPatch(w, 89, 70, 93, 74);
    w.tileMut(oreX, oreY).ore = OreType::Iron;
    w.tileMut(oreX, oreY).richness = 1.2f;
    CHECK(w.isInDirectHubZone(oreX, oreY));

    CHECK(w.placeBuilding(BuildingType::IronDrill, oreX, oreY));
    CHECK(w.placeBuilding(BuildingType::HandCrank, oreX - 1, oreY));

    const auto* drill = w.buildingAt(oreX, oreY);
    CHECK(drill && drill->status == NetworkStatus::Running);

    double ironBefore = w.resources()[ResourceType::Iron];
    for (int i = 0; i < 160; ++i) {
        w.step(0.05f);
    }
    CHECK(w.resources()[ResourceType::Iron] > ironBefore);

    CHECK(w.placeBuilding(BuildingType::Lubricator, oreX - 1, oreY - 1));
    CHECK(w.buildingAt(oreX, oreY)->lubricated);

    double beforeRefund = w.resources()[ResourceType::Iron];
    ResourceBag refunded;
    CHECK(w.removeBuildingAt(oreX, oreY, &refunded));
    CHECK(std::abs(w.resources()[ResourceType::Iron] - (beforeRefund + Catalog::getBuilding(BuildingType::IronDrill).cost[ResourceType::Iron])) < 1e-4);

    // Flywheel kinetic buffer test when SteamEngine runs out of coal
    World w2;
    grantAllResources(w2, 500.0);
    w2.resourcesMut()[ResourceType::Coal] = 1.0;
    clearGroundPatch(w2, 82, 66, 90, 70);
    w2.setUpgradeLevelForTest(UpgradeId::UnlockSteamEngine, 1);
    w2.setUpgradeLevelForTest(UpgradeId::UnlockUtilityMachines, 1);

    CHECK(w2.placeBuilding(BuildingType::SteamEngine, 84, 68));
    CHECK(w2.placeBuilding(BuildingType::Flywheel, 85, 68));
    CHECK(w2.placeBuilding(BuildingType::SmallCog, 86, 68));
    CHECK(w2.buildingAt(84, 68)->status == NetworkStatus::Running);

    for (int i = 0; i < 100; ++i) {
        w2.step(0.05f);
    }
    CHECK(w2.buildingAt(85, 68)->flywheelEnergy > 0.9f);

    for (int i = 0; i < 700; ++i) {
        w2.step(0.05f);
        if (!w2.buildingAt(84, 68)->hasFuel) {
            break;
        }
    }
    CHECK(!w2.buildingAt(84, 68)->hasFuel);
    CHECK(w2.buildingAt(86, 68)->status == NetworkStatus::Buffering);
    CHECK(std::abs(w2.buildingAt(86, 68)->rpm) > 10.0f);
}

void testOverloadDetection() {
    World w;
    grantAllResources(w);
    clearGroundPatch(w, 86, 66, 93, 70);
    w.setUpgradeLevelForTest(UpgradeId::UnlockHeavyDrill, 1);

    w.tileMut(89, 68).ore = OreType::Iron;
    w.tileMut(90, 68).ore = OreType::Iron;

    CHECK(w.placeBuilding(BuildingType::HandCrank, 88, 68));
    CHECK(w.placeBuilding(BuildingType::HeavyDrill, 89, 68));
    CHECK(w.buildingAt(89, 68)->status == NetworkStatus::Running);

    CHECK(w.placeBuilding(BuildingType::HeavyDrill, 90, 68));
    CHECK(w.buildingAt(89, 68)->status == NetworkStatus::Overloaded);
    CHECK(approx(w.buildingAt(89, 68)->rpm, 0.0f));

    CHECK(w.removeBuildingAt(90, 68));
    CHECK(w.placeBuilding(BuildingType::IronDrill, 90, 68));
    CHECK(w.buildingAt(89, 68)->status == NetworkStatus::Running);
}

void testExpandedBiomesTitaniumUraniumAndAetherTiers() {
    CHECK(GRID_W == 192 && GRID_H == 144);
    World w;
    grantAllResources(w);
    w.setUpgradeLevelForTest(UpgradeId::UnlockHeavyDrill, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricDrill, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockAetherTier, 1);

    bool hasWater = false, hasRock = false;
    int biomeMask = 0;
    int tiX = -1, tiY = -1;
    int aeX = -1, aeY = -1;
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            const Tile& t = w.tile(x, y);
            biomeMask |= (1 << std::min<int>(4, t.biome));
            if (t.terrain == TerrainType::Water) hasWater = true;
            if (t.terrain == TerrainType::Rock) hasRock = true;
            if (t.ore == OreType::Titanium && tiX < 0 && t.terrain == TerrainType::Ground && w.buildingAt(x, y) == nullptr) {
                tiX = x;
                tiY = y;
            }
            if (t.ore == OreType::Aether && aeX < 0 && t.terrain == TerrainType::Ground && w.buildingAt(x, y) == nullptr) {
                aeX = x;
                aeY = y;
            }
        }
    }
    CHECK(hasWater && hasRock);
    CHECK(biomeMask >= 7); // Multiple procedural biomes generated
    CHECK(tiX >= 0 && aeX >= 0);

    CHECK(!w.checkPlacement(BuildingType::IronDrill, tiX, tiY).valid);
    CHECK(w.checkPlacement(BuildingType::HeavyDrill, tiX, tiY).valid);
    CHECK(w.checkPlacement(BuildingType::ElectricDrill, tiX, tiY).valid);
    CHECK(!w.checkPlacement(BuildingType::HeavyDrill, aeX, aeY).valid);
    CHECK(!w.checkPlacement(BuildingType::ElectricDrill, aeX, aeY).valid);
    CHECK(w.checkPlacement(BuildingType::AetherExtractor, aeX, aeY).valid);
}

void testWorldSeedsAndProceduralPresets() {
    CHECK(hashWorldSeed("123456") == 123456u);
    CHECK(hashWorldSeed("COGWILL-ALPHA") == hashWorldSeed("  COGWILL-ALPHA  "));
    CHECK(hashWorldSeed("COGWILL-ALPHA") != hashWorldSeed("COGWILL-BETA"));

    WorldGenSettings genA;
    genA.seedText = "SEED-OMEGA-77";
    genA.preset = WorldPreset::BasaltCanyons;
    genA.oreRichness = 1.5f;
    genA.waterCoverage = 1.25f;
    genA.cliffCoverage = 1.5f;

    World w1, w2;
    w1.resetNewWorld(genA, false);
    w2.resetNewWorld(genA, false);
    CHECK(w1.mapSeed() == w2.mapSeed());

    for (int i = 0; i < GRID_W * GRID_H; ++i) {
        CHECK(w1.tiles()[static_cast<std::size_t>(i)].terrain == w2.tiles()[static_cast<std::size_t>(i)].terrain);
        CHECK(w1.tiles()[static_cast<std::size_t>(i)].biome == w2.tiles()[static_cast<std::size_t>(i)].biome);
        CHECK(w1.tiles()[static_cast<std::size_t>(i)].ore == w2.tiles()[static_cast<std::size_t>(i)].ore);
        CHECK(approx(w1.tiles()[static_cast<std::size_t>(i)].elevation, w2.tiles()[static_cast<std::size_t>(i)].elevation, 0.001f));
    }

    // Different seed must produce different world layout
    WorldGenSettings genB = genA;
    genB.seedText = "SEED-OMEGA-99";
    World w3;
    w3.resetNewWorld(genB, false);
    int diffs = 0;
    for (int i = 0; i < GRID_W * GRID_H; ++i) {
        if (w1.tiles()[static_cast<std::size_t>(i)].terrain != w3.tiles()[static_cast<std::size_t>(i)].terrain ||
            w1.tiles()[static_cast<std::size_t>(i)].biome != w3.tiles()[static_cast<std::size_t>(i)].biome) {
            ++diffs;
        }
    }
    CHECK(diffs > 2000);

    // Zero water & zero cliff coverage sliders produce completely dry, cliff-free plains
    WorldGenSettings dryFlat;
    dryFlat.seedText = "FLAT-MEGABASE";
    dryFlat.preset = WorldPreset::MegabasePlains;
    dryFlat.waterCoverage = 0.0f;
    dryFlat.cliffCoverage = 0.0f;
    World wFlat;
    wFlat.resetNewWorld(dryFlat, false);
    for (const Tile& t : wFlat.tiles()) {
        CHECK(t.terrain == TerrainType::Ground);
    }

    // RiverArchipelago has significantly more water than MegabasePlains
    WorldGenSettings arch;
    arch.seedText = "ISLANDS-42";
    arch.preset = WorldPreset::RiverArchipelago;
    World wArch;
    wArch.resetNewWorld(arch, false);

    WorldGenSettings plains;
    plains.seedText = "ISLANDS-42";
    plains.preset = WorldPreset::MegabasePlains;
    World wPlains;
    wPlains.resetNewWorld(plains, false);

    int archWater = 0, plainsWater = 0;
    for (int i = 0; i < GRID_W * GRID_H; ++i) {
        if (wArch.tiles()[static_cast<std::size_t>(i)].terrain == TerrainType::Water) ++archWater;
        if (wPlains.tiles()[static_cast<std::size_t>(i)].terrain == TerrainType::Water) ++plainsWater;
    }
    CHECK(archWater > plainsWater * 2);
}

void testSandboxModeCreativeFeatures() {
    WorldGenSettings gen;
    gen.seedText = "SANDBOX-LAB";
    World w;
    w.resetNewWorld(gen, true);
    CHECK(w.isSandboxMode());

    // Zero resources in stockpile, zero techs researched
    for (double& v : w.resourcesMut().values) {
        v = 0.0;
    }
    CHECK(w.getUpgradeLevel(UpgradeId::UnlockFusionPower) == 0);
    CHECK(w.isBuildingUnlocked(BuildingType::FusionReactor));
    CHECK(w.isBuildingUnlocked(BuildingType::QuantumForge));
    CHECK(w.isBuildingUnlocked(BuildingType::ExpressBelt));

    clearGroundPatch(w, 84, 68, 94, 76);

    // Build endgame FusionReactor, Substation, QuantumForge & ExpressBelt at 0 resource cost!
    CHECK(w.placeBuilding(BuildingType::FusionReactor, 87, 72));
    CHECK(w.placeBuilding(BuildingType::Substation, 90, 72));
    CHECK(w.placeBuilding(BuildingType::QuantumForge, 92, 72));
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 93, 72, 0));
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 94, 72, 0));

    // HubCore remains unique & protected
    CHECK(!w.removeBuildingAt(HUB_GX, HUB_GY));
    CHECK(!w.isBuildingUnlocked(BuildingType::HubCore));

    // Free [F] manual machine feed even with 0 stockpile
    CHECK(w.manualFeedMachineAt(92, 72));
    CHECK(w.buildingAt(92, 72)->localStorage[ResourceType::Steel] >= 2.0);

    // FusionReactor stays fueled with 0 Aether in stockpile & powers QuantumForge
    for (int i = 0; i < 120; ++i) {
        w.step(0.05f);
    }
    CHECK(w.buildingAt(87, 72)->hasFuel);
    CHECK(w.buildingAt(92, 72)->electricPowered);

    // Free tech & Spire unlocks in Sandbox Mode
    CHECK(w.buyUpgrade(UpgradeId::UnlockFusionPower));
    CHECK(w.completeSpirePhase());
    CHECK(w.spirePhase() == 1);
}

void testTechTree40UpgradesAndMegastructures() {
    const auto& order = Catalog::upgradeOrder();
    CHECK(order.size() == 40);

    // Verify true rooted, strictly downward acyclic branching tree topology & non-overlapping node spacing
    int rootBoughs = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const UpgradeDef& def = Catalog::getUpgrade(order[i]);
        CHECK(def.treeY >= 1.0f && def.treeY <= 7.0f);
        if (def.prerequisite == UpgradeId::Count) {
            ++rootBoughs;
            CHECK(approx(def.treeY, 1.0f));
        } else {
            const UpgradeDef& p1 = Catalog::getUpgrade(def.prerequisite);
            CHECK(p1.treeY < def.treeY);
        }
        if (def.prerequisite2 != UpgradeId::Count) {
            const UpgradeDef& p2 = Catalog::getUpgrade(def.prerequisite2);
            CHECK(p2.treeY < def.treeY);
        }
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const UpgradeDef& other = Catalog::getUpgrade(order[j]);
            if (std::abs(def.treeY - other.treeY) < 0.5f) {
                CHECK(std::abs(def.treeX - other.treeX) >= 1.20f);
            }
        }
    }
    CHECK(rootBoughs == 4);

    World w;
    grantAllResources(w, 100000.0);

    // UnlockNuclearPower requires UnlockSubstations + UnlockElectricDrill
    w.setUpgradeLevelForTest(UpgradeId::UnlockSubstations, 1);
    CHECK(!w.canBuyUpgrade(UpgradeId::UnlockNuclearPower));
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricDrill, 1);
    CHECK(w.canBuyUpgrade(UpgradeId::UnlockNuclearPower));
    CHECK(w.buyUpgrade(UpgradeId::UnlockNuclearPower));

    // UnlockFusionPower requires UnlockNuclearPower + UnlockQuantumForge
    CHECK(!w.canBuyUpgrade(UpgradeId::UnlockFusionPower));
    w.setUpgradeLevelForTest(UpgradeId::UnlockQuantumForge, 1);
    CHECK(w.canBuyUpgrade(UpgradeId::UnlockFusionPower));
    CHECK(w.buyUpgrade(UpgradeId::UnlockFusionPower));

    // Walk branching boughs & Megastructure convergence capstones
    CHECK(w.buyUpgrade(UpgradeId::SpringTension));
    float solar0 = w.solarPowerMultiplier();
    CHECK(w.buyUpgrade(UpgradeId::SolarEfficiency));
    CHECK(w.solarPowerMultiplier() > solar0);

    float cooler0 = w.coolingTowerBonusMultiplier();
    CHECK(w.buyUpgrade(UpgradeId::HydroThermalMastery));
    CHECK(w.coolingTowerBonusMultiplier() > cooler0);

    CHECK(w.buyUpgrade(UpgradeId::UnlockSplittersTunnels));
    CHECK(w.buyUpgrade(UpgradeId::UnlockFastBelts));
    float belt0 = w.beltSpeedMultiplier();
    CHECK(w.buyUpgrade(UpgradeId::BeltCompression));
    CHECK(w.beltSpeedMultiplier() > belt0);

    w.setUpgradeLevelForTest(UpgradeId::UnlockSolarBattery, 1);
    float sc0 = w.superconductingMult();
    CHECK(w.buyUpgrade(UpgradeId::SuperconductingGrid));
    CHECK(w.superconductingMult() > sc0);

    CHECK(w.buyUpgrade(UpgradeId::UnlockLargeGear));
    CHECK(w.buyUpgrade(UpgradeId::UnlockAssembler));
    float craft0 = w.craftingSpeedMultiplier();
    CHECK(w.buyUpgrade(UpgradeId::SmartLoaders));
    CHECK(w.craftingSpeedMultiplier() > craft0);

    w.setUpgradeLevelForTest(UpgradeId::MagneticBearings, 1);
    CHECK(w.buyUpgrade(UpgradeId::UnlockOverclockers));
    int aura0 = w.beaconAuraRadius();
    CHECK(w.buyUpgrade(UpgradeId::BeaconAmplification));
    CHECK(w.beaconAuraRadius() > aura0);

    double sink0 = w.sinkPointMultiplier();
    CHECK(w.buyUpgrade(UpgradeId::MatterCompression));
    CHECK(w.sinkPointMultiplier() > sink0);

    float global0 = w.globalPrestigeMultiplier();
    CHECK(w.buyUpgrade(UpgradeId::PlanetaryLogistics));
    CHECK(w.globalPrestigeMultiplier() > global0 + 0.20f);
}

void testConveyorBeltsSplittersTunnelsAndCoreDelivery() {
    World w;
    grantAllResources(w, 2000.0);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSplittersTunnels, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockFastBelts, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockExpressBelts, 1);

    clearGroundPatch(w, 82, 70, 94, 74);
    CHECK(!w.isInDirectHubZone(84, 72));
    w.tileMut(84, 72).ore = OreType::Copper;
    w.tileMut(84, 72).richness = 2.0f;

    w.tileMut(89, 72).terrain = TerrainType::Rock;

    CHECK(w.placeBuilding(BuildingType::WindTurbine, 84, 71));
    CHECK(w.placeBuilding(BuildingType::IronDrill, 84, 72));
    CHECK(w.placeBuilding(BuildingType::ConveyorBelt, 85, 72, 0));
    CHECK(w.placeBuilding(BuildingType::BeltSplitter, 86, 72, 0));
    CHECK(w.placeBuilding(BuildingType::StorageVault, 86, 73));
    CHECK(w.placeBuilding(BuildingType::ConveyorBelt, 87, 72, 0));
    CHECK(w.placeBuilding(BuildingType::UndergroundBelt, 88, 72, 0));
    CHECK(w.placeBuilding(BuildingType::UndergroundBelt, 90, 72, 0));
    CHECK(w.findUndergroundExitIndex(88, 72, 0) >= 0);

    for (int bx = 91; bx <= 94; ++bx) {
        CHECK(w.placeBuilding(BuildingType::ConveyorBelt, bx, 72, 0));
    }

    CHECK(w.checkPlacement(BuildingType::FastBelt, 94, 72, 0).isUpgradeReplace);
    CHECK(w.placeBuilding(BuildingType::FastBelt, 94, 72, 0));
    CHECK(w.buildingAt(94, 72)->type == BuildingType::FastBelt);
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 94, 72, 0));
    CHECK(w.buildingAt(94, 72)->type == BuildingType::ExpressBelt);

    double cuBefore = w.resources()[ResourceType::Copper];
    for (int i = 0; i < 360; ++i) {
        w.step(0.05f);
    }
    CHECK(w.buildingAt(86, 73)->localStorage[ResourceType::Copper] >= 1.0);
    CHECK(w.resources()[ResourceType::Copper] > cuBefore + 2.0);
}

void testSmelterArcFurnaceAssemblerQuantumForgeAndOverclock() {
    World w;
    grantAllResources(w, 1000.0);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSteamEngine, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockAssembler, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockArcFurnace, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockQuantumForge, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockOverclockers, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricalGrid, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSubstations, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockNuclearPower, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockExpressBelts, 1);

    clearGroundPatch(w, 84, 64, 98, 78);
    w.tileMut(89, 68).terrain = TerrainType::Water;

    CHECK(w.placeBuilding(BuildingType::NuclearReactor, 86, 74));
    CHECK(w.placeBuilding(BuildingType::Substation, 91, 73));

    CHECK(w.placeBuilding(BuildingType::WindTurbine, 93, 70));
    CHECK(w.placeBuilding(BuildingType::StoneSmelter, 93, 71));
    CHECK(w.placeBuilding(BuildingType::WaterPump, 89, 68));
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 94, 71, 0));
    CHECK(w.manualFeedMachineAt(93, 71));

    CHECK(w.placeBuilding(BuildingType::ArcFurnace, 93, 72));
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 94, 72, 0));
    CHECK(w.manualFeedMachineAt(93, 72));

    CHECK(w.placeBuilding(BuildingType::Assembler, 93, 73));
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 94, 73, 0));
    CHECK(w.manualFeedMachineAt(93, 73));

    CHECK(w.placeBuilding(BuildingType::QuantumForge, 96, 75));
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 96, 74, 3));
    CHECK(w.manualFeedMachineAt(96, 75));

    CHECK(w.placeBuilding(BuildingType::OverclockBeacon, 94, 74));
    CHECK(w.buildingAt(93, 72)->overclocked);
    CHECK(w.buildingAt(93, 73)->overclocked);
    CHECK(w.buildingAt(96, 75)->overclocked);

    double stBefore = w.resources()[ResourceType::Steel];
    double ciBefore = w.resources()[ResourceType::Circuit];
    double qcBefore = w.resources()[ResourceType::QuantumCore];

    for (int i = 0; i < 300; ++i) {
        w.step(0.05f);
    }

    CHECK(w.resources()[ResourceType::Steel] > stBefore + 2.0);
    CHECK(w.resources()[ResourceType::Circuit] > ciBefore + 1.0);
    CHECK(w.resources()[ResourceType::QuantumCore] > qcBefore + 0.9);
}

void testElectricGridSolarAccumulatorAndFusionReactor() {
    World w;
    grantAllResources(w, 2000.0);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricalGrid, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSolarBattery, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSubstations, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockFusionPower, 1);

    clearGroundPatch(w, 74, 64, 90, 76);

    CHECK(w.placeBuilding(BuildingType::SolarPanel, 77, 68));
    CHECK(w.placeBuilding(BuildingType::SolarPanel, 78, 68));
    CHECK(w.placeBuilding(BuildingType::SolarPanel, 79, 68));
    CHECK(w.placeBuilding(BuildingType::Accumulator, 80, 68));
    CHECK(w.placeBuilding(BuildingType::PowerPole, 80, 69));
    CHECK(w.placeBuilding(BuildingType::ElectricMotor, 81, 69));
    CHECK(w.buildingAt(81, 69)->electricPowered);

    for (int i = 0; i < 240; ++i) {
        w.step(0.05f);
    }
    CHECK(w.buildingAt(80, 68)->batteryCharge > 0.45f);

    for (int i = 0; i < 600; ++i) {
        w.step(0.1f);
        if (w.solarFactor() < 0.05f) break;
    }
    CHECK(w.solarFactor() < 0.05f);
    w.step(0.1f);
    CHECK(w.buildingAt(81, 69)->electricPowered);

    CHECK(w.placeBuilding(BuildingType::FusionReactor, 85, 69));
    CHECK(w.placeBuilding(BuildingType::Substation, 83, 69));
    const auto* fusion = w.buildingAt(85, 69);
    CHECK(fusion && fusion->hasFuel);
    const ElectricNetworkInfo* enet = w.electricNetworkById(fusion->electricNetId);
    CHECK(enet && enet->supplyKW >= 2400.0f);
}

void testNuclearReactorAndCoolingTowers() {
    World w;
    grantAllResources(w);
    clearGroundPatch(w, 76, 66, 86, 74);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricalGrid, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSubstations, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockNuclearPower, 1);

    CHECK(w.placeBuilding(BuildingType::NuclearReactor, 80, 70));
    CHECK(w.placeBuilding(BuildingType::PowerPole, 83, 70));
    const auto* reactor = w.buildingAt(80, 70);
    CHECK(reactor && reactor->hasFuel);
    const ElectricNetworkInfo* net = w.electricNetworkById(reactor->electricNetId);
    CHECK(net && approx(net->supplyKW, 600.0f, 1.0f));

    CHECK(w.placeBuilding(BuildingType::CoolingTower, 78, 70));
    CHECK(w.placeBuilding(BuildingType::CoolingTower, 80, 68));
    reactor = w.buildingAt(80, 70);
    net = w.electricNetworkById(reactor->electricNetId);
    CHECK(reactor->adjacentCoolers == 2);
    CHECK(net && approx(net->supplyKW, 1200.0f, 1.0f));
}

void testSpirePhasesAndMatterSinkCoupons() {
    World w;
    grantAllResources(w, 50000.0);
    w.setUpgradeLevelForTest(UpgradeId::UnlockAssembler, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSplittersTunnels, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockOverclockers, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockExpressBelts, 1);
    clearGroundPatch(w, 84, 68, 92, 72);

    CHECK(w.spirePhase() == 0);
    for (int p = 1; p <= 5; ++p) {
        CHECK(w.canCompleteSpirePhase());
        CHECK(w.completeSpirePhase());
        CHECK(w.spirePhase() == p);
    }
    CHECK(w.canCompleteSpirePhase());
    CHECK(w.completeSpirePhase());
    CHECK(w.prestigeCount() == 1);
    CHECK(w.globalPrestigeMultiplier() > 1.35f);

    int couponsBefore = w.coupons();
    CHECK(w.placeBuilding(BuildingType::StorageVault, 86, 70));
    w.buildingAtMut(86, 70)->localStorage[ResourceType::QuantumCore] = 30.0;
    CHECK(w.placeBuilding(BuildingType::ExpressBelt, 87, 70, 0));
    CHECK(w.placeBuilding(BuildingType::MatterSink, 88, 70));

    for (int i = 0; i < 240; ++i) {
        w.step(0.05f);
    }
    CHECK(w.totalCouponsMinted() > 0);
    CHECK(w.coupons() > couponsBefore);

    float beltMultBefore = w.beltSpeedMultiplier();
    CHECK(w.spendCoupon(CouponItem::BeltAndCraftSpeed));
    CHECK(w.beltSpeedMultiplier() > beltMultBefore);

    float gridMultBefore = w.nuclearPowerMultiplier();
    float mechCapBefore = w.mechanicalCapMultiplier();
    CHECK(w.spendCoupon(CouponItem::GridAndShaftPower));
    CHECK(w.nuclearPowerMultiplier() > gridMultBefore);
    CHECK(w.mechanicalCapMultiplier() > mechCapBefore);
}

void testRemoteOutpostAndTrainsTransport() {
    World w;
    grantAllResources(w, 5000.0);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricalGrid, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockSubstations, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockHeavyDrill, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricDrill, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockNuclearPower, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockRailways, 1);
    w.setUpgradeLevelForTest(UpgradeId::FreightBonus, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockElectricTrains, 1);

    clearGroundPatch(w, 98, 62, 132, 72);
    CHECK(!w.isInDirectHubZone(125, 68));
    w.tileMut(125, 68).ore = OreType::Uranium;
    w.tileMut(125, 68).richness = 2.0f;

    CHECK(w.placeBuilding(BuildingType::NuclearReactor, 125, 64));
    CHECK(w.placeBuilding(BuildingType::PowerPole, 125, 67));
    CHECK(w.placeBuilding(BuildingType::ElectricDrill, 125, 68));

    double urStart = w.resources()[ResourceType::Uranium];
    for (int i = 0; i < 60; ++i) {
        w.step(0.05f);
    }
    CHECK(w.buildingAt(125, 68)->localStorage[ResourceType::Uranium] >= 1.0);

    CHECK(w.placeBuilding(BuildingType::TrainStation, 125, 70, 0));
    CHECK(w.placeBuilding(BuildingType::TrainStation, 100, 70, 1));
    for (int rx = 101; rx < 125; ++rx) {
        CHECK(w.placeBuilding(BuildingType::RailTrack, rx, 70));
    }
    CHECK(w.placeBuilding(BuildingType::ElectricTrain, 102, 70));
    CHECK(w.buildingAt(125, 70)->electricPowered);

    for (int i = 0; i < 400; ++i) {
        w.step(0.05f);
    }
    CHECK(w.resources()[ResourceType::Uranium] > urStart + 2.0);
}

void testSaveLoadV4AndSettingsRoundTrip() {
    WorldGenSettings gen;
    gen.seedText = "SAVE-SEED-25D";
    gen.preset = WorldPreset::AetherBadlands;
    gen.oreRichness = 1.75f;
    gen.waterCoverage = 1.25f;
    World w;
    w.resetNewWorld(gen, true);

    grantAllResources(w, 600.0);
    w.resourcesMut()[ResourceType::Titanium] = 177.0;
    w.resourcesMut()[ResourceType::Steel] = 94.0;
    w.resourcesMut()[ResourceType::QuantumCore] = 19.0;
    w.setUpgradeLevelForTest(UpgradeId::UnlockLargeGear, 1);
    w.setUpgradeLevelForTest(UpgradeId::UnlockRailways, 1);
    w.setUpgradeLevelForTest(UpgradeId::SolarEfficiency, 2);
    w.completeSpirePhase();

    clearGroundPatch(w, 80, 64, 92, 72);
    w.tileMut(81, 65).terrain = TerrainType::Water;
    w.tileMut(81, 65).biome = 3;
    w.tileMut(82, 65).terrain = TerrainType::Rock;
    w.tileMut(82, 65).elevation = 0.85f;
    CHECK(w.placeBuilding(BuildingType::WindTurbine, 84, 68));
    CHECK(w.placeBuilding(BuildingType::LargeGear, 86, 68));
    CHECK(w.placeBuilding(BuildingType::ConveyorBelt, 88, 70, 0));
    w.buildingAtMut(88, 70)->beltItemA = ResourceType::Circuit;
    w.buildingAtMut(88, 70)->beltProgA = 0.65f;
    CHECK(w.placeBuilding(BuildingType::TrainStation, 84, 70, 0));
    CHECK(w.placeBuilding(BuildingType::RailTrack, 85, 70));
    CHECK(w.placeBuilding(BuildingType::SteamTrain, 85, 70));

    w.resourcesMut()[ResourceType::Titanium] = 177.0;
    w.resourcesMut()[ResourceType::Steel] = 94.0;
    w.resourcesMut()[ResourceType::QuantumCore] = 19.0;

    const std::string tmpPath = "/tmp/cogwill_test_save_v4.json";
    std::string err;
    CHECK(SaveLoad::saveToFile(w, tmpPath, &err));

    World loaded;
    CHECK(SaveLoad::loadFromFile(loaded, tmpPath, &err));
    CHECK(loaded.mapSeed() == w.mapSeed());
    CHECK(loaded.worldGen().seedText == "SAVE-SEED-25D");
    CHECK(loaded.worldGen().preset == WorldPreset::AetherBadlands);
    CHECK(approx(loaded.worldGen().oreRichness, 1.75f, 0.01f));
    CHECK(loaded.isSandboxMode() == true);
    CHECK(loaded.tile(81, 65).terrain == TerrainType::Water);
    CHECK(loaded.tile(81, 65).biome == 3);
    CHECK(loaded.tile(82, 65).terrain == TerrainType::Rock);
    CHECK(approx(loaded.tile(82, 65).elevation, 0.85f, 0.02f));
    CHECK(approx(static_cast<float>(loaded.resources()[ResourceType::Titanium]), 177.0f));
    CHECK(approx(static_cast<float>(loaded.resources()[ResourceType::Steel]), 94.0f));
    CHECK(approx(static_cast<float>(loaded.resources()[ResourceType::QuantumCore]), 19.0f));
    CHECK(loaded.spirePhase() == 1);
    CHECK(loaded.getUpgradeLevel(UpgradeId::UnlockLargeGear) == 1);
    CHECK(loaded.getUpgradeLevel(UpgradeId::SolarEfficiency) == 2);
    CHECK(loaded.buildingAt(86, 68) != nullptr);
    CHECK(approx(loaded.buildingAt(86, 68)->rpm, -8.0f));
    CHECK(loaded.buildingAt(88, 70)->beltItemA == ResourceType::Circuit);
    CHECK(loaded.trains().size() == 1);
    std::remove(tmpPath.c_str());

    // GameSettings JSON persistence round-trip
    GameSettings cfg;
    cfg.mode25D = true;
    cfg.shadersEnabled = true;
    cfg.rtQuality = 3;
    cfg.extrusionDepth = 1.4f;
    cfg.bloomStrength = 1.30f;
    cfg.ambientOcclusion = false;
    cfg.waterReflections = true;
    cfg.postFxMode = 2;
    cfg.pixelScale = 1;
    cfg.vsync = false;
    cfg.fpsLimit = 144;
    cfg.fullscreen = false;
    cfg.showFps = true;
    cfg.masterVolume = 0.60f;
    cfg.muted = true;
    cfg.lightingTimeMode = 2;
    cfg.particleDensity = 2;
    cfg.showPopups = false;
    cfg.gridStyle = 2;
    cfg.autoSaveSec = 300;
    cfg.cameraSpeed = 1.5f;

    const std::string cfgPath = "/tmp/cogwill_test_settings.json";
    CHECK(SaveLoad::saveSettings(cfg, cfgPath, &err));
    GameSettings loadedCfg{};
    CHECK(SaveLoad::loadSettings(loadedCfg, cfgPath, &err));
    CHECK(loadedCfg.mode25D == true);
    CHECK(loadedCfg.rtQuality == 3);
    CHECK(approx(loadedCfg.extrusionDepth, 1.4f, 0.01f));
    CHECK(approx(loadedCfg.bloomStrength, 1.30f, 0.01f));
    CHECK(loadedCfg.ambientOcclusion == false);
    CHECK(loadedCfg.postFxMode == 2);
    CHECK(loadedCfg.pixelScale == 1);
    CHECK(loadedCfg.vsync == false);
    CHECK(loadedCfg.fpsLimit == 144);
    CHECK(approx(loadedCfg.masterVolume, 0.60f, 0.01f));
    CHECK(loadedCfg.muted == true);
    CHECK(loadedCfg.lightingTimeMode == 2);
    CHECK(loadedCfg.particleDensity == 2);
    CHECK(loadedCfg.showPopups == false);
    CHECK(loadedCfg.gridStyle == 2);
    CHECK(loadedCfg.autoSaveSec == 300);
    CHECK(approx(loadedCfg.cameraSpeed, 1.5f, 0.01f));
    std::remove(cfgPath.c_str());
}

} // namespace

int main() {
    testCogChainAndPhase();
    testLargeGear3to1Ratio();
    testSpeedGearboxAndSynchronizedEngines();
    testAxleBridgeIndependentCrossings();
    testDrillingOverloadLubricatorAndFlywheel();
    testOverloadDetection();
    testExpandedBiomesTitaniumUraniumAndAetherTiers();
    testWorldSeedsAndProceduralPresets();
    testSandboxModeCreativeFeatures();
    testTechTree40UpgradesAndMegastructures();
    testConveyorBeltsSplittersTunnelsAndCoreDelivery();
    testSmelterArcFurnaceAssemblerQuantumForgeAndOverclock();
    testElectricGridSolarAccumulatorAndFusionReactor();
    testNuclearReactorAndCoolingTowers();
    testSpirePhasesAndMatterSinkCoupons();
    testRemoteOutpostAndTrainsTransport();
    testSaveLoadV4AndSettingsRoundTrip();
    std::cout << "All 17 Cogwill simulation, worldgen, sandbox & settings test suites passed!\n";
    return 0;
}
