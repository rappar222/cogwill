#include "Game.hpp"

#include "SaveLoad.hpp"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/ContextSettings.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/VideoMode.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

namespace cogwill {

namespace {

bool isDirectionalBelt(BuildingType t) {
    return t == BuildingType::ConveyorBelt ||
           t == BuildingType::FastBelt ||
           t == BuildingType::ExpressBelt;
}

bool isContinuousDragType(BuildingType t) {
    return t == BuildingType::SmallCog ||
           t == BuildingType::RailTrack;
}

bool isAutoSpacedPoleType(BuildingType t) {
    return t == BuildingType::PowerPole || t == BuildingType::Substation;
}

const char* rtToastName(int q) {
    switch (q) {
    case 0:  return "Off";
    case 1:  return "Medium (8-Ray)";
    case 2:  return "High (16-Ray)";
    case 3:  return "Ultra (24-Ray GI)";
    default: return "High";
    }
}

} // namespace

Game::Game() {
    worldView_.setSize({1440.0f, 900.0f / TILT_25D_Y});
    worldView_.setCenter({(HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY + 0.5f) * CELL_SIZE});
}

void Game::initWindow() {
    sf::ContextSettings ctx;
    ctx.antiAliasingLevel = 0;

    sf::VideoMode mode = settings_.fullscreen ? sf::VideoMode::getDesktopMode() : sf::VideoMode({1440u, 900u});
    sf::State state = settings_.fullscreen ? sf::State::Fullscreen : sf::State::Windowed;
    currentFullscreen_ = settings_.fullscreen;

    window_.create(
        mode,
        "Cogwill - 2.5D Ray-Traced Clockwork, Belts, Atomics & Spire Automation",
        sf::Style::Default,
        state,
        ctx
    );

    applySettings(false, false);
    resetCamera();
}

void Game::applySettings(bool allowWindowRecreate, bool persistToDisk) {
    if (allowWindowRecreate && window_.isOpen() && settings_.fullscreen != currentFullscreen_) {
        sf::Vector2f savedCenter = worldView_.getCenter();
        float savedZoom = zoom_;
        initWindow();
        zoom_ = savedZoom;
        syncWorldViewSize(window_.getSize());
        worldView_.setCenter(savedCenter);
    } else if (window_.isOpen()) {
        window_.setVerticalSyncEnabled(settings_.vsync);
        if (settings_.vsync) {
            window_.setFramerateLimit(0);
        } else {
            window_.setFramerateLimit(static_cast<unsigned int>(std::max(0, settings_.fpsLimit)));
        }
        syncWorldViewSize(window_.getSize());
    }

    audio_.setMasterVolume(settings_.masterVolume);
    audio_.setMuted(settings_.muted);
    world_.setLightingTimeMode(settings_.lightingTimeMode);
    world_.setParticleDensity(settings_.particleDensity);
    world_.setPopupsEnabled(settings_.showPopups);

    if (persistToDisk) {
        (void)SaveLoad::saveSettings(settings_, settingsPath_);
    }
}

bool Game::loadFont() {
    std::vector<std::string> candidates = {
        "assets/fonts/LiberationSans-Regular.ttf",
        "../assets/fonts/LiberationSans-Regular.ttf",
#ifdef COGWILL_ASSET_DIR
        std::string(COGWILL_ASSET_DIR) + "/fonts/LiberationSans-Regular.ttf",
#endif
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/ArimoNerdFontPropo-Regular.ttf"
    };

    for (const std::string& p : candidates) {
        if (std::filesystem::exists(p) && font_.openFromFile(p)) {
            font_.setSmooth(false);
            return true;
        }
    }
    return false;
}

float Game::maxWorldZoom(sf::Vector2u winSize) const {
    const float w = std::max(1.0f, static_cast<float>(winSize.x));
    const float h = std::max(1.0f, static_cast<float>(winSize.y));
    const float tiltY = settings_.mode25D ? TILT_25D_Y : 1.0f;
    const float fitX = (GRID_W * CELL_SIZE * 1.06f) / w;
    const float fitY = (GRID_H * CELL_SIZE * 1.15f * tiltY) / h;
    return std::clamp(std::max(fitX, fitY), 2.5f, 9.6f);
}

void Game::syncWorldViewSize(sf::Vector2u winSize) {
    if (winSize.x == 0 || winSize.y == 0) {
        return;
    }
    zoom_ = std::clamp(zoom_, 0.45f, maxWorldZoom(winSize));
    const float tiltY = settings_.mode25D ? TILT_25D_Y : 1.0f;
    worldView_.setSize({
        static_cast<float>(winSize.x) * zoom_,
        (static_cast<float>(winSize.y) * zoom_) / tiltY
    });
}

sf::Vector2f Game::screenToWorld(sf::Vector2f screenPos, sf::Vector2u winSize) const {
    if (winSize.x == 0 || winSize.y == 0) {
        return worldView_.getCenter();
    }
    float nx = screenPos.x / static_cast<float>(winSize.x);
    float ny = screenPos.y / static_cast<float>(winSize.y);
    sf::Vector2f vSize = worldView_.getSize();
    sf::Vector2f vTopLeft = worldView_.getCenter() - vSize * 0.5f;
    return {vTopLeft.x + nx * vSize.x, vTopLeft.y + ny * vSize.y};
}

void Game::resetCamera() {
    zoom_ = 1.0f;
    syncWorldViewSize(window_.getSize());
    worldView_.setCenter({(HUB_GX + 0.5f) * CELL_SIZE, (HUB_GY + 0.5f) * CELL_SIZE});
}

void Game::updateHoverCoords() {
    sf::Vector2u winSize = window_.getSize();
    syncWorldViewSize(winSize);
    if (!hasMousePos_) {
        mouseScreenPos_ = sf::Vector2f(sf::Mouse::getPosition(window_));
    }
    hoverWorldPos_ = screenToWorld(mouseScreenPos_, winSize);
    hoverGx_ = static_cast<int>(std::floor(hoverWorldPos_.x / CELL_SIZE));
    hoverGy_ = static_cast<int>(std::floor(hoverWorldPos_.y / CELL_SIZE));
}

void Game::saveFactory(bool silentToast) {
    std::string err;
    if (SaveLoad::saveToFile(world_, savePath_, &err)) {
        if (!silentToast) {
            ui_.setToast("Factory saved to " + savePath_);
            audio_.play(SfxType::Upgrade, 30.0f, 1.25f);
        }
    } else if (!silentToast) {
        ui_.setToast("Save failed: " + err, true);
    }
}

void Game::loadFactory() {
    std::string err;
    if (SaveLoad::loadFromFile(world_, savePath_, &err)) {
        applySettings(false, false);
        ui_.syncWorldGenFromWorld(world_);
        ui_.setToast("Loaded saved factory (" + savePath_ + ")");
        audio_.play(SfxType::Upgrade, 32.0f, 1.15f);
    } else {
        ui_.setToast("Load failed: " + err, true);
        audio_.play(SfxType::Error, 32.0f);
    }
}

int Game::run() {
    if (std::filesystem::exists(settingsPath_)) {
        (void)SaveLoad::loadSettings(settings_, settingsPath_);
    }

    initWindow();
    if (!loadFont()) {
        std::cerr << "Failed to load UI font.\n";
        return 1;
    }

    if (std::filesystem::exists(savePath_)) {
        std::string err;
        if (SaveLoad::loadFromFile(world_, savePath_, &err)) {
            applySettings(false, false);
            ui_.setToast("Loaded saved factory (" + savePath_ + ")");
        }
    } else {
        ui_.setToast("Welcome! Press [N] for World Seeds & Sandbox, [O] for 2.5D & Ray-Tracing!");
    }
    ui_.syncWorldGenFromWorld(world_);

    sf::Clock clock;
    while (window_.isOpen()) {
        float rawDt = clock.restart().asSeconds();
        if (rawDt > 0.0005f) {
            float instFps = 1.0f / rawDt;
            smoothedFps_ = smoothedFps_ * 0.92f + instFps * 0.08f;
        }
        float dt = std::min(rawDt, 0.08f);

        handleEvents();
        update(dt);
        render();
    }

    return 0;
}

void Game::handleEvents() {
    while (const std::optional<sf::Event> event = window_.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window_.close();
            return;
        }
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            syncWorldViewSize(resized->size);
        } else if (const auto* te = event->getIf<sf::Event::TextEntered>()) {
            ui_.handleTextEntered(te->unicode);
        } else if (const auto* mm = event->getIf<sf::Event::MouseMoved>()) {
            mouseScreenPos_ = sf::Vector2f(mm->position);
            hasMousePos_ = true;
        } else if (const auto* kp = event->getIf<sf::Event::KeyPressed>()) {
            handleKeyPress(*kp);
        } else if (const auto* mp = event->getIf<sf::Event::MouseButtonPressed>()) {
            mouseScreenPos_ = sf::Vector2f(mp->position);
            hasMousePos_ = true;
            handleMousePress(*mp);
        } else if (const auto* mr = event->getIf<sf::Event::MouseButtonReleased>()) {
            mouseScreenPos_ = sf::Vector2f(mr->position);
            hasMousePos_ = true;
            handleMouseRelease(*mr);
        } else if (const auto* mw = event->getIf<sf::Event::MouseWheelScrolled>()) {
            mouseScreenPos_ = sf::Vector2f(mw->position);
            hasMousePos_ = true;
            handleMouseWheel(*mw);
        }
    }
}

