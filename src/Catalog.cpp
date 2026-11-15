#include "Catalog.hpp"

#include <algorithm>
#include <cmath>

namespace cogwill {

namespace {

ResourceBag makeCost(
    double iron = 0.0,
    double copper = 0.0,
    double coal = 0.0,
    double titanium = 0.0,
    double uranium = 0.0,
    double aether = 0.0,
    double steel = 0.0,
    double circuit = 0.0,
    double qcore = 0.0
) {
    ResourceBag bag;
    bag[ResourceType::Iron] = iron;
    bag[ResourceType::Copper] = copper;
    bag[ResourceType::Coal] = coal;
    bag[ResourceType::Titanium] = titanium;
    bag[ResourceType::Uranium] = uranium;
    bag[ResourceType::Aether] = aether;
    bag[ResourceType::Steel] = steel;
    bag[ResourceType::Circuit] = circuit;
    bag[ResourceType::QuantumCore] = qcore;
    return bag;
}

const std::array<BuildingDef, static_cast<std::size_t>(BuildingType::Count)>& buildBuildingTable() {
    static const std::array<BuildingDef, static_cast<std::size_t>(BuildingType::Count)> table = {{
        // 0: HubCore
        BuildingDef{
            .type = BuildingType::HubCore,
            .category = BuildingCategory::Factory,
            .id = "hub_core",
            .name = "Grand Clockwork Core",
            .shortLabel = "Core 3x3",
            .description = "3x3 Central Hub & Orbital Spire. Belt items into any edge to store them globally!",
            .cost = makeCost(),
            .footprintRadius = 1,
            .height25D = 46.0f,
            .isMechanical = false
        },

        // --- F1: Transmission ---
        // 1: SmallCog
        BuildingDef{
            .type = BuildingType::SmallCog,
            .category = BuildingCategory::Transmission,
            .id = "small_cog",
            .name = "Iron Cog",
            .shortLabel = "Cog 1x1",
            .description = "12-tooth 1:1 cogwheel. Meshes orthogonally and reverses spin.",
            .cost = makeCost(2),
            .teeth = 12,
            .baseStressLoad = 0.25f,
            .height25D = 10.0f,
            .isMechanical = true
        },
        // 2: LargeGear
        BuildingDef{
            .type = BuildingType::LargeGear,
            .category = BuildingCategory::Transmission,
            .id = "large_gear",
            .name = "Large Brass Gear",
            .shortLabel = "Gear 3x3",
            .description = "36-tooth 3x3 wheel. Meshes at cardinal edges for 3:1 speed or 1:3 torque.",
            .cost = makeCost(10, 6),
            .footprintRadius = 1,
            .teeth = 36,
            .baseStressLoad = 0.6f,
            .height25D = 14.0f,
            .isMechanical = true
        },
        // 3: SpeedGearbox
        BuildingDef{
            .type = BuildingType::SpeedGearbox,
            .category = BuildingCategory::Transmission,
            .id = "speed_gearbox",
            .name = "Speed Gearbox",
            .shortLabel = "2x Box",
            .description = "Inline 2:1 overdrive along arrow [R to rotate] (0.5x in reverse).",
            .cost = makeCost(8, 6, 0, 0, 0, 0, 2),
            .teeth = 12,
            .baseStressLoad = 0.5f,
            .height25D = 18.0f,
            .rotatable = true,
            .isMechanical = true
        },
        // 4: AxleBridge
        BuildingDef{
            .type = BuildingType::AxleBridge,
            .category = BuildingCategory::Transmission,
            .id = "axle_bridge",
            .name = "Axle Bridge",
            .shortLabel = "Bridge",
            .description = "Two insulated shafts (H & V) let separate gear lines cross safely (even over Water!).",
            .cost = makeCost(6, 6, 0, 0, 0, 0, 2),
            .teeth = 12,
            .baseStressLoad = 0.35f,
            .height25D = 16.0f,
            .isMechanical = true
        },
        // 5: Lubricator
        BuildingDef{
            .type = BuildingType::Lubricator,
            .category = BuildingCategory::Transmission,
            .id = "lubricator",
            .name = "Drill Lubricator",
            .shortLabel = "Lube",
            .description = "When spinning, boosts drills & smelters in a 3x3 aura by +35% speed (2.5 PU).",
            .cost = makeCost(12, 10, 4, 0, 0, 0, 4),
            .teeth = 12,
            .baseStressLoad = 2.5f,
            .height25D = 18.0f,
            .isMechanical = true
        },
        // 6: Flywheel
        BuildingDef{
            .type = BuildingType::Flywheel,
            .category = BuildingCategory::Transmission,
            .id = "flywheel",
            .name = "Kinetic Flywheel",
            .shortLabel = "Flywhl",
            .description = "Stores rotational inertia; keeps cog networks running during fuel dips.",
            .cost = makeCost(14, 10, 0, 0, 0, 0, 4),
            .teeth = 12,
            .baseStressLoad = 1.5f,
            .height25D = 15.0f,
            .isMechanical = true
        },

        // --- F2: Power & Storage ---
        // 7: HandCrank
        BuildingDef{
            .type = BuildingType::HandCrank,
            .category = BuildingCategory::Generator,
            .id = "hand_crank",
            .name = "Hand Crank",
            .shortLabel = "Crank",
            .description = "12 RPM / 24 PU passive. Click or hold [Space] over it to boost to 24 RPM / 48 PU.",
            .cost = makeCost(4),
            .teeth = 12,
            .baseRpm = 12.0f,
            .basePowerCapacity = 24.0f,
            .height25D = 14.0f,
            .isGenerator = true,
            .isRatchetGenerator = true,
            .isMechanical = true
        },
        // 8: WindTurbine
        BuildingDef{
            .type = BuildingType::WindTurbine,
            .category = BuildingCategory::Generator,
            .id = "wind_turbine",
            .name = "Wind Turbine",
            .shortLabel = "Wind",
            .description = "Steady automatic 24 RPM / 50 PU mechanical generator. Needs no fuel.",
            .cost = makeCost(10, 6),
            .teeth = 12,
            .baseRpm = 24.0f,
            .basePowerCapacity = 50.0f,
            .height25D = 28.0f,
            .isGenerator = true,
            .isMechanical = true
        },
        // 9: SteamEngine
        BuildingDef{
            .type = BuildingType::SteamEngine,
            .category = BuildingCategory::Generator,
            .id = "steam_engine",
            .name = "Steam Engine",
            .shortLabel = "Steam",
            .description = "48 RPM / 160 PU Coal boiler (+35% PU near Water/Pump, +25% if belt-fed Coal!).",
            .cost = makeCost(16, 10, 0, 0, 0, 0, 4),
            .teeth = 12,
            .baseRpm = 48.0f,
            .basePowerCapacity = 160.0f,
            .height25D = 24.0f,
            .isGenerator = true,
            .isMechanical = true,
            .fuelType = ResourceType::Coal,
            .fuelInterval = 6.0f
        },
        // 10: SolarPanel
        BuildingDef{
            .type = BuildingType::SolarPanel,
            .category = BuildingCategory::Generator,
            .id = "solar_panel",
            .name = "Solar Array",
            .shortLabel = "Solar",
            .description = "Generates up to 60 kW clean electricity during daylight. Pair with Accumulators!",
            .cost = makeCost(8, 12, 0, 4, 0, 0, 6, 4),
            .baseElectricKW = 60.0f,
            .height25D = 12.0f,
            .isMechanical = false
        },
        // 11: Accumulator
        BuildingDef{
            .type = BuildingType::Accumulator,
            .category = BuildingCategory::Generator,
            .id = "accumulator",
            .name = "Grid Accumulator",
            .shortLabel = "Battery",
            .description = "Stores 1800 kJ excess grid electricity and discharges up to 140 kW at night/peaks.",
            .cost = makeCost(8, 10, 0, 6, 0, 0, 6, 4),
            .baseElectricKW = 140.0f,
            .height25D = 22.0f,
            .isMechanical = false
        },
        // 12: AetherDynamo
        BuildingDef{
            .type = BuildingType::AetherDynamo,
            .category = BuildingCategory::Generator,
            .id = "aether_dynamo",
            .name = "Aether Dynamo",
            .shortLabel = "Dynamo",
            .description = "Hyper-torque 72 RPM / 500 PU core. Burns 1 Aether every 14s.",
            .cost = makeCost(0, 20, 0, 12, 0, 6, 24, 10),
            .teeth = 12,
            .baseRpm = 72.0f,
            .basePowerCapacity = 500.0f,
            .height25D = 28.0f,
            .isGenerator = true,
            .isMechanical = true,
            .fuelType = ResourceType::Aether,
            .fuelInterval = 14.0f
        },

        // --- F3: Drills & Pumps ---
        // 13: IronDrill
        BuildingDef{
            .type = BuildingType::IronDrill,
            .category = BuildingCategory::Drill,
            .id = "iron_drill",
            .name = "Iron Drill",
            .shortLabel = "Drill",
            .description = "Tier 1 Cog Drill: mines Iron, Copper & Coal onto adjacent Belts (6 PU @ 12 RPM).",
            .cost = makeCost(10, 4),
            .teeth = 12,
            .baseStressLoad = 6.0f,
            .baseMineRate = 0.65f,
            .minDrillTier = 1,
            .height25D = 20.0f,
            .requiresOre = true,
            .isMechanical = true
        },
        // 14: HeavyDrill
        BuildingDef{
            .type = BuildingType::HeavyDrill,
            .category = BuildingCategory::Drill,
            .id = "heavy_drill",
            .name = "Heavy Bore Drill",
            .shortLabel = "Heavy",
            .description = "Tier 2 Cog Bore: extracts Titanium, Uranium & basic ores fast (14 PU @ 12 RPM).",
            .cost = makeCost(18, 12, 0, 0, 0, 0, 8),
            .teeth = 12,
            .baseStressLoad = 14.0f,
            .baseMineRate = 1.15f,
            .minDrillTier = 2,
            .height25D = 24.0f,
            .requiresOre = true,
            .isMechanical = true
        },
        // 15: ElectricDrill
        BuildingDef{
            .type = BuildingType::ElectricDrill,
            .category = BuildingCategory::Drill,
            .id = "electric_drill",
            .name = "Electric Drill",
            .shortLabel = "E-Drill",
            .description = "Tier 2 Grid Drill (-45 kW). Mines Titanium, Uranium & basic ores with no cog shafts!",
            .cost = makeCost(12, 14, 0, 6, 0, 0, 10, 4),
            .baseElectricKW = -45.0f,
            .baseMineRate = 1.55f,
            .minDrillTier = 2,
            .height25D = 24.0f,
            .requiresOre = true,
            .isMechanical = false
        },
        // 16: AetherExtractor
        BuildingDef{
            .type = BuildingType::AetherExtractor,
            .category = BuildingCategory::Drill,
            .id = "aether_extractor",
            .name = "Aether Laser Extractor",
            .shortLabel = "AethEx",
            .description = "Tier 3 Extractor (Cog OR -85 kW Grid): mines ALL ores including Aether Crystals!",
            .cost = makeCost(0, 20, 0, 20, 0, 0, 30, 16),
            .teeth = 12,
            .baseStressLoad = 18.0f,
            .baseElectricKW = -85.0f,
            .baseMineRate = 2.10f,
            .minDrillTier = 3,
            .height25D = 28.0f,
            .requiresOre = true,
            .isMechanical = true
        },
        // 17: WaterPump
        BuildingDef{
            .type = BuildingType::WaterPump,
            .category = BuildingCategory::Drill,
            .id = "water_pump",
            .name = "Hydro Coolant Pump",
            .shortLabel = "Pump",
            .description = "Place on or beside Water: boosts Steam Engines, Coolers & Smelters in 3-tile radius by +35%!",
            .cost = makeCost(12, 10, 0, 0, 0, 0, 4),
            .height25D = 20.0f,
            .requiresWater = true,
            .isMechanical = false
        },

        // --- F4: Factory, Crafting & Sink ---
        // 18: StoneSmelter
        BuildingDef{
            .type = BuildingType::StoneSmelter,
            .category = BuildingCategory::Factory,
            .id = "stone_smelter",
            .name = "Cog Blast Smelter",
            .shortLabel = "Smelter",
            .description = "Cog-driven furnace (8 PU): smelts 2 Iron + 1 Coal -> 1 Steel onto outgoing Belts!",
            .cost = makeCost(12, 6),
            .teeth = 12,
            .baseStressLoad = 8.0f,
            .baseMineRate = 0.55f,
            .height25D = 24.0f,
            .rotatable = true,
            .isMechanical = true
        },
        // 19: ArcFurnace
        BuildingDef{
            .type = BuildingType::ArcFurnace,
            .category = BuildingCategory::Factory,
            .id = "arc_furnace",
            .name = "Electric Arc Furnace",
            .shortLabel = "ArcFurn",
            .description = "Coal-free -75 kW plasma smelter: refines 2 Iron -> 1 Steel rapidly!",
            .cost = makeCost(0, 18, 0, 12, 0, 0, 20, 10),
            .baseElectricKW = -75.0f,
            .baseMineRate = 1.15f,
            .height25D = 26.0f,
            .rotatable = true,
            .isMechanical = false
        },
        // 20: Assembler
        BuildingDef{
            .type = BuildingType::Assembler,
            .category = BuildingCategory::Factory,
            .id = "assembler",
            .name = "Circuit Assembler",
            .shortLabel = "Assem",
            .description = "Cog (10 PU) OR Grid (-50 kW) fabricator: crafts 2 Copper + 1 Iron -> 1 Circuit!",
            .cost = makeCost(16, 14, 0, 0, 0, 0, 8),
            .teeth = 12,
            .baseStressLoad = 10.0f,
            .baseElectricKW = -50.0f,
            .baseMineRate = 0.70f,
            .height25D = 24.0f,
            .rotatable = true,
            .isMechanical = true
        },
        // 21: QuantumForge
        BuildingDef{
            .type = BuildingType::QuantumForge,
            .category = BuildingCategory::Factory,
            .id = "quantum_forge",
            .name = "Quantum Forge",
            .shortLabel = "Q-Forge",
            .description = "Endgame -180 kW forge: 2 Steel + 2 Circuits + 1 Titanium + 1 Aether -> 1 Quantum Core!",
            .cost = makeCost(0, 0, 0, 30, 0, 10, 50, 40),
            .baseElectricKW = -180.0f,
            .baseMineRate = 0.42f,
            .height25D = 30.0f,
            .rotatable = true,
            .isMechanical = false
        },
        // 22: OverclockBeacon
        BuildingDef{
            .type = BuildingType::OverclockBeacon,
            .category = BuildingCategory::Factory,
            .id = "overclock_beacon",
            .name = "Overclock Beacon",
            .shortLabel = "Beacon",
            .description = "5x5 Aura (-140 kW): grants +50% speed & +25% bonus output to nearby drills & factories!",
            .cost = makeCost(0, 0, 0, 28, 0, 8, 40, 36, 4),
            .baseElectricKW = -140.0f,
            .height25D = 32.0f,
            .isMechanical = false
        },
        // 23: MatterSink
        BuildingDef{
            .type = BuildingType::MatterSink,
            .category = BuildingCategory::Factory,
            .id = "matter_sink",
            .name = "A.W.E.S.O.M.E. Sink",
            .shortLabel = "Sink",
            .description = "Converts any belted excess items into Sink Points & permanent Factory Coupons!",
            .cost = makeCost(20, 14, 0, 0, 0, 0, 10),
            .height25D = 26.0f,
            .isMechanical = false
        },

        // --- F5: Belts & Logistics ---
        // 24: ConveyorBelt
        BuildingDef{
            .type = BuildingType::ConveyorBelt,
            .category = BuildingCategory::Belt,
            .id = "conveyor_belt",
            .name = "Conveyor Belt Mk1",
            .shortLabel = "Belt Mk1",
            .description = "Transports 2.8 items/s [R rotate, LMB drag auto-turns]. Feeds Core, Smelters & Forges.",
            .cost = makeCost(1),
            .beltSpeed = 2.8f,
            .height25D = 6.0f,
            .rotatable = true,
            .isMechanical = false
        },
        // 25: FastBelt
        BuildingDef{
            .type = BuildingType::FastBelt,
            .category = BuildingCategory::Belt,
            .id = "fast_belt",
            .name = "Titanium Belt Mk2",
            .shortLabel = "Belt Mk2",
            .description = "High-speed 6.0 items/s conveyor. Place directly over Mk1 Belts to upgrade in-place!",
            .cost = makeCost(1, 0, 0, 1, 0, 0, 1),
            .beltSpeed = 6.0f,
            .height25D = 6.5f,
            .rotatable = true,
            .isMechanical = false
        },
        // 26: ExpressBelt
        BuildingDef{
            .type = BuildingType::ExpressBelt,
            .category = BuildingCategory::Belt,
            .id = "express_belt",
            .name = "Quantum Mag-Belt Mk3",
            .shortLabel = "Belt Mk3",
            .description = "Blazing 12.0 items/s maglev belt. Upgrades Mk1/Mk2 belts in-place!",
            .cost = makeCost(0, 0, 0, 2, 0, 0, 1, 1),
            .beltSpeed = 12.0f,
            .height25D = 7.0f,
            .rotatable = true,
            .isMechanical = false
        },
        // 27: BeltSplitter
        BuildingDef{
            .type = BuildingType::BeltSplitter,
            .category = BuildingCategory::Belt,
            .id = "belt_splitter",
            .name = "Splitter / Merger",
            .shortLabel = "Splitter",
            .description = "4-way smart router: merges incoming belts and splits items evenly round-robin!",
            .cost = makeCost(4, 2),
            .beltSpeed = 8.0f,
            .height25D = 12.0f,
            .isMechanical = false
        },
        // 28: UndergroundBelt
        BuildingDef{
            .type = BuildingType::UndergroundBelt,
            .category = BuildingCategory::Belt,
            .id = "underground_belt",
            .name = "Underground Tunnel",
            .shortLabel = "Tunnel",
            .description = "Pairs with same-direction Tunnel up to 5 tiles away, passing under Cogs, Rails & Cliffs!",
            .cost = makeCost(6, 2),
            .beltSpeed = 7.0f,
            .height25D = 14.0f,
            .rotatable = true,
            .isMechanical = false
        },
        // 29: StorageVault
        BuildingDef{
            .type = BuildingType::StorageVault,
            .category = BuildingCategory::Belt,
            .id = "storage_vault",
            .name = "Buffer Vault",
            .shortLabel = "Vault",
            .description = "Stores up to 250 items inline between incoming and outgoing Conveyor Belts.",
            .cost = makeCost(10, 4, 0, 0, 0, 0, 4),
            .height25D = 22.0f,
            .isMechanical = false
        },

        // --- F6: Electric Grid, Nuclear & Fusion ---
        // 30: PowerPole
        BuildingDef{
            .type = BuildingType::PowerPole,
            .category = BuildingCategory::Electric,
            .id = "power_pole",
            .name = "Copper Power Pole",
            .shortLabel = "Pole",
            .description = "Links grid machines & poles within 9.5 tiles (drag LMB to auto-space at max reach!).",
            .cost = makeCost(3, 5),
            .powerRange = 9.5f,
            .height25D = 32.0f,
            .isMechanical = false
        },
        // 31: Substation
        BuildingDef{
            .type = BuildingType::Substation,
            .category = BuildingCategory::Electric,
            .id = "substation",
            .name = "HV Substation",
            .shortLabel = "SubStn",
            .description = "Heavy 16.5-tile transformer spanning canyons, outposts & large factories.",
            .cost = makeCost(0, 16, 0, 8, 0, 0, 12, 6),
            .powerRange = 16.5f,
            .height25D = 40.0f,
            .isMechanical = false
        },
        // 32: Alternator
        BuildingDef{
            .type = BuildingType::Alternator,
            .category = BuildingCategory::Electric,
            .id = "alternator",
            .name = "Dynamo Alternator",
            .shortLabel = "Alt kW",
            .description = "Converts cog rotation to electricity: 120 kW @ 24 RPM (draws 16 PU @ 12 RPM).",
            .cost = makeCost(12, 16, 0, 0, 0, 0, 6),
            .teeth = 12,
            .baseStressLoad = 16.0f,
            .baseElectricKW = 120.0f,
            .height25D = 22.0f,
            .isMechanical = true
        },
        // 33: ElectricMotor
        BuildingDef{
            .type = BuildingType::ElectricMotor,
            .category = BuildingCategory::Electric,
            .id = "electric_motor",
            .name = "Electric Motor",
            .shortLabel = "Motor",
            .description = "Converts -80 kW grid power into steady 24 RPM / 65 PU mechanical cog drive.",
            .cost = makeCost(10, 16, 0, 0, 0, 0, 6, 2),
            .teeth = 12,
            .baseRpm = 24.0f,
            .basePowerCapacity = 65.0f,
            .baseElectricKW = -80.0f,
            .height25D = 22.0f,
            .isGenerator = true,
            .isMechanical = true
        },
        // 34: NuclearReactor
        BuildingDef{
            .type = BuildingType::NuclearReactor,
            .category = BuildingCategory::Electric,
            .id = "nuclear_reactor",
            .name = "Nuclear Reactor",
            .shortLabel = "Nuke 3x3",
            .description = "3x3 Fission Core: 600 kW (+300 kW per adjacent Cooling Tower). Burns Uranium.",
            .cost = makeCost(0, 40, 0, 35, 10, 0, 60, 30),
            .footprintRadius = 1,
            .baseElectricKW = 600.0f,
            .height25D = 42.0f,
            .isMechanical = false,
            .fuelType = ResourceType::Uranium,
            .fuelInterval = 16.0f
        },
        // 35: CoolingTower
        BuildingDef{
            .type = BuildingType::CoolingTower,
            .category = BuildingCategory::Electric,
            .id = "cooling_tower",
            .name = "Cooling Tower",
            .shortLabel = "Cooler",
            .description = "Touch a 3x3 Nuclear/Fusion Reactor for +300 kW (+420 kW if hydro-cooled by Water!).",
            .cost = makeCost(16, 12, 0, 8, 0, 0, 16),
            .height25D = 34.0f,
            .isMechanical = false
        },
        // 36: FusionReactor
        BuildingDef{
            .type = BuildingType::FusionReactor,
            .category = BuildingCategory::Electric,
            .id = "fusion_reactor",
            .name = "Tokamak Fusion Core",
            .shortLabel = "Fuse 3x3",
            .description = "3x3 Endgame Plasma Core: 2400 kW (+400 kW/Cooler). Burns 1 Aether every 20s!",
            .cost = makeCost(0, 0, 0, 60, 20, 20, 100, 80, 8),
            .footprintRadius = 1,
            .baseElectricKW = 2400.0f,
            .height25D = 48.0f,
            .isMechanical = false,
            .fuelType = ResourceType::Aether,
            .fuelInterval = 20.0f
        },

        // --- F7: Railways & Trains ---
        // 37: RailTrack
        BuildingDef{
            .type = BuildingType::RailTrack,
            .category = BuildingCategory::Rail,
            .id = "rail_track",
            .name = "Steel Rail Track",
            .shortLabel = "Rail",
            .description = "Auto-connecting steel track (can cross Water!). Drag LMB to lay long freight lines.",
            .cost = makeCost(2, 0, 0, 0, 0, 0, 1),
            .height25D = 5.0f,
            .isMechanical = false,
            .isRailPlaceable = true
        },
        // 38: TrainStation
        BuildingDef{
            .type = BuildingType::TrainStation,
            .category = BuildingCategory::Rail,
            .id = "train_station",
            .name = "Train Station",
            .shortLabel = "Station",
            .description = "Collects nearby drills & belt inputs. [R] toggles LOAD (Outpost) / UNLOAD (Hub/Belts).",
            .cost = makeCost(16, 10, 0, 0, 0, 0, 12),
            .height25D = 18.0f,
            .rotatable = true,
            .isMechanical = false,
            .isRailPlaceable = true
        },
        // 39: SteamTrain
        BuildingDef{
            .type = BuildingType::SteamTrain,
            .category = BuildingCategory::Rail,
            .id = "steam_train",
            .name = "Steam Locomotive",
            .shortLabel = "StmTrain",
            .description = "Place on Rail/Station. Hauls freight from LOAD to UNLOAD stations (burns Coal).",
            .cost = makeCost(24, 16, 10, 0, 0, 0, 18),
            .height25D = 18.0f,
            .isMechanical = false,
            .isTrainSpawner = true,
            .fuelType = ResourceType::Coal,
            .fuelInterval = 14.0f
        },
        // 40: ElectricTrain
        BuildingDef{
            .type = BuildingType::ElectricTrain,
            .category = BuildingCategory::Rail,
            .id = "electric_train",
            .name = "Electric Express",
            .shortLabel = "ElcTrain",
            .description = "High-speed Coal-free locomotive powered by electrified Train Stations.",
            .cost = makeCost(0, 24, 0, 20, 0, 0, 28, 16),
            .height25D = 18.0f,
            .isMechanical = false,
            .isTrainSpawner = true
        }
    }};
    return table;
}

const std::array<UpgradeDef, static_cast<std::size_t>(UpgradeId::Count)>& buildUpgradeTable() {
    static const std::array<UpgradeDef, static_cast<std::size_t>(UpgradeId::Count)> table = {{
        // ==================== BOUGH 2: CLOCKWORK & FACTORY ====================
        UpgradeDef{
            .id = UpgradeId::UnlockLargeGear,
            .key = "unlock_large_gear",
            .name = "Large Brass Gear",
            .branch = "Clockwork & Craft",
            .description = "Unlocks 3x3 36-tooth gears for 3:1 speed or 1:3 torque multiplication.",
            .maxLevel = 1,
            .baseCost = makeCost(18, 10),
            .costGrowth = 1.0,
            .treeX = -1.60f, .treeY = 1.0f,
            .iconType = BuildingType::LargeGear
        },
        UpgradeDef{
            .id = UpgradeId::UnlockGearboxBridge,
            .key = "unlock_gearbox_bridge",
            .name = "Gearbox & Bridge",
            .branch = "Clockwork & Craft",
            .description = "Unlocks 2:1 Speed Gearbox and insulated Axle Bridge crossings.",
            .maxLevel = 1,
            .baseCost = makeCost(24, 16, 0, 0, 0, 0, 6),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockLargeGear,
            .treeX = -2.30f, .treeY = 2.0f,
            .iconType = BuildingType::SpeedGearbox
        },
        UpgradeDef{
            .id = UpgradeId::UnlockAssembler,
            .key = "unlock_assembler",
            .name = "Circuit Assembly",
            .branch = "Clockwork & Craft",
            .description = "Unlocks Circuit Assemblers (2 Cu + 1 Fe -> Circuit) & the A.W.E.S.O.M.E. Sink!",
            .maxLevel = 1,
            .baseCost = makeCost(30, 24, 0, 0, 0, 0, 12),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockLargeGear,
            .treeX = -0.95f, .treeY = 2.0f,
            .iconType = BuildingType::Assembler
        },
        UpgradeDef{
            .id = UpgradeId::PrecisionBearings,
            .key = "precision_bearings",
            .name = "Precision Bearings",
            .branch = "Clockwork & Craft",
            .description = "-12% mechanical stress load across all cog-driven machines per level.",
            .maxLevel = 5,
            .baseCost = makeCost(26, 18, 0, 0, 0, 0, 10),
            .costGrowth = 1.60,
            .prerequisite = UpgradeId::UnlockGearboxBridge,
            .treeX = -2.55f, .treeY = 3.0f,
            .iconType = BuildingType::SmallCog
        },
        UpgradeDef{
            .id = UpgradeId::UnlockUtilityMachines,
            .key = "unlock_utility",
            .name = "Lubricator & Flywheel",
            .branch = "Clockwork & Craft",
            .description = "Unlocks 3x3 Drill/Smelter Lubricators (+35% speed) & Kinetic Flywheels.",
            .maxLevel = 1,
            .baseCost = makeCost(36, 28, 0, 0, 0, 0, 16, 8),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::PrecisionBearings,
            .treeX = -2.80f, .treeY = 4.0f,
            .iconType = BuildingType::Lubricator
        },
        UpgradeDef{
            .id = UpgradeId::UnlockArcFurnace,
            .key = "unlock_arc_furnace",
            .name = "Arc Plasma Smelting",
            .branch = "Clockwork & Craft",
            .description = "Unlocks Coal-free Electric Arc Furnaces (2 Iron -> 1 Steel at 2x speed).",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 20, 0, 0, 50, 32),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::PrecisionBearings,
            .prerequisite2 = UpgradeId::UnlockElectricalGrid,
            .treeX = -1.30f, .treeY = 4.0f,
            .iconType = BuildingType::ArcFurnace
        },
        UpgradeDef{
            .id = UpgradeId::MagneticBearings,
            .key = "magnetic_bearings",
            .name = "Magnetic Bearings",
            .branch = "Clockwork & Craft",
            .description = "+25% mechanical PU capacity & additional -8% stress load per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 35, 0, 0, 75, 55),
            .costGrowth = 1.70,
            .prerequisite = UpgradeId::UnlockArcFurnace,
            .treeX = -1.55f, .treeY = 5.0f,
            .iconType = BuildingType::Flywheel
        },
        UpgradeDef{
            .id = UpgradeId::UnlockOverclockers,
            .key = "unlock_overclockers",
            .name = "Overclock Beacons",
            .branch = "Megastructures",
            .description = "Unlocks 5x5 Overclock Beacons (+50% speed & +25% bonus output to factories/drills).",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 60, 0, 0, 120, 95, 8),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::MagneticBearings,
            .prerequisite2 = UpgradeId::UnlockQuantumForge,
            .treeX = -1.15f, .treeY = 6.0f,
            .iconType = BuildingType::OverclockBeacon
        },

        // ==================== BOUGH 3: POWER, SOLAR & ATOMICS ====================
        UpgradeDef{
            .id = UpgradeId::SpringTension,
            .key = "spring_tension",
            .name = "Wind & Torque Springs",
            .branch = "Power & Atomics",
            .description = "+22% Hand Crank & Wind Turbine Power (PU) per level.",
            .maxLevel = 5,
            .baseCost = makeCost(18, 12),
            .costGrowth = 1.55,
            .treeX = 1.60f, .treeY = 1.0f,
            .iconType = BuildingType::WindTurbine
        },
        UpgradeDef{
            .id = UpgradeId::UnlockSteamEngine,
            .key = "unlock_steam_engine",
            .name = "Steam & Hydro Pumps",
            .branch = "Power & Atomics",
            .description = "Unlocks Coal Steam Engines (160 PU) & Hydro Coolant Pumps (+35% wet boost!).",
            .maxLevel = 1,
            .baseCost = makeCost(30, 20, 0, 0, 0, 0, 8),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::SpringTension,
            .treeX = 0.95f, .treeY = 2.0f,
            .iconType = BuildingType::SteamEngine
        },
        UpgradeDef{
            .id = UpgradeId::BoilerPressure,
            .key = "boiler_pressure",
            .name = "Superheated Boilers",
            .branch = "Power & Atomics",
            .description = "+35% Steam Engine Power (PU) & +25% Coal burn duration per level.",
            .maxLevel = 4,
            .baseCost = makeCost(35, 22, 16, 0, 0, 0, 14),
            .costGrowth = 1.70,
            .prerequisite = UpgradeId::UnlockSteamEngine,
            .treeX = 0.25f, .treeY = 3.0f,
            .iconType = BuildingType::SteamEngine
        },
        UpgradeDef{
            .id = UpgradeId::UnlockElectricalGrid,
            .key = "unlock_electrical_grid",
            .name = "Electrical Grid",
            .branch = "Power & Atomics",
            .description = "Unlocks Copper Power Poles, Alternators (cogs->kW) & Electric Motors (kW->cogs).",
            .maxLevel = 1,
            .baseCost = makeCost(30, 40, 0, 0, 0, 0, 28, 12),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockSteamEngine,
            .prerequisite2 = UpgradeId::UnlockAssembler,
            .treeX = 1.50f, .treeY = 3.0f,
            .iconType = BuildingType::PowerPole
        },
        UpgradeDef{
            .id = UpgradeId::UnlockSolarBattery,
            .key = "unlock_solar_battery",
            .name = "Solar & Accumulators",
            .branch = "Power & Atomics",
            .description = "Unlocks fuel-free Solar Arrays (60 kW day) & 1800 kJ Grid Accumulator Batteries.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 35, 0, 20, 0, 0, 45, 28),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockElectricalGrid,
            .treeX = 0.65f, .treeY = 4.0f,
            .iconType = BuildingType::Accumulator
        },
        UpgradeDef{
            .id = UpgradeId::UnlockSubstations,
            .key = "unlock_substations",
            .name = "HV Substations",
            .branch = "Power & Atomics",
            .description = "Unlocks 16.5-tile HV Substations & +25% Accumulator energy capacity.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 40, 0, 35, 0, 0, 65, 45),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockElectricalGrid,
            .treeX = 2.15f, .treeY = 4.0f,
            .iconType = BuildingType::Substation
        },
        UpgradeDef{
            .id = UpgradeId::UnlockNuclearPower,
            .key = "unlock_nuclear_power",
            .name = "Nuclear Fission",
            .branch = "Power & Atomics",
            .description = "Unlocks 3x3 Nuclear Reactors (600 kW Uranium) & +300 kW Cooling Towers.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 55, 12, 0, 110, 75),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockSubstations,
            .prerequisite2 = UpgradeId::UnlockElectricDrill,
            .treeX = 2.20f, .treeY = 5.0f,
            .iconType = BuildingType::NuclearReactor
        },
        UpgradeDef{
            .id = UpgradeId::UnlockFusionPower,
            .key = "unlock_fusion_power",
            .name = "Tokamak Fusion & Breeders",
            .branch = "Megastructures",
            .description = "Lv1 unlocks 3x3 Fusion Core (2400 kW Aether); each lvl adds +30% Nuclear/Fusion kW!",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 90, 25, 20, 160, 130, 8),
            .costGrowth = 1.75,
            .prerequisite = UpgradeId::UnlockNuclearPower,
            .prerequisite2 = UpgradeId::UnlockQuantumForge,
            .treeX = 2.20f, .treeY = 6.0f,
            .iconType = BuildingType::FusionReactor
        },

