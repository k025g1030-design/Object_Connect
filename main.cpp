#include "RetroFPS/Core/Application.hpp"
#include "RetroFPS/Game/Game.hpp"

#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    fps::Application application;
    fps::Game game;
    return application.Run(game);
}
