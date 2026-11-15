#pragma once

#include "Renderer.hpp"
#include "World.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Keyboard.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cogwill {

enum class UIActionType {
    None,
    Consumed,
    SelectBuilding,
    SelectCategory,
    ClearTool,
    ToggleUpgrades,
    OpenSpireTab,
    OpenStatsTab,
    OpenWorldTab,
    OpenSettingsTab,
    BuyUpgrade,
    CompleteSpirePhase,
    SpendCoupon,
    ToggleMode25D,
    ToggleSandbox,
    SandboxUnlockAllTechs,
    SandboxGrantResources,
    SandboxCompleteSpire,
    GenerateWorld,
    UpdateSettings,
    ResetSettings,
    ToggleSpeed,
    SaveGame,
    LoadGame,
    ToggleGoals,
    MinimapJump
};

struct UIAction {
    UIActionType type{UIActionType::None};
    BuildingType building{BuildingType::SmallCog};
    BuildingCategory category{BuildingCategory::Transmission};
    UpgradeId upgrade{UpgradeId::Count};
    CouponItem coupon{CouponItem::BeltAndCraftSpeed};
    sf::Vector2f worldPos{0.0f, 0.0f};
    WorldGenSettings worldGen{};
    bool newWorldSandbox{false};
    GameSettings settings{};
};

class UI {
public:
    UI();

    void setToast(const std::string& msg, bool isError = false);
    void update(float dt);

    void syncWorldGenFromWorld(const World& world);

    bool isUpgradesOpen() const { return upgradesOpen_; }
    void setUpgradesOpen(bool open) {
        upgradesOpen_ = open;
        if (!open) {
            seedInputFocused_ = false;
        }
    }
    void toggleUpgrades() {
        if (upgradesOpen_ && modalTab_ == 0) {
            setUpgradesOpen(false);
        } else {
            upgradesOpen_ = true;
            modalTab_ = 0;
            seedInputFocused_ = false;
        }
    }
    void openModalTab(int tab) {
        int clamped = std::clamp(tab, 0, 4);
        if (upgradesOpen_ && modalTab_ == clamped) {
            setUpgradesOpen(false);
        } else {
            upgradesOpen_ = true;
            modalTab_ = clamped;
            if (clamped != 3) {
                seedInputFocused_ = false;
            }
        }
    }
    int modalTab() const { return modalTab_; }

    bool isSeedInputFocused() const { return upgradesOpen_ && modalTab_ == 3 && seedInputFocused_; }
    void clearSeedFocus() { seedInputFocused_ = false; }
    void handleTextEntered(char32_t unicode);
    bool handleSeedKey(sf::Keyboard::Key key);

    void toggleGoals() { goalsCollapsed_ = !goalsCollapsed_; }
    void toggleMinimap() { minimapCollapsed_ = !minimapCollapsed_; }

    BuildingCategory activeCategory() const { return activeCategory_; }
    void setActiveCategory(BuildingCategory cat) { activeCategory_ = cat; }

    void panTechTree(sf::Vector2f screenDelta);
    void zoomTechTree(float wheelDelta, sf::Vector2f mousePos, sf::Vector2u winSize);
    void fitTechTree(sf::Vector2u winSize);
    bool isOverTechTreeCanvas(sf::Vector2f mousePos, sf::Vector2u winSize) const;

    bool isPointerOverUI(sf::Vector2f mousePos, sf::Vector2u winSize, const World& world) const;
    bool isOverMinimap(sf::Vector2f mousePos, sf::Vector2u winSize) const;
    sf::Vector2f minimapPosToWorld(sf::Vector2f mousePos, sf::Vector2u winSize) const;

    UIAction handleClick(
        sf::Vector2f mousePos,
        sf::Vector2u winSize,
        const World& world,
        const GameSettings& settings,
        std::optional<BuildingType> activeTool
    );

