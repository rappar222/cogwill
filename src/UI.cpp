#include "UI.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Graphics/View.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace cogwill {

namespace {

std::string fmtInt(double v) {
    long long n = static_cast<long long>(std::floor(v + 1e-4));
    return std::to_string(n);
}

std::string fmtCompact(double v) {
    long long n = static_cast<long long>(std::floor(v + 1e-4));
    if (n >= 1000000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(n) / 1000000.0) << "M";
        return oss.str();
    }
    if (n >= 10000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(n) / 1000.0) << "k";
        return oss.str();
    }
    return std::to_string(n);
}

std::string fmtRate(double r) {
    if (r < 0.05) {
        return "";
    }
    std::ostringstream oss;
    if (r >= 1000.0) {
        oss << "+" << std::fixed << std::setprecision(1) << (r / 1000.0) << "k";
    } else if (r >= 100.0) {
        oss << "+" << static_cast<long long>(std::round(r));
    } else {
        oss << "+" << std::fixed << std::setprecision(1) << r;
    }
    return oss.str();
}

std::string fmtFloat1(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << v;
    return oss.str();
}

std::string fmtFloat2(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

std::string fmtTimerMMSS(float sec) {
    int s = std::max(0, static_cast<int>(std::floor(sec)));
    int m = s / 60;
    int rem = s % 60;
    std::ostringstream oss;
    oss << m << ":" << (rem < 10 ? "0" : "") << rem;
    return oss.str();
}

const char* biomeName(uint8_t b) {
    switch (b) {
    case 0:  return "Clockwork Slate";
    case 1:  return "Rust Mesa";
    case 2:  return "Coolant Moss Basin";
    case 3:  return "Obsidian Rift";
    case 4:  return "Quartz Tundra";
    default: return "Clockwork Slate";
    }
}

const char* presetSummary(WorldPreset p) {
    switch (p) {
    case WorldPreset::BalancedValley:   return "Multi-biome basin, rivers & balanced rings";
    case WorldPreset::RiverArchipelago: return "Coolant island chains & hydro lakes";
    case WorldPreset::BasaltCanyons:    return "Tall 2.5D cliff mesas & canyon veins";
    case WorldPreset::MegabasePlains:   return "Vast open plains for massive bus & rail";
    case WorldPreset::AetherBadlands:   return "Volcanic rifts & rich exotic crystal beds";
    default:                            return "";
    }
}

const char* rtQualityLabel(int q) {
    switch (q) {
    case 0:  return "Off";
    case 1:  return "Medium (8-Ray)";
    case 2:  return "High (16-Ray)";
    case 3:  return "Ultra (24-Ray GI)";
    default: return "High (16-Ray)";
    }
}

const char* rtQualityShort(int q) {
    switch (q) {
    case 0:  return "Off";
    case 1:  return "Med";
    case 2:  return "High";
    case 3:  return "Ultra";
    default: return "High";
    }
}

sf::Color resourceColor(ResourceType type) {
    switch (type) {
    case ResourceType::Iron:        return {178, 198, 224};
    case ResourceType::Copper:      return {242, 152, 82};
    case ResourceType::Coal:        return {172, 158, 142};
    case ResourceType::Titanium:    return {196, 172, 255};
    case ResourceType::Uranium:     return {118, 245, 96};
    case ResourceType::Aether:      return {88, 238, 255};
    case ResourceType::Steel:       return {215, 226, 242};
    case ResourceType::Circuit:     return {96, 238, 148};
    case ResourceType::QuantumCore: return {235, 128, 255};
    default:                        return {220, 220, 220};
    }
}

std::string formatCostHotbar(const ResourceBag& cost) {
    std::string s;
    int shown = 0;
    int total = 0;
    for (int i = 0; i < static_cast<int>(ResourceType::Count); ++i) {
        double v = cost.values[static_cast<std::size_t>(i)];
        if (v > 0.0) {
            ++total;
            if (shown < 3) {
                if (!s.empty()) s += " ";
                s += fmtInt(v) + resourceTag(static_cast<ResourceType>(i));
                ++shown;
            }
        }
    }
    if (total > 3) {
        s += "+";
    }
    return s;
}

std::string formatCostFull(const ResourceBag& cost) {
    std::string s;
    for (int i = 0; i < static_cast<int>(ResourceType::Count); ++i) {
        double v = cost.values[static_cast<std::size_t>(i)];
        if (v > 0.0) {
            if (!s.empty()) s += "   ";
            s += fmtInt(v) + " " + resourceName(static_cast<ResourceType>(i));
        }
    }
    return s.empty() ? "Free" : s;
}

void drawPanel(sf::RenderTarget& target, sf::FloatRect rect, sf::Color fill, sf::Color outline, float thickness = 2.0f) {
    sf::RectangleShape box(rect.size);
    box.setPosition({std::round(rect.position.x), std::round(rect.position.y)});
    box.setFillColor(fill);
    box.setOutlineColor(outline);
    box.setOutlineThickness(std::round(thickness));
    target.draw(box);

    sf::RectangleShape hiTop({std::max(0.0f, rect.size.x - 4.0f), 1.0f});
    hiTop.setPosition({std::round(rect.position.x) + 2.0f, std::round(rect.position.y) + 1.0f});
    hiTop.setFillColor(sf::Color(255, 255, 255, 24));
    target.draw(hiTop);
}

void drawCenteredText(sf::RenderTarget& target, const sf::Font& font, const std::string& str, unsigned int size, sf::Vector2f center, sf::Color col, bool bold = false) {
    sf::Text t(font, str, size);
    if (bold) t.setStyle(sf::Text::Bold);
    t.setFillColor(col);
    auto b = t.getLocalBounds();
    t.setOrigin({b.position.x + b.size.x * 0.5f, b.position.y + b.size.y * 0.5f});
    t.setPosition({std::round(center.x), std::round(center.y)});
    target.draw(t);
}

constexpr float TREE_PITCH_X = 172.0f;
constexpr float TREE_PITCH_Y = 128.0f;
constexpr float TREE_CARD_W  = 186.0f;
constexpr float TREE_CARD_H  = 66.0f;

sf::Color techBranchColor(const std::string& branch) {
    if (branch.find("Mining") != std::string::npos)    return {196, 138, 255};
    if (branch.find("Clockwork") != std::string::npos) return {244, 194, 84};
    if (branch.find("Power") != std::string::npos)     return {86, 224, 255};
    if (branch.find("Belts") != std::string::npos)     return {90, 232, 160};
    return {255, 168, 82}; // Megastructures
}

sf::Vector2f evalCubicBezier(sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2, sf::Vector2f p3, float t) {
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    return p0 * (uu * u) + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * (tt * t);
}

void drawLineSegment(sf::RenderTarget& target, sf::Vector2f a, sf::Vector2f b, float width, sf::Color col) {
    sf::Vector2f d = b - a;
    float len = std::hypot(d.x, d.y);
    if (len < 0.5f) return;
    sf::RectangleShape seg({len, width});
    seg.setOrigin({0.f, width * 0.5f});
    seg.setPosition(a);
    seg.setRotation(sf::radians(std::atan2(d.y, d.x)));
    seg.setFillColor(col);
    target.draw(seg);
}

void drawTreeBezierBough(
    sf::RenderTarget& target,
    sf::Vector2f p0,
    sf::Vector2f p3,
    float wStart,
    float wEnd,
    sf::Color glowCol,
    sf::Color coreCol,
    float animTime,
    bool showEnergyPulse
) {
    float dy = std::max(24.0f, std::abs(p3.y - p0.y) * 0.52f);
    sf::Vector2f p1{p0.x, p0.y + dy};
    sf::Vector2f p2{p3.x, p3.y - dy};

    constexpr int SEGS = 18;
    sf::VertexArray haloVa(sf::PrimitiveType::TriangleStrip, (SEGS + 1) * 2);
    sf::VertexArray coreVa(sf::PrimitiveType::TriangleStrip, (SEGS + 1) * 2);

    for (int i = 0; i <= SEGS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SEGS);
        sf::Vector2f pos = evalCubicBezier(p0, p1, p2, p3, t);
        float tPrev = std::max(0.0f, t - 0.02f);
        float tNext = std::min(1.0f, t + 0.02f);
        sf::Vector2f tang = evalCubicBezier(p0, p1, p2, p3, tNext) - evalCubicBezier(p0, p1, p2, p3, tPrev);
        float tLen = std::hypot(tang.x, tang.y);
        sf::Vector2f nrm = (tLen > 0.001f) ? sf::Vector2f{-tang.y / tLen, tang.x / tLen} : sf::Vector2f{1.0f, 0.0f};

        float halfCore = (wStart + (wEnd - wStart) * t) * 0.5f;
        float halfHalo = halfCore * 2.25f;

        haloVa[static_cast<std::size_t>(i * 2)]     = sf::Vertex{pos + nrm * halfHalo, glowCol};
        haloVa[static_cast<std::size_t>(i * 2 + 1)] = sf::Vertex{pos - nrm * halfHalo, glowCol};
        coreVa[static_cast<std::size_t>(i * 2)]     = sf::Vertex{pos + nrm * halfCore, coreCol};
        coreVa[static_cast<std::size_t>(i * 2 + 1)] = sf::Vertex{pos - nrm * halfCore, coreCol};
    }

    if (glowCol.a > 0) {
        target.draw(haloVa);
    }
    target.draw(coreVa);

    if (showEnergyPulse) {
        float pulseT = std::fmod(animTime * 0.55f + std::abs(p0.x + p3.x) * 0.0013f, 1.0f);
        sf::Vector2f sparkPos = evalCubicBezier(p0, p1, p2, p3, pulseT);
        float r = std::max(2.2f, wEnd * 0.85f);
        sf::CircleShape spark(r, 10);
        spark.setOrigin({r, r});
        spark.setPosition(sparkPos);
        spark.setFillColor(sf::Color(255, 250, 220, 235));
        target.draw(spark);
    }
}

void drawTreeSynergyArc(
    sf::RenderTarget& target,
    sf::Vector2f p0,
    sf::Vector2f p3,
    float width,
    sf::Color col
) {
    float dy = std::max(30.0f, std::abs(p3.y - p0.y) * 0.45f);
    sf::Vector2f p1{p0.x, p0.y + dy};
    sf::Vector2f p2{p3.x, p3.y - dy};
    constexpr int STEPS = 22;
    for (int i = 0; i < STEPS; i += 2) {
        float t0 = static_cast<float>(i) / static_cast<float>(STEPS);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(STEPS);
        drawLineSegment(target, evalCubicBezier(p0, p1, p2, p3, t0), evalCubicBezier(p0, p1, p2, p3, t1), width, col);
    }
}

void drawProgressBar(sf::RenderTarget& target, sf::FloatRect rect, float ratio, sf::Color fillCol) {
    float r = std::clamp(ratio, 0.0f, 1.0f);
    drawPanel(target, rect, sf::Color(14, 18, 26), sf::Color(58, 70, 92), 1.0f);
    if (r > 0.005f) {
        sf::RectangleShape fg({(rect.size.x - 2.0f) * r, rect.size.y - 2.0f});
        fg.setPosition({rect.position.x + 1.0f, rect.position.y + 1.0f});
        fg.setFillColor(fillCol);
        target.draw(fg);
    }
}

// Shared layout helpers for Tab 3 (World & Sandbox) and Tab 4 (Settings & RT)
struct WorldTabLayout {
    sf::FloatRect leftCard;
    sf::FloatRect rightCard;
    sf::FloatRect seedBox;
    sf::FloatRect seedRandomBtn;
    sf::FloatRect seedClearBtn;
    sf::FloatRect presetBtns[5];
    sf::FloatRect stepperMinus[4];
    sf::FloatRect stepperPlus[4];
    sf::FloatRect newSandboxBtn;
    sf::FloatRect generateBtn;

    sf::FloatRect creativeBtns[4];
    sf::FloatRect timeModeBtns[4];
};

WorldTabLayout computeWorldTabLayout(sf::FloatRect modal) {
    WorldTabLayout L{};
    L.leftCard = sf::FloatRect({modal.position.x + 18.0f, modal.position.y + 54.0f},
                               {modal.size.x * 0.52f - 24.0f, modal.size.y - 68.0f});
    L.rightCard = sf::FloatRect({modal.position.x + modal.size.x * 0.52f + 6.0f, modal.position.y + 54.0f},
                                {modal.size.x * 0.48f - 24.0f, modal.size.y - 68.0f});

    float sx = L.leftCard.position.x + 18.0f;
    float sy = L.leftCard.position.y + 64.0f;
    float availW = L.leftCard.size.x - 36.0f;
    L.seedClearBtn = sf::FloatRect({sx + availW - 62.0f, sy}, {62.0f, 32.0f});
    L.seedRandomBtn = sf::FloatRect({sx + availW - 62.0f - 6.0f - 86.0f, sy}, {86.0f, 32.0f});
    L.seedBox = sf::FloatRect({sx, sy}, {availW - 160.0f, 32.0f});

    for (int p = 0; p < 5; ++p) {
        L.presetBtns[p] = sf::FloatRect({sx, L.leftCard.position.y + 126.0f + static_cast<float>(p) * 36.0f},
                                        {availW, 31.0f});
    }

    for (int s = 0; s < 4; ++s) {
        float ry = L.leftCard.position.y + 318.0f + static_cast<float>(s) * 34.0f;
        L.stepperPlus[s] = sf::FloatRect({sx + availW - 34.0f, ry}, {34.0f, 28.0f});
        L.stepperMinus[s] = sf::FloatRect({sx + availW - 34.0f - 78.0f - 34.0f, ry}, {34.0f, 28.0f});
    }

    L.newSandboxBtn = sf::FloatRect({sx, L.leftCard.position.y + L.leftCard.size.y - 98.0f}, {availW, 34.0f});
    L.generateBtn = sf::FloatRect({sx, L.leftCard.position.y + L.leftCard.size.y - 54.0f}, {availW, 42.0f});

    float rx = L.rightCard.position.x + 18.0f;
    float ravailW = L.rightCard.size.x - 36.0f;
    for (int c = 0; c < 4; ++c) {
        L.creativeBtns[c] = sf::FloatRect({rx, L.rightCard.position.y + 126.0f + static_cast<float>(c) * 50.0f},
                                          {ravailW, 40.0f});
    }

    float halfW = (ravailW - 10.0f) * 0.5f;
    for (int m = 0; m < 4; ++m) {
        int col = m % 2;
        int row = m / 2;
        L.timeModeBtns[m] = sf::FloatRect(
            {rx + static_cast<float>(col) * (halfW + 10.0f), L.rightCard.position.y + 360.0f + static_cast<float>(row) * 44.0f},
            {halfW, 36.0f}
        );
    }
    return L;
}

struct SettingsTabLayout {
    sf::FloatRect leftCard;
    sf::FloatRect rightCard;
    sf::FloatRect leftBtns[8];
    sf::FloatRect rightBtns[11];
    sf::FloatRect volMinus;
    sf::FloatRect volCenter;
    sf::FloatRect volPlus;
    sf::FloatRect resetBtn;
    sf::FloatRect saveBtn;
    sf::FloatRect loadBtn;
};

SettingsTabLayout computeSettingsTabLayout(sf::FloatRect modal) {
    SettingsTabLayout S{};
    S.leftCard = sf::FloatRect({modal.position.x + 18.0f, modal.position.y + 54.0f},
                               {modal.size.x * 0.50f - 24.0f, modal.size.y - 68.0f});
    S.rightCard = sf::FloatRect({modal.position.x + modal.size.x * 0.50f + 6.0f, modal.position.y + 54.0f},
                                {modal.size.x * 0.50f - 24.0f, modal.size.y - 68.0f});

    float lBtnW = 216.0f;
    float lBtnX = S.leftCard.position.x + S.leftCard.size.x - 18.0f - lBtnW;
    for (int i = 0; i < 8; ++i) {
        S.leftBtns[i] = sf::FloatRect({lBtnX, S.leftCard.position.y + 54.0f + static_cast<float>(i) * 48.0f},
                                      {lBtnW, 36.0f});
    }
    S.resetBtn = sf::FloatRect({S.leftCard.position.x + 18.0f, S.leftCard.position.y + S.leftCard.size.y - 54.0f},
                               {S.leftCard.size.x - 36.0f, 40.0f});

    float rBtnW = 196.0f;
    float rBtnX = S.rightCard.position.x + S.rightCard.size.x - 18.0f - rBtnW;
    for (int i = 0; i < 11; ++i) {
        S.rightBtns[i] = sf::FloatRect({rBtnX, S.rightCard.position.y + 50.0f + static_cast<float>(i) * 41.0f},
                                       {rBtnW, 32.0f});
    }
    // Row 5 is Master Audio Volume: [-] [80% / Muted] [+]
    sf::FloatRect vr = S.rightBtns[5];
    S.volMinus = sf::FloatRect({vr.position.x, vr.position.y}, {34.0f, vr.size.y});
    S.volPlus = sf::FloatRect({vr.position.x + vr.size.x - 34.0f, vr.position.y}, {34.0f, vr.size.y});
    S.volCenter = sf::FloatRect({vr.position.x + 38.0f, vr.position.y}, {vr.size.x - 76.0f, vr.size.y});

    float halfW = (S.rightCard.size.x - 46.0f) * 0.5f;
    float botY = S.rightCard.position.y + S.rightCard.size.y - 54.0f;
    S.saveBtn = sf::FloatRect({S.rightCard.position.x + 18.0f, botY}, {halfW, 40.0f});
    S.loadBtn = sf::FloatRect({S.rightCard.position.x + 28.0f + halfW, botY}, {halfW, 40.0f});
    return S;
}

} // namespace

UI::UI() : minimapTex_({static_cast<unsigned int>(GRID_W), static_cast<unsigned int>(GRID_H)}) {
    minimapTex_.setSmooth(false);
}

void UI::setToast(const std::string& msg, bool isError) {
    toastText_ = msg;
    toastIsError_ = isError;
    toastTimer_ = 2.8f;
}

void UI::update(float dt) {
    if (toastTimer_ > 0.0f) {
        toastTimer_ = std::max(0.0f, toastTimer_ - dt);
    }
    minimapTimer_ += dt;
}

void UI::syncWorldGenFromWorld(const World& world) {
    pendingWorldGen_ = world.worldGen();
    pendingNewWorldSandbox_ = world.isSandboxMode();
}

void UI::handleTextEntered(char32_t unicode) {
    if (!isSeedInputFocused()) {
        return;
    }
    if (unicode >= 32 && unicode <= 126 && pendingWorldGen_.seedText.size() < 24) {
        pendingWorldGen_.seedText.push_back(static_cast<char>(unicode));
    }
}

bool UI::handleSeedKey(sf::Keyboard::Key key) {
    if (!isSeedInputFocused()) {
        return false;
    }
    if (key == sf::Keyboard::Key::Escape || key == sf::Keyboard::Key::Enter) {
        seedInputFocused_ = false;
        return true;
    }
    if (key == sf::Keyboard::Key::Backspace) {
        if (!pendingWorldGen_.seedText.empty()) {
            pendingWorldGen_.seedText.pop_back();
        }
        return true;
    }
    return true; // consume other keys while typing seed
}

void UI::panTechTree(sf::Vector2f screenDelta) {
    float z = std::max(0.40f, techZoom_);
    techPan_.x = std::clamp(techPan_.x + screenDelta.x / z, -1250.0f, 1380.0f);
    techPan_.y = std::clamp(techPan_.y + screenDelta.y / z, -80.0f, 980.0f);
    techViewInitialized_ = true;
}

