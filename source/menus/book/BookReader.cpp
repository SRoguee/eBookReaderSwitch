#include "BookReader.hpp"
#include "PageLayout.hpp"
#include "LandscapePageLayout.hpp"
#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <math.h>
#include <libconfig.h>

extern "C"  {
    #include "SDL_helper.h"
    #include "status_bar.h"
    #include "config.h"
    #include "textures.h"
    #include "common.h"
}

// Text size limits for reflowable documents (EPUB, FB2, HTML). The value is
// the em size in screen pixels, since pages are laid out 1:1 with the screen.
#define MIN_FONT_SIZE 12
#define MAX_FONT_SIZE 40
#define FONT_SIZE_STEP 2
#define DEFAULT_FONT_SIZE 22

fz_context *ctx = NULL;
int windowX, windowY;
config_t *config = NULL;
char* configFile = "/switch/eBookReader/saved_pages.cfg";

static config_t *ensure_config() {
    if (!config) {
        config = (config_t *)malloc(sizeof(config_t));
        config_init(config);
        config_read_file(config, configFile);
    }

    return config;
}

static int cfg_get_int(const char *name, int fallback) {
    config_setting_t *setting = config_setting_get_member(config_root_setting(ensure_config()), name);
    
    if (setting) {
        return config_setting_get_int(setting);
    }

    return fallback;
}

static void cfg_set_int(const char *name, int value) {
    config_setting_t *setting = config_setting_get_member(config_root_setting(ensure_config()), name);
    
    if (!setting) {
        setting = config_setting_add(config_root_setting(config), name, CONFIG_TYPE_INT);
    }
    
    if (setting) {
        config_setting_set_int(setting, value);
    }
}

BookReader::BookReader(const char *path, int* result) {
    if (ctx == NULL) {
        ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
        fz_register_document_handlers(ctx);
    }

    SDL_GetWindowSize(WINDOW, &windowX, &windowY);
    
    book_name = std::string(path).substr(std::string(path).find_last_of("/\\") + 1);
    
    std::string invalid_chars = " :/?#[]@!$&'()*+,;=.";
    for (char& c: invalid_chars) {
        book_name.erase(std::remove(book_name.begin(), book_name.end(), c), book_name.end());
    }
    
    fz_try(ctx)	{
        std::cout << "fz_open_document" << std::endl;
        doc = fz_open_document(ctx, path);

        if (!doc)
        {
            std::cout << "Error opening file!" << std::endl;
            *result = -1;
            return;
        }
        
        // Reflowable formats can be re-paginated to any page size. Lay them
        // out so a page is exactly one full screen in the current orientation
        // instead of mupdf's 450x600pt default (which leaves the text small
        // and the screen half empty).
        reflowable = fz_is_document_reflowable(ctx, doc);
        if (reflowable) {
            font_size = cfg_get_int("fontSize", DEFAULT_FONT_SIZE);
            font_size = fmin(fmax(font_size, (float)MIN_FONT_SIZE), (float)MAX_FONT_SIZE);

            apply_reflow_layout(_currentPageLayout);
        }
        
        int current_page = load_position();

        std::cout << "current_page = " << current_page << std::endl;

        switch_current_page_layout(_currentPageLayout, current_page);

        if (current_page > 0) {
            show_status_bar();
        }
    }
    fz_catch(ctx){
        std::cout << "fz_catch reached, closing gracefully" << std::endl;
        *result = -2;
        return;
    }
}

BookReader::~BookReader() {
    fz_drop_document(ctx, doc);
    
    delete layout;
}

void BookReader::previous_page(int n) {
    layout->previous_page(n);
    show_status_bar();
    save_position(layout->current_page());
}

void BookReader::next_page(int n) {
    layout->next_page(n);
    show_status_bar();
    save_position(layout->current_page());
}

void BookReader::zoom_in() {
    if (reflowable) {
        set_font_size(font_size + FONT_SIZE_STEP);
        return;
    }

    layout->zoom_in();
    show_status_bar();
}

void BookReader::zoom_out() {
    if (reflowable) {
        set_font_size(font_size - FONT_SIZE_STEP);
        return;
    }

    layout->zoom_out();
    show_status_bar();
}

