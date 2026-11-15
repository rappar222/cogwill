#pragma once

#include "World.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace cogwill {

struct GearColors {
    sf::Color outline;
    sf::Color toothLight;
    sf::Color toothDark;
    sf::Color rim;
    sf::Color spoke;
    sf::Color hub;
    sf::Color pin;
};

class Renderer {
public:
    Renderer();

    void renderScene(
        sf::RenderTarget& finalTarget,
        const sf::View& worldView,
        const World& world,
        const GameSettings& settings,
        float animTime,
        std::optional<BuildingType> ghostType,
        int hoverGx,
        int hoverGy,
        int ghostDir,
        const PlacementCheck& ghostCheck,
        int selectedGx,
        int selectedGy,
        const std::vector<BlueprintEntry>* blueprint = nullptr,
        int bpRot = 0,
        std::optional<std::pair<int, int>> copyBoxStart = std::nullopt
    ) const;

    void renderWorld(
        sf::RenderTarget& target,
        const World& world,
        const GameSettings& settings,
        float animTime,
        std::optional<BuildingType> ghostType,
        int hoverGx,
        int hoverGy,
        int ghostDir,
        const PlacementCheck& ghostCheck,
        int selectedGx,
        int selectedGy,
        const std::vector<BlueprintEntry>* blueprint = nullptr,
        int bpRot = 0,
        std::optional<std::pair<int, int>> copyBoxStart = std::nullopt,
        sf::RenderTarget* matTarget = nullptr,
        sf::RenderTarget* emisTarget = nullptr
    ) const;

    void renderPopups(
        sf::RenderTarget& target,
        const World& world,
        const GameSettings& settings,
        const sf::Font& font,
        const sf::View& worldView
    ) const;

    void drawBuildingPreviewIcon(
        sf::RenderTarget& target,
        BuildingType type,
        sf::Vector2f center,
        float scale,
        float angle
    ) const;

    bool isShaderAvailable() const { return shaderLoaded_; }

private:
    void buildPixelTextures();
    void loadShaders();
    void ensureGBufferSize(unsigned int w, unsigned int h) const;

    static sf::VertexArray buildGearVertexArray(
        int teeth,
        float pitchRadius,
        float rootRadius,
        float outerRadius,
        float innerRimRadius,
        int spokeCount,
        const GearColors& colors
    );

    void drawBaseTerrainAndOres(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        sf::RenderTarget* emisTarget,
        const World& world,
        const GameSettings& settings,
        float animTime
    ) const;

    void drawRockCliff25D(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        const World& world,
        int gx,
        int gy,
        float extrusion,
        bool cpuSunTint
    ) const;

    void drawRailBuilding(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        sf::RenderTarget* emisTarget,
        const World* world,
        const BuildingEntity& b,
        const GameSettings& settings,
        float animTime,
        bool isGhost,
        bool ghostValid
    ) const;

    void drawBeltTrack(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        sf::RenderTarget* emisTarget,
        const World* world,
        const BuildingEntity& b,
        const GameSettings& settings,
        float animTime,
        bool isGhost,
        bool ghostValid
    ) const;

    void drawBeltItems(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        sf::RenderTarget* emisTarget,
        const BuildingEntity& b,
        const GameSettings& settings
    ) const;

    void drawResourceItem(
        sf::RenderTarget& target,
        sf::RenderTarget* emisTarget,
        ResourceType type,
        sf::Vector2f pos,
        bool mode25D
    ) const;

    void drawBuilding(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        sf::RenderTarget* emisTarget,
        const BuildingEntity& b,
        const GameSettings& settings,
        float animTime,
        bool isGhost,
        bool ghostValid,
        const World* world = nullptr
    ) const;

    void drawPowerLines(
        sf::RenderTarget& target,
        sf::RenderTarget* emisTarget,
        const World& world,
        const GameSettings& settings,
        float animTime
    ) const;

    void drawSingleTrain(
        sf::RenderTarget& target,
        sf::RenderTarget* matTarget,
        sf::RenderTarget* emisTarget,
        const World& world,
        const TrainEntity& tr,
        const GameSettings& settings
    ) const;

    void drawGhostMeshingGuides(
        sf::RenderTarget& target,
        sf::RenderTarget* emisTarget,
        const World& world,
        BuildingType type,
        int gx,
        int gy,
        int dir,
        float animTime
    ) const;

    void drawParticles(
        sf::RenderTarget& target,
        sf::RenderTarget* emisTarget,
        const World& world
    ) const;

    void drawStatusBadge(sf::RenderTarget& target, sf::Vector2f pos, sf::Color ringCol) const;

    sf::VertexArray ironCog12_;
    sf::VertexArray copperCog12_;
    sf::VertexArray brassGear36_;
    sf::VertexArray darkCollar12_;
    sf::VertexArray darkGear36_;
    sf::VertexArray flywheelWheel_;
    sf::VertexArray shadowCog12_;
    sf::VertexArray shadowGear36_;

    sf::Texture bedrockTex_;
    std::array<sf::Texture, 2> waterTex_;
    sf::Texture rockTex_;
    std::array<sf::Texture, 6> oreTex_;
    std::array<sf::Texture, 6> oreRichTex_;

    mutable sf::RenderTexture iconRT_;
    mutable sf::RenderTexture albedoRT_;
    mutable sf::RenderTexture materialRT_;
    mutable sf::RenderTexture emissiveRT_;
    mutable unsigned int gbufW_{0};
    mutable unsigned int gbufH_{0};

    mutable sf::Shader rtShader_;
    bool shaderLoaded_{false};
};

} // namespace cogwill