void UI::zoomTechTree(float wheelDelta, sf::Vector2f mousePos, sf::Vector2u winSize) {
    ensureTechViewInitialized(winSize);
    sf::FloatRect canvas = computeTechCanvasRect(winSize);
    sf::Vector2f canvasCenter = canvas.getCenter();
    sf::Vector2f anchor = canvas.contains(mousePos) ? mousePos : canvasCenter;

    float oldZoom = techZoom_;
    sf::Vector2f treeUnderCursor = techPan_ + (anchor - canvasCenter) / oldZoom;

    float factor = (wheelDelta > 0.0f) ? 1.13f : (1.0f / 1.13f);
    techZoom_ = std::clamp(oldZoom * factor, 0.48f, 1.45f);
    techPan_ = treeUnderCursor - (anchor - canvasCenter) / techZoom_;
    techPan_.x = std::clamp(techPan_.x, -1250.0f, 1380.0f);
    techPan_.y = std::clamp(techPan_.y, -80.0f, 980.0f);
}

void UI::fitTechTree(sf::Vector2u winSize) {
    sf::FloatRect canvas = computeTechCanvasRect(winSize);
    constexpr float treeSpanW = 2285.0f; // [-1068 .. +1217]
    constexpr float treeSpanH = 980.0f;  // [-42 .. +938]
    float zx = (canvas.size.x - 32.0f) / treeSpanW;
    float zy = (canvas.size.y - 28.0f) / treeSpanH;
    techZoom_ = std::clamp(std::min(zx, zy), 0.48f, 1.15f);
    techPan_ = {68.0f, 448.0f};
    techViewInitialized_ = true;
}

bool UI::isOverTechTreeCanvas(sf::Vector2f mousePos, sf::Vector2u winSize) const {
    return upgradesOpen_ && modalTab_ == 0 && computeTechCanvasRect(winSize).contains(mousePos);
}

std::vector<UI::CategoryTab> UI::computeCategoryTabs(sf::Vector2u winSize) const {
    const float tabW = 96.0f;
    const float tabH = 26.0f;
    const float gap = 5.0f;
    const float totalW = 7.0f * tabW + 6.0f * gap;
    const float startX = (static_cast<float>(winSize.x) - totalW) * 0.5f;
    const float startY = static_cast<float>(winSize.y) - 74.0f - 12.0f - tabH - 6.0f;

    struct TabInfo { BuildingCategory cat; const char* lbl; };
    static const TabInfo TABS[7] = {
        {BuildingCategory::Transmission, "F1 Gears"},
        {BuildingCategory::Generator,    "F2 Power"},
        {BuildingCategory::Drill,        "F3 Drills"},
        {BuildingCategory::Factory,      "F4 Factory"},
        {BuildingCategory::Belt,         "F5 Belts"},
        {BuildingCategory::Electric,     "F6 Grid"},
        {BuildingCategory::Rail,         "F7 Rails"}
    };

    std::vector<CategoryTab> out;
    out.reserve(7);
    for (int i = 0; i < 7; ++i) {
        out.push_back({
            TABS[i].cat,
            sf::FloatRect({startX + static_cast<float>(i) * (tabW + gap), startY}, {tabW, tabH}),
            TABS[i].lbl
        });
    }
    return out;
}

std::vector<UI::HotbarSlot> UI::computeHotbarSlots(sf::Vector2u winSize) const {
    const auto& order = Catalog::buildingsForCategory(activeCategory_);
    const float slotW = 104.0f;
    const float slotH = 74.0f;
    const float gap = 6.0f;
    const std::size_t count = std::min<std::size_t>(order.size(), 7);
    float totalW = static_cast<float>(count) * slotW + static_cast<float>(std::max<std::size_t>(1, count) - 1) * gap;
    float startX = (static_cast<float>(winSize.x) - totalW) * 0.5f;
    float startY = static_cast<float>(winSize.y) - slotH - 12.0f;

    static const char* KEY_LABELS[7] = {"1", "2", "3", "4", "5", "6", "7"};

    std::vector<HotbarSlot> slots;
    slots.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        slots.push_back(HotbarSlot{
            order[i],
            sf::FloatRect({startX + static_cast<float>(i) * (slotW + gap), startY}, {slotW, slotH}),
            KEY_LABELS[i]
        });
    }
    return slots;
}

std::vector<UI::ButtonRect> UI::computeTopButtons(sf::Vector2u winSize) const {
    float w = static_cast<float>(winSize.x);
    std::vector<ButtonRect> btns;
    float x = w - 8.0f;
    auto pushRight = [&](float bw, UIActionType act) {
        x -= bw;
        btns.push_back({sf::FloatRect({x, 10.0f}, {bw, 36.0f}), act, UpgradeId::Count});
        x -= 4.0f;
    };
    pushRight(46.0f, UIActionType::SaveGame);
    pushRight(42.0f, UIActionType::ToggleSpeed);
    pushRight(78.0f, UIActionType::OpenSettingsTab);
    pushRight(68.0f, UIActionType::OpenWorldTab);
    pushRight(74.0f, UIActionType::ToggleSandbox);
    pushRight(72.0f, UIActionType::ToggleMode25D);
    pushRight(78.0f, UIActionType::OpenSpireTab);
    pushRight(78.0f, UIActionType::ToggleUpgrades);
    return btns;
}

sf::FloatRect UI::computeStatusBannerRect(sf::Vector2u winSize) const {
    return sf::FloatRect({static_cast<float>(winSize.x) - 274.0f, 66.0f}, {262.0f, 66.0f});
}

sf::FloatRect UI::computeMinimapBoxRect(sf::Vector2u winSize) const {
    if (minimapCollapsed_) {
        return sf::FloatRect({static_cast<float>(winSize.x) - 166.0f, static_cast<float>(winSize.y) - 42.0f}, {154.0f, 30.0f});
    }
    float imgW = 192.0f;
    float imgH = 144.0f;
    float boxW = imgW + 12.0f;
    float boxH = imgH + 30.0f;
    return sf::FloatRect({static_cast<float>(winSize.x) - boxW - 12.0f, static_cast<float>(winSize.y) - boxH - 12.0f}, {boxW, boxH});
}

sf::FloatRect UI::computeMinimapImageRect(sf::Vector2u winSize) const {
    sf::FloatRect box = computeMinimapBoxRect(winSize);
    return sf::FloatRect({box.position.x + 6.0f, box.position.y + 24.0f}, {192.0f, 144.0f});
}

bool UI::isOverMinimap(sf::Vector2f mousePos, sf::Vector2u winSize) const {
    if (upgradesOpen_ || minimapCollapsed_) {
        return false;
    }
    return computeMinimapImageRect(winSize).contains(mousePos);
}

sf::Vector2f UI::minimapPosToWorld(sf::Vector2f mousePos, sf::Vector2u winSize) const {
    sf::FloatRect ir = computeMinimapImageRect(winSize);
    float nx = std::clamp((mousePos.x - ir.position.x) / std::max(1.0f, ir.size.x), 0.0f, 1.0f);
    float ny = std::clamp((mousePos.y - ir.position.y) / std::max(1.0f, ir.size.y), 0.0f, 1.0f);
    return {
        nx * static_cast<float>(GRID_W) * CELL_SIZE,
        ny * static_cast<float>(GRID_H) * CELL_SIZE
    };
}

sf::FloatRect UI::computeUpgradeModalRect(sf::Vector2u winSize) const {
    float mw = std::min(1420.0f, static_cast<float>(winSize.x) - 20.0f);
    float mh = std::min(836.0f, static_cast<float>(winSize.y) - 52.0f);
    return sf::FloatRect({(static_cast<float>(winSize.x) - mw) * 0.5f, (static_cast<float>(winSize.y) - mh) * 0.5f}, {mw, mh});
}

sf::FloatRect UI::computeTechCanvasRect(sf::Vector2u winSize) const {
    sf::FloatRect modal = computeUpgradeModalRect(winSize);
    return sf::FloatRect({modal.position.x + 14.0f, modal.position.y + 50.0f},
                         {modal.size.x - 28.0f, modal.size.y - 152.0f});
}

void UI::ensureTechViewInitialized(sf::Vector2u winSize) const {
    if (techViewInitialized_) {
        return;
    }
    sf::FloatRect canvas = computeTechCanvasRect(winSize);
    constexpr float treeSpanW = 2285.0f;
    constexpr float treeSpanH = 980.0f;
    float zx = (canvas.size.x - 32.0f) / treeSpanW;
    float zy = (canvas.size.y - 28.0f) / treeSpanH;
    techZoom_ = std::clamp(std::min(zx, zy), 0.48f, 1.15f);
    techPan_ = {68.0f, 448.0f};
    techViewInitialized_ = true;
}

std::vector<UI::TechNodeRect> UI::computeTechNodes(sf::Vector2u winSize) const {
    ensureTechViewInitialized(winSize);
    sf::FloatRect canvas = computeTechCanvasRect(winSize);
    sf::Vector2f cCenter = canvas.getCenter();
    const float z = techZoom_;
    const float cw = std::round(TREE_CARD_W * z);
    const float ch = std::round(TREE_CARD_H * z);

    std::vector<TechNodeRect> out;
    out.reserve(Catalog::upgradeOrder().size());
    for (UpgradeId id : Catalog::upgradeOrder()) {
        const UpgradeDef& def = Catalog::getUpgrade(id);
        sf::Vector2f treeCenter{def.treeX * TREE_PITCH_X, def.treeY * TREE_PITCH_Y};
        sf::Vector2f scrCenter = cCenter + (treeCenter - techPan_) * z;
        float x = std::round(scrCenter.x - cw * 0.5f);
        float y = std::round(scrCenter.y - ch * 0.5f);
        sf::FloatRect cRect({x, y}, {cw, ch});

        float bw = std::round(std::clamp(38.0f * z, 26.0f, 48.0f));
        float bh = std::round(std::clamp(17.0f * z, 13.0f, 22.0f));
        sf::FloatRect bRect({x + cw - bw - 4.0f, y + ch - bh - 4.0f}, {bw, bh});
        out.push_back({id, cRect, bRect});
    }
    return out;
}

sf::FloatRect UI::computeDetailBuyRect(sf::Vector2u winSize) const {
    sf::FloatRect modal = computeUpgradeModalRect(winSize);
    return sf::FloatRect(
        {modal.position.x + modal.size.x - 170.0f, modal.position.y + modal.size.y - 76.0f},
        {148.0f, 44.0f}
    );
}

sf::FloatRect UI::computeGoalsRect() const {
    if (goalsCollapsed_) {
        return sf::FloatRect({12.0f, 66.0f}, {208.0f, 32.0f});
    }
    return sf::FloatRect({12.0f, 66.0f}, {310.0f, 206.0f});
}

bool UI::isPointerOverUI(sf::Vector2f mousePos, sf::Vector2u winSize, const World& /*world*/) const {
    if (upgradesOpen_) {
        return true;
    }
    if (mousePos.y <= 56.0f) {
        return true;
    }
    if (computeGoalsRect().contains(mousePos)) {
        return true;
    }
    if (computeStatusBannerRect(winSize).contains(mousePos)) {
        return true;
    }
    if (computeMinimapBoxRect(winSize).contains(mousePos)) {
        return true;
    }
    for (const auto& t : computeCategoryTabs(winSize)) {
        if (t.rect.contains(mousePos)) {
            return true;
        }
    }
    for (const auto& s : computeHotbarSlots(winSize)) {
        sf::FloatRect expanded({s.rect.position.x - 6.0f, s.rect.position.y - 8.0f}, {s.rect.size.x + 12.0f, s.rect.size.y + 20.0f});
        if (expanded.contains(mousePos)) {
            return true;
        }
    }
    return false;
}

UIAction UI::handleClick(
    sf::Vector2f mousePos,
    sf::Vector2u winSize,
    const World& world,
    const GameSettings& settings,
    std::optional<BuildingType> activeTool
) {
    if (upgradesOpen_) {
        sf::FloatRect modal = computeUpgradeModalRect(winSize);
        if (!modal.contains(mousePos)) {
            setUpgradesOpen(false);
            return {UIActionType::Consumed};
        }

        // Close [X]
        sf::FloatRect closeRect({modal.position.x + modal.size.x - 42.0f, modal.position.y + 11.0f}, {30.0f, 30.0f});
        if (closeRect.contains(mousePos)) {
            setUpgradesOpen(false);
            return {UIActionType::Consumed};
        }

        // 5 Modal Tab buttons
        const float tabPitch = std::min(220.0f, (modal.size.x - 246.0f) / 5.0f);
        const float tabW = tabPitch - 6.0f;
        for (int t = 0; t < 5; ++t) {
            sf::FloatRect tabRect({modal.position.x + 14.0f + static_cast<float>(t) * tabPitch, modal.position.y + 11.0f}, {tabW, 32.0f});
            if (tabRect.contains(mousePos)) {
                modalTab_ = t;
                if (t != 3) {
                    seedInputFocused_ = false;
                }
                return {UIActionType::Consumed};
            }
        }

        if (modalTab_ == 0) {
            ensureTechViewInitialized(winSize);
            sf::FloatRect zoomOutBtn({modal.position.x + modal.size.x - 210.0f, modal.position.y + 11.0f}, {28.0f, 30.0f});
            sf::FloatRect zoomInBtn({modal.position.x + modal.size.x - 126.0f, modal.position.y + 11.0f}, {28.0f, 30.0f});
            sf::FloatRect fitBtn({modal.position.x + modal.size.x - 94.0f, modal.position.y + 11.0f}, {46.0f, 30.0f});
            sf::FloatRect canvas = computeTechCanvasRect(winSize);

            if (zoomOutBtn.contains(mousePos)) {
                zoomTechTree(-1.0f, canvas.getCenter(), winSize);
                return {UIActionType::Consumed};
            }
            if (zoomInBtn.contains(mousePos)) {
                zoomTechTree(+1.0f, canvas.getCenter(), winSize);
                return {UIActionType::Consumed};
            }
            if (fitBtn.contains(mousePos)) {
                fitTechTree(winSize);
                return {UIActionType::Consumed};
            }

            sf::FloatRect detailBtn = computeDetailBuyRect(winSize);
            if (detailBtn.contains(mousePos)) {
                return {UIActionType::BuyUpgrade, BuildingType::SmallCog, activeCategory_, selectedUpgrade_};
            }

            const auto nodes = computeTechNodes(winSize);
            if (canvas.contains(mousePos)) {
                for (const auto& nr : nodes) {
                    if (nr.buyRect.contains(mousePos)) {
                        selectedUpgrade_ = nr.id;
                        return {UIActionType::BuyUpgrade, BuildingType::SmallCog, activeCategory_, nr.id};
                    }
                    if (nr.cardRect.contains(mousePos)) {
                        if (selectedUpgrade_ == nr.id && world.canBuyUpgrade(nr.id)) {
                            return {UIActionType::BuyUpgrade, BuildingType::SmallCog, activeCategory_, nr.id};
                        }
                        selectedUpgrade_ = nr.id;
                        return {UIActionType::Consumed};
                    }
                }
            }
            return {UIActionType::Consumed};
        } else if (modalTab_ == 1) {
            sf::FloatRect leftCard({modal.position.x + 20.0f, modal.position.y + 62.0f}, {modal.size.x * 0.49f - 28.0f, modal.size.y - 82.0f});
            sf::FloatRect launchBtn({leftCard.position.x + 24.0f, leftCard.position.y + leftCard.size.y - 68.0f}, {leftCard.size.x - 48.0f, 48.0f});
            if (launchBtn.contains(mousePos)) {
                return {UIActionType::CompleteSpirePhase};
            }

            sf::FloatRect rightCard({modal.position.x + modal.size.x * 0.49f + 8.0f, modal.position.y + 62.0f}, {modal.size.x * 0.51f - 28.0f, modal.size.y - 82.0f});
            for (int c = 0; c < 4; ++c) {
                float cy = rightCard.position.y + 186.0f + static_cast<float>(c) * 112.0f;
                sf::FloatRect redeemBtn({rightCard.position.x + rightCard.size.x - 156.0f, cy + 22.0f}, {132.0f, 42.0f});
                if (redeemBtn.contains(mousePos)) {
                    UIAction act;
                    act.type = UIActionType::SpendCoupon;
                    act.coupon = static_cast<CouponItem>(c);
                    return act;
                }
            }
        } else if (modalTab_ == 3) {
            WorldTabLayout L = computeWorldTabLayout(modal);
            seedInputFocused_ = L.seedBox.contains(mousePos);

            if (L.seedRandomBtn.contains(mousePos)) {
                static const char* PREFIXES[6] = {"COGWILL", "AETHER", "MESA", "TOKAMAK", "BASALT", "QUANTUM"};
                uint32_t r = hashWorldSeed(pendingWorldGen_.seedText) * 1664525u + 1013904223u + (++seedRandomCounter_ * 7919u);
                const char* pfx = PREFIXES[r % 6];
                int num = static_cast<int>((r >> 8) % 9000u + 1000u);
                pendingWorldGen_.seedText = std::string(pfx) + "-" + std::to_string(num);
                return {UIActionType::Consumed};
            }
            if (L.seedClearBtn.contains(mousePos)) {
                pendingWorldGen_.seedText.clear();
                seedInputFocused_ = true;
                return {UIActionType::Consumed};
            }
            for (int p = 0; p < 5; ++p) {
                if (L.presetBtns[p].contains(mousePos)) {
                    pendingWorldGen_.preset = static_cast<WorldPreset>(p);
                    return {UIActionType::Consumed};
                }
            }
            auto stepParam = [&](int idx, float dir) {
                if (idx == 0) pendingWorldGen_.oreRichness = std::clamp(pendingWorldGen_.oreRichness + dir * 0.25f, 0.50f, 3.00f);
                if (idx == 1) pendingWorldGen_.oreFrequency = std::clamp(pendingWorldGen_.oreFrequency + dir * 0.25f, 0.50f, 2.50f);
                if (idx == 2) pendingWorldGen_.waterCoverage = std::clamp(pendingWorldGen_.waterCoverage + dir * 0.25f, 0.00f, 2.00f);
                if (idx == 3) pendingWorldGen_.cliffCoverage = std::clamp(pendingWorldGen_.cliffCoverage + dir * 0.25f, 0.00f, 2.00f);
            };
            for (int s = 0; s < 4; ++s) {
                if (L.stepperMinus[s].contains(mousePos)) {
                    stepParam(s, -1.0f);
                    return {UIActionType::Consumed};
                }
                if (L.stepperPlus[s].contains(mousePos)) {
                    stepParam(s, +1.0f);
                    return {UIActionType::Consumed};
                }
            }
            if (L.newSandboxBtn.contains(mousePos)) {
                pendingNewWorldSandbox_ = !pendingNewWorldSandbox_;
                return {UIActionType::Consumed};
            }
            if (L.generateBtn.contains(mousePos)) {
                if (pendingWorldGen_.seedText.empty()) {
                    pendingWorldGen_.seedText = "COGWILL-25D";
                }
                UIAction act;
                act.type = UIActionType::GenerateWorld;
                act.worldGen = pendingWorldGen_;
                act.newWorldSandbox = pendingNewWorldSandbox_;
                setUpgradesOpen(false);
                return act;
            }

            if (L.creativeBtns[0].contains(mousePos)) {
                return {UIActionType::ToggleSandbox};
            }
            if (L.creativeBtns[1].contains(mousePos)) {
                return {UIActionType::SandboxUnlockAllTechs};
            }
            if (L.creativeBtns[2].contains(mousePos)) {
                return {UIActionType::SandboxGrantResources};
            }
            if (L.creativeBtns[3].contains(mousePos)) {
                return {UIActionType::SandboxCompleteSpire};
            }
            for (int m = 0; m < 4; ++m) {
                if (L.timeModeBtns[m].contains(mousePos)) {
                    UIAction act;
                    act.type = UIActionType::UpdateSettings;
                    act.settings = settings;
                    act.settings.lightingTimeMode = m;
                    return act;
                }
            }
        } else if (modalTab_ == 4) {
            SettingsTabLayout S = computeSettingsTabLayout(modal);
            GameSettings next = settings;
            bool changed = false;

            if (S.leftBtns[0].contains(mousePos)) { next.mode25D = !next.mode25D; changed = true; }
            else if (S.leftBtns[1].contains(mousePos)) { next.shadersEnabled = !next.shadersEnabled; changed = true; }
            else if (S.leftBtns[2].contains(mousePos)) { next.rtQuality = (next.rtQuality + 1) % 4; changed = true; }
            else if (S.leftBtns[3].contains(mousePos)) {
                if (next.extrusionDepth < 0.8f) next.extrusionDepth = 1.0f;
                else if (next.extrusionDepth < 1.2f) next.extrusionDepth = 1.4f;
                else next.extrusionDepth = 0.6f;
                changed = true;
            } else if (S.leftBtns[4].contains(mousePos)) {
                if (next.bloomStrength < 0.1f) next.bloomStrength = 0.45f;
                else if (next.bloomStrength < 0.65f) next.bloomStrength = 0.85f;
                else if (next.bloomStrength < 1.1f) next.bloomStrength = 1.30f;
                else next.bloomStrength = 0.0f;
                changed = true;
            } else if (S.leftBtns[5].contains(mousePos)) { next.ambientOcclusion = !next.ambientOcclusion; changed = true; }
            else if (S.leftBtns[6].contains(mousePos)) { next.waterReflections = !next.waterReflections; changed = true; }
            else if (S.leftBtns[7].contains(mousePos)) { next.postFxMode = (next.postFxMode + 1) % 3; changed = true; }

            if (S.rightBtns[0].contains(mousePos)) { next.pixelScale = (next.pixelScale % 4) + 1; changed = true; }
            else if (S.rightBtns[1].contains(mousePos)) { next.fullscreen = !next.fullscreen; changed = true; }
            else if (S.rightBtns[2].contains(mousePos)) { next.vsync = !next.vsync; changed = true; }
            else if (S.rightBtns[3].contains(mousePos)) {
                if (next.fpsLimit == 60) next.fpsLimit = 120;
                else if (next.fpsLimit == 120) next.fpsLimit = 144;
                else if (next.fpsLimit == 144) next.fpsLimit = 0;
                else next.fpsLimit = 60;
                changed = true;
            } else if (S.rightBtns[4].contains(mousePos)) { next.showFps = !next.showFps; changed = true; }
            else if (S.volMinus.contains(mousePos)) {
                next.masterVolume = std::clamp(std::round((next.masterVolume - 0.10f) * 10.0f) / 10.0f, 0.0f, 1.0f);
                next.muted = false;
                changed = true;
            } else if (S.volPlus.contains(mousePos)) {
                next.masterVolume = std::clamp(std::round((next.masterVolume + 0.10f) * 10.0f) / 10.0f, 0.0f, 1.0f);
                next.muted = false;
                changed = true;
            } else if (S.volCenter.contains(mousePos)) {
                next.muted = !next.muted;
                changed = true;
            } else if (S.rightBtns[6].contains(mousePos)) { next.particleDensity = (next.particleDensity + 1) % 3; changed = true; }
            else if (S.rightBtns[7].contains(mousePos)) { next.showPopups = !next.showPopups; changed = true; }
            else if (S.rightBtns[8].contains(mousePos)) { next.gridStyle = (next.gridStyle + 1) % 3; changed = true; }
            else if (S.rightBtns[9].contains(mousePos)) {
                if (next.autoSaveSec == 0) next.autoSaveSec = 60;
                else if (next.autoSaveSec == 60) next.autoSaveSec = 120;
                else if (next.autoSaveSec == 120) next.autoSaveSec = 300;
                else next.autoSaveSec = 0;
                changed = true;
            } else if (S.rightBtns[10].contains(mousePos)) {
                if (next.cameraSpeed < 0.8f) next.cameraSpeed = 1.0f;
                else if (next.cameraSpeed < 1.25f) next.cameraSpeed = 1.5f;
                else if (next.cameraSpeed < 1.75f) next.cameraSpeed = 2.0f;
                else next.cameraSpeed = 0.6f;
                changed = true;
            }

            if (changed) {
                UIAction act;
                act.type = UIActionType::UpdateSettings;
                act.settings = next;
                return act;
            }
            if (S.resetBtn.contains(mousePos)) {
                return {UIActionType::ResetSettings};
            }
            if (S.saveBtn.contains(mousePos)) {
                return {UIActionType::SaveGame};
            }
            if (S.loadBtn.contains(mousePos)) {
                return {UIActionType::LoadGame};
            }
        }

        return {UIActionType::Consumed};
    }

    // Top bar
    if (mousePos.y <= 56.0f) {
        for (const auto& btn : computeTopButtons(winSize)) {
            if (btn.rect.contains(mousePos)) {
                return {btn.action};
            }
        }
        return {UIActionType::Consumed};
    }

    // World / Solar / 2.5D RT Status Banner opens World & Sandbox studio
    if (computeStatusBannerRect(winSize).contains(mousePos)) {
        return {UIActionType::OpenWorldTab};
    }

    // Goals collapse header
    sf::FloatRect goalsRect = computeGoalsRect();
    if (goalsRect.contains(mousePos)) {
        if (mousePos.y <= goalsRect.position.y + 32.0f) {
            return {UIActionType::ToggleGoals};
        }
        return {UIActionType::Consumed};
    }

    // Minimap
    sf::FloatRect mmBox = computeMinimapBoxRect(winSize);
    if (mmBox.contains(mousePos)) {
        if (mousePos.y <= mmBox.position.y + 24.0f) {
            minimapCollapsed_ = !minimapCollapsed_;
            return {UIActionType::Consumed};
        }
        if (!minimapCollapsed_ && computeMinimapImageRect(winSize).contains(mousePos)) {
            UIAction act;
            act.type = UIActionType::MinimapJump;
            act.worldPos = minimapPosToWorld(mousePos, winSize);
            return act;
        }
        return {UIActionType::Consumed};
    }

    // Category tabs F1..F7
    for (const auto& tab : computeCategoryTabs(winSize)) {
        if (tab.rect.contains(mousePos)) {
            activeCategory_ = tab.category;
            UIAction act;
            act.type = UIActionType::SelectCategory;
            act.category = tab.category;
            return act;
        }
    }

    // Hotbar 1..7
    for (const auto& slot : computeHotbarSlots(winSize)) {
        if (slot.rect.contains(mousePos)) {
            if (activeTool.has_value() && *activeTool == slot.type) {
                return {UIActionType::ClearTool};
            }
            if (!world.isBuildingUnlocked(slot.type)) {
                setToast(Catalog::buildingUnlockHint(slot.type), true);
                return {UIActionType::Consumed};
            }
            return {UIActionType::SelectBuilding, slot.type};
        }
    }

    return {UIActionType::None};
}

