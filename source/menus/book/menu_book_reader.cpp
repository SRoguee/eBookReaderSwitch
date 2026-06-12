extern "C" {
    #include "menu_book_reader.h"
    #include "MenuChooser.h"
    #include "common.h"
    #include "config.h"
    #include "SDL_helper.h"
}

#include <iostream>
#include "BookReader.hpp"

void Menu_OpenBook(char *path) {
    BookReader *reader = NULL;
    int result = 0;

    // The book open (especially EPUB pagination) can take a moment, so show a
    // "Loading..." splash first instead of leaving the screen frozen.
    {
        SDL_Color back = configDarkMode ? BACK_BLACK : BACK_WHITE;
        SDL_Color text = configDarkMode ? WHITE : BLACK;
        SDL_ClearScreen(RENDERER, back);
        SDL_RenderClear(RENDERER);
        const char *msg = "Loading...";
        int tw = 0, th = 0;
        TTF_SizeText(ROBOTO_30, msg, &tw, &th);
        SDL_DrawText(RENDERER, ROBOTO_30, (1280 - tw) / 2, (720 - th) / 2, text, msg);
        SDL_RenderPresent(RENDERER);
    }

    reader = new BookReader(path, &result);
    
    if(result < 0){
        std::cout << "Menu_OpenBook: document not loaded" << std::endl;
    }
    
    /*TouchInfo touchInfo;
    Touch_Init(&touchInfo);*/
    hidInitializeTouchScreen();

    s32 prev_touchcount=0;
    bool helpMenu = false;
    
    // Configure our supported input layout: a single player with standard controller syles
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    PadState pad;
    padInitializeDefault(&pad);
    //Touch_Process(&touchInfo);

    while(result >= 0 && appletMainLoop()) {
        reader->draw(helpMenu);
        
	padUpdate(&pad);

	u64 kDown = padGetButtonsDown(&pad);
	u64 kHeld = padGetButtons(&pad);	
	u64 kUp = padGetButtonsUp(&pad);

	HidTouchScreenState state={0};

	// Only register a touch as a tap when the screen transitions from
	// "not touched" to "touched". This prevents a single hold from
	// flipping many pages on consecutive frames.
	bool newTouch = false;
	if(hidGetTouchScreenStates(&state, 1)) {
		if(state.count != prev_touchcount) {
			if(prev_touchcount == 0 && state.count > 0)
				newTouch = true;
			prev_touchcount = state.count;
		}
	}

	for(s32 i=0; newTouch && i<state.count; i++) {
		// If the chapter list is open, it captures all taps.
		if (reader->showChapters) {
			reader->handle_chapter_menu(state.touches[i].x, state.touches[i].y);
			continue;
		}

		// First give the touch overlay menu a chance to handle this tap.
		// It opens when the centre strip is tapped, and while open it
		// captures taps for its buttons. If it consumes the tap, skip the
		// normal page-turn / zoom touch handling below.
		bool exitBook = false;
		if (reader->handle_touch_menu(state.touches[i].x, state.touches[i].y, &exitBook)) {
			if (exitBook) {
				// Give immediate visual feedback, then leave. Without this the
				// exit can feel laggy because the chooser has to re-scan books.
				reader->draw_exiting_overlay();
				result = -1;   // leave the reader loop
				break;         // stop processing further touches this frame
			}
			continue;
		}

		if (state.touches[i].x > 1000 && (state.touches[i].y > 200 && state.touches[i].y < 500))
			if (reader->currentPageLayout() == BookPageLayoutPortrait)
				reader->next_page(1);
			else if (reader->currentPageLayout() == BookPageLayoutLandscape)
				reader->zoom_in();

		if (state.touches[i].x < 280 && (state.touches[i].y > 200 && state.touches[i].y < 500))
			if (reader->currentPageLayout() == BookPageLayoutPortrait)
				reader->previous_page(1);
			else if (reader->currentPageLayout() == BookPageLayoutLandscape)
				reader->zoom_out();

		if (state.touches[i].y < 200)
			if (reader->currentPageLayout() == BookPageLayoutPortrait)
				reader->zoom_in();
			else if (reader->currentPageLayout() == BookPageLayoutLandscape)
				reader->previous_page(1);

		if (state.touches[i].y > 500)
			if (reader->currentPageLayout() == BookPageLayoutPortrait)
				reader->zoom_out();
			else if (reader->currentPageLayout() == BookPageLayoutLandscape)
				reader->next_page(1);
	}

        // While the chapter list is open it captures all button input:
        // Up/Down move the selection, A jumps to it, B/X closes the list.
        if (reader->showChapters) {
            if ((kDown & HidNpadButton_Up) || (kDown & HidNpadButton_StickLUp))
                reader->chapters_move(-1);
            else if ((kDown & HidNpadButton_Down) || (kDown & HidNpadButton_StickLDown))
                reader->chapters_move(1);
            else if (kDown & HidNpadButton_L)
                reader->chapters_move(-reader->chapter_page_step());
            else if (kDown & HidNpadButton_R)
                reader->chapters_move(reader->chapter_page_step());
            else if (kDown & HidNpadButton_A)
                reader->chapters_select();
            else if ((kDown & HidNpadButton_B) || (kDown & HidNpadButton_X))
                reader->showChapters = false;

            // Don't fall through to the normal reader controls this frame.
            continue;
        }

        // While the touch menu is open, swallow page/zoom button input so
        // physical buttons don't act on the book underneath. B closes it.
        if (reader->showTouchMenu) {
            if (kDown & HidNpadButton_B) {
                reader->showTouchMenu = false;
            }
        }

        if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_Left) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->previous_page(1);
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->zoom_out();
            }
        } else if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_Right) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->next_page(1);
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->zoom_in();
            }
        }

        if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_R) {
            reader->next_page(10);
        } else if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_L) {
            reader->previous_page(10);
        }

        if (!helpMenu && !reader->showTouchMenu && ((kDown & HidNpadButton_Up) || (kHeld & HidNpadButton_StickRUp))) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->zoom_in();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->previous_page(1);
            }
        } else if (!helpMenu && !reader->showTouchMenu && ((kDown & HidNpadButton_Down) || (kHeld & HidNpadButton_StickRDown))) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->zoom_out();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->next_page(1);
            }
        }

        if (!helpMenu && !reader->showTouchMenu && kHeld & HidNpadButton_StickLUp) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->move_page_up();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_right();
            }
        } else if (!helpMenu && !reader->showTouchMenu && kHeld & HidNpadButton_StickLDown) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->move_page_down();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_left();
            }
        } else if (!helpMenu && !reader->showTouchMenu && kHeld & HidNpadButton_StickLRight) {
            if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_down();
            }
        } else if (!helpMenu && !reader->showTouchMenu && kHeld & HidNpadButton_StickLLeft) {
            if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_up();
            }
        }

	if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_LeftSR)
		reader->next_page(10);
	else if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_LeftSL)
		reader->previous_page(10);

        if (kUp & HidNpadButton_B) {
            if (helpMenu) {
                helpMenu = !helpMenu;
            } else if (reader->showTouchMenu) {
                // The kDown handler already closed the menu; just don't exit.
            } else {
                break;
            }
        }

        if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_X) {
            reader->permStatusBar = !reader->permStatusBar;
        }
            
        if ((!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_StickL) || kDown & HidNpadButton_StickR) {
            reader->reset_page();
        }
        
        if (!helpMenu && !reader->showTouchMenu && kDown & HidNpadButton_Y) {
            reader->switch_page_layout();
        }

        if (!helpMenu && !reader->showTouchMenu && kUp & HidNpadButton_Minus) {
            // Cycle Light -> Dark -> Night with the physical theme button.
            config_cycle_color_mode();
            reader->previous_page(0);
        }

        if (!reader->showTouchMenu && kDown & HidNpadButton_Plus) {
            helpMenu = !helpMenu;
        }
 
        /*if (touchInfo.state == TouchEnded && touchInfo.tapType != TapNone) {
            float tapRegion = 120;
            
            switch (reader->currentPageLayout()) {
                case BookPageLayoutPortrait:
                    if (tapped_inside(touchInfo, 0, 0, tapRegion, 720))
                        reader->previous_page(1);
                    else if (tapped_inside(touchInfo, 1280 - tapRegion, 0, 1280, 720))
                        reader->next_page(1);
                    break;
                case BookPageLayoutLandscape:
                    if (tapped_inside(touchInfo, 0, 0, 1280, tapRegion))
                        reader->previous_page(1);
                    else if (tapped_inside(touchInfo, 0, 720 - tapRegion, 1280, 720))
                        reader->next_page(1);
                    reader->reset_page();
                    break;
            }
        }*/
    }

    std::cout << "Exiting reader" << std::endl;
    std::cout << "Opening chooser" << std::endl;
    Menu_StartChoosing();
    delete reader;
    // consoleExit(NULL);
}