void BookReader::move_page_up() {
    layout->move_up();
}

void BookReader::move_page_down() {
    layout->move_down();
}

void BookReader::move_page_left() {
    layout->move_left();
}

void BookReader::move_page_right() {
    layout->move_right();
}

void BookReader::reset_page() {
    layout->reset();
    show_status_bar();
}

void BookReader::switch_page_layout() {
    BookPageLayout next = (_currentPageLayout == BookPageLayoutPortrait)
        ? BookPageLayoutLandscape
        : BookPageLayoutPortrait;

    int page = layout ? layout->current_page() : 0;

    // Reflowable documents get re-paginated for the new orientation, so the
    // page number has to be carried across the re-layout with a bookmark.
    page = reflow_to(next, page);

    switch_current_page_layout(next, page);

    if (reflowable) {
        save_position(page);
        show_status_bar();
    }
}

void BookReader::draw(bool drawHelp) {
    if (configDarkMode == true) {
        SDL_ClearScreen(RENDERER, BLACK);
    } else {
        SDL_ClearScreen(RENDERER, WHITE);
    }

    SDL_RenderClear(RENDERER);
    
    layout->draw_page();
    
    if (drawHelp) { // Help menu
        int helpWidth = 680;
        int helpHeight = 365;
        helpHeight -= 38; // Removed due to removing the skip forward page button prompt.

        if (!configDarkMode) { // Display a dimmed background if on light mode
            SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));
        }

        SDL_DrawRect(RENDERER, (windowX - helpWidth) / 2, (windowY - helpHeight) / 2, helpWidth, helpHeight, configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);

        int textX = (windowX - helpWidth) / 2 + 20;
        int textY = (windowY - helpHeight) / 2 + 87;
        SDL_Color textColor = configDarkMode ? WHITE : BLACK;
        SDL_DrawText(RENDERER, ROBOTO_30, textX, (windowY - helpHeight) / 2 + 10, textColor, "Help Menu:");

        SDL_DrawButtonPrompt(RENDERER, button_b,               ROBOTO_25, textColor, "Stop reading / Close help menu.",   textX, textY,          35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_minus,           ROBOTO_25, textColor, "Switch to dark/light theme.",       textX, textY + 38,     35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, right_stick_up_down,    ROBOTO_25, textColor, "Zoom in/out (text size on EPUB).",  textX, textY + 38 * 2, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, left_stick_up_down,     ROBOTO_25, textColor, "Page up/down.",                     textX, textY + 38 * 3, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_y,               ROBOTO_25, textColor, "Rotate page.",                      textX, textY + 38 * 4, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_x,               ROBOTO_25, textColor, "Keep status bar on.",               textX, textY + 38 * 5, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_dpad_left_right, ROBOTO_25, textColor, "Next/previous page.",               textX, textY + 38 * 6, 35, 35, 5, 0);
        //SDL_DrawButtonPrompt(RENDERER, button_dpad_up_down,    ROBOTO_25, textColor, "Skip forward/backward 10 pages.", textX, textY + 38 * 7, 35, 35, 5, 0);
    }

    if (permStatusBar || --status_bar_visible_counter > 0)  {
        char reflow_info[128];
        char *title = layout->info();

        if (reflowable) {
            // Zoom is meaningless for reflowed pages (always 100%); show the
            // text size instead.
            snprintf(reflow_info, sizeof(reflow_info), "%i/%i, %ipt", layout->current_page() + 1, layout->page_count(), (int)font_size);
            title = reflow_info;
        }
        
        int title_width = 0, title_height = 0;
        TTF_SizeText(ROBOTO_15, title, &title_width, &title_height);
        
        SDL_Color color = configDarkMode ? STATUS_BAR_DARK : STATUS_BAR_LIGHT;
        
        if (_currentPageLayout == BookPageLayoutPortrait) {
            SDL_DrawRect(RENDERER, 0, 0, 1280, 45, SDL_MakeColour(color.r, color.g, color.b , 180));
            SDL_DrawText(RENDERER, ROBOTO_25, (1280 - title_width) / 2, (40 - title_height) / 2, WHITE, title);
            
            StatusBar_DisplayTime(false);
        } else if (_currentPageLayout == BookPageLayoutLandscape) {
            SDL_DrawRect(RENDERER, 1280 - 45, 0, 45, 720, SDL_MakeColour(color.r, color.g, color.b , 180));
            int x = (1280 - title_width) - ((40 - title_height) / 2);
            int y = (720 - title_height) / 2;
            SDL_DrawRotatedText(RENDERER, ROBOTO_25, (double) 90, x, y, WHITE, title);

            StatusBar_DisplayTime(true);
        }
    }
    
    
    SDL_RenderPresent(RENDERER);
}

