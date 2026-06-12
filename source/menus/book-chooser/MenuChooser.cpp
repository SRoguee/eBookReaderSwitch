extern "C" {
    #include "MenuChooser.h"
    #include "menu_book_reader.h"
    #include "SDL_helper.h"
    #include "common.h"
    #include "textures.h"
    #include "config.h"
    #include "fs.h"
}

#include <switch.h>
#include <iostream>
#include <filesystem>
#include <bits/stdc++.h>

#include <mupdf/pdf.h>
#include <SDL2/SDL_image.h>

using namespace std;
namespace fs = filesystem;

template <typename T> bool contains(list<T> & listOfElements, const T & element) {
	auto it = find(listOfElements.begin(), listOfElements.end(), element);
	return it != listOfElements.end();
}

extern TTF_Font *ROBOTO_35, *ROBOTO_25, *ROBOTO_15;

// ---- Card grid layout constants ----
#define CARD_COLS        4
#define CARD_W           250
#define CARD_H           340   // cover area; label drawn below
#define CARD_LABEL_H     45
#define CARD_GAP_X       40
#define CARD_GAP_Y       30
#define GRID_TOP         30
#define GRID_LEFT        45

struct BookEntry {
    string filename;
    string fullpath;
    string extention;
    bool   warned;
    SDL_Texture *cover;     // NULL until rendered (or if no cover available)
    bool   cover_tried;     // true once we've attempted to render the cover
};