void UI::updateMinimapTexture(const World& world) const {
    sf::Image img({static_cast<unsigned int>(GRID_W), static_cast<unsigned int>(GRID_H)}, sf::Color(20, 26, 36));
    for (int y = 0; y < GRID_H; ++y) {
        for (int x = 0; x < GRID_W; ++x) {
            const Tile& t = world.tile(x, y);
            sf::Color col;
            switch (t.biome) {
            case 1:  col = {42, 30, 26}; break;
            case 2:  col = {22, 36, 34}; break;
            case 3:  col = {26, 22, 36}; break;
            case 4:  col = {34, 42, 52}; break;
            default: col = {22, 27, 37}; break;
            }
            if (world.isInDirectHubZone(x, y)) {
                col.r = static_cast<uint8_t>(std::min(255, col.r + 12));
                col.g = static_cast<uint8_t>(std::min(255, col.g + 14));
                col.b = static_cast<uint8_t>(std::min(255, col.b + 18));
            }
            if (t.terrain == TerrainType::Water) {
                col = {26, 94, 158};
            } else if (t.terrain == TerrainType::Rock) {
                col = {62, 68, 84};
            } else if (t.ore != OreType::None) {
                switch (t.ore) {
                case OreType::Iron:     col = {110, 146, 195}; break;
                case OreType::Copper:   col = {215, 118, 56}; break;
                case OreType::Coal:     col = {68, 72, 84}; break;
                case OreType::Titanium: col = {152, 124, 235}; break;
                case OreType::Uranium:  col = {78, 224, 72}; break;
                case OreType::Aether:   col = {56, 212, 240}; break;
                default: break;
                }
            }
            if (t.occupantId > 0) {
                if (const BuildingEntity* b = world.buildingById(t.occupantId)) {
                    if (b->type == BuildingType::HubCore) {
                        col = {255, 224, 110};
                    } else if (b->type == BuildingType::RailTrack) {
                        col = {175, 185, 200};
                    } else if (World::isBeltLike(b->type)) {
                        col = {196, 170, 86};
                    } else if (b->type == BuildingType::NuclearReactor || b->type == BuildingType::FusionReactor) {
                        col = {115, 255, 175};
                    } else {
                        col = {225, 196, 108};
                    }
                }
            }
            img.setPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)}, col);
        }
    }

    // Active Trains
    for (const TrainEntity& tr : world.trains()) {
        int gx = std::clamp(static_cast<int>(tr.wx / CELL_SIZE), 0, GRID_W - 1);
        int gy = std::clamp(static_cast<int>(tr.wy / CELL_SIZE), 0, GRID_H - 1);
        sf::Color tCol = tr.isElectric ? sf::Color(90, 210, 255) : sf::Color(255, 150, 75);
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                img.setPixel({static_cast<unsigned int>(std::clamp(gx + dx, 0, GRID_W - 1)),
                              static_cast<unsigned int>(std::clamp(gy + dy, 0, GRID_H - 1))}, tCol);
            }
        }
    }

    minimapTex_.update(img);
}