        // ==================== BOUGH 1: MINING, TITANIUM & QUANTUM ====================
        UpgradeDef{
            .id = UpgradeId::DrillSpeed,
            .key = "drill_speed",
            .name = "High-Speed Bits",
            .branch = "Mining & Matter",
            .description = "+25% mining speed for all mechanical & electric drills per level.",
            .maxLevel = 6,
            .baseCost = makeCost(16, 10),
            .costGrowth = 1.50,
            .treeX = -4.80f, .treeY = 1.0f,
            .iconType = BuildingType::IronDrill
        },
        UpgradeDef{
            .id = UpgradeId::OreYield,
            .key = "ore_yield",
            .name = "Rich Seam Extraction",
            .branch = "Mining & Matter",
            .description = "+20% ore output per completed mining cycle per level.",
            .maxLevel = 6,
            .baseCost = makeCost(20, 12, 0, 0, 0, 0, 4),
            .costGrowth = 1.58,
            .prerequisite = UpgradeId::DrillSpeed,
            .treeX = -5.55f, .treeY = 2.0f,
            .iconType = BuildingType::IronDrill
        },
        UpgradeDef{
            .id = UpgradeId::UnlockHeavyDrill,
            .key = "unlock_heavy_drill",
            .name = "Titanium & Heavy Bore",
            .branch = "Mining & Matter",
            .description = "Unlocks Tier 2 Heavy Bore Drills able to mine Titanium & Uranium deposits!",
            .maxLevel = 1,
            .baseCost = makeCost(40, 26, 0, 0, 0, 0, 18),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::DrillSpeed,
            .treeX = -4.15f, .treeY = 2.0f,
            .iconType = BuildingType::HeavyDrill
        },
        UpgradeDef{
            .id = UpgradeId::SmeltingMastery,
            .key = "smelting_mastery",
            .name = "Metallurgical Mastery",
            .branch = "Mining & Matter",
            .description = "+22% Smelter, Furnace, Assembler & Forge crafting speed per level.",
            .maxLevel = 5,
            .baseCost = makeCost(0, 24, 0, 14, 0, 0, 32, 14),
            .costGrowth = 1.65,
            .prerequisite = UpgradeId::UnlockHeavyDrill,
            .treeX = -5.65f, .treeY = 3.0f,
            .iconType = BuildingType::StoneSmelter
        },
        UpgradeDef{
            .id = UpgradeId::UnlockElectricDrill,
            .key = "unlock_electric_drill",
            .name = "Electric Drills",
            .branch = "Mining & Matter",
            .description = "Unlocks grid-powered Tier 2 Electric Drills (-45 kW) requiring zero cogwheels.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 30, 0, 28, 0, 0, 55, 35),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockHeavyDrill,
            .prerequisite2 = UpgradeId::UnlockSteamEngine,
            .treeX = -4.20f, .treeY = 3.0f,
            .iconType = BuildingType::ElectricDrill
        },
        UpgradeDef{
            .id = UpgradeId::UnlockAetherTier,
            .key = "unlock_aether_tier",
            .name = "Aether Crystal Tech",
            .branch = "Mining & Matter",
            .description = "Unlocks Tier 3 Aether Laser Extractors (mines Aether!) & Aether Dynamos.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 55, 16, 0, 95, 65),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockElectricDrill,
            .treeX = -4.75f, .treeY = 4.0f,
            .iconType = BuildingType::AetherExtractor
        },
        UpgradeDef{
            .id = UpgradeId::UnlockQuantumForge,
            .key = "unlock_quantum_forge",
            .name = "Quantum Fabrication",
            .branch = "Mining & Matter",
            .description = "Unlocks Quantum Forges crafting Quantum Cores (St + Ci + Ti + Ae -> Qc).",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 75, 20, 15, 130, 100),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockAetherTier,
            .treeX = -3.95f, .treeY = 5.0f,
            .iconType = BuildingType::QuantumForge
        },
        UpgradeDef{
            .id = UpgradeId::CoreFracking,
            .key = "core_fracking",
            .name = "Deep Core Fracking",
            .branch = "Mining & Matter",
            .description = "+35% rich deposit yield & +25% mining/crafting productivity per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 80, 30, 25, 150, 120, 10),
            .costGrowth = 1.80,
            .prerequisite = UpgradeId::UnlockAetherTier,
            .treeX = -5.55f, .treeY = 5.0f,
            .iconType = BuildingType::AetherDynamo
        },