void Game::handleKeyPress(const sf::Event::KeyPressed& kp) {
    if (ui_.handleSeedKey(kp.code)) {
        return;
    }

    if ((kp.control && kp.code == sf::Keyboard::Key::S) || kp.code == sf::Keyboard::Key::F5) {
        saveFactory(false);
        return;
    }

    if ((kp.control && kp.code == sf::Keyboard::Key::L) || kp.code == sf::Keyboard::Key::F8) {
        loadFactory();
        return;
    }

    if (kp.code == sf::Keyboard::Key::F9) {
        settings_.mode25D = !settings_.mode25D;
        applySettings(false, true);
        ui_.setToast(settings_.mode25D ? "2.5D Extruded Camera ENABLED" : "Classic Top-Down 2D Camera ENABLED");
        audio_.play(SfxType::Crank, 28.0f, 1.25f);
        return;
    }

    if (kp.code == sf::Keyboard::Key::F10) {
        settings_.rtQuality = (settings_.rtQuality + 1) % 4;
        if (settings_.rtQuality > 0) {
            settings_.shadersEnabled = true;
        }
        applySettings(false, true);
        ui_.setToast(std::string("Ray-Tracing Quality: ") + rtToastName(settings_.rtQuality));
        audio_.play(SfxType::Crank, 28.0f, 1.35f);
        return;
    }

    if (kp.code == sf::Keyboard::Key::F11) {
        settings_.fullscreen = !settings_.fullscreen;
        applySettings(true, true);
        ui_.setToast(settings_.fullscreen ? "Switched to Fullscreen" : "Switched to Windowed (1440x900)");
        return;
    }

    if (kp.control && kp.code == sf::Keyboard::Key::C) {
        copyBoxArmed_ = true;
        copyBoxStart_.reset();
        blueprintActive_ = false;
        activeTool_.reset();
        ui_.setToast("Blueprint Copy: Drag LMB over buildings to copy!");
        return;
    }

    if (kp.control && kp.code == sf::Keyboard::Key::V) {
        if (blueprint_.empty()) {
            ui_.setToast("Clipboard empty - press Ctrl+C and drag over buildings first!", true);
            audio_.play(SfxType::Error, 30.0f);
        } else {
            blueprintActive_ = !blueprintActive_;
            copyBoxArmed_ = false;
            copyBoxStart_.reset();
            if (blueprintActive_) {
                activeTool_.reset();
                ui_.setToast("Blueprint Stamp active ([R] Rotate, LMB Stamp, RMB Cancel)");
            }
        }
        return;
    }

    if (kp.code == sf::Keyboard::Key::Escape) {
        if (ui_.isUpgradesOpen()) {
            ui_.setUpgradesOpen(false);
        } else if (copyBoxArmed_ || copyBoxStart_.has_value() || blueprintActive_) {
            copyBoxArmed_ = false;
            copyBoxStart_.reset();
            blueprintActive_ = false;
        } else if (activeTool_.has_value()) {
            activeTool_.reset();
            stationDirOverride_ = false;
        } else {
            selectedGx_ = -1;
            selectedGy_ = -1;
        }
        return;
    }

    if (kp.code == sf::Keyboard::Key::U) {
        ui_.openModalTab(0);
        return;
    }
    if (kp.code == sf::Keyboard::Key::V) {
        ui_.openModalTab(1);
        return;
    }
    if (kp.code == sf::Keyboard::Key::Tab) {
        ui_.openModalTab(2);
        return;
    }
    if (kp.code == sf::Keyboard::Key::N) {
        if (!ui_.isUpgradesOpen() || ui_.modalTab() != 3) {
            ui_.syncWorldGenFromWorld(world_);
        }
        ui_.openModalTab(3);
        return;
    }
    if (kp.code == sf::Keyboard::Key::O) {
        ui_.openModalTab(4);
        return;
    }
    if (kp.code == sf::Keyboard::Key::B) {
        world_.toggleSandboxMode();
        ui_.setToast(world_.isSandboxMode()
                         ? "Sandbox Mode ON: All buildings unlocked, zero cost & auto-fuel!"
                         : "Sandbox Mode OFF: Standard resource & research progression");
        audio_.play(SfxType::Upgrade, 36.0f, world_.isSandboxMode() ? 1.25f : 0.95f);
        return;
    }
    if (kp.code == sf::Keyboard::Key::P) {
        if (simSpeed_ > 0.05f) {
            prevSimSpeed_ = simSpeed_;
            simSpeed_ = 0.0f;
            ui_.setToast("Simulation PAUSED [P]");
        } else {
            simSpeed_ = (prevSimSpeed_ > 0.05f) ? prevSimSpeed_ : 1.0f;
            ui_.setToast("Simulation RESUMED (" + std::to_string(static_cast<int>(simSpeed_)) + "x)");
        }
        return;
    }
    if (kp.code == sf::Keyboard::Key::F) {
        if (world_.inBounds(hoverGx_, hoverGy_) && world_.manualFeedMachineAt(hoverGx_, hoverGy_)) {
            audio_.play(SfxType::Place, 32.0f, 1.25f);
        } else {
            ui_.setToast("Hover a Smelter, Assembler, Forge, or Generator to feed [F]", true);
        }
        return;
    }
    if (kp.code == sf::Keyboard::Key::G) {
        ui_.toggleGoals();
        return;
    }
    if (kp.code == sf::Keyboard::Key::M) {
        ui_.toggleMinimap();
        return;
    }
    if (kp.code == sf::Keyboard::Key::Home || (!kp.control && kp.code == sf::Keyboard::Key::C)) {
        resetCamera();
        return;
    }
    if (kp.code == sf::Keyboard::Key::Q) {
        if (world_.inBounds(hoverGx_, hoverGy_)) {
            if (const BuildingEntity* b = world_.buildingAt(hoverGx_, hoverGy_)) {
                if (b->type != BuildingType::HubCore) {
                    BuildingType t = b->type;
                    ui_.setActiveCategory(Catalog::getBuilding(t).category);
                    activeTool_ = t;
                    blueprintActive_ = false;
                    copyBoxArmed_ = false;
                    ghostDir_ = b->dir;
                    stationDirOverride_ = (t == BuildingType::TrainStation);
                    audio_.play(SfxType::Crank, 22.0f, 1.2f);
                }
            }
        }
        return;
    }
    if (kp.code == sf::Keyboard::Key::R) {
        if (blueprintActive_) {
            bpRot_ = (bpRot_ + 1) & 3;
            audio_.play(SfxType::Crank, 25.0f, 1.3f);
        } else if (activeTool_.has_value()) {
            if (*activeTool_ == BuildingType::TrainStation) {
                ghostDir_ = (ghostDir_ + 1) % 2;
                stationDirOverride_ = true;
            } else {
                ghostDir_ = (ghostDir_ + 1) % 4;
            }
            audio_.play(SfxType::Crank, 25.0f, 1.3f);
        } else if (world_.inBounds(hoverGx_, hoverGy_)) {
            if (world_.rotateBuildingAt(hoverGx_, hoverGy_)) {
                audio_.play(SfxType::Crank, 28.0f, 1.15f);
            }
        }
        return;
    }

    // Category tabs F1..F7
    if (kp.code == sf::Keyboard::Key::F1) { ui_.setActiveCategory(BuildingCategory::Transmission); return; }
    if (kp.code == sf::Keyboard::Key::F2) { ui_.setActiveCategory(BuildingCategory::Generator); return; }
    if (kp.code == sf::Keyboard::Key::F3) { ui_.setActiveCategory(BuildingCategory::Drill); return; }
    if (kp.code == sf::Keyboard::Key::F4) { ui_.setActiveCategory(BuildingCategory::Factory); return; }
    if (kp.code == sf::Keyboard::Key::F5) { ui_.setActiveCategory(BuildingCategory::Belt); return; }
    if (kp.code == sf::Keyboard::Key::F6) { ui_.setActiveCategory(BuildingCategory::Electric); return; }
    if (kp.code == sf::Keyboard::Key::F7) { ui_.setActiveCategory(BuildingCategory::Rail); return; }

    // Hotbar hotkeys 1..7 within active category
    const auto& catOrder = Catalog::buildingsForCategory(ui_.activeCategory());
    int slotIdx = -1;
    switch (kp.code) {
    case sf::Keyboard::Key::Num1: slotIdx = 0; break;
    case sf::Keyboard::Key::Num2: slotIdx = 1; break;
    case sf::Keyboard::Key::Num3: slotIdx = 2; break;
    case sf::Keyboard::Key::Num4: slotIdx = 3; break;
    case sf::Keyboard::Key::Num5: slotIdx = 4; break;
    case sf::Keyboard::Key::Num6: slotIdx = 5; break;
    case sf::Keyboard::Key::Num7: slotIdx = 6; break;
    default: break;
    }
    if (slotIdx >= 0 && slotIdx < static_cast<int>(catOrder.size())) {
        BuildingType t = catOrder[static_cast<std::size_t>(slotIdx)];
        blueprintActive_ = false;
        copyBoxArmed_ = false;
        if (activeTool_.has_value() && *activeTool_ == t) {
            activeTool_.reset();
            stationDirOverride_ = false;
        } else if (!world_.isBuildingUnlocked(t)) {
            ui_.setToast(Catalog::buildingUnlockHint(t), true);
            audio_.play(SfxType::Error, 32.0f);
        } else {
            activeTool_ = t;
            stationDirOverride_ = false;
        }
    }
}