void UI::render(
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
    bool hasBlueprint
) const {
    const sf::Vector2u winSize = target.getSize();
    const float W = static_cast<float>(winSize.x);
    const float H = static_cast<float>(winSize.y);

    // 1. Top resource & control bar (9 resources + 8 quick-command buttons)
    drawPanel(target, sf::FloatRect({0.0f, 0.0f}, {W, 56.0f}), sf::Color(15, 19, 27, 244), sf::Color(72, 84, 104), 1.5f);

    sf::Text brand(font, "COGWILL", 16);
    brand.setStyle(sf::Text::Bold);
    brand.setFillColor(sf::Color(238, 194, 96));
    brand.setPosition({10.0f, 17.0f});
    target.draw(brand);

    float rx = 88.0f;
    for (int i = 0; i < static_cast<int>(ResourceType::Count); ++i) {
        ResourceType rt = static_cast<ResourceType>(i);
        bool isCrafted = (i >= static_cast<int>(ResourceType::Steel));
        sf::FloatRect pill({rx, 9.0f}, {82.0f, 38.0f});
        drawPanel(target, pill,
                  isCrafted ? sf::Color(28, 28, 44, 238) : sf::Color(22, 28, 40, 235),
                  isCrafted ? sf::Color(92, 84, 128) : sf::Color(58, 70, 90), 1.2f);

        sf::RectangleShape dot({6.0f, 6.0f});
        dot.setOrigin({3.0f, 3.0f});
        dot.setPosition({rx + 9.0f, 17.0f});
        dot.setFillColor(resourceColor(rt));
        target.draw(dot);

        sf::Text nameTxt(font, resourceName(rt), 10);
        nameTxt.setFillColor(sf::Color(162, 176, 198));
        nameTxt.setPosition({rx + 16.0f, 10.0f});
        target.draw(nameTxt);

        std::string amt = fmtCompact(world.resources()[rt]);
        std::string rate = fmtRate(world.stats().ratePerSec[static_cast<std::size_t>(i)]);
        sf::Text valTxt(font, amt + (rate.empty() ? "" : " " + rate), 11);
        valTxt.setStyle(sf::Text::Bold);
        valTxt.setFillColor(resourceColor(rt));
        valTxt.setPosition({rx + 7.0f, 26.0f});
        target.draw(valTxt);

        rx += 85.0f;
    }

    int affordableCount = 0;
    for (UpgradeId id : Catalog::upgradeOrder()) {
        if (world.canBuyUpgrade(id)) {
            ++affordableCount;
        }
    }
    bool spireReady = world.canCompleteSpirePhase();

    for (const auto& btn : computeTopButtons(winSize)) {
        bool hovered = btn.rect.contains(mousePos);
        sf::Color fill = hovered ? sf::Color(48, 58, 78) : sf::Color(30, 38, 52);
        sf::Color outline = hovered ? sf::Color(235, 195, 98) : sf::Color(82, 96, 122);
        std::string label;
        if (btn.action == UIActionType::ToggleUpgrades) {
            label = "Tech [U]";
            if (affordableCount > 0) {
                fill = hovered ? sf::Color(72, 62, 34) : sf::Color(54, 46, 24);
                outline = sf::Color(246, 202, 88);
            }
        } else if (btn.action == UIActionType::OpenSpireTab) {
            int ph = world.spirePhase();
            label = (ph < 5) ? ("Spire P" + std::to_string(ph + 1) + " [V]") : "Spire+ [V]";
            if (spireReady || world.coupons() > 0) {
                fill = hovered ? sf::Color(68, 38, 78) : sf::Color(48, 26, 58);
                outline = sf::Color(224, 118, 255);
            }
        } else if (btn.action == UIActionType::ToggleMode25D) {
            label = settings.mode25D ? "2.5D [F9]" : "2D [F9]";
            if (settings.mode25D) {
                fill = hovered ? sf::Color(34, 72, 92) : sf::Color(24, 52, 68);
                outline = sf::Color(96, 228, 255);
            }
        } else if (btn.action == UIActionType::ToggleSandbox) {
            label = world.isSandboxMode() ? "Sndbx ON" : "Sndbx [B]";
            if (world.isSandboxMode()) {
                fill = hovered ? sf::Color(36, 82, 56) : sf::Color(24, 60, 40);
                outline = sf::Color(112, 245, 168);
            }
        } else if (btn.action == UIActionType::OpenWorldTab) {
            label = "World [N]";
        } else if (btn.action == UIActionType::OpenSettingsTab) {
            label = "Cfg/RT [O]";
        } else if (btn.action == UIActionType::ToggleSpeed) {
            if (simSpeed < 0.05f) label = "||";
            else if (simSpeed > 3.0f) label = "4x";
            else if (simSpeed > 1.5f) label = "2x";
            else label = "1x";
        } else if (btn.action == UIActionType::SaveGame) {
            label = "Save";
        }

        drawPanel(target, btn.rect, fill, outline, 1.4f);
        drawCenteredText(target, font, label, 10, btn.rect.getCenter(), sf::Color(236, 240, 248), true);

        if (btn.action == UIActionType::ToggleUpgrades && affordableCount > 0) {
            sf::RectangleShape badge({16.0f, 16.0f});
            badge.setOrigin({8.0f, 8.0f});
            badge.setPosition({btn.rect.position.x + btn.rect.size.x - 4.0f, btn.rect.position.y + 4.0f});
            badge.setFillColor(sf::Color(242, 184, 58));
            target.draw(badge);
            drawCenteredText(target, font, std::to_string(affordableCount), 9, badge.getPosition(), sf::Color(18, 22, 28), true);
        }
        if (btn.action == UIActionType::OpenSpireTab && (spireReady || world.coupons() > 0)) {
            sf::RectangleShape badge({16.0f, 16.0f});
            badge.setOrigin({8.0f, 8.0f});
            badge.setPosition({btn.rect.position.x + btn.rect.size.x - 4.0f, btn.rect.position.y + 4.0f});
            badge.setFillColor(spireReady ? sf::Color(102, 245, 178) : sf::Color(225, 118, 255));
            target.draw(badge);
            drawCenteredText(target, font, spireReady ? "!" : std::to_string(world.coupons()), 9, badge.getPosition(), sf::Color(18, 22, 28), true);
        }
    }

    // 2. Top-left Workshop Goals card
    {
        sf::FloatRect gRect = computeGoalsRect();
        drawPanel(target, gRect, sf::Color(18, 22, 32, 225), sf::Color(68, 80, 102), 1.3f);
        sf::Text gHdr(font, goalsCollapsed_ ? "Workshop Goals [G] (+)" : "Workshop Goals [G] (-)", 13);
        gHdr.setStyle(sf::Text::Bold);
        gHdr.setFillColor(sf::Color(236, 194, 96));
        gHdr.setPosition({gRect.position.x + 12.0f, gRect.position.y + 8.0f});
        target.draw(gHdr);

        if (!goalsCollapsed_) {
            bool hasDrillAndBelt = (world.stats().drillCount > 0 && world.stats().beltCount > 0);
            bool hasSteel = world.stats().totalMined[static_cast<std::size_t>(ResourceType::Steel)] >= 1.0;
            bool hasCircuits = world.stats().totalMined[static_cast<std::size_t>(ResourceType::Circuit)] >= 1.0;
            bool hasGrid = world.stats().electricCount >= 2;
            bool hasTiOrTrain = (world.stats().totalMined[static_cast<std::size_t>(ResourceType::Titanium)] >= 1.0 || !world.trains().empty());
            bool hasBigPower = false;
            for (const auto& b : world.buildings()) {
                if ((b.type == BuildingType::NuclearReactor || b.type == BuildingType::FusionReactor) && b.hasFuel) {
                    hasBigPower = true;
                    break;
                }
            }
            bool hasSpireOrQC = (world.spirePhase() >= 1 || world.stats().totalMined[static_cast<std::size_t>(ResourceType::QuantumCore)] >= 1.0);

            struct GoalItem { std::string text; bool done; };
            std::vector<GoalItem> goals = {
                {"1. Power Drill & Belt to Core [F3/F5]", hasDrillAndBelt},
                {"2. Smelt Steel in Stone Smelter [F4]", hasSteel},
                {"3. Craft Electronic Circuits [F4]", hasCircuits},
                {"4. Wire Power Grid / Solar [F6/F2]", hasGrid},
                {"5. Mine Titanium & Run Trains [F7]", hasTiOrTrain},
                {"6. Run Nuclear / Fusion Reactor [F6]", hasBigPower},
                {"7. Launch Grand Spire Phase [V]", hasSpireOrQC}
            };
            float gy = gRect.position.y + 33.0f;
            for (const auto& g : goals) {
                sf::Text line(font, (g.done ? "[x] " : "[ ] ") + g.text, 11);
                line.setFillColor(g.done ? sf::Color(110, 224, 152) : sf::Color(202, 212, 228));
                line.setPosition({gRect.position.x + 12.0f, gy});
                target.draw(line);
                gy += 24.0f;
            }
        }
    }

    // 3. Top-right World Seed, Solar & 2.5D RT Status Banner + Controls Cheat Sheet
    {
        sf::FloatRect sRect = computeStatusBannerRect(winSize);
        bool sbx = world.isSandboxMode();
        sf::Color sFill = sbx ? sf::Color(18, 36, 30, 232) : sf::Color(18, 24, 36, 228);
        sf::Color sOut = sRect.contains(mousePos)
            ? sf::Color(235, 195, 98)
            : (sbx ? sf::Color(92, 224, 158) : sf::Color(72, 96, 128));
        drawPanel(target, sRect, sFill, sOut, 1.4f);

        std::string seedShort = world.worldGen().seedText;
        if (seedShort.size() > 16) {
            seedShort = seedShort.substr(0, 16) + "..";
        }
        std::string line1 = "Seed: " + seedShort + (sbx ? "  [SANDBOX]" : "");
        sf::Text tx1(font, line1, 11);
        tx1.setStyle(sf::Text::Bold);
        tx1.setFillColor(sbx ? sf::Color(122, 248, 178) : sf::Color(246, 206, 102));
        tx1.setPosition({sRect.position.x + 10.0f, sRect.position.y + 6.0f});
        target.draw(tx1);

        int sunPct = static_cast<int>(std::round(world.solarFactor() * 100.0f));
        std::string line2 = std::string(worldPresetName(world.worldGen().preset)) + "  |  Solar " + std::to_string(sunPct) + "%";
        sf::Text tx2(font, line2, 10);
        tx2.setFillColor(sf::Color(168, 218, 248));
        tx2.setPosition({sRect.position.x + 10.0f, sRect.position.y + 26.0f});
        target.draw(tx2);

        bool rtActive = settings.shadersEnabled && renderer.isShaderAvailable();
        std::string line3 = std::string(settings.mode25D ? "2.5D Extruded" : "Classic 2D")
                          + " | RT: " + (rtActive ? rtQualityShort(settings.rtQuality) : "Off");
        if (settings.showFps) {
            line3 += " | " + std::to_string(static_cast<int>(std::round(currentFps))) + " FPS";
        }
        sf::Text tx3(font, line3, 10);
        tx3.setStyle(sf::Text::Bold);
        tx3.setFillColor(settings.mode25D ? sf::Color(115, 236, 255) : sf::Color(196, 208, 228));
        tx3.setPosition({sRect.position.x + 10.0f, sRect.position.y + 44.0f});
        target.draw(tx3);

        sf::FloatRect cRect({W - 274.0f, 138.0f}, {262.0f, 102.0f});
        drawPanel(target, cRect, sf::Color(18, 22, 32, 198), sf::Color(58, 68, 86), 1.2f);
        const char* lines[5] = {
            "LMB: Build / Drag Belts & Poles",
            "RMB: Demolish   Q: Pipette   F: Feed",
            "F1-F7: Tabs   1-7: Tool   R: Rotate",
            "F9: 2.5D   F10: RT   B: Sandbox",
            "U: Tech  V: Spire  N: World  O: Cfg"
        };
        for (int i = 0; i < 5; ++i) {
            sf::Text t(font, lines[i], 10);
            t.setFillColor(sf::Color(175, 188, 210));
            t.setPosition({cRect.position.x + 10.0f, cRect.position.y + 8.0f + static_cast<float>(i) * 18.0f});
            target.draw(t);
        }
    }

    // 4. Bottom-right 192x144 Pixel Minimap
    if (!upgradesOpen_) {
        sf::FloatRect mmBox = computeMinimapBoxRect(winSize);
        drawPanel(target, mmBox, sf::Color(16, 20, 28, 236), sf::Color(86, 102, 128), 1.5f);
        sf::Text mmTitle(font, minimapCollapsed_ ? "MINIMAP [M] (+)" : "MINIMAP [M] (192x144)", 11);
        mmTitle.setStyle(sf::Text::Bold);
        mmTitle.setFillColor(sf::Color(236, 194, 96));
        mmTitle.setPosition({mmBox.position.x + 8.0f, mmBox.position.y + 5.0f});
        target.draw(mmTitle);

        if (!minimapCollapsed_) {
            if (!minimapInitialized_ || minimapTimer_ >= 0.18f) {
                updateMinimapTexture(world);
                minimapTimer_ = 0.0f;
                minimapInitialized_ = true;
            }
            sf::FloatRect imgRect = computeMinimapImageRect(winSize);
            sf::Sprite mmSp(minimapTex_);
            mmSp.setPosition(imgRect.position);
            mmSp.setScale({imgRect.size.x / static_cast<float>(GRID_W), imgRect.size.y / static_cast<float>(GRID_H)});
            target.draw(mmSp);

            sf::Vector2f vc = worldView.getCenter();
            sf::Vector2f vs = worldView.getSize();
            float scaleX = imgRect.size.x / (static_cast<float>(GRID_W) * CELL_SIZE);
            float scaleY = imgRect.size.y / (static_cast<float>(GRID_H) * CELL_SIZE);
            float vx = imgRect.position.x + (vc.x - vs.x * 0.5f) * scaleX;
            float vy = imgRect.position.y + (vc.y - vs.y * 0.5f) * scaleY;
            float vw = std::min(imgRect.size.x, vs.x * scaleX);
            float vh = std::min(imgRect.size.y, vs.y * scaleY);
            sf::RectangleShape viewBox({std::max(4.f, vw), std::max(4.f, vh)});
            viewBox.setPosition({
                std::clamp(vx, imgRect.position.x, imgRect.position.x + imgRect.size.x - 4.f),
                std::clamp(vy, imgRect.position.y, imgRect.position.y + imgRect.size.y - 4.f)
            });
            viewBox.setFillColor(sf::Color::Transparent);
            viewBox.setOutlineColor(sf::Color(250, 224, 120));
            viewBox.setOutlineThickness(1.5f);
            target.draw(viewBox);
        }
    }

    // 5. Bottom F1..F7 Category Tabs & 1..7 Build Hotbar
    for (const auto& tab : computeCategoryTabs(winSize)) {
        bool active = (tab.category == activeCategory_);
        bool hov = !upgradesOpen_ && tab.rect.contains(mousePos);
        sf::Color fill = active ? sf::Color(52, 62, 84) : (hov ? sf::Color(36, 44, 60) : sf::Color(22, 26, 36, 235));
        sf::Color out = active ? sf::Color(242, 200, 92) : sf::Color(68, 80, 102);
        drawPanel(target, tab.rect, fill, out, active ? 1.8f : 1.2f);
        drawCenteredText(target, font, tab.label, 11, tab.rect.getCenter(),
                         active ? sf::Color(250, 232, 165) : sf::Color(175, 190, 212), active);
    }

    const auto slots = computeHotbarSlots(winSize);
    if (!slots.empty()) {
        sf::FloatRect barBg(
            {slots.front().rect.position.x - 10.0f, slots.front().rect.position.y - 8.0f},
            {slots.back().rect.position.x + slots.back().rect.size.x - slots.front().rect.position.x + 20.0f, 90.0f}
        );
        drawPanel(target, barBg, sf::Color(15, 19, 27, 240), sf::Color(70, 82, 102), 1.5f);
    }

    std::optional<BuildingType> hoveredHotbar;
    sf::FloatRect hoveredSlotRect;

    for (const auto& s : slots) {
        const BuildingDef& def = Catalog::getBuilding(s.type);
        bool unlocked = world.isBuildingUnlocked(s.type);
        bool affordable = world.isSandboxMode() || world.resources().canAfford(def.cost);
        bool selected = activeTool.has_value() && *activeTool == s.type && !hasBlueprint;
        bool hovered = !upgradesOpen_ && s.rect.contains(mousePos);
        if (hovered) {
            hoveredHotbar = s.type;
            hoveredSlotRect = s.rect;
        }

        sf::Color fill(26, 32, 44);
        sf::Color outline(66, 78, 98);
        if (selected) {
            fill = sf::Color(58, 52, 28);
            outline = sf::Color(246, 204, 92);
        } else if (hovered) {
            fill = sf::Color(36, 44, 60);
            outline = sf::Color(145, 175, 220);
        }
        if (!unlocked) {
            fill = sf::Color(20, 23, 30);
            outline = sf::Color(48, 54, 66);
        }

        drawPanel(target, s.rect, fill, outline, selected ? 2.2f : 1.3f);

        sf::Text keyTxt(font, s.keyLabel, 11);
        keyTxt.setStyle(sf::Text::Bold);
        keyTxt.setFillColor(sf::Color(150, 165, 190));
        keyTxt.setPosition({s.rect.position.x + 5.0f, s.rect.position.y + 3.0f});
        target.draw(keyTxt);

        sf::Vector2f iconCenter{s.rect.position.x + s.rect.size.x * 0.5f, s.rect.position.y + 29.0f};
        renderer.drawBuildingPreviewIcon(target, s.type, iconCenter, 0.52f, unlocked ? animTime * 0.8f : 0.0f);

        sf::Color lblCol = !unlocked ? sf::Color(100, 108, 122) : (affordable ? sf::Color(225, 232, 244) : sf::Color(235, 120, 115));
        drawCenteredText(target, font, def.shortLabel, 11, {iconCenter.x, s.rect.position.y + 52.0f}, lblCol, true);

        std::string sub = !unlocked ? "LOCKED" : (world.isSandboxMode() ? "FREE (SANDBOX)" : formatCostHotbar(def.cost));
        sf::Color subCol = !unlocked ? sf::Color(180, 130, 75) : (affordable ? sf::Color(150, 210, 165) : sf::Color(228, 102, 98));
        drawCenteredText(target, font, sub, 9, {iconCenter.x, s.rect.position.y + 65.0f}, subCol, false);
    }

    // Hover tooltip above Hotbar
    if (hoveredHotbar.has_value()) {
        const BuildingDef& def = Catalog::getBuilding(*hoveredHotbar);
        bool unlocked = world.isBuildingUnlocked(*hoveredHotbar);
        float tw = 450.0f;
        float th = unlocked ? 76.0f : 94.0f;
        float tx = std::clamp(hoveredSlotRect.position.x + hoveredSlotRect.size.x * 0.5f - tw * 0.5f, 12.0f, W - tw - 12.0f);
        float ty = hoveredSlotRect.position.y - th - 38.0f;
        drawPanel(target, sf::FloatRect({tx, ty}, {tw, th}), sf::Color(16, 20, 28, 248), sf::Color(226, 184, 88), 1.5f);

        std::string costTxt = world.isSandboxMode() ? "FREE in Sandbox" : formatCostFull(def.cost);
        sf::Text title(font, def.name + "   [" + costTxt + "]", 12);
        title.setStyle(sf::Text::Bold);
        title.setFillColor(sf::Color(246, 206, 102));
        title.setPosition({tx + 12.0f, ty + 8.0f});
        target.draw(title);

        sf::Text desc(font, def.description, 11);
        desc.setFillColor(sf::Color(210, 220, 235));
        desc.setPosition({tx + 12.0f, ty + 32.0f});
        target.draw(desc);

        if (!unlocked) {
            sf::Text hint(font, Catalog::buildingUnlockHint(*hoveredHotbar), 11);
            hint.setStyle(sf::Text::Bold);
            hint.setFillColor(sf::Color(245, 170, 82));
            hint.setPosition({tx + 12.0f, ty + 68.0f});
            target.draw(hint);
        }
    }

    // 6. Bottom-left Contextual Inspector Card
    if (!upgradesOpen_) {
        const TrainEntity* hoverTrain = nullptr;
        if (!isPointerOverUI(mousePos, winSize, world)) {
            for (const TrainEntity& tr : world.trains()) {
                if (std::hypot(tr.wx - hoverWorldPos.x, tr.wy - hoverWorldPos.y) <= 32.0f) {
                    hoverTrain = &tr;
                    break;
                }
            }
        }

        const BuildingEntity* inspectB = world.buildingAt(hoverGx, hoverGy);
        if (!inspectB && world.inBounds(selectedGx, selectedGy)) {
            inspectB = world.buildingAt(selectedGx, selectedGy);
        }

        if (activeTool.has_value() && !hasBlueprint && !ghostCheck.valid && world.inBounds(hoverGx, hoverGy) && !isPointerOverUI(mousePos, winSize, world)) {
            sf::FloatRect iRect({12.0f, H - 170.0f}, {340.0f, 64.0f});
            drawPanel(target, iRect, sf::Color(38, 18, 22, 235), sf::Color(235, 88, 82), 1.6f);
            sf::Text t1(font, "Cannot Place " + Catalog::getBuilding(*activeTool).name, 13);
            t1.setStyle(sf::Text::Bold);
            t1.setFillColor(sf::Color(255, 145, 138));
            t1.setPosition({iRect.position.x + 12.0f, iRect.position.y + 10.0f});
            target.draw(t1);
            sf::Text t2(font, ghostCheck.reason, 12);
            t2.setFillColor(sf::Color(236, 210, 210));
            t2.setPosition({iRect.position.x + 12.0f, iRect.position.y + 34.0f});
            target.draw(t2);
        } else if (hoverTrain) {
            sf::FloatRect iRect({12.0f, H - 186.0f}, {336.0f, 110.0f});
            drawPanel(target, iRect, sf::Color(16, 20, 28, 235), sf::Color(92, 186, 245), 1.5f);
            sf::Text tName(font, hoverTrain->isElectric ? "Electric Express Train" : "Steam Freight Locomotive", 14);
            tName.setStyle(sf::Text::Bold);
            tName.setFillColor(sf::Color(242, 206, 108));
            tName.setPosition({iRect.position.x + 12.0f, iRect.position.y + 10.0f});
            target.draw(tName);

            const char* stateLabel = "Idle (Waiting for LOAD/UNLOAD stations)";
            switch (hoverTrain->state) {
            case TrainState::MovingToLoad:   stateLabel = "En route -> LOAD Station"; break;
            case TrainState::Loading:        stateLabel = "Loading cargo at Outpost..."; break;
            case TrainState::MovingToUnload: stateLabel = "Hauling freight -> Hub UNLOAD"; break;
            case TrainState::Unloading:      stateLabel = "Unloading freight to Hub..."; break;
            default: break;
            }
            sf::Text stTxt(font, hoverTrain->hasFuel ? stateLabel : (hoverTrain->isElectric ? "NO GRID POWER AT STATIONS" : "OUT OF COAL"), 12);
            stTxt.setStyle(sf::Text::Bold);
            stTxt.setFillColor(hoverTrain->hasFuel ? sf::Color(95, 232, 152) : sf::Color(245, 96, 88));
            stTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 34.0f});
            target.draw(stTxt);

            sf::Text cargoTxt(font, "Cargo: " + fmtInt(hoverTrain->cargo.totalAmount()) + " / " + fmtInt(world.trainCargoCapacity()), 12);
            cargoTxt.setFillColor(sf::Color(210, 224, 240));
            cargoTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 58.0f});
            target.draw(cargoTxt);

            sf::Text hintTxt(font, "RMB to recall locomotive & refund cargo", 11);
            hintTxt.setFillColor(sf::Color(150, 168, 192));
            hintTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 82.0f});
            target.draw(hintTxt);
        } else if (inspectB) {
            const BuildingDef& def = Catalog::getBuilding(inspectB->type);
            const NetworkInfo* net = world.networkById(inspectB->networkId);
            const ElectricNetworkInfo* enet = world.electricNetworkById(inspectB->electricNetId);
            bool hasDiag = net && !net->diagnostic.empty()
                        && (net->status == NetworkStatus::Jammed || net->status == NetworkStatus::Overloaded);
            float cardH = hasDiag ? 184.0f : 162.0f;
            sf::FloatRect iRect({12.0f, H - 24.0f - cardH}, {342.0f, cardH});
            drawPanel(target, iRect, sf::Color(16, 20, 28, 238), sf::Color(76, 90, 114), 1.5f);

            std::string titleStr = def.name + "  (" + std::to_string(inspectB->gx) + ", " + std::to_string(inspectB->gy) + ")";
            sf::Text bName(font, titleStr, 13);
            bName.setStyle(sf::Text::Bold);
            bName.setFillColor(sf::Color(242, 206, 108));
            bName.setPosition({iRect.position.x + 12.0f, iRect.position.y + 10.0f});
            target.draw(bName);

            std::string stStr = "ACTIVE";
            sf::Color stCol(95, 232, 152);
            if (inspectB->type == BuildingType::HubCore) {
                stStr = "GRAND CLOCKWORK CORE  (Spire Phase " + std::to_string(world.spirePhase()) + "/5)";
                stCol = sf::Color(250, 215, 102);
            } else if (World::isBeltLike(inspectB->type)) {
                stStr = "BELT SPEED: " + fmtFloat1(def.beltSpeed * world.beltSpeedMultiplier()) + " items/s  [R Rotate]";
                stCol = sf::Color(115, 235, 255);
            } else if (inspectB->type == BuildingType::StorageVault || inspectB->type == BuildingType::MatterSink) {
                stStr = "OPERATIONAL";
            } else if (inspectB->type == BuildingType::Assembler || inspectB->type == BuildingType::WaterPump) {
                bool on = inspectB->electricPowered || inspectB->status == NetworkStatus::Running || inspectB->status == NetworkStatus::Buffering;
                stStr = on ? (inspectB->electricPowered ? "HYBRID: GRID POWERED" : "HYBRID: COG DRIVEN") : "NEEDS COGS OR POWER POLE";
                stCol = on ? sf::Color(96, 240, 175) : sf::Color(245, 174, 62);
            } else if (!def.isMechanical) {
                if (inspectB->type == BuildingType::RailTrack) {
                    stStr = "STEEL RAIL TRACK";
                    stCol = sf::Color(180, 196, 218);
                } else if (inspectB->type == BuildingType::TrainStation) {
                    stStr = (inspectB->dir % 2 == 1) ? "UNLOAD STATION (Hub) [R Toggle]" : "LOAD STATION (Outpost) [R Toggle]";
                    stCol = (inspectB->dir % 2 == 1) ? sf::Color(92, 232, 162) : sf::Color(246, 174, 72);
                } else if (def.fuelType != ResourceType::Count && !inspectB->hasFuel) {
                    stStr = "NO FUEL (" + resourceName(def.fuelType) + ")";
                    stCol = sf::Color(245, 98, 92);
                } else if (enet && enet->overloaded) {
                    stStr = "GRID OVERLOADED";
                    stCol = sf::Color(248, 174, 62);
                } else if (inspectB->electricPowered) {
                    stStr = "GRID POWERED";
                    stCol = sf::Color(96, 236, 255);
                } else {
                    stStr = "CONNECT POWER POLE";
                    stCol = sf::Color(245, 174, 62);
                }
            } else {
                if (def.fuelType != ResourceType::Count && !inspectB->hasFuel) {
                    stStr = "NO FUEL (" + resourceName(def.fuelType) + ")";
                    stCol = sf::Color(245, 98, 92);
                } else if (inspectB->status == NetworkStatus::Running) {
                    stStr = "RUNNING";
                } else if (inspectB->status == NetworkStatus::Buffering) {
                    stStr = "FLYWHEEL BUFFER";
                    stCol = sf::Color(96, 234, 255);
                } else if (inspectB->status == NetworkStatus::Overloaded) {
                    stStr = "OVERLOADED";
                    stCol = sf::Color(248, 174, 62);
                } else if (inspectB->status == NetworkStatus::Jammed) {
                    stStr = "JAMMED";
                    stCol = sf::Color(245, 86, 82);
                } else {
                    stStr = "UNPOWERED";
                    stCol = sf::Color(150, 160, 175);
                }
            }

            sf::Text stTxt(font, stStr, 11);
            stTxt.setStyle(sf::Text::Bold);
            stTxt.setFillColor(stCol);
            stTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 32.0f});
            target.draw(stTxt);

            std::string line2;
            if (inspectB->type == BuildingType::StoneSmelter) {
                line2 = "2 Fe + 1 Co -> 1 St | In: " + fmtInt(inspectB->localStorage[ResourceType::Iron]) + "Fe " + fmtInt(inspectB->localStorage[ResourceType::Coal]) + "Co";
            } else if (inspectB->type == BuildingType::ArcFurnace) {
                line2 = "2 Fe -> 1 Steel | In: " + fmtInt(inspectB->localStorage[ResourceType::Iron]) + "Fe  Out: " + std::to_string(inspectB->craftedCount);
            } else if (inspectB->type == BuildingType::Assembler) {
                line2 = "2 Cu + 1 Fe -> 1 Circuit | In: " + fmtInt(inspectB->localStorage[ResourceType::Copper]) + "Cu " + fmtInt(inspectB->localStorage[ResourceType::Iron]) + "Fe";
            } else if (inspectB->type == BuildingType::QuantumForge) {
                line2 = "2St+2Ci+1Ti+1Ae -> 1 Q-Core | Out: " + std::to_string(inspectB->craftedCount);
            } else if (World::isBeltLike(inspectB->type)) {
                line2 = "Slot A: " + resourceName(inspectB->beltItemA) + "   |   Slot B: " + resourceName(inspectB->beltItemB);
            } else if (inspectB->type == BuildingType::StorageVault) {
                line2 = "Stored: " + fmtInt(inspectB->localStorage.totalAmount()) + " / " + fmtInt(world.vaultBufferCapacity()) + " items";
            } else if (inspectB->type == BuildingType::SolarPanel) {
                float peak = def.baseElectricKW * world.solarPowerMultiplier() * world.superconductingMult();
                line2 = "Solar Output: " + fmtInt(peak * world.solarFactor()) + " / " + fmtInt(peak) + " kW";
            } else if (inspectB->type == BuildingType::Accumulator) {
                line2 = "Battery Charge: " + fmtInt(inspectB->batteryCharge * 100.0f) + "%  (" + fmtInt(world.accumulatorCapacityKJ()) + " kJ cap)";
            } else if (inspectB->type == BuildingType::NuclearReactor) {
                float outKW = inspectB->hasFuel
                    ? (def.baseElectricKW + 300.0f * world.coolingTowerBonusMultiplier() * static_cast<float>(inspectB->adjacentCoolers)) * world.nuclearPowerMultiplier() * world.superconductingMult()
                    : 0.0f;
                line2 = "Output: " + fmtInt(outKW) + " kW  (" + std::to_string(inspectB->adjacentCoolers) + " Coolers)";
            } else if (inspectB->type == BuildingType::FusionReactor) {
                line2 = "Tokamak Output: " + fmtInt(inspectB->hasFuel ? def.baseElectricKW * world.nuclearPowerMultiplier() * world.superconductingMult() : 0.0f) + " kW";
            } else if (def.isMechanical) {
                line2 = "Speed: " + fmtFloat1(std::abs(inspectB->rpm)) + " RPM";
            }

            if (!line2.empty()) {
                sf::Text spTxt(font, line2, 11);
                spTxt.setFillColor(sf::Color(215, 224, 238));
                spTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 52.0f});
                target.draw(spTxt);
            }

            if (net && net->capacityPU > 0.0f && def.isMechanical) {
                std::string pStr = "Mech Power: " + fmtFloat1(net->loadPU) + " / " + fmtFloat1(net->capacityPU) + " PU";
                sf::Text pTxt(font, pStr, 11);
                pTxt.setFillColor(net->loadPU > net->capacityPU ? sf::Color(250, 155, 80) : sf::Color(175, 210, 235));
                pTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 72.0f});
                target.draw(pTxt);
                float frac = std::clamp(net->loadPU / std::max(1.0f, net->capacityPU), 0.0f, 1.0f);
                drawProgressBar(target, sf::FloatRect({iRect.position.x + 12.0f, iRect.position.y + 92.0f}, {318.0f, 9.0f}),
                                frac, net->loadPU > net->capacityPU ? sf::Color(245, 92, 82) : sf::Color(92, 224, 148));
            } else if (enet) {
                std::string kStr = "Electric Grid: " + fmtInt(enet->demandKW) + " / " + fmtInt(enet->supplyKW) + " kW";
                sf::Text kTxt(font, kStr, 11);
                kTxt.setFillColor(enet->overloaded ? sf::Color(250, 155, 80) : sf::Color(125, 232, 255));
                kTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 72.0f});
                target.draw(kTxt);
                float frac = (enet->supplyKW > 0.1f) ? std::clamp(enet->demandKW / enet->supplyKW, 0.0f, 1.0f) : 0.0f;
                drawProgressBar(target, sf::FloatRect({iRect.position.x + 12.0f, iRect.position.y + 92.0f}, {318.0f, 9.0f}),
                                frac, enet->overloaded ? sf::Color(245, 92, 82) : sf::Color(86, 224, 255));
            }

            std::string extra;
            if (inspectB->type == BuildingType::StoneSmelter || inspectB->type == BuildingType::ArcFurnace ||
                inspectB->type == BuildingType::Assembler || inspectB->type == BuildingType::QuantumForge) {
                extra = "Feed via Belt OR press [F] to load!";
            } else if (inspectB->type == BuildingType::MatterSink) {
                extra = "Sink Pts: " + fmtInt(world.sinkPoints()) + "/" + fmtInt(world.nextCouponCost()) + "  |  Coupons: " + std::to_string(world.coupons()) + " [V]";
            } else if (def.category == BuildingCategory::Drill && inspectB->type != BuildingType::WaterPump) {
                const Tile& t = world.tile(inspectB->gx, inspectB->gy);
                extra = oreName(t.ore) + " (" + fmtFloat1(t.richness) + "x) | Attach Belt for bonus!";
            } else if (inspectB->type == BuildingType::WaterPump) {
                extra = "Hydro-cools Drills, Smelters & Reactors within 3.5 tiles";
            } else if (inspectB->type == BuildingType::HandCrank) {
                extra = inspectB->crankBoostTimer > 0.0f ? "BOOSTED! (24 RPM / 48 PU)" : "Click Crank or hold [Space] for 2x!";
            }
            if (inspectB->overclocked) {
                int bonusPct = static_cast<int>(std::round((world.beaconSpeedMultiplier() - 1.0f) * 100.0f));
                extra = "[OVERCLOCK +" + std::to_string(bonusPct) + "%] " + extra;
            }

            if (!extra.empty()) {
                sf::Text eTxt(font, extra, 11);
                eTxt.setFillColor(sf::Color(196, 215, 236));
                eTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 112.0f});
                target.draw(eTxt);
            }
            if (hasDiag) {
                sf::Text dTxt(font, net->diagnostic, 11);
                dTxt.setStyle(sf::Text::Bold);
                dTxt.setFillColor(sf::Color(250, 128, 118));
                dTxt.setPosition({iRect.position.x + 12.0f, iRect.position.y + 138.0f});
                target.draw(dTxt);
            }
        } else if (world.inBounds(hoverGx, hoverGy) && !isPointerOverUI(mousePos, winSize, world)) {
            const Tile& t = world.tile(hoverGx, hoverGy);
            sf::FloatRect iRect({12.0f, H - 170.0f}, {342.0f, 64.0f});
            drawPanel(target, iRect, sf::Color(16, 20, 28, 228), sf::Color(72, 86, 108), 1.3f);
            std::string hdr;
            sf::Color hCol;
            if (t.terrain == TerrainType::Water) {
                hdr = std::string("Coolant Basin  [") + biomeName(t.biome) + "]";
                hCol = sf::Color(96, 205, 255);
            } else if (t.terrain == TerrainType::Rock) {
                hdr = "Basalt Cliff Mesa  (Elev " + fmtFloat1(t.elevation * 100.0f) + "m)";
                hCol = sf::Color(175, 184, 204);
            } else if (t.ore != OreType::None) {
                hdr = oreName(t.ore) + "  (" + fmtFloat1(t.richness) + "x) - " + biomeName(t.biome);
                hCol = resourceColor(oreToResource(t.ore));
            } else {
                hdr = std::string(biomeName(t.biome)) + "  (" + std::to_string(hoverGx) + ", " + std::to_string(hoverGy) + ")";
                hCol = sf::Color(196, 208, 226);
            }
            sf::Text t1(font, hdr, 12);
            t1.setStyle(sf::Text::Bold);
            t1.setFillColor(hCol);
            t1.setPosition({iRect.position.x + 12.0f, iRect.position.y + 10.0f});
            target.draw(t1);

            std::string hint = (t.terrain == TerrainType::Water) ? "Place Water Pump [F3->5] for hydro cooling!"
                             : (t.terrain == TerrainType::Rock ? "Impassable cliff (Use Underground Belt [F5->5])"
                             : (t.ore == OreType::Aether ? "Needs Tier 3 Aether Extractor [F3->4]"
                             : ((t.ore == OreType::Titanium || t.ore == OreType::Uranium) ? "Needs Tier 2+ Drill (Heavy / Electric)"
                             : (t.ore != OreType::None ? "Place Drill [F3] + Conveyor Belt [F5]"
                             : (world.isInDirectHubZone(hoverGx, hoverGy) ? "Inside Hub Drone Zone - select F1..F7 to build" : "Frontier buildable terrain - route Belts or Trains")))));
            sf::Text t2(font, hint, 11);
            t2.setFillColor(sf::Color(186, 200, 222));
            t2.setPosition({iRect.position.x + 12.0f, iRect.position.y + 34.0f});
            target.draw(t2);
        }
    }

    // 7. 5-Tab Command Modal (Tech Tree / Spire & Sink / Analytics / World & Sandbox / Settings & RT)
    if (upgradesOpen_) {
        sf::RectangleShape dim({W, H});
        dim.setFillColor(sf::Color(6, 8, 12, 196));
        target.draw(dim);

        sf::FloatRect modal = computeUpgradeModalRect(winSize);
        drawPanel(target, modal, sf::Color(17, 22, 32, 253), sf::Color(225, 182, 86), 2.2f);

        const char* tabLabels[5] = {
            "TECH TREE (40) [U]",
            "SPIRE & SINK [V]",
            "ANALYTICS [Tab]",
            "WORLD & SANDBOX [N]",
            "SETTINGS & RT [O]"
        };
        const float tabPitch = std::min(220.0f, (modal.size.x - 246.0f) / 5.0f);
        const float tabW = tabPitch - 6.0f;
        for (int t = 0; t < 5; ++t) {
            sf::FloatRect tabRect({modal.position.x + 14.0f + static_cast<float>(t) * tabPitch, modal.position.y + 11.0f}, {tabW, 32.0f});
            bool active = (modalTab_ == t);
            bool hov = tabRect.contains(mousePos);
            sf::Color fill = active ? sf::Color(54, 66, 92) : (hov ? sf::Color(34, 42, 58) : sf::Color(24, 30, 42));
            sf::Color out = active ? sf::Color(246, 206, 102) : sf::Color(76, 90, 114);
            drawPanel(target, tabRect, fill, out, active ? 1.8f : 1.2f);
            drawCenteredText(target, font, tabLabels[t], 11, tabRect.getCenter(),
                             active ? sf::Color(255, 236, 175) : sf::Color(180, 194, 215), true);
        }

        sf::FloatRect closeRect({modal.position.x + modal.size.x - 42.0f, modal.position.y + 11.0f}, {30.0f, 30.0f});
        drawPanel(target, closeRect, closeRect.contains(mousePos) ? sf::Color(150, 52, 52) : sf::Color(48, 32, 36),
                  sf::Color(235, 110, 105), 1.4f);
        drawCenteredText(target, font, "X", 15, closeRect.getCenter(), sf::Color::White, true);

        if (modalTab_ == 0) {
            renderTechTreeTab(target, font, renderer, world, mousePos, winSize, animTime, modal);
        } else if (modalTab_ == 1) {
            renderSpireAndSinkTab(target, font, world, mousePos, modal);
        } else if (modalTab_ == 2) {
            renderAnalyticsTab(target, font, world, modal);
        } else if (modalTab_ == 3) {
            renderWorldAndSandboxTab(target, font, world, settings, mousePos, animTime, modal);
        } else {
            renderSettingsTab(target, font, renderer, settings, mousePos, modal);
        }
    }

    // 8. Toast notification
    if (toastTimer_ > 0.0f && !toastText_.empty()) {
        float alpha = std::min(1.0f, toastTimer_ * 1.4f);
        sf::FloatRect tRect({W * 0.5f - 250.0f, 68.0f}, {500.0f, 34.0f});
        sf::Color bg = toastIsError_ ? sf::Color(70, 24, 28, static_cast<std::uint8_t>(235 * alpha)) : sf::Color(22, 48, 36, static_cast<std::uint8_t>(235 * alpha));
        sf::Color out = toastIsError_ ? sf::Color(240, 96, 88, static_cast<std::uint8_t>(245 * alpha)) : sf::Color(102, 228, 152, static_cast<std::uint8_t>(245 * alpha));
        drawPanel(target, tRect, bg, out, 1.6f);
        drawCenteredText(target, font, toastText_, 13, tRect.getCenter(), sf::Color(245, 248, 252, static_cast<std::uint8_t>(255 * alpha)), true);
    }
}