        // ==================== BOUGH 4: BELTS, LOGISTICS & RAILWAYS ====================
        UpgradeDef{
            .id = UpgradeId::UnlockSplittersTunnels,
            .key = "unlock_splitters_tunnels",
            .name = "Splitters & Tunnels",
            .branch = "Belts & Rails",
            .description = "Unlocks 4-way Belt Splitters, 5-tile Underground Tunnels & Buffer Vaults.",
            .maxLevel = 1,
            .baseCost = makeCost(18, 12),
            .costGrowth = 1.0,
            .treeX = 4.80f, .treeY = 1.0f,
            .iconType = BuildingType::BeltSplitter
        },
        UpgradeDef{
            .id = UpgradeId::UnlockFastBelts,
            .key = "unlock_fast_belts",
            .name = "Titanium Fast Belts",
            .branch = "Belts & Rails",
            .description = "Unlocks 6.0/s Titanium Belts Mk2 & +50% Buffer Vault capacity.",
            .maxLevel = 1,
            .baseCost = makeCost(24, 16, 0, 10, 0, 0, 20),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockSplittersTunnels,
            .treeX = 4.10f, .treeY = 2.0f,
            .iconType = BuildingType::FastBelt
        },
        UpgradeDef{
            .id = UpgradeId::HubLogistics,
            .key = "hub_logistics",
            .name = "Core Drone Logistics",
            .branch = "Belts & Rails",
            .description = "+5 tiles Core drone trickle radius & +50 drill/station storage per level.",
            .maxLevel = 5,
            .baseCost = makeCost(28, 18, 0, 0, 0, 0, 10),
            .costGrowth = 1.60,
            .prerequisite = UpgradeId::UnlockSplittersTunnels,
            .treeX = 5.55f, .treeY = 2.0f,
            .iconType = BuildingType::StorageVault
        },
        UpgradeDef{
            .id = UpgradeId::UnlockRailways,
            .key = "unlock_railways",
            .name = "Railways & Steam Freight",
            .branch = "Belts & Rails",
            .description = "Unlocks Steel Rails, Train Stations & Steam Locomotives for remote outposts.",
            .maxLevel = 1,
            .baseCost = makeCost(30, 20, 20, 0, 0, 0, 35, 10),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::HubLogistics,
            .prerequisite2 = UpgradeId::UnlockSteamEngine,
            .treeX = 5.65f, .treeY = 3.0f,
            .iconType = BuildingType::SteamTrain
        },
        UpgradeDef{
            .id = UpgradeId::HeavyFreight,
            .key = "heavy_freight",
            .name = "Heavy Freight Cars",
            .branch = "Belts & Rails",
            .description = "+55 train cargo capacity & +25% station storage per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 25, 0, 0, 60, 35),
            .costGrowth = 1.68,
            .prerequisite = UpgradeId::UnlockRailways,
            .treeX = 5.15f, .treeY = 4.0f,
            .iconType = BuildingType::TrainStation
        },
        UpgradeDef{
            .id = UpgradeId::UnlockExpressBelts,
            .key = "unlock_express_belts",
            .name = "Quantum Express Belts",
            .branch = "Belts & Rails",
            .description = "Unlocks blazing 12.0 items/s Mag-Belts Mk3 & faster Underground Tunnels.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 50, 0, 10, 90, 70),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::BeltCompression,
            .treeX = 3.85f, .treeY = 4.0f,
            .iconType = BuildingType::ExpressBelt
        },
        UpgradeDef{
            .id = UpgradeId::UnlockElectricTrains,
            .key = "unlock_electric_trains",
            .name = "Electric Express Trains",
            .branch = "Belts & Rails",
            .description = "Unlocks high-speed Coal-free Electric Locomotives charged by grid stations.",
            .maxLevel = 1,
            .baseCost = makeCost(0, 0, 0, 55, 14, 0, 110, 85),
            .costGrowth = 1.0,
            .prerequisite = UpgradeId::UnlockRailways,
            .prerequisite2 = UpgradeId::UnlockElectricalGrid,
            .treeX = 6.45f, .treeY = 4.0f,
            .iconType = BuildingType::ElectricTrain
        },
        UpgradeDef{
            .id = UpgradeId::FreightBonus,
            .key = "freight_bonus",
            .name = "Bulk Freight & Sink Boost",
            .branch = "Belts & Rails",
            .description = "+20% bonus cargo on Train Hub unload & +25% Sink Coupon points per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 60, 20, 15, 120, 95, 6),
            .costGrowth = 1.72,
            .prerequisite = UpgradeId::UnlockElectricTrains,
            .treeX = 5.75f, .treeY = 5.0f,
            .iconType = BuildingType::MatterSink
        },