void Game::captureBlueprint(int x0, int y0, int x1, int y1) {
    int minX = std::min(x0, x1);
    int maxX = std::max(x0, x1);
    int minY = std::min(y0, y1);
    int maxY = std::max(y0, y1);

    std::vector<const BuildingEntity*> picked;
    int sumX = 0, sumY = 0;
    for (const BuildingEntity& b : world_.buildings()) {
        if (b.type == BuildingType::HubCore) continue;
        const BuildingDef& def = Catalog::getBuilding(b.type);
        if (def.isTrainSpawner) continue;
        if (b.gx >= minX && b.gx <= maxX && b.gy >= minY && b.gy <= maxY) {
            picked.push_back(&b);
            sumX += b.gx;
            sumY += b.gy;
        }
    }

    if (picked.empty()) {
        ui_.setToast("No copyable buildings inside selection", true);
        return;
    }

    int cx = static_cast<int>(std::round(static_cast<float>(sumX) / static_cast<float>(picked.size())));
    int cy = static_cast<int>(std::round(static_cast<float>(sumY) / static_cast<float>(picked.size())));
    blueprint_.clear();
    for (const BuildingEntity* b : picked) {
        blueprint_.push_back(BlueprintEntry{b->type, b->gx - cx, b->gy - cy, b->dir});
    }
    bpRot_ = 0;
    blueprintActive_ = true;
    ui_.setToast("Copied " + std::to_string(blueprint_.size()) + " buildings! LMB to stamp, [R] rotate");
    audio_.play(SfxType::Upgrade, 32.0f, 1.25f);
}