void UI::renderTechTreeTab(
    sf::RenderTarget& target,
    const sf::Font& font,
    const Renderer& renderer,
    const World& world,
    sf::Vector2f mousePos,
    sf::Vector2u winSize,
    float animTime,
    sf::FloatRect modal
) const {
    ensureTechViewInitialized(winSize);
    const auto nodes = computeTechNodes(winSize);

    // Header Zoom & Fit controls
    sf::FloatRect zoomOutBtn({modal.position.x + modal.size.x - 210.0f, modal.position.y + 11.0f}, {28.0f, 30.0f});
    sf::FloatRect zoomReadout({modal.position.x + modal.size.x - 178.0f, modal.position.y + 11.0f}, {48.0f, 30.0f});
    sf::FloatRect zoomInBtn({modal.position.x + modal.size.x - 126.0f, modal.position.y + 11.0f}, {28.0f, 30.0f});
    sf::FloatRect fitBtn({modal.position.x + modal.size.x - 94.0f, modal.position.y + 11.0f}, {46.0f, 30.0f});

    drawPanel(target, zoomOutBtn, zoomOutBtn.contains(mousePos) ? sf::Color(48, 58, 78) : sf::Color(28, 34, 48), sf::Color(108, 134, 175), 1.2f);
    drawCenteredText(target, font, "-", 16, zoomOutBtn.getCenter(), sf::Color::White, true);

    drawPanel(target, zoomReadout, sf::Color(18, 24, 34), sf::Color(72, 88, 114), 1.0f);
    drawCenteredText(target, font, std::to_string(static_cast<int>(std::round(techZoom_ * 100.0f))) + "%", 11,
                     zoomReadout.getCenter(), sf::Color(215, 228, 248), true);

    drawPanel(target, zoomInBtn, zoomInBtn.contains(mousePos) ? sf::Color(48, 58, 78) : sf::Color(28, 34, 48), sf::Color(108, 134, 175), 1.2f);
    drawCenteredText(target, font, "+", 15, zoomInBtn.getCenter(), sf::Color::White, true);

    drawPanel(target, fitBtn, fitBtn.contains(mousePos) ? sf::Color(54, 68, 94) : sf::Color(30, 38, 54), sf::Color(225, 186, 86), 1.2f);
    drawCenteredText(target, font, "FIT", 11, fitBtn.getCenter(), sf::Color(255, 232, 168), true);

    const sf::FloatRect canvas = computeTechCanvasRect(winSize);
    drawPanel(target, canvas, sf::Color(10, 14, 22), sf::Color(62, 76, 102), 1.4f);

    // Hardware 1:1 pixel viewport clipping inside canvas so fonts remain razor-sharp at every zoom!
    const sf::View savedView = target.getView();
    const float W = std::max(1.0f, static_cast<float>(winSize.x));
    const float H = std::max(1.0f, static_cast<float>(winSize.y));
    sf::View clipView(canvas);
    clipView.setViewport(sf::FloatRect(
        {canvas.position.x / W, canvas.position.y / H},
        {canvas.size.x / W, canvas.size.y / H}
    ));
    target.setView(clipView);

    const sf::Vector2f cCenter = canvas.getCenter();
    const float z = techZoom_;
    auto treeToScr = [&](float tx, float ty) -> sf::Vector2f {
        return cCenter + (sf::Vector2f{tx, ty} - techPan_) * z;
    };

    // 1. Subtle radial clockwork astrolabe rings & tier depth horizons behind the tree
    sf::Vector2f rootScr = treeToScr(0.0f, 0.0f);
    for (int ring = 1; ring <= 6; ++ring) {
        float rad = static_cast<float>(ring) * 195.0f * z;
        sf::CircleShape arc(rad, 72);
        arc.setOrigin({rad, rad});
        arc.setPosition(rootScr);
        arc.setFillColor(sf::Color::Transparent);
        arc.setOutlineColor(sf::Color(44, 58, 82, 36));
        arc.setOutlineThickness(1.0f);
        target.draw(arc);
    }

    static const char* TIER_NAMES[7] = {
        "TIER I  -  PRIMARY BOUGHS",
        "TIER II  -  STEAM, SOLAR & SMELTING",
        "TIER III  -  GRID, RAILS & PRECISION",
        "TIER IV  -  AETHER, ARC & EXPRESS",
        "TIER V  -  NUCLEAR & QUANTUM FORGE",
        "TIER VI  -  TOKAMAK FUSION & BEACONS",
        "TIER VII  -  PLANETARY ASCENSION"
    };
    for (int tier = 1; tier <= 7; ++tier) {
        float ty = static_cast<float>(tier) * TREE_PITCH_Y;
        sf::Vector2f lPos = treeToScr(-1130.0f, ty);
        sf::Vector2f rPos = treeToScr(+1240.0f, ty);
        drawLineSegment(target, lPos, rPos, 1.0f, sf::Color(36, 48, 68, 52));
        if (z >= 0.54f) {
            sf::Text tLbl(font, TIER_NAMES[tier - 1], 9);
            tLbl.setFillColor(sf::Color(92, 112, 144, 130));
            tLbl.setPosition({std::max(canvas.position.x + 8.0f, std::round(lPos.x + 6.0f)), std::round(lPos.y - 26.0f * z)});
            target.draw(tLbl);
        }
    }

    auto findNodeRect = [&](UpgradeId id) -> const TechNodeRect* {
        for (const auto& nr : nodes) {
            if (nr.id == id) return &nr;
        }
        return nullptr;
    };

    // 2. Secondary Cross-Branch Synergy Arcs (subtle dashed curves underneath main tree boughs)
    for (const auto& nr : nodes) {
        const UpgradeDef& def = Catalog::getUpgrade(nr.id);
        if (def.prerequisite2 == UpgradeId::Count) continue;
        const TechNodeRect* pn = findNodeRect(def.prerequisite2);
        if (!pn) continue;
        bool met = world.isSandboxMode() || world.getUpgradeLevel(def.prerequisite2) > 0;
        bool isSel = (selectedUpgrade_ == nr.id || selectedUpgrade_ == def.prerequisite2);
        sf::Vector2f p0{pn->cardRect.position.x + pn->cardRect.size.x * 0.5f, pn->cardRect.position.y + pn->cardRect.size.y};
        sf::Vector2f p3{nr.cardRect.position.x + nr.cardRect.size.x * 0.5f, nr.cardRect.position.y};
        sf::Color arcCol = isSel ? sf::Color(255, 224, 115, 220)
                         : (met ? sf::Color(96, 224, 245, 135) : sf::Color(86, 102, 132, 68));
        drawTreeSynergyArc(target, p0, p3, std::max(1.4f, 2.0f * z), arcCol);
    }

    // 3. Central Root Trunk -> 4 Primary Boughs (Tier 1) + Bough Banners
    const float rootW = std::max(186.0f, std::round(272.0f * z));
    const float rootH = std::max(40.0f, std::round(66.0f * z));
    const sf::FloatRect rootRect({std::round(rootScr.x - rootW * 0.5f), std::round(rootScr.y - rootH * 0.5f)}, {rootW, rootH});
    const sf::Vector2f rootBottom{rootScr.x, rootRect.position.y + rootRect.size.y};

    struct BoughHeader {
        UpgradeId rootTech;
        const char* label;
        float bannerTreeX;
    };
    static const BoughHeader BOUGHS[4] = {
        {UpgradeId::DrillSpeed,             "MINING & MATTER",     -4.80f},
        {UpgradeId::UnlockLargeGear,        "CLOCKWORK & CRAFT",   -1.60f},
        {UpgradeId::SpringTension,          "POWER & ATOMICS",     +1.60f},
        {UpgradeId::UnlockSplittersTunnels, "BELTS & RAILWAYS",    +4.80f}
    };
    // Pass A: Draw all 4 sweeping primary bough trunks first
    for (const auto& bh : BOUGHS) {
        const TechNodeRect* cNode = findNodeRect(bh.rootTech);
        if (!cNode) continue;
        const UpgradeDef& cDef = Catalog::getUpgrade(bh.rootTech);
        sf::Color bCol = techBranchColor(cDef.branch);
        bool unlocked = world.getUpgradeLevel(bh.rootTech) > 0;
        sf::Vector2f childTop{cNode->cardRect.position.x + cNode->cardRect.size.x * 0.5f, cNode->cardRect.position.y};

        sf::Color glow = unlocked ? sf::Color(bCol.r, bCol.g, bCol.b, 72) : sf::Color(bCol.r, bCol.g, bCol.b, 38);
        sf::Color core = unlocked ? bCol : sf::Color(
            static_cast<std::uint8_t>(bCol.r * 0.68f),
            static_cast<std::uint8_t>(bCol.g * 0.68f),
            static_cast<std::uint8_t>(bCol.b * 0.68f),
            215
        );
        drawTreeBezierBough(target, rootBottom, childTop, std::max(3.5f, 7.2f * z), std::max(2.4f, 4.5f * z),
                            glow, core, animTime, unlocked);
    }
    // Pass B: Draw crisp bough banner badges on top of the trunk lines
    for (const auto& bh : BOUGHS) {
        const UpgradeDef& cDef = Catalog::getUpgrade(bh.rootTech);
        sf::Color bCol = techBranchColor(cDef.branch);
        sf::Vector2f banCenter = treeToScr(bh.bannerTreeX * TREE_PITCH_X, 0.57f * TREE_PITCH_Y);
        float banW = std::max(116.0f, std::round(158.0f * z));
        float banH = std::max(15.0f, std::round(22.0f * z));
        sf::FloatRect banRect({std::round(banCenter.x - banW * 0.5f), std::round(banCenter.y - banH * 0.5f)}, {banW, banH});
        drawPanel(target, banRect, sf::Color(11, 16, 26, 250), bCol, 1.3f);
        drawCenteredText(target, font, bh.label, z >= 0.78f ? 10u : 8u, banRect.getCenter(), bCol, true);
    }

    // 4. Primary Parent -> Child Organic Cubic-Bezier Tree Branches
    for (const auto& nr : nodes) {
        const UpgradeDef& def = Catalog::getUpgrade(nr.id);
        if (def.prerequisite == UpgradeId::Count) continue;
        const TechNodeRect* pn = findNodeRect(def.prerequisite);
        if (!pn) continue;

        sf::Color bCol = techBranchColor(def.branch);
        int childLv = world.getUpgradeLevel(nr.id);
        int parentLv = world.getUpgradeLevel(def.prerequisite);
        bool prereqMet = world.isSandboxMode() || Catalog::canMeetPrerequisites(nr.id, world.upgradeLevels());
        bool activePath = (childLv > 0);
        bool isHighlighted = (selectedUpgrade_ == nr.id);

        sf::Color glowCol(0, 0, 0, 0);
        sf::Color coreCol;
        if (activePath) {
            glowCol = sf::Color(bCol.r, bCol.g, bCol.b, 76);
            coreCol = bCol;
        } else if (prereqMet || parentLv > 0) {
            glowCol = sf::Color(bCol.r, bCol.g, bCol.b, 38);
            coreCol = sf::Color(
                static_cast<std::uint8_t>(bCol.r * 0.80f),
                static_cast<std::uint8_t>(bCol.g * 0.80f),
                static_cast<std::uint8_t>(bCol.b * 0.80f),
                220
            );
        } else {
            coreCol = sf::Color(
                static_cast<std::uint8_t>(38 + bCol.r * 0.22f),
                static_cast<std::uint8_t>(46 + bCol.g * 0.22f),
                static_cast<std::uint8_t>(62 + bCol.b * 0.22f),
                170
            );
        }
        if (isHighlighted) {
            glowCol = sf::Color(255, 230, 130, 95);
            coreCol = sf::Color(255, 238, 165);
        }

        sf::Vector2f p0{pn->cardRect.position.x + pn->cardRect.size.x * 0.5f, pn->cardRect.position.y + pn->cardRect.size.y};
        sf::Vector2f p3{nr.cardRect.position.x + nr.cardRect.size.x * 0.5f, nr.cardRect.position.y};
        drawTreeBezierBough(target, p0, p3, std::max(2.2f, 4.4f * z), std::max(1.7f, 3.0f * z),
                            glowCol, coreCol, animTime, activePath);

        // Brass/branch junction rivets at fork endpoints
        float rivR = std::max(2.0f, 3.2f * z);
        sf::CircleShape riv(rivR, 10);
        riv.setOrigin({rivR, rivR});
        riv.setFillColor(coreCol);
        riv.setPosition(p0);
        target.draw(riv);
        riv.setPosition(p3);
        target.draw(riv);
    }

    // 5. GRAND CLOCKWORK CORE Root Node at (0, 0)
    {
        drawPanel(target, rootRect, sf::Color(30, 26, 18, 250), sf::Color(250, 206, 96), 2.2f);
        float iconSz = std::clamp(22.0f * z, 12.0f, 26.0f);
        sf::Vector2f icPos{rootRect.position.x + iconSz + 6.0f, rootRect.getCenter().y};
        renderer.drawBuildingPreviewIcon(target, BuildingType::HubCore, icPos, 0.30f * z, animTime * 0.7f);
        // Restore clipView because drawBuildingPreviewIcon uses iconRT_ internally
        target.setView(clipView);

        unsigned int rTitleSz = (z >= 0.85f) ? 12u : ((z >= 0.64f) ? 10u : 9u);
        sf::Text rTitle(font, "GRAND CLOCKWORK CORE", rTitleSz);
        rTitle.setStyle(sf::Text::Bold);
        rTitle.setFillColor(sf::Color(255, 228, 145));
        rTitle.setPosition({std::round(rootRect.position.x + iconSz * 1.85f + 8.0f), std::round(rootRect.position.y + 8.0f * z)});
        target.draw(rTitle);

        int unlockedCount = 0;
        for (UpgradeId id : Catalog::upgradeOrder()) {
            if (world.getUpgradeLevel(id) > 0) ++unlockedCount;
        }
        std::string rSub = "ROOT  -  " + std::to_string(unlockedCount) + "/40 Researched";
        sf::Text rSubTxt(font, rSub, (z >= 0.80f) ? 10u : 8u);
        rSubTxt.setFillColor(sf::Color(206, 194, 162));
        rSubTxt.setPosition({std::round(rootRect.position.x + iconSz * 1.85f + 8.0f), std::round(rootRect.position.y + rootRect.size.y * 0.54f)});
        target.draw(rSubTxt);
    }

    // 6. 40 Branching Tech Node Cards
    for (const auto& nr : nodes) {
        if (nr.cardRect.position.x + nr.cardRect.size.x < canvas.position.x - 4.0f ||
            nr.cardRect.position.x > canvas.position.x + canvas.size.x + 4.0f ||
            nr.cardRect.position.y + nr.cardRect.size.y < canvas.position.y - 4.0f ||
            nr.cardRect.position.y > canvas.position.y + canvas.size.y + 4.0f) {
            continue;
        }

        const UpgradeDef& def = Catalog::getUpgrade(nr.id);
        sf::Color bCol = techBranchColor(def.branch);
        int curLv = world.getUpgradeLevel(nr.id);
        bool maxed = curLv >= def.maxLevel;
        bool prereqMet = world.isSandboxMode() || Catalog::canMeetPrerequisites(nr.id, world.upgradeLevels());
        bool canBuy = world.canBuyUpgrade(nr.id);
        bool isSelected = (nr.id == selectedUpgrade_);
        bool hovered = canvas.contains(mousePos) && nr.cardRect.contains(mousePos);

        sf::Color fill = maxed ? sf::Color(20, 40, 32, 248)
                       : (canBuy ? sf::Color(32, 40, 56, 248)
                       : (prereqMet ? sf::Color(22, 28, 40, 245) : sf::Color(15, 19, 28, 240)));
        if (hovered && !maxed) {
            fill = sf::Color(static_cast<std::uint8_t>(std::min(255, fill.r + 12)),
                             static_cast<std::uint8_t>(std::min(255, fill.g + 14)),
                             static_cast<std::uint8_t>(std::min(255, fill.b + 18)), 252);
        }
        sf::Color out = isSelected ? sf::Color(255, 255, 255)
                      : (maxed ? sf::Color(86, 214, 142)
                      : (canBuy ? sf::Color(246, 202, 88)
                      : (prereqMet ? bCol : sf::Color(52, 60, 78))));
        drawPanel(target, nr.cardRect, fill, out, isSelected ? 2.2f : (canBuy ? 1.8f : 1.2f));

        // Left branch color accent strip
        sf::RectangleShape strip({std::max(3.0f, std::round(4.0f * z)), nr.cardRect.size.y - 2.0f});
        strip.setPosition({nr.cardRect.position.x + 1.0f, nr.cardRect.position.y + 1.0f});
        strip.setFillColor(prereqMet || curLv > 0 ? bCol : sf::Color(bCol.r / 2, bCol.g / 2, bCol.b / 2, 180));
        target.draw(strip);

        // Building/tech icon badge on left of card
        float iconColW = std::round(28.0f * z);
        if (nr.cardRect.size.y >= 28.0f) {
            sf::Vector2f icCenter{
                nr.cardRect.position.x + strip.getSize().x + iconColW * 0.5f + 2.0f,
                nr.cardRect.position.y + nr.cardRect.size.y * 0.5f
            };
            renderer.drawBuildingPreviewIcon(target, def.iconType, icCenter, 0.25f * z,
                                             curLv > 0 ? animTime * 0.8f : 0.0f);
            target.setView(clipView);
        }

        float textLeft = std::round(nr.cardRect.position.x + strip.getSize().x + iconColW + 4.0f);
        float availTextW = std::max(24.0f, (nr.cardRect.position.x + nr.cardRect.size.x - 4.0f) - textLeft);

        unsigned int titleSz = (z >= 0.92f) ? 11u : ((z >= 0.68f) ? 10u : 8u);
        sf::Text nName(font, def.name, titleSz);
        nName.setStyle(sf::Text::Bold);
        if (nName.getLocalBounds().size.x > availTextW && titleSz > 8u) {
            nName.setCharacterSize(titleSz - 1u);
        }
        if (nName.getLocalBounds().size.x > availTextW) {
            std::string shortTitle = def.name;
            while (shortTitle.size() > 6 && nName.getLocalBounds().size.x > availTextW) {
                shortTitle.pop_back();
                nName.setString(shortTitle + ".");
            }
        }
        nName.setFillColor(maxed ? sf::Color(130, 245, 174)
                         : (prereqMet ? sf::Color(244, 238, 222) : sf::Color(148, 158, 176)));
        nName.setPosition({textLeft, std::round(nr.cardRect.position.y + 4.0f * z + 1.0f)});
        target.draw(nName);

        // Level & compact cost line
        unsigned int subSz = (z >= 0.88f) ? 10u : 8u;
        std::string lvStr = "Lv " + std::to_string(curLv) + "/" + std::to_string(def.maxLevel);
        sf::Text nLv(font, lvStr, subSz);
        nLv.setFillColor(curLv > 0 ? sf::Color(135, 232, 175) : sf::Color(168, 182, 206));
        nLv.setPosition({textLeft, std::round(nr.cardRect.position.y + nr.cardRect.size.y * 0.39f)});
        target.draw(nLv);

        if (z >= 0.72f) {
            std::string cShort = maxed ? "MAXED" : (world.isSandboxMode() ? "FREE" : formatCostHotbar(Catalog::upgradeCost(nr.id, curLv)));
            sf::Text nCost(font, cShort, (z >= 0.95f) ? 9u : 8u);
            float maxCW = std::max(16.0f, nr.buyRect.position.x - textLeft - 3.0f);
            while (cShort.size() > 5 && nCost.getLocalBounds().size.x > maxCW) {
                cShort.pop_back();
                nCost.setString(cShort);
            }
            nCost.setFillColor(maxed ? sf::Color(110, 226, 156) : (canBuy ? sf::Color(150, 235, 168) : sf::Color(218, 142, 132)));
            nCost.setPosition({textLeft, std::round(nr.cardRect.position.y + nr.cardRect.size.y - 13.0f * z - 2.0f)});
            target.draw(nCost);
        }

        // Action pill button in bottom-right of card
        bool bHov = canvas.contains(mousePos) && nr.buyRect.contains(mousePos);
        sf::Color bFill = maxed ? sf::Color(28, 64, 46)
                        : (canBuy ? (bHov ? sf::Color(92, 162, 104) : sf::Color(56, 118, 72)) : sf::Color(30, 36, 48));
        sf::Color bOut = maxed ? sf::Color(92, 204, 142) : (canBuy ? sf::Color(138, 242, 162) : sf::Color(62, 72, 90));
        drawPanel(target, nr.buyRect, bFill, bOut, 1.0f);
        drawCenteredText(target, font, maxed ? "OK" : (!prereqMet ? "LCK" : "BUY"),
                         (z >= 0.82f) ? 9u : 8u, nr.buyRect.getCenter(),
                         canBuy ? sf::Color::White : sf::Color(148, 158, 174), true);
    }

    target.setView(savedView);

    // Subtle navigation legend overlay in bottom-right corner of canvas
    {
        sf::Text navHint(font, "Scroll: Zoom Tree   |   Drag LMB/MMB or WASD: Pan   |   Click Node: Inspect / Research", 10);
        navHint.setFillColor(sf::Color(156, 176, 206, 185));
        auto hb = navHint.getLocalBounds();
        navHint.setPosition({std::round(canvas.position.x + canvas.size.x - hb.size.x - 12.0f),
                             std::round(canvas.position.y + canvas.size.y - 18.0f)});
        target.draw(navHint);
    }

    // Bottom Selected Tech Detail Strip with live Building Preview Icon
    sf::FloatRect detailRect({modal.position.x + 14.0f, modal.position.y + modal.size.y - 94.0f},
                             {modal.size.x - 28.0f, 82.0f});
    const UpgradeDef& sel = Catalog::getUpgrade(selectedUpgrade_);
    sf::Color selBranchCol = techBranchColor(sel.branch);
    drawPanel(target, detailRect, sf::Color(20, 26, 38), selBranchCol, 1.6f);

    sf::FloatRect iconBox({detailRect.position.x + 12.0f, detailRect.position.y + 11.0f}, {60.0f, 60.0f});
    drawPanel(target, iconBox, sf::Color(12, 16, 24), sf::Color(68, 82, 106), 1.2f);
    renderer.drawBuildingPreviewIcon(target, sel.iconType, iconBox.getCenter(), 0.44f, animTime * 0.9f);
    target.setView(savedView);

    int curLv = world.getUpgradeLevel(selectedUpgrade_);
    bool maxed = curLv >= sel.maxLevel;
    bool prereqMet = world.isSandboxMode() || Catalog::canMeetPrerequisites(selectedUpgrade_, world.upgradeLevels());
    bool canBuy = world.canBuyUpgrade(selectedUpgrade_);

    float infoX = detailRect.position.x + 84.0f;
    sf::Text sTitle(font, "[" + sel.branch + "]  " + sel.name + "  (Level " + std::to_string(curLv) + "/" + std::to_string(sel.maxLevel) + ")", 15);
    sTitle.setStyle(sf::Text::Bold);
    sTitle.setFillColor(selBranchCol);
    sTitle.setPosition({infoX, detailRect.position.y + 8.0f});
    target.draw(sTitle);

    sf::Text sDesc(font, sel.description, 12);
    sDesc.setFillColor(sf::Color(215, 224, 238));
    sDesc.setPosition({infoX, detailRect.position.y + 31.0f});
    target.draw(sDesc);

    std::string subLine;
    if (maxed) {
        subLine = "Technology fully researched!";
    } else if (!prereqMet) {
        subLine = "Requires: ";
        if (sel.prerequisite != UpgradeId::Count) {
            subLine += Catalog::getUpgrade(sel.prerequisite).name;
        }
        if (sel.prerequisite2 != UpgradeId::Count) {
            if (sel.prerequisite != UpgradeId::Count) subLine += " + ";
            subLine += Catalog::getUpgrade(sel.prerequisite2).name;
        }
    } else if (world.isSandboxMode()) {
        subLine = "Research Cost: FREE (Sandbox Mode Active)";
    } else {
        subLine = "Research Cost: " + formatCostFull(Catalog::upgradeCost(selectedUpgrade_, curLv));
    }
    sf::Text sCost(font, subLine, 12);
    sCost.setStyle(sf::Text::Bold);
    sCost.setFillColor(maxed ? sf::Color(110, 226, 156) : (!prereqMet ? sf::Color(232, 155, 86) : (canBuy ? sf::Color(150, 236, 172) : sf::Color(235, 118, 112))));
    sCost.setPosition({infoX, detailRect.position.y + 54.0f});
    target.draw(sCost);

    sf::FloatRect buyBtn = computeDetailBuyRect(winSize);
    bool dHov = buyBtn.contains(mousePos);
    sf::Color bFill = maxed ? sf::Color(34, 72, 54) : (canBuy ? (dHov ? sf::Color(92, 162, 106) : sf::Color(60, 122, 76)) : sf::Color(38, 42, 54));
    sf::Color bOut = maxed ? sf::Color(92, 200, 140) : (canBuy ? sf::Color(135, 240, 160) : sf::Color(68, 76, 94));
    drawPanel(target, buyBtn, bFill, bOut, 1.5f);
    drawCenteredText(target, font, maxed ? "COMPLETE" : (!prereqMet ? "LOCKED" : "RESEARCH"), 14,
                     buyBtn.getCenter(), canBuy ? sf::Color::White : sf::Color(155, 165, 182), true);
}

