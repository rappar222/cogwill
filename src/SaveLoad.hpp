#pragma once

#include "Types.hpp"
#include "World.hpp"

#include <string>

namespace cogwill {

class SaveLoad {
public:
    static bool saveToFile(const World& world, const std::string& path, std::string* errorOut = nullptr);
    static bool loadFromFile(World& world, const std::string& path, std::string* errorOut = nullptr);

    static bool saveSettings(const GameSettings& settings, const std::string& path, std::string* errorOut = nullptr);
    static bool loadSettings(GameSettings& settings, const std::string& path, std::string* errorOut = nullptr);
};

} // namespace cogwill