void Game::stampBlueprintAt(int anchorGx, int anchorGy) {
    int placed = 0;
    for (const BlueprintEntry& be : blueprint_) {
        int rdx = be.dx;
        int rdy = be.dy;
        for (int r = 0; r < (bpRot_ & 3); ++r) {
            int tmp = rdx;
            rdx = -rdy;
            rdy = tmp;
        }
        int gx = anchorGx + rdx;
        int gy = anchorGy + rdy;
        int dir = (be.type == BuildingType::TrainStation) ? (be.dir % 2) : ((be.dir + bpRot_) & 3);
        if (world_.placeBuilding(be.type, gx, gy, dir)) {
            ++placed;
        }
    }
    if (placed > 0) {
        audio_.play(SfxType::Place, 44.0f, 1.05f);
        ui_.setToast("Stamped " + std::to_string(placed) + " blueprint buildings");
    } else {
        audio_.play(SfxType::Error, 32.0f);
        ui_.setToast("No valid/affordable tiles for blueprint here", true);
    }
}

void Game::handleMousePress(const sf::Event::MouseButtonPressed& mp) {
    sf::Vector2f mPos{static_cast<float>(mp.position.x), static_cast<float>(mp.position.y)};
    updateHoverCoords();

    if (mp.button == sf::Mouse::Button::Middle) {
        if (ui_.isOverTechTreeCanvas(mPos, window_.getSize())) {
            draggingTechTree_ = true;
            panLastMouse_ = mp.position;
        } else if (!ui_.isUpgradesOpen()) {
            panning_ = true;
            panLastMouse_ = mp.position;
        }
        return;
    }

    if (mp.button == sf::Mouse::Button::Right) {
        if (ui_.isUpgradesOpen()) {
            ui_.setUpgradesOpen(false);
            return;
        }
        if (copyBoxArmed_ || copyBoxStart_.has_value() || blueprintActive_) {
            copyBoxArmed_ = false;
            copyBoxStart_.reset();
            blueprintActive_ = false;
            return;
        }
        if (ui_.isPointerOverUI(mPos, window_.getSize(), world_)) {
            return;
        }
        ResourceBag refunded;
        if (world_.removeTrainNear(hoverWorldPos_.x, hoverWorldPos_.y, 32.0f, &refunded)) {
            audio_.play(SfxType::Remove, 40.0f);
            ui_.setToast("Recalled locomotive (100% refunded)");
            return;
        }
        if (world_.inBounds(hoverGx_, hoverGy_) && world_.buildingAt(hoverGx_, hoverGy_) != nullptr) {
            if (world_.removeBuildingAt(hoverGx_, hoverGy_, &refunded)) {
                audio_.play(SfxType::Remove, 40.0f);
                if (hoverGx_ == selectedGx_ && hoverGy_ == selectedGy_) {
                    selectedGx_ = -1;
                    selectedGy_ = -1;
                }
            } else {
                ui_.setToast("Grand Clockwork Core cannot be demolished!", true);
            }
        } else if (activeTool_.has_value()) {
            activeTool_.reset();
            stationDirOverride_ = false;
        }
        return;
    }

    if (mp.button == sf::Mouse::Button::Left) {
        const bool overTechCanvas = ui_.isOverTechTreeCanvas(mPos, window_.getSize());
        UIAction act = ui_.handleClick(mPos, window_.getSize(), world_, settings_, activeTool_);
        if (act.type != UIActionType::None) {
            if (overTechCanvas && act.type == UIActionType::Consumed) {
                draggingTechTree_ = true;
                panLastMouse_ = mp.position;
            }
            executeUIAction(act);
            return;
        }

        if (!world_.inBounds(hoverGx_, hoverGy_)) {
            return;
        }

        if (copyBoxArmed_) {
            copyBoxStart_ = {hoverGx_, hoverGy_};
            return;
        }

        if (blueprintActive_ && !blueprint_.empty()) {
            stampBlueprintAt(hoverGx_, hoverGy_);
            return;
        }

        if (activeTool_.has_value()) {
            int placeDir = (*activeTool_ == BuildingType::TrainStation && !stationDirOverride_) ? -1 : ghostDir_;
            std::string reason;
            if (world_.placeBuilding(*activeTool_, hoverGx_, hoverGy_, placeDir, &reason)) {
                audio_.play(SfxType::Place, 42.0f);
                selectedGx_ = hoverGx_;
                selectedGy_ = hoverGy_;
                if (isDirectionalBelt(*activeTool_) || isContinuousDragType(*activeTool_) || isAutoSpacedPoleType(*activeTool_)) {
                    dragPlacingTile_ = true;
                    lastDragGx_ = hoverGx_;
                    lastDragGy_ = hoverGy_;
                    lastPoleGx_ = hoverGx_;
                    lastPoleGy_ = hoverGy_;
                }
            } else {
                ui_.setToast(reason, true);
                audio_.play(SfxType::Error, 35.0f);
            }
        } else {
            const BuildingEntity* b = world_.buildingAt(hoverGx_, hoverGy_);
            if (b) {
                selectedGx_ = b->gx;
                selectedGy_ = b->gy;
                if (b->type == BuildingType::HubCore) {
                    ui_.openModalTab(1);
                } else if (b->type == BuildingType::HandCrank) {
                    world_.crankAt(b->gx, b->gy, 1.8f);
                    holdingCrank_ = true;
                    audio_.play(SfxType::Crank, 34.0f, 1.0f);
                    crankSfxCooldown_ = 0.25f;
                }
            } else {
                selectedGx_ = -1;
                selectedGy_ = -1;
            }
        }
    }
}

void Game::handleMouseRelease(const sf::Event::MouseButtonReleased& mr) {
    if (mr.button == sf::Mouse::Button::Middle) {
        panning_ = false;
        draggingTechTree_ = false;
    }
    if (mr.button == sf::Mouse::Button::Left) {
        if (copyBoxArmed_ && copyBoxStart_.has_value() && world_.inBounds(hoverGx_, hoverGy_)) {
            captureBlueprint(copyBoxStart_->first, copyBoxStart_->second, hoverGx_, hoverGy_);
            copyBoxArmed_ = false;
            copyBoxStart_.reset();
        }
        dragPlacingTile_ = false;
        draggingTechTree_ = false;
        draggingMinimap_ = false;
        holdingCrank_ = false;
        lastDragGx_ = -1;
        lastDragGy_ = -1;
    }
}

void Game::handleMouseWheel(const sf::Event::MouseWheelScrolled& mw) {
    if (mw.wheel != sf::Mouse::Wheel::Vertical) {
        return;
    }
    sf::Vector2u winSize = window_.getSize();
    sf::Vector2f mPos{static_cast<float>(mw.position.x), static_cast<float>(mw.position.y)};
    if (ui_.isUpgradesOpen()) {
        if (ui_.modalTab() == 0) {
            ui_.zoomTechTree(mw.delta, mPos, winSize);
        }
        return;
    }
    sf::Vector2f beforeWorld = screenToWorld(mPos, winSize);
    float factor = (mw.delta > 0.0f) ? 0.88f : 1.13f;
    zoom_ = std::clamp(zoom_ * factor, 0.45f, maxWorldZoom(winSize));
    syncWorldViewSize(winSize);
    sf::Vector2f afterWorld = screenToWorld(mPos, winSize);
    worldView_.move(beforeWorld - afterWorld);
}