        // ==================== SUB-BOUGHS & PLANETARY MEGASTRUCTURES ====================
        UpgradeDef{
            .id = UpgradeId::SolarEfficiency,
            .key = "solar_efficiency",
            .name = "Photovoltaic & Wind Tuning",
            .branch = "Power & Atomics",
            .description = "+20% Wind Turbine PU & +25% Solar Array kW output per level.",
            .maxLevel = 5,
            .baseCost = makeCost(20, 14),
            .costGrowth = 1.55,
            .prerequisite = UpgradeId::SpringTension,
            .treeX = 2.35f, .treeY = 2.0f,
            .iconType = BuildingType::SolarPanel
        },
        UpgradeDef{
            .id = UpgradeId::HydroThermalMastery,
            .key = "hydro_thermal_mastery",
            .name = "Hydro-Thermal Loops",
            .branch = "Power & Atomics",
            .description = "+20% Hydro Pump boost & +25% Cooling Tower thermal kW output per level.",
            .maxLevel = 4,
            .baseCost = makeCost(26, 16, 0, 0, 0, 0, 10),
            .costGrowth = 1.62,
            .prerequisite = UpgradeId::SolarEfficiency,
            .treeX = 2.75f, .treeY = 3.0f,
            .iconType = BuildingType::WaterPump
        },
        UpgradeDef{
            .id = UpgradeId::BeltCompression,
            .key = "belt_compression",
            .name = "Mag-Drive Belt Rollers",
            .branch = "Belts & Rails",
            .description = "+18% Conveyor Belt, Splitter & Underground Tunnel throughput per level.",
            .maxLevel = 5,
            .baseCost = makeCost(28, 22, 0, 0, 0, 0, 24, 12),
            .costGrowth = 1.62,
            .prerequisite = UpgradeId::UnlockFastBelts,
            .treeX = 4.25f, .treeY = 3.0f,
            .iconType = BuildingType::UndergroundBelt
        },
        UpgradeDef{
            .id = UpgradeId::SuperconductingGrid,
            .key = "superconducting_grid",
            .name = "Superconducting Grid",
            .branch = "Power & Atomics",
            .description = "+20% Alternator kW, +20% Electric Motor PU & +30% Accumulator kJ per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 32, 0, 20, 0, 0, 48, 32),
            .costGrowth = 1.68,
            .prerequisite = UpgradeId::UnlockSolarBattery,
            .treeX = 0.55f, .treeY = 5.0f,
            .iconType = BuildingType::Alternator
        },
        UpgradeDef{
            .id = UpgradeId::SmartLoaders,
            .key = "smart_loaders",
            .name = "High-Speed Inserters",
            .branch = "Clockwork & Craft",
            .description = "+20% crafting speed & +15% bonus belted mining speed per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 45, 0, 8, 70, 50),
            .costGrowth = 1.70,
            .prerequisite = UpgradeId::UnlockAssembler,
            .treeX = -1.15f, .treeY = 3.0f,
            .iconType = BuildingType::Assembler
        },
        UpgradeDef{
            .id = UpgradeId::BeaconAmplification,
            .key = "beacon_amplification",
            .name = "Wide-Band Beacons",
            .branch = "Megastructures",
            .description = "Expands Overclock Beacons to 7x7 range and adds +20% extra beacon speed/lvl!",
            .maxLevel = 3,
            .baseCost = makeCost(0, 0, 0, 60, 0, 18, 100, 80, 4),
            .costGrowth = 1.75,
            .prerequisite = UpgradeId::UnlockOverclockers,
            .treeX = -1.55f, .treeY = 7.0f,
            .iconType = BuildingType::OverclockBeacon
        },
        UpgradeDef{
            .id = UpgradeId::MatterCompression,
            .key = "matter_compression",
            .name = "Quantum Matter Folding",
            .branch = "Megastructures",
            .description = "+35% Matter Sink Coupon points & +150 Buffer Vault storage per level.",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 75, 20, 22, 130, 105, 8),
            .costGrowth = 1.75,
            .prerequisite = UpgradeId::UnlockQuantumForge,
            .treeX = -3.95f, .treeY = 6.0f,
            .iconType = BuildingType::MatterSink
        },
        UpgradeDef{
            .id = UpgradeId::PlanetaryLogistics,
            .key = "planetary_logistics",
            .name = "Orbital Factory Network",
            .branch = "Megastructures",
            .description = "+25% global mining, crafting & power output across the entire planet per level!",
            .maxLevel = 4,
            .baseCost = makeCost(0, 0, 0, 95, 30, 35, 170, 145, 14),
            .costGrowth = 1.85,
            .prerequisite = UpgradeId::UnlockFusionPower,
            .prerequisite2 = UpgradeId::MatterCompression,
            .treeX = 1.15f, .treeY = 7.0f,
            .iconType = BuildingType::HubCore
        }
    }};
    return table;
}