    void render(
        sf::RenderTarget& target,
        const sf::Font& font,
        const Renderer& renderer,
        const World& world,
        const GameSettings& settings,
        sf::Vector2f mousePos,
        sf::Vector2f hoverWorldPos,
        const sf::View& worldView,
        float animTime,
        float currentFps,
        std::optional<BuildingType> activeTool,
        int hoverGx,
        int hoverGy,
        const PlacementCheck& ghostCheck,
        int selectedGx,
        int selectedGy,
        float simSpeed,
        bool hasBlueprint = false
    ) const;

private:
    struct CategoryTab {
        BuildingCategory category;
        sf::FloatRect rect;
        std::string label;
    };

    struct HotbarSlot {
        BuildingType type;
        sf::FloatRect rect;
        std::string keyLabel;
    };

    struct ButtonRect {
        sf::FloatRect rect;
        UIActionType action;
        UpgradeId upgrade{UpgradeId::Count};
    };

    struct TechNodeRect {
        UpgradeId id;
        sf::FloatRect cardRect;
        sf::FloatRect buyRect;
    };

    std::vector<CategoryTab> computeCategoryTabs(sf::Vector2u winSize) const;
    std::vector<HotbarSlot> computeHotbarSlots(sf::Vector2u winSize) const;
    std::vector<ButtonRect> computeTopButtons(sf::Vector2u winSize) const;
    sf::FloatRect computeStatusBannerRect(sf::Vector2u winSize) const;
    sf::FloatRect computeMinimapBoxRect(sf::Vector2u winSize) const;
    sf::FloatRect computeMinimapImageRect(sf::Vector2u winSize) const;
    sf::FloatRect computeUpgradeModalRect(sf::Vector2u winSize) const;
    sf::FloatRect computeTechCanvasRect(sf::Vector2u winSize) const;
    void ensureTechViewInitialized(sf::Vector2u winSize) const;
    std::vector<TechNodeRect> computeTechNodes(sf::Vector2u winSize) const;
    sf::FloatRect computeDetailBuyRect(sf::Vector2u winSize) const;
    sf::FloatRect computeGoalsRect() const;

    void updateMinimapTexture(const World& world) const;
    void renderTechTreeTab(sf::RenderTarget& target, const sf::Font& font, const Renderer& renderer, const World& world, sf::Vector2f mousePos, sf::Vector2u winSize, float animTime, sf::FloatRect modal) const;
    void renderSpireAndSinkTab(sf::RenderTarget& target, const sf::Font& font, const World& world, sf::Vector2f mousePos, sf::FloatRect modal) const;
    void renderAnalyticsTab(sf::RenderTarget& target, const sf::Font& font, const World& world, sf::FloatRect modal) const;
    void renderWorldAndSandboxTab(sf::RenderTarget& target, const sf::Font& font, const World& world, const GameSettings& settings, sf::Vector2f mousePos, float animTime, sf::FloatRect modal) const;
    void renderSettingsTab(sf::RenderTarget& target, const sf::Font& font, const Renderer& renderer, const GameSettings& settings, sf::Vector2f mousePos, sf::FloatRect modal) const;

    bool upgradesOpen_{false};
    int modalTab_{0}; // 0=Tech Tree, 1=Spire & Sink, 2=Analytics, 3=World & Sandbox, 4=Settings & RT
    bool goalsCollapsed_{false};
    bool minimapCollapsed_{false};
    BuildingCategory activeCategory_{BuildingCategory::Transmission};
    UpgradeId selectedUpgrade_{UpgradeId::UnlockLargeGear};
    mutable sf::Vector2f techPan_{68.0f, 448.0f};
    mutable float techZoom_{0.60f};
    mutable bool techViewInitialized_{false};

    // Pending New-World Generator state (Tab 3)
    WorldGenSettings pendingWorldGen_{};
    bool pendingNewWorldSandbox_{false};
    bool seedInputFocused_{false};
    uint32_t seedRandomCounter_{1u};

    std::string toastText_;
    bool toastIsError_{false};
    float toastTimer_{0.0f};

    mutable sf::Texture minimapTex_;
    mutable float minimapTimer_{0.0f};
    mutable bool minimapInitialized_{false};
};

} // namespace cogwill