void UI::renderSpireAndSinkTab(
    sf::RenderTarget& target,
    const sf::Font& font,
    const World& world,
    sf::Vector2f mousePos,
    sf::FloatRect modal
) const {
    // LEFT: Grand Clockwork Spire Milestones
    sf::FloatRect leftCard({modal.position.x + 20.0f, modal.position.y + 62.0f}, {modal.size.x * 0.49f - 28.0f, modal.size.y - 82.0f});
    drawPanel(target, leftCard, sf::Color(20, 26, 38), sf::Color(236, 194, 92), 1.6f);

    int ph = world.spirePhase();
    int pr = world.prestigeCount();
    std::string hdr = (ph < 5)
        ? ("GRAND SPIRE - PHASE " + std::to_string(ph + 1) + " OF 5")
        : ("ORBITAL ASCENSION PRESTIGE #" + std::to_string(pr + 1));
    sf::Text lTitle(font, hdr, 17);
    lTitle.setStyle(sf::Text::Bold);
    lTitle.setFillColor(sf::Color(248, 208, 102));
    lTitle.setPosition({leftCard.position.x + 20.0f, leftCard.position.y + 16.0f});
    target.draw(lTitle);

    ResourceBag req{};
    std::string phaseTitle, phaseDesc, rewardStr;
    if (ph < 5) {
        const SpirePhaseDef& sdef = Catalog::getSpirePhase(ph);
        phaseTitle = sdef.title;
        phaseDesc = sdef.subtitle;
        rewardStr = "Phase Reward: " + sdef.rewardText;
        req = sdef.requirement;
    } else {
        phaseTitle = "Infinite Deep-Space Megastructure Expansion";
        phaseDesc = "Supply exponentially scaled late-game resources to ascend the Planetary Spire.";
        rewardStr = "Ascension Reward: Permanent +20% Global Speed & Yield + 5 Coupons!";
        double mult = std::pow(1.65, static_cast<double>(pr));
        const SpirePhaseDef& p5 = Catalog::getSpirePhase(4);
        for (std::size_t i = 0; i < req.values.size(); ++i) {
            req.values[i] = std::ceil(p5.requirement.values[i] * mult);
        }
    }

    sf::Text sPhaseName(font, phaseTitle, 13);
    sPhaseName.setStyle(sf::Text::Bold);
    sPhaseName.setFillColor(sf::Color(235, 240, 250));
    sPhaseName.setPosition({leftCard.position.x + 20.0f, leftCard.position.y + 44.0f});
    target.draw(sPhaseName);

    sf::Text sSub(font, phaseDesc, 11);
    sSub.setFillColor(sf::Color(192, 208, 232));
    sSub.setPosition({leftCard.position.x + 20.0f, leftCard.position.y + 64.0f});
    target.draw(sSub);

    sf::Text sRew(font, rewardStr, 12);
    sRew.setStyle(sf::Text::Bold);
    sRew.setFillColor(sf::Color(115, 240, 172));
    sRew.setPosition({leftCard.position.x + 20.0f, leftCard.position.y + 88.0f});
    target.draw(sRew);

    sf::Text sMult(font, "Current Spire Output Bonus: +" + fmtInt((world.globalPrestigeMultiplier() - 1.0f) * 100.0f) + "% to All Machines", 12);
    sMult.setFillColor(sf::Color(135, 224, 255));
    sMult.setPosition({leftCard.position.x + 20.0f, leftCard.position.y + 110.0f});
    target.draw(sMult);

    float ry = leftCard.position.y + 144.0f;
    for (int i = 0; i < static_cast<int>(ResourceType::Count); ++i) {
        double need = req.values[static_cast<std::size_t>(i)];
        if (need <= 0.5) continue;
        ResourceType rt = static_cast<ResourceType>(i);
        double have = world.resources()[rt];
        float ratio = world.isSandboxMode() ? 1.0f : static_cast<float>(std::clamp(have / need, 0.0, 1.0));

        std::string needStr = world.isSandboxMode() ? "FREE (Sandbox)" : (fmtInt(have) + " / " + fmtInt(need));
        sf::Text rLbl(font, resourceName(rt) + ":  " + needStr, 12);
        rLbl.setStyle(sf::Text::Bold);
        rLbl.setFillColor((world.isSandboxMode() || have + 1e-4 >= need) ? sf::Color(115, 240, 165) : resourceColor(rt));
        rLbl.setPosition({leftCard.position.x + 24.0f, ry});
        target.draw(rLbl);

        drawProgressBar(target, sf::FloatRect({leftCard.position.x + 24.0f, ry + 20.0f}, {leftCard.size.x - 48.0f, 12.0f}),
                        ratio, (world.isSandboxMode() || have + 1e-4 >= need) ? sf::Color(92, 232, 152) : resourceColor(rt));
        ry += 44.0f;
    }

    bool canLaunch = world.canCompleteSpirePhase();
    sf::FloatRect launchBtn({leftCard.position.x + 24.0f, leftCard.position.y + leftCard.size.y - 68.0f}, {leftCard.size.x - 48.0f, 48.0f});
    bool lHov = launchBtn.contains(mousePos);
    drawPanel(target, launchBtn,
              canLaunch ? (lHov ? sf::Color(86, 168, 108) : sf::Color(52, 124, 76)) : sf::Color(34, 40, 54),
              canLaunch ? sf::Color(135, 250, 175) : sf::Color(72, 84, 106), 1.8f);
    drawCenteredText(target, font,
                     canLaunch ? "LAUNCH SPIRE PHASE!" : "GATHER REQUIRED RESOURCES TO LAUNCH",
                     15, launchBtn.getCenter(), canLaunch ? sf::Color::White : sf::Color(155, 168, 190), true);

    // RIGHT: A.W.E.S.O.M.E. Matter Sink & Coupon Exchange
    sf::FloatRect rightCard({modal.position.x + modal.size.x * 0.49f + 8.0f, modal.position.y + 62.0f}, {modal.size.x * 0.51f - 28.0f, modal.size.y - 82.0f});
    drawPanel(target, rightCard, sf::Color(22, 22, 38), sf::Color(214, 112, 255), 1.6f);

    sf::Text rTitle(font, "MATTER SINK & COUPON SHOP", 17);
    rTitle.setStyle(sf::Text::Bold);
    rTitle.setFillColor(sf::Color(232, 145, 255));
    rTitle.setPosition({rightCard.position.x + 20.0f, rightCard.position.y + 16.0f});
    target.draw(rTitle);

    sf::Text rSub(font, "Belt excess ores, Steel, Circuits, or Q-Cores into a Matter Sink [F4->6]!", 12);
    rSub.setFillColor(sf::Color(205, 215, 235));
    rSub.setPosition({rightCard.position.x + 20.0f, rightCard.position.y + 46.0f});
    target.draw(rSub);

    double curPts = world.sinkPoints();
    double nxtPts = std::max(1.0, world.nextCouponCost());
    sf::Text pTxt(font, "Next Coupon Progress:  " + fmtInt(curPts) + " / " + fmtInt(nxtPts) + " Sink Points", 13);
    pTxt.setStyle(sf::Text::Bold);
    pTxt.setFillColor(sf::Color(135, 236, 255));
    pTxt.setPosition({rightCard.position.x + 20.0f, rightCard.position.y + 78.0f});
    target.draw(pTxt);

    drawProgressBar(target, sf::FloatRect({rightCard.position.x + 20.0f, rightCard.position.y + 102.0f}, {rightCard.size.x - 40.0f, 16.0f}),
                    static_cast<float>(curPts / nxtPts), sf::Color(214, 108, 255));

    sf::Text cBadge(font, "AVAILABLE FACTORY COUPONS:  " + std::to_string(world.coupons()), 15);
    cBadge.setStyle(sf::Text::Bold);
    cBadge.setFillColor(sf::Color(255, 218, 96));
    cBadge.setPosition({rightCard.position.x + 20.0f, rightCard.position.y + 134.0f});
    target.draw(cBadge);

    struct CouponCardDef {
        CouponItem item;
        const char* title;
        const char* desc;
    };
    static const CouponCardDef CARDS[4] = {
        {CouponItem::BeltAndCraftSpeed, "Kinetic Overdrive", "+12% Conveyor Belt & Factory Crafting Speed per rank"},
        {CouponItem::GridAndShaftPower, "Turbine & Grid Overdrive", "+15% Mechanical PU & Electrical kW Output per rank"},
        {CouponItem::MiningYield,       "Deep-Core Resonance", "+12% Global Ore Mining Yield per rank"},
        {CouponItem::CarePackage,       "Orbital Care Package", "Instant +250 Fe/Cu, +120 St/Ci, +40 Ti/Ur/Ae, +5 Qc!"}
    };

    for (int c = 0; c < 4; ++c) {
        float cy = rightCard.position.y + 186.0f + static_cast<float>(c) * 112.0f;
        sf::FloatRect box({rightCard.position.x + 18.0f, cy}, {rightCard.size.x - 36.0f, 96.0f});
        drawPanel(target, box, sf::Color(28, 32, 48), sf::Color(78, 90, 118), 1.2f);

        int lv = world.couponLevel(CARDS[c].item);
        int cost = world.couponItemCost(CARDS[c].item);
        bool afford = world.isSandboxMode() || (world.coupons() >= cost);

        std::string ct = std::string(CARDS[c].title) + (c < 3 ? ("  (Rank " + std::to_string(lv) + ")") : "");
        sf::Text tt(font, ct, 14);
        tt.setStyle(sf::Text::Bold);
        tt.setFillColor(sf::Color(246, 214, 118));
        tt.setPosition({box.position.x + 14.0f, box.position.y + 14.0f});
        target.draw(tt);

        sf::Text td(font, CARDS[c].desc, 11);
        td.setFillColor(sf::Color(202, 214, 232));
        td.setPosition({box.position.x + 14.0f, box.position.y + 42.0f});
        target.draw(td);

        sf::FloatRect redeemBtn({rightCard.position.x + rightCard.size.x - 156.0f, cy + 22.0f}, {132.0f, 42.0f});
        bool hov = redeemBtn.contains(mousePos);
        drawPanel(target, redeemBtn,
                  afford ? (hov ? sf::Color(118, 66, 152) : sf::Color(86, 46, 116)) : sf::Color(36, 40, 54),
                  afford ? sf::Color(228, 138, 255) : sf::Color(68, 76, 94), 1.4f);
        std::string btnLbl = world.isSandboxMode() ? "FREE" : ("REDEEM (" + std::to_string(cost) + ")");
        drawCenteredText(target, font, btnLbl, 12, redeemBtn.getCenter(),
                         afford ? sf::Color::White : sf::Color(148, 158, 178), true);
    }
}