const std::array<SpirePhaseDef, 5>& buildSpireTable() {
    static const std::array<SpirePhaseDef, 5> phases = {{
        SpirePhaseDef{
            .phaseIndex = 1,
            .title = "Phase I: Clockwork Metallurgy",
            .subtitle = "Establish automated Belt & Cog Smelter lines feeding Steel into the Grand Core.",
            .rewardText = "Permanent +15% Global Mining Speed & +2 Factory Coupons!",
            .requirement = makeCost(80, 50, 0, 0, 0, 0, 25)
        },
        SpirePhaseDef{
            .phaseIndex = 2,
            .title = "Phase II: High-Voltage & Titanium",
            .subtitle = "Expand to mid-frontier Titanium veins, electrify your workshop & automate Circuits.",
            .rewardText = "Permanent +20% Crafting Speed, +15% Grid Supply & +3 Coupons!",
            .requirement = makeCost(0, 0, 0, 60, 0, 0, 120, 65)
        },
        SpirePhaseDef{
            .phaseIndex = 3,
            .title = "Phase III: Atomic Rail Empire",
            .subtitle = "Haul Uranium & Titanium by Freight Train and commission Nuclear Fission power.",
            .rewardText = "Permanent +25% Belt/Train Speed, +25% Train Cargo & +4 Coupons!",
            .requirement = makeCost(0, 0, 0, 140, 45, 0, 220, 150)
        },
        SpirePhaseDef{
            .phaseIndex = 4,
            .title = "Phase IV: Quantum Singularity",
            .subtitle = "Harvest frontier Aether Crystals and forge Quantum Cores in Quantum Forges.",
            .rewardText = "Permanent +30% All Ore/Craft Yields & +6 Coupons!",
            .requirement = makeCost(0, 0, 0, 200, 80, 75, 300, 240, 25)
        },
        SpirePhaseDef{
            .phaseIndex = 5,
            .title = "Phase V: Grand Clockwork Ascension",
            .subtitle = "Power the Planetary Orbital Spire with Tokamak Fusion & mass-produced Quantum Cores!",
            .rewardText = "GRAND VICTORY + Repeatable Prestige Launches (+20% Global Output each)!",
            .requirement = makeCost(0, 0, 0, 320, 120, 140, 500, 420, 100)
        }
    }};
    return phases;
}

} // namespace

