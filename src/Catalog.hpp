#pragma once

#include "Types.hpp"

#include <array>
#include <string>
#include <vector>

namespace cogwill {

class Catalog {
public:
    static const BuildingDef& getBuilding(BuildingType type);
    static const UpgradeDef& getUpgrade(UpgradeId id);
    static const SpirePhaseDef& getSpirePhase(int phaseIndex); // 0..4 for Phases 1..5
    static int spirePhaseCount() { return 5; }

    static const std::vector<BuildingType>& hotbarOrder();
    static const std::vector<BuildingType>& buildingsForCategory(BuildingCategory category);
    static const std::vector<UpgradeId>& upgradeOrder();

    static ResourceBag upgradeCost(UpgradeId id, int currentLevel);
    static bool canMeetPrerequisites(
        UpgradeId id,
        const std::array<int, static_cast<std::size_t>(UpgradeId::Count)>& levels
    );
    static bool isBuildingUnlocked(
        BuildingType type,
        const std::array<int, static_cast<std::size_t>(UpgradeId::Count)>& levels
    );
    static std::string buildingUnlockHint(BuildingType type);
};

} // namespace cogwill