void BookReader::show_status_bar() {
    status_bar_visible_counter = 200;
}

void BookReader::switch_current_page_layout(BookPageLayout bookPageLayout, int current_page) {
    if (layout) {
        delete layout;
        layout = NULL;
    }
    
    _currentPageLayout = bookPageLayout;
    
    switch (bookPageLayout) {
        case BookPageLayoutPortrait:
            layout = new PageLayout(doc, current_page);
            break;
        case BookPageLayoutLandscape:
            layout = new LandscapePageLayout(doc, current_page);
            break;
    }
}

void BookReader::apply_reflow_layout(BookPageLayout bookPageLayout) {
    if (!reflowable) {
        return;
    }

    // The framebuffer is always landscape (1280x720). The "landscape" page
    // layout is the rotated one used while physically holding the Switch
    // vertically, so its pages are laid out 720 wide x 1280 tall. Layout
    // units map 1:1 to screen pixels because the page then renders at 100%.
    float w = (bookPageLayout == BookPageLayoutLandscape) ? windowY : windowX;
    float h = (bookPageLayout == BookPageLayoutLandscape) ? windowX : windowY;

    fz_layout_document(ctx, doc, w, h, font_size);
}

int BookReader::reflow_to(BookPageLayout bookPageLayout, int current_page) {
    if (!reflowable) {
        return current_page;
    }

    // Remember the reading position, re-paginate, then find that position
    // again. Bookmarks survive a re-layout; raw page numbers do not.
    fz_bookmark mark = fz_make_bookmark(ctx, doc, fz_location_from_page_number(ctx, doc, current_page));

    apply_reflow_layout(bookPageLayout);

    int new_page = fz_page_number_from_location(ctx, doc, fz_lookup_bookmark(ctx, doc, mark));
    return new_page >= 0 ? new_page : current_page;
}

void BookReader::set_font_size(float size) {
    size = fmin(fmax(size, (float)MIN_FONT_SIZE), (float)MAX_FONT_SIZE);

    if (!reflowable || size == font_size) {
        show_status_bar();
        return;
    }

    font_size = size;

    int page = layout ? layout->current_page() : 0;
    page = reflow_to(_currentPageLayout, page);
    switch_current_page_layout(_currentPageLayout, page);

    cfg_set_int("fontSize", (int)font_size);
    save_position(page);
    show_status_bar();
}

int BookReader::load_position() {
    int page = cfg_get_int(book_name.c_str(), 0);

    if (reflowable) {
        // Prefer the saved chapter + page within that chapter. The flat page
        // number shifts whenever the font size changes the pagination, but
        // the chapter does not, so this restores a much closer position.
        int chapter = cfg_get_int((book_name + "_c").c_str(), -1);
        int chapter_page = cfg_get_int((book_name + "_p").c_str(), -1);

        if (chapter >= 0 && chapter < fz_count_chapters(ctx, doc) && chapter_page >= 0) {
            page = fz_page_number_from_location(ctx, doc, fz_make_location(chapter, chapter_page));
        }
    }

    return std::max(page, 0);
}

void BookReader::save_position(int page) {
    cfg_set_int(book_name.c_str(), page);

    if (reflowable) {
        fz_location loc = fz_location_from_page_number(ctx, doc, page);
        cfg_set_int((book_name + "_c").c_str(), loc.chapter);
        cfg_set_int((book_name + "_p").c_str(), loc.page);
    }

    config_write_file(config, configFile);
}