void Game::executeUIAction(const UIAction& act) {
    switch (act.type) {
    case UIActionType::SelectBuilding:
        activeTool_ = act.building;
        blueprintActive_ = false;
        copyBoxArmed_ = false;
        stationDirOverride_ = false;
        break;
    case UIActionType::SelectCategory:
        ui_.setActiveCategory(act.category);
        break;
    case UIActionType::ClearTool:
        activeTool_.reset();
        blueprintActive_ = false;
        stationDirOverride_ = false;
        break;
    case UIActionType::ToggleUpgrades:
        ui_.toggleUpgrades();
        break;
    case UIActionType::OpenSpireTab:
        ui_.openModalTab(1);
        break;
    case UIActionType::OpenStatsTab:
        ui_.openModalTab(2);
        break;
    case UIActionType::OpenWorldTab:
        if (!ui_.isUpgradesOpen() || ui_.modalTab() != 3) {
            ui_.syncWorldGenFromWorld(world_);
        }
        ui_.openModalTab(3);
        break;
    case UIActionType::OpenSettingsTab:
        ui_.openModalTab(4);
        break;
    case UIActionType::ToggleGoals:
        ui_.toggleGoals();
        break;
    case UIActionType::ToggleMode25D:
        settings_.mode25D = !settings_.mode25D;
        applySettings(false, true);
        ui_.setToast(settings_.mode25D ? "2.5D Extruded Mode ON" : "Classic Top-Down 2D Mode ON");
        audio_.play(SfxType::Crank, 28.0f, 1.25f);
        break;
    case UIActionType::ToggleSandbox:
        world_.toggleSandboxMode();
        ui_.setToast(world_.isSandboxMode()
                         ? "Sandbox Mode ON: Zero-cost build, all 40 buildings & auto-fuel!"
                         : "Sandbox Mode OFF: Standard progression");
        audio_.play(SfxType::Upgrade, 36.0f, world_.isSandboxMode() ? 1.25f : 0.95f);
        break;
    case UIActionType::SandboxUnlockAllTechs:
        world_.unlockAllTechs();
        audio_.play(SfxType::Milestone, 50.0f);
        ui_.setToast("Unlocked all 40 technologies at maximum rank!");
        break;
    case UIActionType::SandboxGrantResources:
        world_.grantSandboxResources(10000.0);
        audio_.play(SfxType::Upgrade, 46.0f, 1.2f);
        ui_.setToast("Granted +10,000 of every resource & +20 Coupons!");
        break;
    case UIActionType::SandboxCompleteSpire: {
        bool prevSbx = world_.isSandboxMode();
        world_.setSandboxMode(true);
        (void)world_.completeSpirePhase();
        world_.setSandboxMode(prevSbx);
        audio_.play(SfxType::Milestone, 52.0f);
        ui_.setToast("Grand Spire Phase advanced!");
        break;
    }
    case UIActionType::GenerateWorld:
        world_.resetNewWorld(act.worldGen, act.newWorldSandbox);
        applySettings(false, false);
        ui_.syncWorldGenFromWorld(world_);
        activeTool_.reset();
        blueprintActive_ = false;
        stationDirOverride_ = false;
        selectedGx_ = -1;
        selectedGy_ = -1;
        resetCamera();
        ui_.setToast("Generated " + std::string(worldPresetName(act.worldGen.preset)) +
                     " (Seed: " + act.worldGen.seedText + ")");
        audio_.play(SfxType::Milestone, 44.0f, 1.05f);
        break;
    case UIActionType::UpdateSettings:
        settings_ = act.settings;
        applySettings(true, true);
        audio_.play(SfxType::Crank, 22.0f, 1.2f);
        break;
    case UIActionType::ResetSettings:
        settings_ = GameSettings{};
        applySettings(true, true);
        ui_.setToast("Reset all graphics, 2.5D RT, audio & gameplay settings to defaults");
        audio_.play(SfxType::Upgrade, 34.0f, 1.1f);
        break;
    case UIActionType::ToggleSpeed:
        if (simSpeed_ < 0.05f) simSpeed_ = 1.0f;
        else if (simSpeed_ < 1.5f) simSpeed_ = 2.0f;
        else if (simSpeed_ < 3.0f) simSpeed_ = 4.0f;
        else simSpeed_ = 0.0f;
        ui_.setToast(simSpeed_ < 0.05f
                         ? "Simulation PAUSED"
                         : ("Simulation speed: " + std::to_string(static_cast<int>(simSpeed_)) + "x"));
        break;
    case UIActionType::MinimapJump:
        worldView_.setCenter(act.worldPos);
        draggingMinimap_ = true;
        break;
    case UIActionType::SaveGame:
        saveFactory(false);
        break;
    case UIActionType::LoadGame:
        loadFactory();
        break;
    case UIActionType::BuyUpgrade:
        if (world_.buyUpgrade(act.upgrade)) {
            audio_.play(SfxType::Upgrade, 48.0f);
            ui_.setToast("Researched: " + Catalog::getUpgrade(act.upgrade).name);
        } else {
            audio_.play(SfxType::Error, 32.0f);
            ui_.setToast("Prerequisites or resources not met", true);
        }
        break;
    case UIActionType::CompleteSpirePhase:
        if (world_.completeSpirePhase()) {
            audio_.play(SfxType::Milestone, 55.0f);
            ui_.setToast("GRAND SPIRE PHASE LAUNCHED! Global factory output boosted!");
        } else {
            audio_.play(SfxType::Error, 32.0f);
            ui_.setToast("Insufficient resources in Core for Spire launch", true);
        }
        break;
    case UIActionType::SpendCoupon:
        if (world_.spendCoupon(act.coupon)) {
            audio_.play(SfxType::Upgrade, 46.0f, 1.15f);
            ui_.setToast("Redeemed Factory Coupon upgrade!");
        } else {
            audio_.play(SfxType::Error, 32.0f);
            ui_.setToast("Feed excess items into a Matter Sink to mint more Coupons!", true);
        }
        break;
    default:
        break;
    }
}

