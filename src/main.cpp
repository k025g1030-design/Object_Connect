#include "ObjectConnect/Core/Application.hpp"
#include "ObjectConnect/Game/Game.hpp"

#include <Windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    object_connect::Application application;
    object_connect::Game game;
    return application.Run(game);
}
