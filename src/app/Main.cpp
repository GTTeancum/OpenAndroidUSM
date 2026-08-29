#include "app/Application.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    usm::Application application;
    return application.run(instance);
}