void Game::update(float dt) {
    animTime_ += dt;
    ui_.update(dt);
    updateHoverCoords();

    if (crankSfxCooldown_ > 0.0f) {
        crankSfxCooldown_ = std::max(0.0f, crankSfxCooldown_ - dt);
    }

    if (settings_.autoSaveSec > 0) {
        autoSaveTimer_ += dt;
        if (autoSaveTimer_ >= static_cast<float>(settings_.autoSaveSec)) {
            autoSaveTimer_ = 0.0f;
            saveFactory(true);
        }
    }

    if (draggingMinimap_ && ui_.isOverMinimap(mouseScreenPos_, window_.getSize())) {
        worldView_.setCenter(ui_.minimapPosToWorld(mouseScreenPos_, window_.getSize()));
    }

    if (draggingTechTree_) {
        if (!ui_.isUpgradesOpen() || ui_.modalTab() != 0) {
            draggingTechTree_ = false;
        } else {
            sf::Vector2i curMouse{static_cast<int>(mouseScreenPos_.x), static_cast<int>(mouseScreenPos_.y)};
            sf::Vector2i delta = panLastMouse_ - curMouse;
            panLastMouse_ = curMouse;
            if (delta.x != 0 || delta.y != 0) {
                ui_.panTechTree({static_cast<float>(delta.x), static_cast<float>(delta.y)});
            }
        }
    }

    if (panning_) {
        sf::Vector2i curMouse{static_cast<int>(mouseScreenPos_.x), static_cast<int>(mouseScreenPos_.y)};
        sf::Vector2i delta = panLastMouse_ - curMouse;
        panLastMouse_ = curMouse;
        const float tiltY = settings_.mode25D ? TILT_25D_Y : 1.0f;
        worldView_.move({static_cast<float>(delta.x) * zoom_, static_cast<float>(delta.y) * zoom_ / tiltY});
    }

    if (ui_.isUpgradesOpen() && ui_.modalTab() == 0 && !ui_.isSeedInputFocused()) {
        sf::Vector2f tPan{0.f, 0.f};
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) tPan.y -= 1.0f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) tPan.y += 1.0f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) tPan.x -= 1.0f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) tPan.x += 1.0f;
        if (tPan.x != 0.f || tPan.y != 0.f) {
            ui_.panTechTree(tPan * (520.0f * dt));
        }
    }

    if (!ui_.isUpgradesOpen()) {
        sf::Vector2f pan{0.f, 0.f};
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) pan.y -= 1.0f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) pan.y += 1.0f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) pan.x -= 1.0f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) pan.x += 1.0f;
        if (pan.x != 0.f || pan.y != 0.f) {
            const float tiltY = settings_.mode25D ? TILT_25D_Y : 1.0f;
            float spd = 620.0f * settings_.cameraSpeed * zoom_ * dt;
            worldView_.move({pan.x * spd, (pan.y * spd) / tiltY});
        }

        const float mapW = GRID_W * CELL_SIZE;
        const float mapH = GRID_H * CELL_SIZE;
        sf::Vector2f vSz = worldView_.getSize();
        sf::Vector2f c = worldView_.getCenter();
        if (vSz.x >= mapW * 0.96f) {
            c.x = std::clamp(c.x, mapW * 0.5f - 240.0f, mapW * 0.5f + 240.0f);
        } else {
            c.x = std::clamp(c.x, 0.0f, mapW);
        }
        if (vSz.y >= mapH * 0.96f) {
            c.y = std::clamp(c.y, mapH * 0.5f - 240.0f, mapH * 0.5f + 240.0f);
        } else {
            c.y = std::clamp(c.y, 0.0f, mapH);
        }
        worldView_.setCenter(c);
    }

    // Smart Drag-Placement: Auto-turning Belts, Continuous Cogs/Rails, & Auto-spaced Power Poles
    if (dragPlacingTile_ && activeTool_.has_value() &&
        world_.inBounds(hoverGx_, hoverGy_) &&
        !ui_.isPointerOverUI(mouseScreenPos_, window_.getSize(), world_) &&
        (hoverGx_ != lastDragGx_ || hoverGy_ != lastDragGy_)) {

        BuildingType tool = *activeTool_;
        if (isDirectionalBelt(tool)) {
            int dx = hoverGx_ - lastDragGx_;
            int dy = hoverGy_ - lastDragGy_;
            int dragDir = ghostDir_;
            if (std::abs(dx) + std::abs(dy) == 1) {
                if (dx == 1) dragDir = 0;
                else if (dy == 1) dragDir = 1;
                else if (dx == -1) dragDir = 2;
                else if (dy == -1) dragDir = 3;
                ghostDir_ = dragDir;

                if (BuildingEntity* prev = world_.buildingAtMut(lastDragGx_, lastDragGy_)) {
                    if (isDirectionalBelt(prev->type) && prev->dir != dragDir) {
                        prev->dir = dragDir;
                    }
                }
            }
            lastDragGx_ = hoverGx_;
            lastDragGy_ = hoverGy_;
            if (world_.placeBuilding(tool, hoverGx_, hoverGy_, dragDir)) {
                audio_.play(SfxType::Place, 30.0f, 1.12f);
            }
        } else if (isContinuousDragType(tool)) {
            lastDragGx_ = hoverGx_;
            lastDragGy_ = hoverGy_;
            if (world_.placeBuilding(tool, hoverGx_, hoverGy_, ghostDir_)) {
                audio_.play(SfxType::Place, 32.0f, 1.06f);
            }
        } else if (isAutoSpacedPoleType(tool)) {
            lastDragGx_ = hoverGx_;
            lastDragGy_ = hoverGy_;
            float range = Catalog::getBuilding(tool).powerRange;
            float dist = std::hypot(static_cast<float>(hoverGx_ - lastPoleGx_), static_cast<float>(hoverGy_ - lastPoleGy_));
            if (dist >= std::max(2.0f, range - 0.7f)) {
                if (world_.placeBuilding(tool, hoverGx_, hoverGy_, 0)) {
                    lastPoleGx_ = hoverGx_;
                    lastPoleGy_ = hoverGy_;
                    audio_.play(SfxType::Place, 36.0f, 1.08f);
                }
            }
        }
    }

    if (!ui_.isUpgradesOpen()) {
        if (holdingCrank_ && world_.inBounds(hoverGx_, hoverGy_)) {
            if (world_.crankAt(hoverGx_, hoverGy_, 0.6f) && crankSfxCooldown_ <= 0.0f) {
                audio_.play(SfxType::Crank, 25.0f, 1.05f);
                crankSfxCooldown_ = 0.24f;
            }
        } else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space)) {
            int cx = world_.inBounds(hoverGx_, hoverGy_) ? hoverGx_ : selectedGx_;
            int cy = world_.inBounds(hoverGx_, hoverGy_) ? hoverGy_ : selectedGy_;
            if (world_.inBounds(cx, cy) && world_.crankAt(cx, cy, 0.6f) && crankSfxCooldown_ <= 0.0f) {
                audio_.play(SfxType::Crank, 25.0f, 1.05f);
                crankSfxCooldown_ = 0.24f;
            }
        }
    }

    if (simSpeed_ > 0.01f) {
        world_.step(dt * simSpeed_);
    }
}

