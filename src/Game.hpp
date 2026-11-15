#pragma once

#include "Audio.hpp"
#include "Renderer.hpp"
#include "UI.hpp"
#include "World.hpp"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/View.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cogwill {

class Game {
public:
    Game();
    int run();
    int runSmokeRender(const std::string& outDir);

private:
    void initWindow();
    bool loadFont();
    void applySettings(bool allowWindowRecreate, bool persistToDisk);
    float maxWorldZoom(sf::Vector2u winSize) const;
    void syncWorldViewSize(sf::Vector2u winSize);
    sf::Vector2f screenToWorld(sf::Vector2f screenPos, sf::Vector2u winSize) const;
    void resetCamera();
    void updateHoverCoords();
    void handleEvents();
    void handleKeyPress(const sf::Event::KeyPressed& kp);
    void handleMousePress(const sf::Event::MouseButtonPressed& mp);
    void handleMouseRelease(const sf::Event::MouseButtonReleased& mr);
    void handleMouseWheel(const sf::Event::MouseWheelScrolled& mw);
    void executeUIAction(const UIAction& act);
    void saveFactory(bool silentToast = false);
    void loadFactory();
    void captureBlueprint(int x0, int y0, int x1, int y1);
    void stampBlueprintAt(int anchorGx, int anchorGy);
    void update(float dt);
    void render();

    sf::RenderWindow window_;
    sf::Font font_;
    sf::View worldView_;
    float zoom_{1.0f};
    bool currentFullscreen_{false};

    GameSettings settings_{};
    World world_;
    Renderer renderer_;
    UI ui_;
    AudioSystem audio_;

    std::optional<BuildingType> activeTool_;
    int ghostDir_{0};
    bool stationDirOverride_{false};
    int hoverGx_{-1};
    int hoverGy_{-1};
    sf::Vector2f hoverWorldPos_{0.f, 0.f};
    int selectedGx_{-1};
    int selectedGy_{-1};
    sf::Vector2f mouseScreenPos_{0.f, 0.f};
    bool hasMousePos_{false};

    // Blueprint Copy & Stamp state
    bool copyBoxArmed_{false};
    std::optional<std::pair<int, int>> copyBoxStart_;
    std::vector<BlueprintEntry> blueprint_;
    bool blueprintActive_{false};
    int bpRot_{0};

    // Drag & Camera state
    bool panning_{false};
    bool draggingTechTree_{false};
    bool draggingMinimap_{false};
    sf::Vector2i panLastMouse_{0, 0};
    bool dragPlacingTile_{false};
    int lastDragGx_{-1};
    int lastDragGy_{-1};
    int lastPoleGx_{-1};
    int lastPoleGy_{-1};
    bool holdingCrank_{false};
    float crankSfxCooldown_{0.0f};

    float simSpeed_{1.0f};
    float prevSimSpeed_{1.0f};
    float animTime_{0.0f};
    float smoothedFps_{60.0f};
    float autoSaveTimer_{0.0f};
    std::string savePath_{"savegame.json"};
    std::string settingsPath_{"settings.json"};
};

} // namespace cogwill