void UI::renderAnalyticsTab(
    sf::RenderTarget& target,
    const sf::Font& font,
    const World& world,
    sf::FloatRect modal
) const {
    // LEFT: Production & Power Ledger
    sf::FloatRect leftCard({modal.position.x + 20.0f, modal.position.y + 62.0f}, {modal.size.x * 0.52f - 28.0f, modal.size.y - 82.0f});
    drawPanel(target, leftCard, sf::Color(20, 26, 38), sf::Color(92, 214, 248), 1.6f);

    sf::Text lTitle(font, "PRODUCTION & NETWORK ANALYTICS", 17);
    lTitle.setStyle(sf::Text::Bold);
    lTitle.setFillColor(sf::Color(125, 232, 255));
    lTitle.setPosition({leftCard.position.x + 20.0f, leftCard.position.y + 16.0f});
    target.draw(lTitle);

    struct HdrCol { const char* lbl; float xOff; };
    static const HdrCol HDRS[4] = {
        {"RESOURCE", 28.0f},
        {"IN CORE", 176.0f},
        {"RATE (/MIN)", 303.0f},
        {"TOTAL PRODUCED", 453.0f}
    };
    for (const auto& hc : HDRS) {
        sf::Text colHdr(font, hc.lbl, 11);
        colHdr.setStyle(sf::Text::Bold);
        colHdr.setFillColor(sf::Color(162, 178, 204));
        colHdr.setPosition({leftCard.position.x + hc.xOff, leftCard.position.y + 54.0f});
        target.draw(colHdr);
    }

    for (int i = 0; i < static_cast<int>(ResourceType::Count); ++i) {
        ResourceType rt = static_cast<ResourceType>(i);
        float y = leftCard.position.y + 80.0f + static_cast<float>(i) * 34.0f;
        sf::FloatRect row({leftCard.position.x + 18.0f, y}, {leftCard.size.x - 36.0f, 28.0f});
        drawPanel(target, row, sf::Color(26, 32, 46), sf::Color(48, 58, 76), 1.0f);

        sf::Text c1(font, resourceName(rt), 12);
        c1.setStyle(sf::Text::Bold);
        c1.setFillColor(resourceColor(rt));
        c1.setPosition({row.position.x + 10.0f, y + 6.0f});
        target.draw(c1);

        sf::Text c2(font, fmtInt(world.resources()[rt]), 12);
        c2.setFillColor(sf::Color(232, 238, 248));
        c2.setPosition({row.position.x + 158.0f, y + 6.0f});
        target.draw(c2);

        double perMin = world.stats().ratePerSec[static_cast<std::size_t>(i)] * 60.0;
        sf::Text c3(font, "+" + fmtFloat1(static_cast<float>(perMin)) + "/m", 12);
        c3.setFillColor(perMin > 0.1 ? sf::Color(108, 238, 162) : sf::Color(135, 148, 168));
        c3.setPosition({row.position.x + 285.0f, y + 6.0f});
        target.draw(c3);

        sf::Text c4(font, fmtInt(world.stats().totalMined[static_cast<std::size_t>(i)]), 12);
        c4.setFillColor(sf::Color(195, 208, 228));
        c4.setPosition({row.position.x + 435.0f, y + 6.0f});
        target.draw(c4);
    }

    float totCapPU = 0.f, totLoadPU = 0.f;
    for (const auto& n : world.networks()) {
        totCapPU += n.capacityPU;
        totLoadPU += n.loadPU;
    }
    float totSupKW = 0.f, totDemKW = 0.f;
    for (const auto& e : world.electricNetworks()) {
        totSupKW += e.supplyKW;
        totDemKW += e.demandKW;
    }

    float sy = leftCard.position.y + 404.0f;
    std::string sum1 = "Mechanical Shafts:  " + fmtInt(totLoadPU) + " / " + fmtInt(totCapPU) + " PU   |   Electrical Grid:  " + fmtInt(totDemKW) + " / " + fmtInt(totSupKW) + " kW";
    std::string sum2 = "Cogs: " + std::to_string(world.stats().cogCount) +
                       "   Drills: " + std::to_string(world.stats().drillCount) +
                       "   Factories: " + std::to_string(world.stats().factoryCount) +
                       "   Belts: " + std::to_string(world.stats().beltCount) +
                       "   Trains: " + std::to_string(world.stats().trainCount);
    sf::Text st1(font, sum1, 13);
    st1.setStyle(sf::Text::Bold);
    st1.setFillColor(sf::Color(246, 208, 108));
    st1.setPosition({leftCard.position.x + 22.0f, sy});
    target.draw(st1);

    sf::Text st2(font, sum2, 12);
    st2.setFillColor(sf::Color(205, 218, 238));
    st2.setPosition({leftCard.position.x + 22.0f, sy + 28.0f});
    target.draw(st2);

    sf::Text st3(font, "Playtime: " + fmtTimerMMSS(static_cast<float>(world.stats().playTimeSec)) +
                       "   |   Total Coupons Minted: " + std::to_string(world.totalCouponsMinted()), 12);
    st3.setFillColor(sf::Color(168, 210, 240));
    st3.setPosition({leftCard.position.x + 22.0f, sy + 54.0f});
    target.draw(st3);

    // RIGHT: World Survey & Megabase Multipliers
    sf::FloatRect rightCard({modal.position.x + modal.size.x * 0.52f + 8.0f, modal.position.y + 62.0f}, {modal.size.x * 0.48f - 28.0f, modal.size.y - 82.0f});
    drawPanel(target, rightCard, sf::Color(20, 26, 36), sf::Color(112, 232, 168), 1.6f);

    sf::Text rTitle(font, "WORLD GEOLOGY & MEGABASE MULTIPLIERS", 16);
    rTitle.setStyle(sf::Text::Bold);
    rTitle.setFillColor(sf::Color(125, 245, 178));
    rTitle.setPosition({rightCard.position.x + 20.0f, rightCard.position.y + 16.0f});
    target.draw(rTitle);

    int waterTiles = 0, rockTiles = 0;
    int oreCounts[7] = {0, 0, 0, 0, 0, 0, 0};
    for (const Tile& t : world.tiles()) {
        if (t.terrain == TerrainType::Water) ++waterTiles;
        else if (t.terrain == TerrainType::Rock) ++rockTiles;
        if (t.ore != OreType::None) {
            int oi = static_cast<int>(t.ore);
            if (oi >= 1 && oi <= 6) ++oreCounts[oi];
        }
    }
    const int totalTiles = GRID_W * GRID_H;
    float waterPct = 100.0f * static_cast<float>(waterTiles) / static_cast<float>(totalTiles);
    float rockPct = 100.0f * static_cast<float>(rockTiles) / static_cast<float>(totalTiles);

    std::vector<std::string> infoLines = {
        "World Seed:   " + world.worldGen().seedText + "  (#" + std::to_string(world.mapSeed()) + ")",
        std::string("World Preset:   ") + worldPresetName(world.worldGen().preset) + (world.isSandboxMode() ? "  [SANDBOX]" : ""),
        "Topography:   " + fmtFloat1(waterPct) + "% Coolant Water   |   " + fmtFloat1(rockPct) + "% Basalt Cliffs",
        "Primary Veins:   " + std::to_string(oreCounts[1]) + " Fe   |   " + std::to_string(oreCounts[2]) + " Cu   |   " + std::to_string(oreCounts[3]) + " Coal",
        "Exotic Veins:   " + std::to_string(oreCounts[4]) + " Titanium   |   " + std::to_string(oreCounts[5]) + " Uranium   |   " + std::to_string(oreCounts[6]) + " Aether",
        "Spire & Prestige Output Multiplier:   " + fmtFloat2(world.globalPrestigeMultiplier()) + "x",
        "Mining Speed / Yield:   " + fmtFloat2(world.drillSpeedMultiplier()) + "x Speed   |   " + fmtFloat2(world.oreYieldMultiplier()) + "x Yield",
        "Factory Craft / Belt Speed:   " + fmtFloat2(world.craftingSpeedMultiplier()) + "x Craft   |   " + fmtFloat2(world.beltSpeedMultiplier()) + "x Belt",
        "Superconducting Grid / Solar:   " + fmtFloat2(world.superconductingMult()) + "x Grid   |   " + fmtFloat2(world.solarPowerMultiplier()) + "x Solar",
        "Hub Drone Collection Radius:   " + std::to_string(world.directHubRadius()) + " tiles around Core",
        "Train Freight Capacity:   " + fmtInt(world.trainCargoCapacity()) + " items  (+" + fmtInt((world.freightBonusMultiplier() - 1.0) * 100.0) + "% delivery bonus)"
    };

    float iy = rightCard.position.y + 56.0f;
    for (const auto& ln : infoLines) {
        sf::Text it(font, ln, 12);
        it.setFillColor(sf::Color(220, 230, 244));
        it.setPosition({rightCard.position.x + 22.0f, iy});
        target.draw(it);
        iy += 34.0f;
    }

    sf::Text foot(font, "Tip: Press [N] to customize seeds & presets, or [O] for 2.5D Ray-Tracing settings!", 11);
    foot.setFillColor(sf::Color(156, 226, 188));
    foot.setPosition({rightCard.position.x + 22.0f, rightCard.position.y + rightCard.size.y - 38.0f});
    target.draw(foot);
}