void Game::render() {
    sf::Vector2u winSize = window_.getSize();
    if (winSize.x == 0 || winSize.y == 0) {
        return;
    }

    syncWorldViewSize(winSize);

    PlacementCheck ghostCheck;
    std::optional<BuildingType> ghostToDraw;
    int effGhostDir = ghostDir_;
    const bool overWorld = !ui_.isUpgradesOpen() && !ui_.isPointerOverUI(mouseScreenPos_, winSize, world_);

    if (activeTool_.has_value() && !blueprintActive_ && !copyBoxArmed_ && overWorld) {
        ghostToDraw = activeTool_;
        if (*activeTool_ == BuildingType::TrainStation && !stationDirOverride_) {
            effGhostDir = world_.isInDirectHubZone(hoverGx_, hoverGy_) ? 1 : 0;
        }
        ghostCheck = world_.checkPlacement(*activeTool_, hoverGx_, hoverGy_, effGhostDir);
    }

    window_.clear(sf::Color(12, 16, 24));
    renderer_.renderScene(
        window_,
        worldView_,
        world_,
        settings_,
        animTime_,
        ghostToDraw,
        hoverGx_,
        hoverGy_,
        effGhostDir,
        ghostCheck,
        selectedGx_,
        selectedGy_,
        (blueprintActive_ && overWorld) ? &blueprint_ : nullptr,
        bpRot_,
        copyBoxStart_
    );

    renderer_.renderPopups(window_, world_, settings_, font_, worldView_);

    sf::View screenView(sf::FloatRect({0.0f, 0.0f}, {static_cast<float>(winSize.x), static_cast<float>(winSize.y)}));
    window_.setView(screenView);
    ui_.render(
        window_,
        font_,
        renderer_,
        world_,
        settings_,
        mouseScreenPos_,
        hoverWorldPos_,
        worldView_,
        animTime_,
        smoothedFps_,
        activeTool_,
        hoverGx_,
        hoverGy_,
        ghostCheck,
        selectedGx_,
        selectedGy_,
        simSpeed_,
        blueprintActive_
    );

    window_.display();
}

