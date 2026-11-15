#include "Game.hpp"

#include <cstring>
#include <string>

int main(int argc, char** argv) {
    cogwill::Game game;
    if (argc >= 3 && std::strcmp(argv[1], "--smoke-render") == 0) {
        return game.runSmokeRender(argv[2]);
    }
    return game.run();
}