const BuildingDef& Catalog::getBuilding(BuildingType type) {
    return buildBuildingTable()[static_cast<std::size_t>(type)];
}

const UpgradeDef& Catalog::getUpgrade(UpgradeId id) {
    const auto& table = buildUpgradeTable();
    std::size_t idx = static_cast<std::size_t>(id);
    if (idx < table.size() && table[idx].id == id) {
        return table[idx];
    }
    for (const auto& def : table) {
        if (def.id == id) return def;
    }
    return table[0];
}

const SpirePhaseDef& Catalog::getSpirePhase(int phaseIndex) {
    const auto& phases = buildSpireTable();
    int clamped = std::clamp(phaseIndex, 0, static_cast<int>(phases.size()) - 1);
    return phases[static_cast<std::size_t>(clamped)];
}

const std::vector<BuildingType>& Catalog::hotbarOrder() {
    static const std::vector<BuildingType> order = {
        BuildingType::SmallCog, BuildingType::LargeGear, BuildingType::SpeedGearbox,
        BuildingType::AxleBridge, BuildingType::Lubricator, BuildingType::Flywheel,
        BuildingType::HandCrank, BuildingType::WindTurbine, BuildingType::SteamEngine,
        BuildingType::SolarPanel, BuildingType::Accumulator, BuildingType::AetherDynamo,
        BuildingType::IronDrill, BuildingType::HeavyDrill, BuildingType::ElectricDrill,
        BuildingType::AetherExtractor, BuildingType::WaterPump,
        BuildingType::StoneSmelter, BuildingType::ArcFurnace, BuildingType::Assembler,
        BuildingType::QuantumForge, BuildingType::OverclockBeacon, BuildingType::MatterSink,
        BuildingType::ConveyorBelt, BuildingType::FastBelt, BuildingType::ExpressBelt,
        BuildingType::BeltSplitter, BuildingType::UndergroundBelt, BuildingType::StorageVault,
        BuildingType::PowerPole, BuildingType::Substation, BuildingType::Alternator,
        BuildingType::ElectricMotor, BuildingType::NuclearReactor, BuildingType::CoolingTower,
        BuildingType::FusionReactor,
        BuildingType::RailTrack, BuildingType::TrainStation, BuildingType::SteamTrain,
        BuildingType::ElectricTrain
    };
    return order;
}