int Game::runSmokeRender(const std::string& outDir) {
    if (!loadFont()) {
        std::cerr << "Failed to load UI font.\n";
        return 1;
    }

    WorldGenSettings showcaseGen;
    showcaseGen.seedText = "COGWILL-25D-RT";
    showcaseGen.preset = WorldPreset::BasaltCanyons;
    showcaseGen.oreRichness = 1.50f;
    showcaseGen.oreFrequency = 1.25f;
    showcaseGen.waterCoverage = 1.25f;
    showcaseGen.cliffCoverage = 1.25f;
    world_.resetNewWorld(showcaseGen, false);

    // Unlock showcase pure-automation technologies across all 5 branches
    world_.unlockAllTechs();
    auto& res = world_.resourcesMut();
    for (double& v : res.values) {
        v = 8000.0;
    }

    // Complete Spire Phase 1, 2 & 3 so Grand Clockwork Core gleams with evolved Spire rings!
    world_.completeSpirePhase();
    world_.completeSpirePhase();
    world_.completeSpirePhase();

    // Sculpt a clean showcase industrial plaza around HUB (96, 72) flanked by 2.5D basalt cliff mesas & coolant lake!
    for (int y = 63; y <= 81; ++y) {
        for (int x = 82; x <= 112; ++x) {
            if (std::abs(x - HUB_GX) <= 1 && std::abs(y - HUB_GY) <= 1) continue;
            world_.tileMut(x, y).terrain = TerrainType::Ground;
            world_.tileMut(x, y).ore = OreType::None;
            world_.tileMut(x, y).elevation = 0.22f;
        }
    }

    // Scenic Northern Basalt Cliff Ridge (casts ray-marched 2.5D shadows!)
    for (int cx = 83; cx <= 88; ++cx) {
        world_.tileMut(cx, 65).terrain = TerrainType::Rock;
        world_.tileMut(cx, 65).elevation = 0.88f;
        if (cx <= 86) {
            world_.tileMut(cx, 66).terrain = TerrainType::Rock;
            world_.tileMut(cx, 66).elevation = 0.78f;
        }
    }
    for (int cx = 105; cx <= 109; ++cx) {
        world_.tileMut(cx, 70).terrain = TerrainType::Rock;
        world_.tileMut(cx, 70).elevation = 0.90f;
        world_.tileMut(cx, 71).terrain = TerrainType::Rock;
        world_.tileMut(cx, 71).elevation = 0.82f;
    }

    // Reflective Coolant Lagoon right beside the Reactors & Core (shows planar RT water reflections!)
    for (int wy = 68; wy <= 70; ++wy) {
        for (int wx = 89; wx <= 93; ++wx) {
            world_.tileMut(wx, wy).terrain = TerrainType::Water;
            world_.tileMut(wx, wy).elevation = 0.0f;
        }
    }

    // Rich glowing ore veins
    world_.tileMut(87, 72).ore = OreType::Iron;
    world_.tileMut(87, 72).richness = 1.9f;
    world_.tileMut(87, 75).ore = OreType::Copper;
    world_.tileMut(87, 75).richness = 1.8f;
    world_.tileMut(105, 75).ore = OreType::Uranium;
    world_.tileMut(105, 75).richness = 2.2f;
    world_.tileMut(106, 76).ore = OreType::Aether;
    world_.tileMut(106, 76).richness = 2.4f;

    // 1. 3D Stacked Mechanical Gears + LargeGear + SteamEngine + Smelter Belt Line into Core
    world_.placeBuilding(BuildingType::SteamEngine, 85, 70);
    world_.placeBuilding(BuildingType::LargeGear, 87, 70);
    world_.placeBuilding(BuildingType::IronDrill, 87, 72);
    world_.placeBuilding(BuildingType::Lubricator, 86, 72);
    world_.placeBuilding(BuildingType::WaterPump, 91, 70);
    world_.placeBuilding(BuildingType::ConveyorBelt, 88, 72, 0);
    world_.placeBuilding(BuildingType::FastBelt, 89, 72, 0);
    world_.placeBuilding(BuildingType::StoneSmelter, 90, 72);
    world_.placeBuilding(BuildingType::SmallCog, 90, 71);
    world_.placeBuilding(BuildingType::WindTurbine, 92, 71);
    world_.placeBuilding(BuildingType::SmallCog, 91, 71);
    world_.manualFeedMachineAt(90, 72);
    world_.placeBuilding(BuildingType::FastBelt, 91, 72, 0);
    world_.placeBuilding(BuildingType::ExpressBelt, 92, 72, 0);
    world_.placeBuilding(BuildingType::ExpressBelt, 93, 72, 0);
    world_.placeBuilding(BuildingType::ExpressBelt, 94, 72, 0);

    // 2. Electric Drill -> Arc Furnace -> Assembler -> Quantum Forge -> Beacon -> Matter Sink
    world_.placeBuilding(BuildingType::ElectricDrill, 87, 75);
    world_.placeBuilding(BuildingType::FastBelt, 88, 75, 0);
    world_.placeBuilding(BuildingType::ArcFurnace, 89, 75);
    world_.manualFeedMachineAt(89, 75);
    world_.placeBuilding(BuildingType::ExpressBelt, 90, 75, 0);
    world_.placeBuilding(BuildingType::Assembler, 91, 75);
    world_.manualFeedMachineAt(91, 75);
    world_.placeBuilding(BuildingType::ExpressBelt, 92, 75, 0);
    world_.placeBuilding(BuildingType::BeltSplitter, 93, 75, 0);
    world_.placeBuilding(BuildingType::QuantumForge, 94, 75);
    world_.manualFeedMachineAt(94, 75);
    world_.placeBuilding(BuildingType::OverclockBeacon, 92, 77);
    world_.placeBuilding(BuildingType::MatterSink, 93, 74);

    // 3. Monumental 3x3 Nuclear Reactor + Cooling Towers + 3x3 Tokamak Fusion Reactor + Solar + Substations
    world_.placeBuilding(BuildingType::NuclearReactor, 86, 78);
    world_.placeBuilding(BuildingType::CoolingTower, 84, 78);
    world_.placeBuilding(BuildingType::CoolingTower, 88, 78);
    world_.placeBuilding(BuildingType::FusionReactor, 101, 73);
    world_.placeBuilding(BuildingType::AetherExtractor, 106, 76);
    world_.placeBuilding(BuildingType::ElectricDrill, 105, 75);
    world_.placeBuilding(BuildingType::ExpressBelt, 104, 75, 2);
    world_.placeBuilding(BuildingType::StorageVault, 103, 75);
    world_.placeBuilding(BuildingType::SolarPanel, 98, 77);
    world_.placeBuilding(BuildingType::SolarPanel, 99, 77);
    world_.placeBuilding(BuildingType::Accumulator, 98, 78);
    world_.placeBuilding(BuildingType::Accumulator, 99, 78);
    world_.placeBuilding(BuildingType::Substation, 90, 78);
    world_.placeBuilding(BuildingType::Substation, 97, 75);
    world_.placeBuilding(BuildingType::Substation, 104, 73);
    world_.placeBuilding(BuildingType::PowerPole, 94, 70);

    // 4. Northern Express Railway & Electric Train (y = 67)
    world_.tileMut(108, 66).ore = OreType::Aether;
    world_.tileMut(108, 66).richness = 2.3f;
    world_.placeBuilding(BuildingType::AetherExtractor, 108, 66);
    world_.placeBuilding(BuildingType::Substation, 107, 68);
    world_.placeBuilding(BuildingType::TrainStation, 108, 67, 0); // LOAD
    world_.placeBuilding(BuildingType::TrainStation, 95, 67, 1);  // UNLOAD near Core
    for (int rx = 96; rx < 108; ++rx) {
        world_.placeBuilding(BuildingType::RailTrack, rx, 67);
    }
    world_.placeBuilding(BuildingType::ElectricTrain, 98, 67);

    selectedGx_ = 101;
    selectedGy_ = 73;
    ui_.syncWorldGenFromWorld(world_);

    // Step simulation so belts carry glowing items, batteries charge, and Electric Train cruises!
    for (int i = 0; i < 260; ++i) {
        world_.step(1.0f / 60.0f);
        animTime_ += 1.0f / 60.0f;
    }

    zoom_ = 1.08f;
    sf::RenderTexture rt({1440u, 900u});

    auto saveFrame = [&](const std::string& filename, int modalMode, const GameSettings& frameCfg, bool sbx) -> bool {
        world_.setSandboxMode(sbx);
        world_.setLightingTimeMode(frameCfg.lightingTimeMode);
        const float tiltY = frameCfg.mode25D ? TILT_25D_Y : 1.0f;
        worldView_.setSize({1440.0f * zoom_, (900.0f * zoom_) / tiltY});
        worldView_.setCenter({96.5f * CELL_SIZE, 72.5f * CELL_SIZE});

        if (modalMode < 0) {
            ui_.setUpgradesOpen(false);
        } else {
            ui_.setUpgradesOpen(false);
            if (modalMode == 3) {
                ui_.syncWorldGenFromWorld(world_);
            }
            ui_.openModalTab(modalMode);
        }
        ui_.setActiveCategory(BuildingCategory::Electric);
        PlacementCheck ghostCheck = world_.checkPlacement(BuildingType::ExpressBelt, 95, 75, 3);

        rt.clear(sf::Color(12, 16, 24));
        renderer_.renderScene(
            rt,
            worldView_,
            world_,
            frameCfg,
            animTime_,
            (modalMode >= 0) ? std::nullopt : std::optional<BuildingType>(BuildingType::ExpressBelt),
            95, 75, 3, ghostCheck, selectedGx_, selectedGy_
        );
        renderer_.renderPopups(rt, world_, frameCfg, font_, worldView_);
        rt.setView(rt.getDefaultView());
        ui_.render(
            rt, font_, renderer_, world_, frameCfg,
            {720.0f, 450.0f}, {96.5f * CELL_SIZE, 72.5f * CELL_SIZE}, worldView_, animTime_, 60.0f,
            BuildingType::ExpressBelt,
            95, 75, ghostCheck, selectedGx_, selectedGy_,
            1.0f, false
        );
        rt.display();
        return rt.getTexture().copyToImage().saveToFile(filename);
    };

    std::filesystem::create_directories(outDir);

    GameSettings dayCfg{};
    dayCfg.mode25D = true;
    dayCfg.shadersEnabled = true;
    dayCfg.rtQuality = 2; // High 16-Ray
    dayCfg.pixelScale = 2;
    dayCfg.lightingTimeMode = 1; // Bright clear Daylight + crisp 2.5D heightfield shadows
    if (!saveFrame(outDir + "/smoke_25d_day.png", -1, dayCfg, false)) return 2;

    GameSettings nightCfg = dayCfg;
    nightCfg.rtQuality = 3; // Ultra 24-Ray GI
    nightCfg.bloomStrength = 1.10f;
    nightCfg.lightingTimeMode = 2; // Neon Night ray-traced colored GI & reflections
    if (!saveFrame(outDir + "/smoke_25d_night_rt.png", -1, nightCfg, true)) return 3;

    if (!saveFrame(outDir + "/smoke_world_sandbox.png", 3, dayCfg, true)) return 4;
    if (!saveFrame(outDir + "/smoke_settings.png", 4, nightCfg, true)) return 5;

    // Reset a few techs to partial progress so smoke_upgrades.png showcases both glowing active boughs & unlockable branches!
    world_.setUpgradeLevelForTest(UpgradeId::UnlockOverclockers, 0);
    world_.setUpgradeLevelForTest(UpgradeId::BeaconAmplification, 0);
    world_.setUpgradeLevelForTest(UpgradeId::UnlockFusionPower, 0);
    world_.setUpgradeLevelForTest(UpgradeId::PlanetaryLogistics, 0);
    world_.setUpgradeLevelForTest(UpgradeId::CoreFracking, 0);
    world_.setUpgradeLevelForTest(UpgradeId::FreightBonus, 0);
    if (!saveFrame(outDir + "/smoke_upgrades.png", 0, dayCfg, false)) return 6;

    // Full-continent zoomed-out strategic view verification!
    settings_ = dayCfg;
    zoom_ = maxWorldZoom({1440u, 900u});
    if (!saveFrame(outDir + "/smoke_zoomed_out.png", -1, dayCfg, false)) return 7;

    return 0;
}

} // namespace cogwill