// Build the on-disk cache path for a book's cover thumbnail.
static string cover_cache_path(const string &fullpath) {
    string name = fullpath;
    size_t slash = name.find_last_of("/\\");
    if (slash != string::npos) name = name.substr(slash + 1);
    // Replace characters that don't belong in a filename.
    for (char &ch : name) {
        if (ch == ' ' || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '?' || ch == '*' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
            ch = '_';
    }
    return "/switch/eBookReader/covers/" + name + ".png";
}

// Render the first page of a document to an SDL texture sized to fit a card.
// Uses its own mupdf context so it is independent of the reader's global ctx.
//
// Covers are cached to disk: the expensive mupdf open+layout+render only
// happens the first time a book is seen. After that the cached PNG is loaded
// directly, which is near-instant - this is what keeps the chooser (and so
// startup and returning from a book) fast.
static SDL_Texture *render_cover(fz_context *cctx, const char *path) {
    SDL_Texture *tex = NULL;
    string cache = cover_cache_path(path);

    // 1) Try the cached PNG first.
    {
        SDL_Surface *cached = IMG_Load(cache.c_str());
        if (cached) {
            tex = SDL_CreateTextureFromSurface(RENDERER, cached);
            SDL_FreeSurface(cached);
            if (tex) return tex;
        }
    }

    // 2) Not cached: render it with mupdf, then save the PNG for next time.
    fz_document *doc = NULL;

    fz_try(cctx) {
        doc = fz_open_document(cctx, path);

        // EPUBs are reflowable; give them a layout so page 0 is the cover.
        if (fz_is_document_reflowable(cctx, doc)) {
            fz_layout_document(cctx, doc, CARD_W, CARD_H, 11);
        }

        if (fz_count_pages(cctx, doc) > 0) {
            fz_page *page = fz_load_page(cctx, doc, 0);
            fz_rect bounds = fz_bound_page(cctx, page);

            float pw = bounds.x1 - bounds.x0;
            float ph = bounds.y1 - bounds.y0;
            if (pw <= 0) pw = 1;
            if (ph <= 0) ph = 1;

            // Scale to fit inside the card cover area, preserving aspect ratio.
            float scale = fmin((float)CARD_W / pw, (float)CARD_H / ph);

            fz_pixmap *pix = fz_new_pixmap_from_page_contents(
                cctx, page, fz_scale(scale, scale), fz_device_rgb(cctx), 0);

            // Save to the disk cache (best-effort; ignore failures).
            fz_try(cctx) {
                fz_save_pixmap_as_png(cctx, pix, cache.c_str());
            } fz_catch(cctx) { }

            SDL_Surface *image = SDL_CreateRGBSurfaceFrom(
                pix->samples, pix->w, pix->h, pix->n * 8, pix->w * pix->n,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0);
            tex = SDL_CreateTextureFromSurface(RENDERER, image);

            SDL_FreeSurface(image);
            fz_drop_pixmap(cctx, pix);
            fz_drop_page(cctx, page);
        }
    }
    fz_always(cctx) {
        if (doc) fz_drop_document(cctx, doc);
    }
    fz_catch(cctx) {
        tex = NULL;
    }

    return tex;
}

// Draw a placeholder card cover (used when no cover image is available).
static void draw_placeholder(int x, int y, SDL_Color textColor, const char *filename) {
    SDL_DrawRect(RENDERER, x, y, CARD_W, CARD_H, SDL_MakeColour(120, 120, 120, 255));
    SDL_DrawRect(RENDERER, x + 4, y + 4, CARD_W - 8, CARD_H - 8, SDL_MakeColour(150, 150, 150, 255));

    const char *noCover = "No Cover";
    int tw = 0, th = 0;
    TTF_SizeText(ROBOTO_25, noCover, &tw, &th);
    SDL_DrawText(RENDERER, ROBOTO_25, x + (CARD_W - tw) / 2, y + (CARD_H - th) / 2,
                 SDL_MakeColour(40, 40, 40, 255), noCover);
}

void Menu_StartChoosing() {
    int choosenIndex = 0;
    bool readingBook = false;
    list<string> allowedExtentions = {".pdf", ".epub", ".cbz", ".xps"};
    list<string> warnedExtentions = {".epub", ".cbz", ".xps"};

    string path = "/switch/eBookReader/books";

    // Ensure the cover cache directory exists (covers are cached as PNGs so
    // they only have to be rendered once, keeping the chooser fast).
    FS_RecursiveMakeDir("/switch/eBookReader/covers");

    // ---- Build the book list (fast: just a directory scan). ----
    // Covers are rendered lazily inside the main loop so the menu appears
    // instantly instead of blocking on every book up front.
    fz_context *cctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    fz_register_document_handlers(cctx);

    vector<BookEntry> books;
    for (const auto & entry : fs::directory_iterator(path)) {
        string filename = entry.path().filename().string();
        size_t dot = filename.find_last_of(".");
        if (dot == string::npos) continue;
        string extention = filename.substr(dot);

        if (contains(allowedExtentions, extention)) {
            BookEntry b;
            b.filename    = filename;
            b.fullpath    = path + "/" + filename;
            b.extention   = extention;
            b.warned      = contains(warnedExtentions, extention);
            b.cover       = NULL;
            b.cover_tried = false;
            books.push_back(b);
        }
    }

    int amountOfFiles = (int)books.size();

    bool isWarningOnScreen = false;
    int windowX, windowY;
    SDL_GetWindowSize(WINDOW, &windowX, &windowY);
    int warningWidth = 700;
    int warningHeight = 300;

    // How many full rows fit on screen (for scrolling).
    int rowStride   = CARD_H + CARD_LABEL_H + CARD_GAP_Y;
    int visibleRows = max(1, (windowY - GRID_TOP - 50) / rowStride);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // Touch support: tap a card to select it, tap the selected card again to
    // open it (two-step avoids accidental opens on a touchscreen).
    hidInitializeTouchScreen();
    s32 prevTouchCount = 0;

    while(appletMainLoop()) {
        if (readingBook) {
            break;
        }

        SDL_Color textColor = configDarkMode ? WHITE : BLACK;
        SDL_Color backColor = configDarkMode ? BACK_BLACK : BACK_WHITE;

        SDL_ClearScreen(RENDERER, backColor);
        SDL_RenderClear(RENDERER);

        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (!isWarningOnScreen && kDown & HidNpadButton_Plus) {
            break;
        }

        if (kDown & HidNpadButton_B) {
            if (!isWarningOnScreen) {
                break;
            } else {
                isWarningOnScreen = false;
            }
        }

        if (kDown & HidNpadButton_A && amountOfFiles > 0) {
            BookEntry &chosen = books[choosenIndex];
            if (chosen.warned && !isWarningOnScreen) {
                isWarningOnScreen = true;
            } else {
                cout << "Opening book: " << chosen.fullpath << endl;
                Menu_OpenBook((char*) chosen.fullpath.c_str());
                readingBook = true;
                break;
            }
        }

        // ---- 2D navigation across the card grid ----
        if (!isWarningOnScreen && amountOfFiles > 0) {
            if (kDown & HidNpadButton_Right || kDown & HidNpadButton_StickRRight) {
                if (choosenIndex < amountOfFiles - 1) choosenIndex++;
            }
            if (kDown & HidNpadButton_Left || kDown & HidNpadButton_StickRLeft) {
                if (choosenIndex > 0) choosenIndex--;
            }
            if (kDown & HidNpadButton_Down || kDown & HidNpadButton_StickRDown) {
                if (choosenIndex + CARD_COLS < amountOfFiles) {
                    choosenIndex += CARD_COLS;
                } else {
                    choosenIndex = amountOfFiles - 1;
                }
            }
            if (kDown & HidNpadButton_Up || kDown & HidNpadButton_StickRUp) {
                if (choosenIndex - CARD_COLS >= 0) choosenIndex -= CARD_COLS;
            }
        }

        if (kDown & HidNpadButton_Minus) {
            config_cycle_color_mode();
        }

        // ---- Prompts ----
        int exitWidth = 0;
        TTF_SizeText(ROBOTO_20, "Exit", &exitWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_b, ROBOTO_20, textColor, "Exit", windowX - exitWidth - 50, windowY - 10, 35, 35, 5, 0);

        int themeWidth = 0;
        TTF_SizeText(ROBOTO_20, "Switch Theme", &themeWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_minus, ROBOTO_20, textColor, "Switch Theme", windowX - themeWidth - 50, windowY - 40, 35, 35, 5, 0);

        // ---- Scroll so the selected card stays visible ----
        int selRow   = (amountOfFiles > 0) ? choosenIndex / CARD_COLS : 0;
        int totalRows = (amountOfFiles + CARD_COLS - 1) / CARD_COLS;
        int firstRow = 0;
        if (selRow >= visibleRows) {
            firstRow = selRow - visibleRows + 1;
        }
        if (firstRow > max(0, totalRows - visibleRows)) {
            firstRow = max(0, totalRows - visibleRows);
        }

        // ---- Touch input: select a card by tapping it; tap the already-
        // selected card again to open it. ----
        {
            HidTouchScreenState ts = {0};
            bool newTouch = false;
            int tx = 0, ty = 0;
            if (hidGetTouchScreenStates(&ts, 1)) {
                if (ts.count != prevTouchCount) {
                    if (prevTouchCount == 0 && ts.count > 0) {
                        newTouch = true;
                        tx = ts.touches[0].x;
                        ty = ts.touches[0].y;
                    }
                    prevTouchCount = ts.count;
                }
            }

            if (newTouch && !isWarningOnScreen && amountOfFiles > 0) {
                for (int i = 0; i < amountOfFiles; i++) {
                    int row = i / CARD_COLS;
                    int col = i % CARD_COLS;
                    if (row < firstRow || row >= firstRow + visibleRows) continue;

                    int x = GRID_LEFT + col * (CARD_W + CARD_GAP_X);
                    int y = GRID_TOP + (row - firstRow) * rowStride;

                    // Hit area covers the cover image plus its label.
                    if (tx >= x && tx <= x + CARD_W &&
                        ty >= y && ty <= y + CARD_H + CARD_LABEL_H) {
                        if (i == choosenIndex) {
                            // Second tap on the selected card -> open it.
                            BookEntry &chosen = books[i];
                            if (chosen.warned) {
                                isWarningOnScreen = true;
                            } else {
                                cout << "Opening book: " << chosen.fullpath << endl;
                                Menu_OpenBook((char*) chosen.fullpath.c_str());
                                readingBook = true;
                            }
                        } else {
                            choosenIndex = i; // first tap just selects
                        }
                        break;
                    }
                }
            } else if (newTouch && isWarningOnScreen) {
                // Tapping while the warning is up confirms opening the book.
                BookEntry &chosen = books[choosenIndex];
                cout << "Opening book: " << chosen.fullpath << endl;
                Menu_OpenBook((char*) chosen.fullpath.c_str());
                readingBook = true;
            }

            if (readingBook) break;
        }

        // ---- Lazily render covers for on-screen cards ----
        // Only a couple per frame, so the grid stays responsive while covers
        // stream in. Off-screen books are never rendered until scrolled to.
        int coverBudget = 2;
        for (int i = 0; i < amountOfFiles && coverBudget > 0; i++) {
            int row = i / CARD_COLS;
            if (row < firstRow || row >= firstRow + visibleRows) continue; // off-screen
            if (books[i].cover_tried) continue;                            // already done
            books[i].cover = render_cover(cctx, books[i].fullpath.c_str());
            books[i].cover_tried = true;
            coverBudget--;
        }

        // ---- Draw the card grid ----
        for (int i = 0; i < amountOfFiles; i++) {
            int row = i / CARD_COLS;
            int col = i % CARD_COLS;

            if (row < firstRow || row >= firstRow + visibleRows) continue;

            int x = GRID_LEFT + col * (CARD_W + CARD_GAP_X);
            int y = GRID_TOP + (row - firstRow) * rowStride;

            // Selection highlight (a frame behind the card).
            if (i == choosenIndex) {
                SDL_DrawRect(RENDERER, x - 6, y - 6, CARD_W + 12, CARD_H + CARD_LABEL_H + 12,
                             configDarkMode ? SELECTOR_COLOUR_DARK : SELECTOR_COLOUR_LIGHT);
            }

            // Cover or placeholder.
            if (books[i].cover) {
                int tw = 0, th = 0;
                SDL_QueryTexture(books[i].cover, NULL, NULL, &tw, &th);
                int dx = x + (CARD_W - tw) / 2;
                int dy = y + (CARD_H - th) / 2;
                SDL_DrawImageScale(RENDERER, books[i].cover, dx, dy, tw, th);
            } else {
                draw_placeholder(x, y, textColor, books[i].filename.c_str());
            }

            // Warning badge on the cover corner.
            if (books[i].warned) {
                SDL_DrawImage(RENDERER, warning, x + 6, y + 6);
            }

            // Filename label below the cover (truncated to fit the card width).
            string label = books[i].filename;
            int lw = 0;
            TTF_SizeText(ROBOTO_15, label.c_str(), &lw, NULL);
            while (lw > CARD_W - 10 && label.size() > 4) {
                label = label.substr(0, label.size() - 4) + "...";
                TTF_SizeText(ROBOTO_15, label.c_str(), &lw, NULL);
            }
            SDL_DrawText(RENDERER, ROBOTO_15, x + (CARD_W - lw) / 2, y + CARD_H + 10, textColor, label.c_str());
        }

        if (amountOfFiles == 0) {
            SDL_DrawText(RENDERER, ROBOTO_25, GRID_LEFT, GRID_TOP, textColor,
                         "No books found in /switch/eBookReader/books");
        }

        // ---- Warning modal ----
        if (isWarningOnScreen) {
            if (!configDarkMode) {
                SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));
            }
            SDL_DrawRect(RENDERER, (windowX - warningWidth) / 2, (windowY - warningHeight) / 2, warningWidth, warningHeight, configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);
            SDL_DrawText(RENDERER, ROBOTO_30, (windowX - warningWidth) / 2 + 15, (windowY - warningHeight) / 2 + 15, textColor, "This file is not yet fully supported, and may");
            SDL_DrawText(RENDERER, ROBOTO_30, (windowX - warningWidth) / 2 + 15, (windowY - warningHeight) / 2 + 50, textColor, "cause a system, or app crash.");
            SDL_DrawText(RENDERER, ROBOTO_20, (windowX - warningWidth) / 2 + warningWidth - 250, (windowY - warningHeight) / 2 + warningHeight - 30, textColor, "\"A\" - Read");
            SDL_DrawText(RENDERER, ROBOTO_20, (windowX - warningWidth) / 2 + warningWidth - 125, (windowY - warningHeight) / 2 + warningHeight - 30, textColor, "\"B\" - Cancel.");
        }

        SDL_RenderPresent(RENDERER);
    }

    // ---- Cleanup ----
    for (auto &b : books) {
        if (b.cover) SDL_DestroyTexture(b.cover);
    }
    fz_drop_context(cctx);
}