const std::vector<BuildingType>& Catalog::buildingsForCategory(BuildingCategory category) {
    static const std::vector<BuildingType> trans = {
        BuildingType::SmallCog,
        BuildingType::LargeGear,
        BuildingType::SpeedGearbox,
        BuildingType::AxleBridge,
        BuildingType::Lubricator,
        BuildingType::Flywheel
    };
    static const std::vector<BuildingType> gens = {
        BuildingType::HandCrank,
        BuildingType::WindTurbine,
        BuildingType::SteamEngine,
        BuildingType::SolarPanel,
        BuildingType::Accumulator,
        BuildingType::AetherDynamo
    };
    static const std::vector<BuildingType> drills = {
        BuildingType::IronDrill,
        BuildingType::HeavyDrill,
        BuildingType::ElectricDrill,
        BuildingType::AetherExtractor,
        BuildingType::WaterPump
    };
    static const std::vector<BuildingType> factory = {
        BuildingType::StoneSmelter,
        BuildingType::ArcFurnace,
        BuildingType::Assembler,
        BuildingType::QuantumForge,
        BuildingType::OverclockBeacon,
        BuildingType::MatterSink
    };
    static const std::vector<BuildingType> belts = {
        BuildingType::ConveyorBelt,
        BuildingType::FastBelt,
        BuildingType::ExpressBelt,
        BuildingType::BeltSplitter,
        BuildingType::UndergroundBelt,
        BuildingType::StorageVault
    };
    static const std::vector<BuildingType> elec = {
        BuildingType::PowerPole,
        BuildingType::Substation,
        BuildingType::Alternator,
        BuildingType::ElectricMotor,
        BuildingType::NuclearReactor,
        BuildingType::CoolingTower,
        BuildingType::FusionReactor
    };
    static const std::vector<BuildingType> rail = {
        BuildingType::RailTrack,
        BuildingType::TrainStation,
        BuildingType::SteamTrain,
        BuildingType::ElectricTrain
    };
    switch (category) {
    case BuildingCategory::Transmission: return trans;
    case BuildingCategory::Generator:    return gens;
    case BuildingCategory::Drill:        return drills;
    case BuildingCategory::Factory:      return factory;
    case BuildingCategory::Belt:         return belts;
    case BuildingCategory::Electric:     return elec;
    case BuildingCategory::Rail:         return rail;
    default:                             return trans;
    }
}

