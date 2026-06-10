#ifndef EBOOK_READER_BOOK_READER_HPP
#define EBOOK_READER_BOOK_READER_HPP

#include <mupdf/pdf.h>
#include <string>
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

        void previous_page(int n);
        void next_page(int n);
        void zoom_in();
        void zoom_out();
        void move_page_up();
        void move_page_down();
        void move_page_left();
        void move_page_right();
        void reset_page();
        void switch_page_layout();
        void draw(bool drawHelp);
    
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
    
        // Re-paginate a reflowable document so one page exactly fills the
        // screen in the given orientation, using the current font size.
        void apply_reflow_layout(BookPageLayout bookPageLayout);
    
        // Re-layout for the given orientation while keeping the reading
        // position. Returns the page number of that position in the new layout.
        int reflow_to(BookPageLayout bookPageLayout, int current_page);
    
        // Change text size of reflowable documents (re-paginates the book).
        void set_font_size(float size);
    
        int load_position();
        void save_position(int page);
    
        fz_document *doc = NULL;
        int status_bar_visible_counter = 0;
    
        bool reflowable = false;
        float font_size = 22;
    
        BookPageLayout _currentPageLayout = BookPageLayoutPortrait;
        PageLayout *layout = NULL;
    
        std::string book_name;
};

#endif
