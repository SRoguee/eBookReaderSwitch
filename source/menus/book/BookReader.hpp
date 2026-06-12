#ifndef EBOOK_READER_BOOK_READER_HPP
#define EBOOK_READER_BOOK_READER_HPP

#include <mupdf/pdf.h>
#include <string>
#include <vector>
#include "PageLayout.hpp"
#include <switch.h>
struct SDL_Texture;

typedef enum {
    BookPageLayoutPortrait,
    BookPageLayoutLandscape
} BookPageLayout;

class BookReader {
    public:
        BookReader(const char *path, int *result);
        ~BookReader();

        bool permStatusBar = false;

        // Touch overlay menu (opened by tapping the centre of the screen).
        bool showTouchMenu = false;

        // Chapter list overlay (opened from the touch menu's "Chapters" button).
        bool showChapters = false;

        void previous_page(int n);
        void next_page(int n);
        void goto_page(int page);           // jump to an absolute page number
        void zoom_in();
        void zoom_out();
        void move_page_up();
        void move_page_down();
        void move_page_left();
        void move_page_right();
        void reset_page();
        void switch_page_layout();
        void draw(bool drawHelp);

        // Draw an immediate "Exiting..." splash and present it, so leaving a
        // book gives instant feedback before the chooser reloads.
        void draw_exiting_overlay();

        // Handle a screen tap while the book is open. Returns true if the tap
        // was consumed by the touch menu (so the reader should skip its normal
        // page-turn / zoom touch handling for this tap). Sets `exitBook` to
        // true if the user pressed the menu's exit button.
        bool handle_touch_menu(int tx, int ty, bool *exitBook);

        // Returns true if the chapter list is open and consumed this tap.
        bool handle_chapter_menu(int tx, int ty);
        // D-pad navigation within the chapter list.
        void chapters_move(int delta);
        void chapters_select();
        int  chapter_page_step();   // rows to jump for L/R paging
        bool has_chapters();

        // One entry in the table of contents.
        struct Chapter { std::string title; int page; };
    
        BookPageLayout currentPageLayout() {
            return _currentPageLayout;
        }
    
        // True for formats mupdf can re-paginate to any page size (EPUB, FB2, HTML).
        bool is_reflowable() {
            return reflowable;
        }
    
    private:
        void show_status_bar();
        void switch_current_page_layout(BookPageLayout bookPageLayout, int current_page);

        // ---- Touch overlay menu helpers ----
        // The menu is a vertical panel of buttons. We compute each button's
        // rectangle from a shared layout so drawing and hit-testing agree.
        struct MenuRect { int x, y, w, h; };
        enum MenuButton {
            MenuBtnClose = 0,   // the "X"
            MenuBtnLight,
            MenuBtnDark,
            MenuBtnNight,
            MenuBtnRotate,
            MenuBtnResetView,
            MenuBtnStatusBar,
            MenuBtnMarginDown,
            MenuBtnMarginUp,
            MenuBtnChapters,
            MenuBtnExit,
            MenuBtnCount
        };
        MenuRect touch_menu_panel();
        MenuRect touch_menu_button(int which);
        void draw_touch_menu();

        // The menu is laid out in a logical space matching the reading
        // orientation (portrait => 1280x720 upright; landscape => 720x1280,
        // then rotated 90 degrees on screen). These give the logical canvas
        // size and map a physical screen touch into that logical space so
        // hit-testing and drawing always agree.
        void touch_menu_canvas(int *cw, int *ch);
        void touch_menu_map_point(int sx, int sy, int *lx, int *ly);

        // ---- Chapter list overlay ----
        // Built once from the document's outline (table of contents) at open.
        std::vector<Chapter> chapters;
        int  chapter_selected = 0;   // highlighted row
        int  chapter_scroll   = 0;   // first visible row
        void load_chapters();        // populate `chapters` from the outline
        void draw_chapter_menu();
        int  chapter_rows_visible(); // how many rows fit on screen
        MenuRect chapter_row_rect(int visible_index); // logical rect of a row
    
        // Re-paginate a reflowable document so one page exactly fills the
        // screen in the given orientation, using the current font size.
        void apply_reflow_layout(BookPageLayout bookPageLayout);
    
        // Re-layout for the given orientation while keeping the reading
        // position. Returns the page number of that position in the new layout.
        int reflow_to(BookPageLayout bookPageLayout, int current_page);
    
        // Change text size of reflowable documents (re-paginates the book).
        void set_font_size(float size);

        // Change the page margin of reflowable documents (re-paginates).
        void set_margin(int margin);

        int load_position();
        void save_position(int page);

        fz_document *doc = NULL;
        int status_bar_visible_counter = 0;

        bool reflowable = false;
        float font_size = 22;
        int page_margin = 0;   // reflowable page inset, in pixels per side
    
        BookPageLayout _currentPageLayout = BookPageLayoutPortrait;
        PageLayout *layout = NULL;
    
        std::string book_name;
};

#endif