const std::vector<UpgradeId>& Catalog::upgradeOrder() {
    static const std::vector<UpgradeId> order = []() {
        std::vector<UpgradeId> v;
        v.reserve(static_cast<std::size_t>(UpgradeId::Count));
        for (int i = 0; i < static_cast<int>(UpgradeId::Count); ++i) {
            v.push_back(static_cast<UpgradeId>(i));
        }
        return v;
    }();
    return order;
}

ResourceBag Catalog::upgradeCost(UpgradeId id, int currentLevel) {
    const UpgradeDef& def = getUpgrade(id);
    ResourceBag cost = def.baseCost;
    const double mult = std::pow(def.costGrowth, currentLevel);
    for (double& v : cost.values) {
        if (v > 0.0) {
            v = std::round(v * mult);
        }
    }
    return cost;
}

bool Catalog::canMeetPrerequisites(
    UpgradeId id,
    const std::array<int, static_cast<std::size_t>(UpgradeId::Count)>& levels
) {
    const UpgradeDef& def = getUpgrade(id);
    if (def.prerequisite != UpgradeId::Count &&
        levels[static_cast<std::size_t>(def.prerequisite)] <= 0) {
        return false;
    }
    if (def.prerequisite2 != UpgradeId::Count &&
        levels[static_cast<std::size_t>(def.prerequisite2)] <= 0) {
        return false;
    }
    return true;
}

bool Catalog::isBuildingUnlocked(
    BuildingType type,
    const std::array<int, static_cast<std::size_t>(UpgradeId::Count)>& levels
) {
    auto lv = [&](UpgradeId id) {
        return levels[static_cast<std::size_t>(id)];
    };
    switch (type) {
    case BuildingType::HubCore:         return false; // unique central monument
    case BuildingType::LargeGear:       return lv(UpgradeId::UnlockLargeGear) > 0;
    case BuildingType::SpeedGearbox:
    case BuildingType::AxleBridge:      return lv(UpgradeId::UnlockGearboxBridge) > 0;
    case BuildingType::Assembler:
    case BuildingType::MatterSink:      return lv(UpgradeId::UnlockAssembler) > 0;
    case BuildingType::Lubricator:
    case BuildingType::Flywheel:        return lv(UpgradeId::UnlockUtilityMachines) > 0;
    case BuildingType::ArcFurnace:      return lv(UpgradeId::UnlockArcFurnace) > 0;
    case BuildingType::OverclockBeacon: return lv(UpgradeId::UnlockOverclockers) > 0;

    case BuildingType::SteamEngine:
    case BuildingType::WaterPump:       return lv(UpgradeId::UnlockSteamEngine) > 0;
    case BuildingType::Alternator:
    case BuildingType::ElectricMotor:
    case BuildingType::PowerPole:       return lv(UpgradeId::UnlockElectricalGrid) > 0;
    case BuildingType::SolarPanel:
    case BuildingType::Accumulator:     return lv(UpgradeId::UnlockSolarBattery) > 0;
    case BuildingType::Substation:      return lv(UpgradeId::UnlockSubstations) > 0;
    case BuildingType::NuclearReactor:
    case BuildingType::CoolingTower:    return lv(UpgradeId::UnlockNuclearPower) > 0;
    case BuildingType::FusionReactor:   return lv(UpgradeId::UnlockFusionPower) > 0;

    case BuildingType::HeavyDrill:      return lv(UpgradeId::UnlockHeavyDrill) > 0;
    case BuildingType::ElectricDrill:   return lv(UpgradeId::UnlockElectricDrill) > 0;
    case BuildingType::AetherDynamo:
    case BuildingType::AetherExtractor: return lv(UpgradeId::UnlockAetherTier) > 0;
    case BuildingType::QuantumForge:    return lv(UpgradeId::UnlockQuantumForge) > 0;

    case BuildingType::BeltSplitter:
    case BuildingType::UndergroundBelt:
    case BuildingType::StorageVault:    return lv(UpgradeId::UnlockSplittersTunnels) > 0;
    case BuildingType::FastBelt:        return lv(UpgradeId::UnlockFastBelts) > 0;
    case BuildingType::ExpressBelt:     return lv(UpgradeId::UnlockExpressBelts) > 0;
    case BuildingType::RailTrack:
    case BuildingType::TrainStation:
    case BuildingType::SteamTrain:      return lv(UpgradeId::UnlockRailways) > 0;
    case BuildingType::ElectricTrain:   return lv(UpgradeId::UnlockElectricTrains) > 0;
    default:                            return true;
    }
}

std::string Catalog::buildingUnlockHint(BuildingType type) {
    switch (type) {
    case BuildingType::LargeGear:       return "Research in Tech Tree [U]: Large Brass Gear";
    case BuildingType::SpeedGearbox:
    case BuildingType::AxleBridge:      return "Research in Tech Tree [U]: Gearbox & Bridge";
    case BuildingType::Assembler:
    case BuildingType::MatterSink:      return "Research in Tech Tree [U]: Circuit Assembly";
    case BuildingType::Lubricator:
    case BuildingType::Flywheel:        return "Research in Tech Tree [U]: Lubricator & Flywheel";
    case BuildingType::ArcFurnace:      return "Research in Tech Tree [U]: Arc Plasma Smelting";
    case BuildingType::OverclockBeacon: return "Research in Tech Tree [U]: Overclock Beacons";
    case BuildingType::SteamEngine:
    case BuildingType::WaterPump:       return "Research in Tech Tree [U]: Steam & Hydro Pumps";
    case BuildingType::Alternator:
    case BuildingType::ElectricMotor:
    case BuildingType::PowerPole:       return "Research in Tech Tree [U]: Electrical Grid";
    case BuildingType::SolarPanel:
    case BuildingType::Accumulator:     return "Research in Tech Tree [U]: Solar & Accumulators";
    case BuildingType::Substation:      return "Research in Tech Tree [U]: HV Substations";
    case BuildingType::NuclearReactor:
    case BuildingType::CoolingTower:    return "Research in Tech Tree [U]: Nuclear Fission";
    case BuildingType::FusionReactor:   return "Research in Tech Tree [U]: Tokamak Fusion & Breeders";
    case BuildingType::HeavyDrill:      return "Research in Tech Tree [U]: Titanium & Heavy Bore";
    case BuildingType::ElectricDrill:   return "Research in Tech Tree [U]: Electric Drills";
    case BuildingType::AetherDynamo:
    case BuildingType::AetherExtractor: return "Research in Tech Tree [U]: Aether Crystal Tech";
    case BuildingType::QuantumForge:    return "Research in Tech Tree [U]: Quantum Fabrication";
    case BuildingType::BeltSplitter:
    case BuildingType::UndergroundBelt:
    case BuildingType::StorageVault:    return "Research in Tech Tree [U]: Splitters & Tunnels";
    case BuildingType::FastBelt:        return "Research in Tech Tree [U]: Titanium Fast Belts";
    case BuildingType::ExpressBelt:     return "Research in Tech Tree [U]: Quantum Express Belts";
    case BuildingType::RailTrack:
    case BuildingType::TrainStation:
    case BuildingType::SteamTrain:      return "Research in Tech Tree [U]: Railways & Steam Freight";
    case BuildingType::ElectricTrain:   return "Research in Tech Tree [U]: Electric Express Trains";
    default:                            return "";
    }
}

} // namespace cogwill