void UI::renderWorldAndSandboxTab(
    sf::RenderTarget& target,
    const sf::Font& font,
    const World& world,
    const GameSettings& settings,
    sf::Vector2f mousePos,
    float animTime,
    sf::FloatRect modal
) const {
    WorldTabLayout L = computeWorldTabLayout(modal);

    // LEFT CARD: Procedural World Generator & Seed Studio
    drawPanel(target, L.leftCard, sf::Color(20, 26, 38), sf::Color(238, 196, 92), 1.6f);

    sf::Text lTitle(font, "PROCEDURAL WORLD GENERATOR & SEED STUDIO", 16);
    lTitle.setStyle(sf::Text::Bold);
    lTitle.setFillColor(sf::Color(248, 208, 102));
    lTitle.setPosition({L.leftCard.position.x + 18.0f, L.leftCard.position.y + 14.0f});
    target.draw(lTitle);

    sf::Text sLbl(font, "World Seed (Type letters or digits, click Random, then Generate):", 11);
    sLbl.setFillColor(sf::Color(186, 202, 226));
    sLbl.setPosition({L.leftCard.position.x + 18.0f, L.leftCard.position.y + 44.0f});
    target.draw(sLbl);

    // Seed Input Box
    bool caretOn = seedInputFocused_ && (std::fmod(animTime * 2.0f, 1.0f) < 0.55f);
    drawPanel(target, L.seedBox,
              seedInputFocused_ ? sf::Color(12, 18, 28) : sf::Color(16, 22, 34),
              seedInputFocused_ ? sf::Color(105, 232, 255) : sf::Color(92, 110, 140),
              seedInputFocused_ ? 2.0f : 1.3f);

    std::string seedDisp = pendingWorldGen_.seedText + (caretOn ? "|" : "");
    if (pendingWorldGen_.seedText.empty() && !seedInputFocused_) {
        seedDisp = "(Click to enter custom seed)";
    }
    sf::Text seedTxt(font, seedDisp, 13);
    seedTxt.setStyle(sf::Text::Bold);
    seedTxt.setFillColor(pendingWorldGen_.seedText.empty() && !seedInputFocused_
                             ? sf::Color(125, 138, 160)
                             : sf::Color(244, 248, 255));
    seedTxt.setPosition({L.seedBox.position.x + 10.0f, L.seedBox.position.y + 7.0f});
    target.draw(seedTxt);

    drawPanel(target, L.seedRandomBtn,
              L.seedRandomBtn.contains(mousePos) ? sf::Color(52, 74, 104) : sf::Color(36, 50, 72),
              sf::Color(125, 188, 255), 1.3f);
    drawCenteredText(target, font, "Random", 12, L.seedRandomBtn.getCenter(), sf::Color::White, true);

    drawPanel(target, L.seedClearBtn,
              L.seedClearBtn.contains(mousePos) ? sf::Color(72, 44, 52) : sf::Color(44, 32, 40),
              sf::Color(186, 118, 128), 1.3f);
    drawCenteredText(target, font, "Clear", 12, L.seedClearBtn.getCenter(), sf::Color(235, 220, 224), true);

    sf::Text pLbl(font, "Select World Biome & Topography Preset:", 11);
    pLbl.setStyle(sf::Text::Bold);
    pLbl.setFillColor(sf::Color(186, 202, 226));
    pLbl.setPosition({L.leftCard.position.x + 18.0f, L.leftCard.position.y + 104.0f});
    target.draw(pLbl);

    for (int p = 0; p < 5; ++p) {
        WorldPreset wp = static_cast<WorldPreset>(p);
        bool sel = (pendingWorldGen_.preset == wp);
        bool hov = L.presetBtns[p].contains(mousePos);
        drawPanel(target, L.presetBtns[p],
                  sel ? sf::Color(44, 62, 88) : (hov ? sf::Color(32, 40, 58) : sf::Color(24, 30, 44)),
                  sel ? sf::Color(248, 208, 96) : sf::Color(68, 82, 106),
                  sel ? 1.8f : 1.1f);

        sf::Text pn(font, std::string(sel ? "> " : "  ") + worldPresetName(wp), 12);
        pn.setStyle(sf::Text::Bold);
        pn.setFillColor(sel ? sf::Color(255, 232, 152) : sf::Color(225, 232, 245));
        pn.setPosition({L.presetBtns[p].position.x + 10.0f, L.presetBtns[p].position.y + 7.0f});
        target.draw(pn);

        sf::Text ps(font, presetSummary(wp), 10);
        ps.setFillColor(sf::Color(168, 186, 212));
        ps.setPosition({L.presetBtns[p].position.x + 216.0f, L.presetBtns[p].position.y + 9.0f});
        target.draw(ps);
    }

    const char* paramNames[4] = {
        "Ore Vein Richness Multiplier",
        "Ore Deposit Cluster Frequency",
        "Coolant River & Lake Coverage",
        "Basalt Cliff Mesa Coverage"
    };
    const float paramVals[4] = {
        pendingWorldGen_.oreRichness,
        pendingWorldGen_.oreFrequency,
        pendingWorldGen_.waterCoverage,
        pendingWorldGen_.cliffCoverage
    };
    for (int s = 0; s < 4; ++s) {
        float ry = L.stepperMinus[s].position.y;
        sf::Text nm(font, paramNames[s], 12);
        nm.setFillColor(sf::Color(218, 228, 242));
        nm.setPosition({L.leftCard.position.x + 18.0f, ry + 6.0f});
        target.draw(nm);

        drawPanel(target, L.stepperMinus[s],
                  L.stepperMinus[s].contains(mousePos) ? sf::Color(52, 64, 88) : sf::Color(32, 40, 56),
                  sf::Color(96, 118, 152), 1.2f);
        drawCenteredText(target, font, "-", 15, L.stepperMinus[s].getCenter(), sf::Color::White, true);

        sf::FloatRect valPill({L.stepperMinus[s].position.x + 38.0f, ry}, {70.0f, 28.0f});
        drawPanel(target, valPill, sf::Color(16, 20, 30), sf::Color(68, 82, 108), 1.0f);
        drawCenteredText(target, font, fmtFloat2(paramVals[s]) + "x", 12, valPill.getCenter(), sf::Color(125, 232, 255), true);

        drawPanel(target, L.stepperPlus[s],
                  L.stepperPlus[s].contains(mousePos) ? sf::Color(52, 64, 88) : sf::Color(32, 40, 56),
                  sf::Color(96, 118, 152), 1.2f);
        drawCenteredText(target, font, "+", 15, L.stepperPlus[s].getCenter(), sf::Color::White, true);
    }

    drawPanel(target, L.newSandboxBtn,
              pendingNewWorldSandbox_ ? sf::Color(28, 68, 48) : sf::Color(28, 34, 48),
              pendingNewWorldSandbox_ ? sf::Color(110, 245, 168) : sf::Color(82, 96, 122), 1.4f);
    drawCenteredText(target, font,
                     pendingNewWorldSandbox_
                         ? "[x] Start New World in SANDBOX MODE (Free Build & All Tech)"
                         : "[ ] Start New World in SANDBOX MODE (Click to Toggle)",
                     12, L.newSandboxBtn.getCenter(),
                     pendingNewWorldSandbox_ ? sf::Color(152, 255, 196) : sf::Color(208, 218, 235), true);

    bool gHov = L.generateBtn.contains(mousePos);
    drawPanel(target, L.generateBtn,
              gHov ? sf::Color(72, 162, 102) : sf::Color(48, 122, 76),
              sf::Color(138, 252, 178), 2.0f);
    drawCenteredText(target, font, "GENERATE NEW 192x144 PROCEDURAL WORLD!", 14, L.generateBtn.getCenter(), sf::Color::White, true);

    // RIGHT CARD: Sandbox & Creative Director
    drawPanel(target, L.rightCard, sf::Color(20, 28, 34), sf::Color(110, 238, 168), 1.6f);

    sf::Text rTitle(font, "SANDBOX & CREATIVE DIRECTOR", 16);
    rTitle.setStyle(sf::Text::Bold);
    rTitle.setFillColor(sf::Color(122, 248, 178));
    rTitle.setPosition({L.rightCard.position.x + 18.0f, L.rightCard.position.y + 14.0f});
    target.draw(rTitle);

    sf::FloatRect curBox({L.rightCard.position.x + 18.0f, L.rightCard.position.y + 46.0f},
                         {L.rightCard.size.x - 36.0f, 66.0f});
    drawPanel(target, curBox, sf::Color(14, 20, 28), sf::Color(68, 96, 118), 1.2f);
    sf::Text ci1(font, "Current Active Seed:  " + world.worldGen().seedText + "   (#" + std::to_string(world.mapSeed()) + ")", 12);
    ci1.setStyle(sf::Text::Bold);
    ci1.setFillColor(sf::Color(246, 208, 106));
    ci1.setPosition({curBox.position.x + 12.0f, curBox.position.y + 10.0f});
    target.draw(ci1);

    sf::Text ci2(font, std::string("Active Preset:  ") + worldPresetName(world.worldGen().preset) +
                       "   |   Status:  " + (world.isSandboxMode() ? "SANDBOX ACTIVE" : "STANDARD PROGRESSION"), 11);
    ci2.setFillColor(world.isSandboxMode() ? sf::Color(118, 245, 172) : sf::Color(190, 208, 232));
    ci2.setPosition({curBox.position.x + 12.0f, curBox.position.y + 36.0f});
    target.draw(ci2);

    const std::string cLabels[4] = {
        world.isSandboxMode() ? "SANDBOX MODE: ON (Click or [B] for Standard)" : "ENABLE LIVE SANDBOX MODE [B] (Zero-Cost Build)",
        "UNLOCK ALL 40 TECHNOLOGIES INSTANTLY",
        "GRANT +10,000 ALL RESOURCES & +20 COUPONS",
        "COMPLETE CURRENT GRAND SPIRE PHASE"
    };
    for (int c = 0; c < 4; ++c) {
        bool hov = L.creativeBtns[c].contains(mousePos);
        bool activeHighlight = (c == 0 && world.isSandboxMode());
        sf::Color fill = activeHighlight
            ? (hov ? sf::Color(46, 118, 76) : sf::Color(34, 88, 58))
            : (hov ? sf::Color(44, 58, 82) : sf::Color(30, 40, 58));
        sf::Color out = activeHighlight ? sf::Color(125, 250, 180) : sf::Color(98, 146, 205);
        drawPanel(target, L.creativeBtns[c], fill, out, 1.4f);
        drawCenteredText(target, font, cLabels[c], 12, L.creativeBtns[c].getCenter(), sf::Color::White, true);
    }

    sf::Text atmLbl(font, "Solar & Ray-Tracing Lighting Override:", 12);
    atmLbl.setStyle(sf::Text::Bold);
    atmLbl.setFillColor(sf::Color(186, 214, 242));
    atmLbl.setPosition({L.rightCard.position.x + 18.0f, L.rightCard.position.y + 334.0f});
    target.draw(atmLbl);

    const char* modeLabels[4] = {
        "Dynamic Day/Night",
        "Always Daylight",
        "Always Neon Night",
        "Golden Dusk Shadows"
    };
    for (int m = 0; m < 4; ++m) {
        bool sel = (settings.lightingTimeMode == m);
        bool hov = L.timeModeBtns[m].contains(mousePos);
        drawPanel(target, L.timeModeBtns[m],
                  sel ? sf::Color(48, 68, 102) : (hov ? sf::Color(32, 42, 60) : sf::Color(24, 30, 44)),
                  sel ? sf::Color(115, 232, 255) : sf::Color(70, 84, 108),
                  sel ? 1.8f : 1.1f);
        drawCenteredText(target, font, modeLabels[m], 11, L.timeModeBtns[m].getCenter(),
                         sel ? sf::Color(255, 240, 175) : sf::Color(205, 218, 238), true);
    }

    sf::FloatRect noteBox({L.rightCard.position.x + 18.0f, L.rightCard.position.y + 462.0f},
                          {L.rightCard.size.x - 36.0f, L.rightCard.size.y - 480.0f});
    if (noteBox.size.y >= 64.0f) {
        drawPanel(target, noteBox, sf::Color(15, 20, 28), sf::Color(58, 82, 96), 1.1f);
        const char* notes[4] = {
            "Sandbox Perks:",
            "- All 40 buildable machines, belts, reactors & trains unlocked",
            "- Building, Blueprints, Research, Spire & Coupons cost 0 resources",
            "- Fuel-burning machines auto-run; press [F] to fill any machine buffer!"
        };
        for (int i = 0; i < 4; ++i) {
            sf::Text nt(font, notes[i], 11);
            if (i == 0) nt.setStyle(sf::Text::Bold);
            nt.setFillColor(i == 0 ? sf::Color(125, 242, 178) : sf::Color(192, 210, 228));
            nt.setPosition({noteBox.position.x + 12.0f, noteBox.position.y + 10.0f + static_cast<float>(i) * 20.0f});
            target.draw(nt);
        }
    }
}

void UI::renderSettingsTab(
    sf::RenderTarget& target,
    const sf::Font& font,
    const Renderer& renderer,
    const GameSettings& settings,
    sf::Vector2f mousePos,
    sf::FloatRect modal
) const {
    SettingsTabLayout S = computeSettingsTabLayout(modal);

    // LEFT CARD: 2.5D Engine, Ray-Tracing & Shaders
    drawPanel(target, S.leftCard, sf::Color(20, 24, 38), sf::Color(102, 224, 255), 1.6f);

    sf::Text lTitle(font, "2.5D ENGINE, RAY-TRACING & SHADERS", 16);
    lTitle.setStyle(sf::Text::Bold);
    lTitle.setFillColor(sf::Color(118, 232, 255));
    lTitle.setPosition({S.leftCard.position.x + 18.0f, S.leftCard.position.y + 14.0f});
    target.draw(lTitle);

    sf::FloatRect gpuBadge({S.leftCard.position.x + S.leftCard.size.x - 144.0f, S.leftCard.position.y + 12.0f}, {126.0f, 24.0f});
    bool gpuOk = renderer.isShaderAvailable();
    drawPanel(target, gpuBadge, gpuOk ? sf::Color(20, 56, 44) : sf::Color(62, 32, 32),
              gpuOk ? sf::Color(98, 238, 168) : sf::Color(235, 108, 98), 1.1f);
    drawCenteredText(target, font, gpuOk ? "GLSL RT READY" : "CPU FALLBACK", 10, gpuBadge.getCenter(), sf::Color::White, true);

    const char* leftLabels[8] = {
        "Camera Projection [F9]",
        "GLSL Shader Pipeline",
        "Ray-Tracing Quality [F10]",
        "2.5D Extrusion Depth",
        "HDR Emissive Bloom",
        "Heightfield Contact SSAO",
        "RT Planar Water Reflections",
        "Post-FX Color Filter"
    };

    std::string extrStr = (settings.extrusionDepth < 0.8f) ? "Subtle (0.6x)"
                        : (settings.extrusionDepth < 1.2f ? "Standard (1.0x)" : "Monumental (1.4x)");
    std::string bloomStr = (settings.bloomStrength < 0.1f) ? "Off"
                         : (settings.bloomStrength < 0.65f ? "Subtle"
                         : (settings.bloomStrength < 1.1f ? "Standard" : "Intense"));
    std::string postStr = (settings.postFxMode == 0) ? "Clean"
                        : (settings.postFxMode == 1 ? "Cinematic Vignette" : "Retro CRT");

    std::string leftVals[8] = {
        settings.mode25D ? "2.5D Oblique Extruded" : "Classic Top-Down 2D",
        settings.shadersEnabled ? "Enabled (Multi-Buffer)" : "Disabled",
        rtQualityLabel(settings.rtQuality),
        extrStr,
        bloomStr,
        settings.ambientOcclusion ? "Enabled" : "Disabled",
        settings.waterReflections ? "Enabled" : "Disabled",
        postStr
    };

    for (int i = 0; i < 8; ++i) {
        float ry = S.leftBtns[i].position.y;
        sf::Text lbl(font, leftLabels[i], 12);
        lbl.setStyle(sf::Text::Bold);
        lbl.setFillColor(sf::Color(220, 230, 245));
        lbl.setPosition({S.leftCard.position.x + 18.0f, ry + 9.0f});
        target.draw(lbl);

        bool hov = S.leftBtns[i].contains(mousePos);
        bool activeCol = (i == 0 && settings.mode25D) ||
                         (i == 1 && settings.shadersEnabled) ||
                         (i == 2 && settings.rtQuality > 0) ||
                         (i == 5 && settings.ambientOcclusion) ||
                         (i == 6 && settings.waterReflections);
        drawPanel(target, S.leftBtns[i],
                  hov ? sf::Color(48, 66, 92) : (activeCol ? sf::Color(28, 48, 68) : sf::Color(28, 34, 48)),
                  activeCol ? sf::Color(102, 224, 255) : sf::Color(78, 92, 118), 1.3f);
        drawCenteredText(target, font, leftVals[i], 12, S.leftBtns[i].getCenter(),
                         activeCol ? sf::Color(185, 246, 255) : sf::Color(225, 232, 245), true);
    }

    sf::FloatRect rtInfo({S.leftCard.position.x + 18.0f, S.leftBtns[7].position.y + 48.0f},
                         {S.leftCard.size.x - 36.0f, S.resetBtn.position.y - (S.leftBtns[7].position.y + 60.0f)});
    if (rtInfo.size.y >= 48.0f) {
        drawPanel(target, rtInfo, sf::Color(14, 18, 28), sf::Color(54, 72, 98), 1.1f);
        const char* info[3] = {
            "G-Buffer Ray-Tracing Pipeline: Albedo + Normal/Spec/Height + HDR Emissive",
            "Ray-marches 2.5D heightfield Sun/Moon shadows, 8..24-ray occluded colored GI,",
            "planar water reflections, contact SSAO, ACES tonemapping & exact tile picking."
        };
        for (int i = 0; i < 3; ++i) {
            sf::Text it(font, info[i], 10);
            it.setFillColor(i == 0 ? sf::Color(135, 228, 255) : sf::Color(182, 198, 220));
            it.setPosition({rtInfo.position.x + 10.0f, rtInfo.position.y + 8.0f + static_cast<float>(i) * 17.0f});
            target.draw(it);
        }
    }

    drawPanel(target, S.resetBtn,
              S.resetBtn.contains(mousePos) ? sf::Color(92, 46, 48) : sf::Color(58, 32, 36),
              sf::Color(232, 118, 112), 1.4f);
    drawCenteredText(target, font, "RESET ALL GRAPHICS & GAMEPLAY SETTINGS TO DEFAULTS", 12, S.resetBtn.getCenter(), sf::Color::White, true);

    // RIGHT CARD: Display, Audio & Gameplay
    drawPanel(target, S.rightCard, sf::Color(20, 26, 38), sf::Color(238, 196, 92), 1.6f);

    sf::Text rTitle(font, "DISPLAY, AUDIO & GAMEPLAY SETTINGS", 16);
    rTitle.setStyle(sf::Text::Bold);
    rTitle.setFillColor(sf::Color(248, 208, 102));
    rTitle.setPosition({S.rightCard.position.x + 18.0f, S.rightCard.position.y + 14.0f});
    target.draw(rTitle);

    const char* rightLabels[11] = {
        "Pixelation Scale",
        "Window Mode [F11]",
        "Vertical Sync (VSync)",
        "Frame Rate Limit",
        "Show FPS & RT Telemetry",
        "Master Audio Volume",
        "Particle Effects Density",
        "Floating Resource Popups",
        "World Grid Overlay",
        "Auto-Save Interval",
        "Camera Pan Speed"
    };

    const char* pixNames[5] = {"", "1x Native HD", "2x Fine Pixel", "3x Classic Pixel", "4x Chunky Retro"};
    std::string fpsStr = (settings.fpsLimit == 0) ? "Unlimited" : (std::to_string(settings.fpsLimit) + " FPS");
    const char* partNames[3] = {"Off", "Normal (1x)", "Lush (2x)"};
    const char* gridNames[3] = {"Hidden", "Subtle", "Bright"};
    std::string autoSaveStr = (settings.autoSaveSec == 0) ? "Off" : ("Every " + std::to_string(settings.autoSaveSec) + "s");

    std::string rightVals[11] = {
        pixNames[std::clamp(settings.pixelScale, 1, 4)],
        settings.fullscreen ? "Fullscreen" : "Windowed",
        settings.vsync ? "Enabled" : "Disabled",
        fpsStr,
        settings.showFps ? "Shown" : "Hidden",
        "",
        partNames[std::clamp(settings.particleDensity, 0, 2)],
        settings.showPopups ? "Shown" : "Hidden",
        gridNames[std::clamp(settings.gridStyle, 0, 2)],
        autoSaveStr,
        fmtFloat1(settings.cameraSpeed) + "x"
    };

    for (int i = 0; i < 11; ++i) {
        float ry = S.rightBtns[i].position.y;
        sf::Text lbl(font, rightLabels[i], 12);
        lbl.setFillColor(sf::Color(220, 230, 245));
        lbl.setPosition({S.rightCard.position.x + 18.0f, ry + 7.0f});
        target.draw(lbl);

        if (i == 5) {
            drawPanel(target, S.volMinus,
                      S.volMinus.contains(mousePos) ? sf::Color(52, 64, 88) : sf::Color(32, 40, 56),
                      sf::Color(96, 118, 152), 1.2f);
            drawCenteredText(target, font, "-", 15, S.volMinus.getCenter(), sf::Color::White, true);

            int pct = static_cast<int>(std::round(settings.masterVolume * 100.0f));
            std::string vStr = settings.muted ? "MUTED" : (std::to_string(pct) + "%");
            drawPanel(target, S.volCenter,
                      S.volCenter.contains(mousePos) ? sf::Color(44, 54, 74) : sf::Color(24, 30, 44),
                      settings.muted ? sf::Color(235, 102, 96) : sf::Color(96, 212, 158), 1.2f);
            drawCenteredText(target, font, vStr, 12, S.volCenter.getCenter(),
                             settings.muted ? sf::Color(255, 138, 132) : sf::Color(152, 245, 186), true);

            drawPanel(target, S.volPlus,
                      S.volPlus.contains(mousePos) ? sf::Color(52, 64, 88) : sf::Color(32, 40, 56),
                      sf::Color(96, 118, 152), 1.2f);
            drawCenteredText(target, font, "+", 15, S.volPlus.getCenter(), sf::Color::White, true);
        } else {
            bool hov = S.rightBtns[i].contains(mousePos);
            drawPanel(target, S.rightBtns[i],
                      hov ? sf::Color(48, 60, 84) : sf::Color(28, 36, 52),
                      sf::Color(86, 102, 132), 1.2f);
            drawCenteredText(target, font, rightVals[i], 11, S.rightBtns[i].getCenter(), sf::Color(236, 242, 250), true);
        }
    }

    drawPanel(target, S.saveBtn,
              S.saveBtn.contains(mousePos) ? sf::Color(48, 108, 72) : sf::Color(32, 78, 52),
              sf::Color(115, 238, 165), 1.4f);
    drawCenteredText(target, font, "SAVE FACTORY [F5]", 12, S.saveBtn.getCenter(), sf::Color::White, true);

    drawPanel(target, S.loadBtn,
              S.loadBtn.contains(mousePos) ? sf::Color(48, 76, 112) : sf::Color(32, 54, 82),
              sf::Color(115, 198, 255), 1.4f);
    drawCenteredText(target, font, "LOAD FACTORY [F8]", 12, S.loadBtn.getCenter(), sf::Color::White, true);
}

} // namespace cogwill
