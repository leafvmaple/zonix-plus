#pragma once

#include "client.h"
#include "../gui/theme.h"
#include <sys/memory.hpp>

namespace desktop_chrome {

inline constexpr int TITLE_HEIGHT = 30;
// The close button is a disc that keeps the same distance from the top, bottom
// and left of the title bar. The title is centred in the bar.
inline constexpr int CLOSE_SIZE = 14;
inline constexpr int CLOSE_MARGIN = (TITLE_HEIGHT - CLOSE_SIZE) / 2;
inline constexpr int CLOSE_TOP = CLOSE_MARGIN;
inline constexpr int CLOSE_LEFT = CLOSE_MARGIN;
inline constexpr int LABEL_COLOR = zonix_gui::theme::TEXT;

// The bar along the top of the screen. It names the system and the program
// in front on the left and shows a clock on the right.
inline constexpr int MENU_HEIGHT = 24;
inline constexpr int MENU_INSET = 14;
inline constexpr const char* MENU_BRAND = "Zonix";
inline constexpr const char* MENU_NO_PROGRAM = "Desktop";
// The longest clock text, with its terminator.
inline constexpr int MENU_CLOCK_CAPACITY = 24;

// Draws each window's title bar. The compositor adds the border, rounds the
// corners and dims unfocused windows.
class Chrome {
public:
    Chrome();
    ~Chrome();

    // `max_width` is the widest title bar a window can have.
    long init(int max_width);
    // Draws the title of a program in a window slot of the given width.
    long configure(int slot, int app, int width);
    unsigned int title_pixel(int slot, int width, int x, int y) const;
    // A pixel of a Dock item's icon, on the Dock's own colour.
    unsigned int dock_pixel(int item, int x, int y) const;
    // Draws the menu bar for the program in front, or NO_APP, and the clock
    // text. Returns 1 if the bar changed, 0 if not, or a negative error.
    long set_menu(int app, const char* clock);
    unsigned int menu_pixel(int screen_width, int x, int y) const;

private:
    static constexpr int MENU_UNSET = -2;
    int menu_app_ = MENU_UNSET;
    sys::array<char, MENU_CLOCK_CAPACITY> menu_clock_{};

    // Records the bar's contents and tells whether they changed.
    bool menu_changed(int app, const char* clock) {
        bool same = menu_app_ == app;
        int length = 0;
        for (; clock[length] != 0 && length < MENU_CLOCK_CAPACITY - 1; ++length)
            same = same && menu_clock_[length] == clock[length];
        same = same && menu_clock_[length] == 0;
        menu_app_ = app;
        for (int index = 0; index < length; ++index)
            menu_clock_[index] = clock[index];
        menu_clock_[length] = 0;
        return !same;
    }
    // The program each slot's title was last drawn for.
    sys::array<int, desktop_ipc::MAX_WINDOWS> apps_{};
    struct Scene;
    struct SceneDeleter {
        void operator()(Scene* scene) const noexcept;
    };
    sys::unique_ptr<Scene, SceneDeleter> scene_;
};

}  // namespace desktop_chrome
