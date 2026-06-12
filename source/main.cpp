#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include <switch.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#ifdef DEBUG
    #include <twili.h>
#endif

extern "C" {
    #include "common.h"
    #include "textures.h"
    #include "MenuChooser.h"
    #include "menu_book_reader.h"
    #include "fs.h"
    #include "config.h"
}

SDL_Renderer* RENDERER;
SDL_Window* WINDOW;
SDL_Event EVENT;
TTF_Font *ROBOTO_35, *ROBOTO_30, *ROBOTO_27, *ROBOTO_25, *ROBOTO_20, *ROBOTO_15;
bool configDarkMode;
int  configColorMode = ColorModeDark;

// Keep the legacy configDarkMode flag in sync with the colour mode.
// The UI uses "dark styling" for both Dark and Night modes.
extern "C" int config_is_dark_ui(void) {
    return (configColorMode != ColorModeLight) ? 1 : 0;
}

extern "C" void config_cycle_color_mode(void) {
    configColorMode = (configColorMode + 1) % 3; // light -> dark -> night -> light
    configDarkMode = config_is_dark_ui();
}

void Term_Services() {
    std::cout << "Terminate Serices" << std::endl;

    timeExit();
    TTF_CloseFont(ROBOTO_35);
    TTF_CloseFont(ROBOTO_30);
    TTF_CloseFont(ROBOTO_27);
    TTF_CloseFont(ROBOTO_25);
    TTF_CloseFont(ROBOTO_20);
    TTF_CloseFont(ROBOTO_15);
    TTF_Quit();

    Textures_Free();
    romfsExit();

    IMG_Quit();

    SDL_DestroyRenderer(RENDERER);
    //SDL_FreeSurface(WINDOW_SURFACE);
    SDL_DestroyWindow(WINDOW);
    SDL_Quit();

    #ifdef DEBUG
        twiliExit();
    #endif
}

void Init_Services() {
    #ifdef DEBUG
        twiliInitialize();
    #endif

    std::cout << "Initalize Serices" << std::endl;

    romfsInit();
    std::cout << "Initalized RomFs" << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        SDL_Log("SDL_Init: %s\n", SDL_GetError());
        Term_Services();
    }
    std::cout << "Initalized SDL" << std::endl;

    timeInitialize();
    std::cout << "Initalized Time" << std::endl;

    // PRESENTVSYNC caps presentation to the display's refresh rate (~60fps).
    // Without it the reader loop spins as fast as possible, redrawing the same
    // static page thousands of times a second - pegging the GPU and draining
    // the battery. Vsync also removes tearing.
    if (SDL_CreateWindowAndRenderer(1280, 720, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC, &WINDOW, &RENDERER) == -1)  {
        SDL_Log("SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        Term_Services();
    }
    std::cout << "Initalized Window and Renderer" << std::endl;

    SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_BLEND);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

    if (!IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG)) {
        SDL_Log("IMG_Init: %s\n", IMG_GetError());
        Term_Services();
    }
    std::cout << "Initalized Image" << std::endl;

    if(TTF_Init() == -1) {
        SDL_Log("TTF_Init: %s\n", TTF_GetError());
        Term_Services();
    }
    std::cout << "Initalized TTF" << std::endl;

    Textures_Load();
    std::cout << "Loaded Textures" << std::endl;

    ROBOTO_35 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 35);
    ROBOTO_30 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 30);
    ROBOTO_27 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 27);
    ROBOTO_25 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 25);
    ROBOTO_20 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 20);
    ROBOTO_15 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 15);
    if (!ROBOTO_35 || !ROBOTO_25 || !ROBOTO_15) {
        std::cout << "Failure to retrieve fonts" << std::endl;
        Term_Services();
    }
    std::cout << "Retrevied Fonts" << std::endl;
    
    for (int i = 0; i < 2; i++) {
        if (SDL_JoystickOpen(i) == NULL) {
            SDL_Log("SDL_JoystickOpen: %s\n", SDL_GetError());
            Term_Services();
        }
    }
    std::cout << "Initalized Input" << std::endl;

    FS_RecursiveMakeDir("/switch/eBookReader/books");
    std::cout << "Created book directory if needed" << std::endl;

    configColorMode = ColorModeDark;
    configDarkMode = config_is_dark_ui();
}

int main(int argc, char *argv[]) {
    Init_Services();

    if (argc == 2) {
        Menu_OpenBook(argv[1]);
    } else {
        Menu_StartChoosing();
    }

    Term_Services();
    return 0;
}
