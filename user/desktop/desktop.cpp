#include "../command.h"
#include "compositor.h"
#include <kernel/psf.h>
#include <sys/algorithm.hpp>
#include <sys/array.hpp>

#ifndef ZONIX_PIXEL_TERMINAL
extern "C" const unsigned char terminal_font[];
#endif

namespace {

using namespace desktop;
using namespace desktop_ipc;
using desktop_chrome::CLOSE_SIZE;

// Owns the windows and the pointer, routes input to clients and decides which
// parts of the screen to repaint. The Compositor supplies the pixels.
class Desktop {
public:
    Desktop(int fd, int width, int height, psf::Font font)
        : fd_(fd),
          screen_{{0, 0}, width, height},
          pointer_{width / 2, height / 2},
          dock_(desktop_dock::bounds(width, height)),
          // A screen with room for the Dock also gets the menu bar.
          menu_height_(dock_.width != 0 ? desktop_chrome::MENU_HEIGHT : 0),
          compositor_({windows_, order_, surfaces_, chrome_, pointer_, screen_, dock_, menu_height_, font}) {
        terminal_fits_ = width >= TERMINAL_OUTER_WIDTH && height >= TERMINAL_OUTER_HEIGHT;
        timezone_minutes_ = read_timezone();
        for (int slot = 0; slot < MAX_WINDOWS; ++slot)
            order_[slot] = slot;
    }

    ~Desktop() {
        for (auto& window : windows_)
            window.client.stop();
    }

    long run() {
        // Frame windows scale with the screen. The others have fixed sizes.
        int widest = sys::max(example_rect(APP_EXAMPLE_A).width, outer_width(static_cast<int>(PIXEL_MAX_WIDTH)));
        long error = chrome_.init(sys::min(screen_.width, sys::max(widest, TERMINAL_OUTER_WIDTH)));
        if (error < 0)
            return error;
        FileStat example{};
        if (sys_stat(const_cast<char*>(APPS[APP_EXAMPLE_A].program), &example) == 0 &&
            example.type == FILE_TYPE_FILE) {
            open_at_front(APP_EXAMPLE_A);
            open_at_front(APP_EXAMPLE_B);
        }
        error = repaint(screen_);
        const unsigned int display_mask = 1U << fd_;
        while (error == 0 && !quit_) {
            unsigned int mask = display_mask;
            for (const auto& window : windows_)
                mask |= window.client.mask();
            long clock_wait_ms = 0;
            error = update_menu(clock_wait_ms);
            if (error < 0)
                break;
            // With a menu bar, wake when its clock moves to the next minute.
            long ready = menu_height_ != 0 ? sys_wait_readable_for(mask, clock_wait_ms) : sys_wait_readable(mask);
            if (ready < 0)
                return ready;
            for (int index = 0; index < MAX_WINDOWS && error == 0; ++index) {
                auto& window = windows_[index];
                if ((static_cast<unsigned long>(ready) & window.client.mask()) == 0)
                    continue;
                if (has_pixels(index))
                    error = receive_pixels(index);
                else
                    error = window.client.receive_frame() ? repaint_client(index) : close_window(index);
            }
            if (error < 0 || (static_cast<unsigned long>(ready) & display_mask) == 0)
                continue;
            DisplayTimedEvent input{};
            error = sys_ioctl(fd_, DISPLAY_READ_TIMED_EVENT, reinterpret_cast<long>(&input));
            if (error < 0)
                break;
            const auto& event = input.event;
            if (event.type == DISPLAY_EVENT_KEY && event.value == DISPLAY_KEY_PRESS) {
                bool client_shortcuts = is_application(top_window());
                if (event.code == KEY_F12 ||
                    (!client_shortcuts && (event.code == DISPLAY_KEY_ESCAPE || event.code == DISPLAY_KEY_Q)))
                    return 0;
                if (!client_shortcuts && event.code == KEY_C && (event.modifiers & DISPLAY_MOD_CTRL) != 0)
                    sys_exit(PROCESS_STATUS_KILLED);
            }
            error = handle(input);
        }
        return error;
    }

private:
    static constexpr unsigned int KEY_C = 46;
    static constexpr unsigned int KEY_F12 = 88;
    static constexpr unsigned long DOUBLE_CLICK_MS = 400;
    static constexpr int CLICK_DISTANCE = 5;
    static constexpr int TILE_SIZE = 64;
    static constexpr long MINUTE_MS = 60000;
    // Margins for a window placed at the right edge or in the top-left corner.
    static constexpr Point RIGHT_MARGIN{24, 56};
    static constexpr Point CORNER_MARGIN{16, 40};
    // Each further window of a program moves this far down and to the right,
    // starting over after CASCADE_STEPS windows.
    static constexpr int CASCADE_OFFSET = 26;
    static constexpr int CASCADE_STEPS = 6;
    static constexpr int TERMINAL_OUTER_WIDTH = outer_width(TERMINAL_WIDTH);
    static constexpr int TERMINAL_OUTER_HEIGHT = outer_height(TERMINAL_HEIGHT);
    inline static sys::array<unsigned int, TILE_SIZE * TILE_SIZE> pixels_{};
    // The window pool is too large for the stack.
    inline static Windows windows_{};
    inline static Surfaces surfaces_{};

    static PixelSurface& surface(int index) { return surfaces_[index]; }
    static bool has_pixels(int index) { return windows_[index].protocol() == Protocol::Pixels; }
    // Application windows take the whole keyboard. The example windows leave
    // Desktop its own keys.
    static bool is_application(int index) {
        return index != NO_WINDOW && windows_[index].protocol() != Protocol::Frame;
    }

    int fd_;
    desktop_chrome::Chrome chrome_;
    Rect screen_;
    Point pointer_;
    Point drag_offset_{};
    Rect dock_;
    int menu_height_;
    // The local time's offset from UTC.
    int timezone_minutes_{};
    // Set by the Dock's exit item.
    bool quit_{};
    Order order_{};
    Compositor compositor_;
    bool terminal_fits_{};
    int drag_ = NO_WINDOW;
    sys::array<int, 3> button_targets_{{NO_WINDOW, NO_WINDOW, NO_WINDOW}};
    struct Click {
        int window = NO_WINDOW;
        Point position{};
        unsigned long timestamp_ms{};
    };
    Click last_click_{};
    Point left_press_{};
    bool left_click_{};

    static bool near(Point a, Point b) {
        int dx = a.x - b.x, dy = a.y - b.y;
        return dx >= -CLICK_DISTANCE && dx <= CLICK_DISTANCE && dy >= -CLICK_DISTANCE && dy <= CLICK_DISTANCE;
    }

    bool finish_click(int index, unsigned long timestamp_ms) {
        bool complete = left_click_ && index != NO_WINDOW && in_body(windows_[index].rect, pointer_);
        left_click_ = false;
        bool double_click = complete && last_click_.window == index && near(last_click_.position, pointer_) &&
                            timestamp_ms - last_click_.timestamp_ms <= DOUBLE_CLICK_MS;
        last_click_ = complete && !double_click ? Click{index, pointer_, timestamp_ms} : Click{};
        return double_click;
    }

    static Rect bounds(const Rect& window) { return Compositor::bounds(window); }

    static Rect close_rect(const Rect& window) {
        return {
            {window.origin.x + desktop_chrome::CLOSE_LEFT, window.origin.y + desktop_chrome::CLOSE_TOP},
            CLOSE_SIZE,
            CLOSE_SIZE};
    }

    int top_window() const { return desktop::top_window(windows_, order_); }

    int window_at(Point point) const {
        for (int index : order_)
            if (windows_[index].open() && windows_[index].rect.contains(point))
                return index;
        return NO_WINDOW;
    }

    // The program a key opens, or NO_APP.
    static int app_for_key(unsigned int code) {
        for (int app = 0; app < APP_COUNT; ++app)
            if (APPS[app].key != 0 && APPS[app].key == code)
                return app;
        return NO_APP;
    }

    // The frontmost window of a program, or NO_WINDOW.
    int front_window(int app) const {
        for (int index : order_)
            if (windows_[index].open() && windows_[index].app == app)
                return index;
        return NO_WINDOW;
    }

    Point cascade(int step) const {
        int offset = step % CASCADE_STEPS * CASCADE_OFFSET;
        return {offset, offset};
    }

    Rect example_rect(int app) const {
        int width = screen_.width / 5 * 2;
        int height = screen_.height / 2;
        return app == APP_EXAMPLE_A ? Rect{{screen_.width / 8, screen_.height / 5}, width, height}
                                    : Rect{{screen_.width / 3, screen_.height / 3}, width, height};
    }

    // Where a window whose size Desktop decides appears. Pixel windows are
    // placed when their client reports a size.
    Rect fixed_rect(int app, int step) const {
        if (APPS[app].protocol == Protocol::Frame)
            return example_rect(app);
        Point shift = cascade(step);
        return {{clamp((screen_.width - TERMINAL_OUTER_WIDTH) / 2 + shift.x, screen_.width - TERMINAL_OUTER_WIDTH),
                 clamp((screen_.height - TERMINAL_OUTER_HEIGHT) / 2 + shift.y, screen_.height - TERMINAL_OUTER_HEIGHT)},
                TERMINAL_OUTER_WIDTH,
                TERMINAL_OUTER_HEIGHT};
    }

    // Starts a program in a free slot, with `path` as its argument if given.
    // Returns the slot, SYS_ERROR_BUSY when every slot is taken, or another
    // negative error.
    long open(int app, const char* path = nullptr) {
        const auto& spec = APPS[app];
        if ((app == APP_TERMINAL || app == APP_EDITOR) && !terminal_fits_)
            return SYS_ERROR_NOT_SUPPORTED;
        int slot = NO_WINDOW;
        int siblings = 0;
        for (int index = MAX_WINDOWS - 1; index >= 0; --index) {
            if (!windows_[index].open())
                slot = index;
            else if (windows_[index].app == app)
                ++siblings;
        }
        if (slot == NO_WINDOW)
            return SYS_ERROR_BUSY;
        auto& window = windows_[slot];
        long error = window.client.start(spec, path);
        if (error < 0)
            return error;
        window.app = app;
        window.step = siblings;
        // The Dock marks programs that have a window.
        if (repaint(dock_) < 0) {
            window.client.stop();
            return SYS_ERROR_IO;
        }
        window.close_requested = false;
        window.path = {};
        if (path)
            memcpy(window.path.data(), path, strlen(const_cast<char*>(path)) + 1);
        if (spec.protocol != Protocol::Pixels) {
            window.rect = fixed_rect(app, siblings);
            if (chrome_.configure(slot, app, window.rect.width) < 0) {
                close_window(slot);
                return SYS_ERROR_IO;
            }
        }
        return slot;
    }

    // Puts a slot at the front of the stacking order.
    void raise(int index) {
        for (int slot = 0; slot < MAX_WINDOWS; ++slot) {
            if (order_[slot] != index)
                continue;
            while (slot > 0) {
                order_[slot] = order_[slot - 1];
                --slot;
            }
            order_[0] = index;
            break;
        }
    }

    void open_at_front(int app) {
        long slot = open(app);
        if (slot < 0)
            command_error("desktop", APPS[app].program, slot);
        else
            raise(static_cast<int>(slot));
    }

    // Opens a file in an editor window, or finds the window that already
    // shows it. Returns the slot or a negative error.
    long edit_text(const char* path) {
        if (TERMINAL_PROTOCOL != Protocol::Pixels)
            return SYS_ERROR_NOT_SUPPORTED;
        for (int index = 0; index < MAX_WINDOWS; ++index) {
            auto& window = windows_[index];
            if (window.open() && window.app == APP_EDITOR &&
                strcmp(window.path.data(), const_cast<char*>(path)) == 0)
                return index;
        }
        FileStat st{};
        long error = sys_stat(const_cast<char*>(path), &st);
        if (error < 0)
            return error;
        if (st.type != FILE_TYPE_FILE)
            return SYS_ERROR_INVALID;
        return open(APP_EDITOR, path);
    }

    long send(int index, const DisplayEvent& event) {
        if (index == NO_WINDOW || !windows_[index].open())
            return 0;
        auto& window = windows_[index];
        if (has_pixels(index) && !surface(index).configured())
            return 0;
        // The client is responding to the user again, so the next close
        // request is a new one.
        if (event.type == DISPLAY_EVENT_KEY || event.type == DISPLAY_EVENT_BUTTON)
            window.close_requested = false;
        Point origin = client_origin(window.rect);
        Point point = has_pixels(index) ? Point{pointer_.x - origin.x, pointer_.y - origin.y}
                                        : surface_point(window.rect, pointer_);
        return window.client.send({event, point.x, point.y}) ? 0 : close_window(index);
    }

    // Focuses the front window of a program. Opens one when there is none, or
    // another when Shift is held and the program allows several.
    long show(int app, unsigned int modifiers) {
        int previous = top_window();
        int index = front_window(app);
        bool another = APPS[app].multiple && (modifiers & DISPLAY_MOD_SHIFT) != 0;
        if (index == NO_WINDOW || another) {
            long slot = open(app);
            if (slot < 0) {
                command_error("desktop", APPS[app].program, slot);
                return 0;
            }
            index = static_cast<int>(slot);
        }
        return focus(index, previous, modifiers);
    }

    long focus(int index, int previous, unsigned int modifiers) {
        if (index == previous)
            return repaint(bounds(windows_[index].rect));
        last_click_ = {};
        left_click_ = false;
        raise(index);
        long error = send(previous, {DISPLAY_EVENT_RESET, 0, 0, modifiers});
        if (error == 0 && previous != NO_WINDOW && windows_[previous].open())
            error = repaint(bounds(windows_[previous].rect));
        return error < 0 ? error : repaint(bounds(windows_[index].rect));
    }

    // The close button. A client that asks before closing gets one chance to
    // exit by itself.
    long request_close(int index) {
        auto& window = windows_[index];
        if (!window.spec().asks_before_closing || window.close_requested || !surface(index).configured())
            return close_window(index);
        long error = focus(index, top_window(), 0);
        if (error < 0)
            return error;
        window.close_requested = true;
        return send(index, {INPUT_CLOSE_REQUEST, 0, 0, 0});
    }

    long close_window(int index) {
        auto& window = windows_[index];
        bool focused = index == top_window();
        Rect old = window.rect;
        window.client.stop();
        window.close_requested = false;
        surface(index).reset();
        window.rect = {};
        if (drag_ == index)
            drag_ = NO_WINDOW;
        if (last_click_.window == index)
            last_click_ = {};
        for (auto& target : button_targets_) {
            if (target == index)
                target = NO_WINDOW;
        }
        int next = top_window();
        if (focused && next != NO_WINDOW) {
            long error = repaint(bounds(windows_[next].rect));
            if (error < 0)
                return error;
        }
        long error = repaint(bounds(old));
        return error < 0 ? error : repaint(dock_);
    }

    // The offset in /etc/timezone, written as +HH:MM or -HH:MM. UTC without
    // the file or when it does not parse.
    static int read_timezone() {
        char text[8]{};
        int fd = sys_open(const_cast<char*>("/etc/timezone"), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return 0;
        long count = sys_read(fd, text, sizeof(text) - 1);
        sys_close(fd);
        auto digit = [&](int index) { return text[index] >= '0' && text[index] <= '9' ? text[index] - '0' : -1; };
        if (count < 6 || (text[0] != '+' && text[0] != '-') || text[3] != ':' || digit(1) < 0 || digit(2) < 0 ||
            digit(4) < 0 || digit(5) < 0)
            return 0;
        int minutes = (digit(1) * 10 + digit(2)) * 60 + digit(4) * 10 + digit(5);
        return text[0] == '-' ? -minutes : minutes;
    }

    static char* put_number(char* out, long value, int min_digits) {
        char digits[20];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while ((value != 0 || count < min_digits) && count < 19);
        while (count > 0)
            *out++ = digits[--count];
        return out;
    }

    static char* put_text(char* out, const char* text) {
        while (*text != 0)
            *out++ = *text++;
        return out;
    }

    // Writes the menu bar's clock and how long it stays right. With a
    // real-time clock this is the local date and time, as "Nov 20  14:05".
    // Without one it is the time since boot, as "Up 1:23".
    void menu_clock(char* out, long& wait_ms) const {
        static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        long now = sys_realtime();
        if (now >= 0)
            now += timezone_minutes_ * 60L;
        if (now < 0) {
            long uptime = sys_uptime_ms();
            out = put_text(out, "Up ");
            out = put_number(out, uptime / MINUTE_MS / 60, 1);
            *out++ = ':';
            out = put_number(out, uptime / MINUTE_MS % 60, 2);
            *out = 0;
            wait_ms = MINUTE_MS - uptime % MINUTE_MS;
            return;
        }
        // Days since 1970-01-01 to a month and day of the Gregorian calendar,
        // counting years from March.
        long days = now / 86400 + 719468;
        long era = days / 146097;
        long day_of_era = days - era * 146097;
        long year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
        long day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
        long shifted_month = (5 * day_of_year + 2) / 153;
        long day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
        long month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
        long second_of_day = now % 86400;
        out = put_text(out, MONTHS[month - 1]);
        *out++ = ' ';
        out = put_number(out, day, 1);
        out = put_text(out, "  ");
        out = put_number(out, second_of_day / 3600, 2);
        *out++ = ':';
        out = put_number(out, second_of_day / 60 % 60, 2);
        *out = 0;
        wait_ms = (60 - second_of_day % 60) * 1000;
    }

    // Keeps the menu bar showing the program in front and the clock. Reports
    // how long the clock text stays right.
    long update_menu(long& wait_ms) {
        if (menu_height_ == 0)
            return 0;
        char clock[desktop_chrome::MENU_CLOCK_CAPACITY];
        menu_clock(clock, wait_ms);
        int top = top_window();
        long changed = chrome_.set_menu(top == NO_WINDOW ? NO_APP : windows_[top].app, clock);
        return changed > 0 ? repaint({{0, 0}, screen_.width, menu_height_}) : changed;
    }

    // Windows stay below the menu bar.
    int clamp_top(long y, int height) const {
        int top = clamp(y, screen_.height - height);
        return top < menu_height_ ? menu_height_ : top;
    }

    // A click on a Dock item: a program is shown as its key would show it.
    long activate(int item, unsigned int modifiers) {
        if (item == desktop_dock::ITEM_EXIT) {
            quit_ = true;
            return 0;
        }
        return show(item, modifiers);
    }

    long handle(const DisplayTimedEvent& input) {
        const auto& event = input.event;
        if (event.type == DISPLAY_EVENT_KEY) {
            int shortcut = app_for_key(event.code);
            // Application windows open from anywhere. The example windows' keys
            // belong to the focused application while one has the keyboard.
            if (shortcut != NO_APP && APPS[shortcut].protocol != Protocol::Frame)
                return event.value == DISPLAY_KEY_PRESS ? show(shortcut, event.modifiers) : 0;
            int top = top_window();
            if (is_application(top))
                return send(top, event);
            if (shortcut != NO_APP)
                return event.value == DISPLAY_KEY_PRESS ? show(shortcut, event.modifiers) : 0;
            if (event.code == DISPLAY_KEY_ESCAPE || event.code == DISPLAY_KEY_Q)
                return 0;
            return send(top, event);
        }
        if (event.type == DISPLAY_EVENT_RESET) {
            drag_ = NO_WINDOW;
            last_click_ = {};
            left_click_ = false;
            for (auto& target : button_targets_)
                target = NO_WINDOW;
            for (int index = 0; index < MAX_WINDOWS; ++index) {
                long error = send(index, event);
                if (error < 0)
                    return error;
            }
            return 0;
        }
        if (event.type == DISPLAY_EVENT_REL && (event.code == DISPLAY_REL_X || event.code == DISPLAY_REL_Y))
            return move(event);
        if (event.type == DISPLAY_EVENT_BUTTON)
            return button(event, input.timestamp_ms);
        return 0;
    }

    long button(DisplayEvent event, unsigned long timestamp_ms) {
        if (event.code < DISPLAY_BUTTON_LEFT || event.code > DISPLAY_BUTTON_MIDDLE)
            return 0;
        bool left = event.code == DISPLAY_BUTTON_LEFT;
        int& target = button_targets_[event.code - DISPLAY_BUTTON_LEFT];
        if (event.value == DISPLAY_KEY_RELEASE) {
            int index = target;
            target = NO_WINDOW;
            if (left) {
                drag_ = NO_WINDOW;
                if (finish_click(index, timestamp_ms))
                    event.modifiers |= INPUT_MOD_DOUBLE_CLICK;
            }
            return send(index, event);
        }
        int index = window_at(pointer_);
        if (!left || index == NO_WINDOW || !in_body(windows_[index].rect, pointer_))
            last_click_ = {};
        DisplayEvent reset{DISPLAY_EVENT_RESET, 0, 0, event.modifiers};
        if (index == NO_WINDOW) {
            int item = left ? desktop_dock::item_at(dock_, pointer_) : desktop_dock::NO_ITEM;
            if (item != desktop_dock::NO_ITEM)
                return activate(desktop_dock::ITEMS[item], event.modifiers);
            return send(top_window(), reset);
        }
        const auto& rect = windows_[index].rect;
        if (left && close_rect(rect).contains(pointer_))
            return request_close(index);
        long error = focus(index, top_window(), event.modifiers);
        if (error < 0)
            return error;
        if (left && pointer_.y - rect.origin.y < TITLE_HEIGHT) {
            drag_ = index;
            drag_offset_ = {pointer_.x - rect.origin.x, pointer_.y - rect.origin.y};
        } else if (in_body(rect, pointer_)) {
            target = index;
            if (left) {
                left_press_ = pointer_;
                left_click_ = true;
            }
            return send(index, event);
        }
        return send(index, reset);
    }

    long move(const DisplayEvent& event) {
        Rect old_cursor = compositor_.cursor_rect();
        if (event.code == DISPLAY_REL_X)
            pointer_.x = clamp(static_cast<long>(pointer_.x) + event.value, screen_.width - 1);
        else
            pointer_.y = clamp(static_cast<long>(pointer_.y) + event.value, screen_.height - 1);
        if (left_click_ && !near(left_press_, pointer_))
            left_click_ = false;
        if (drag_ != NO_WINDOW) {
            Rect& window = windows_[drag_].rect;
            Rect old_window = window;
            window.origin = {clamp(pointer_.x - drag_offset_.x, screen_.width - window.width),
                             clamp_top(pointer_.y - drag_offset_.y, window.height)};
            long error = repaint(bounds(old_window));
            if (error == 0)
                error = repaint(bounds(window));
            if (error < 0)
                return error;
        } else if (int top = top_window();
                   top != NO_WINDOW && windows_[top].spec().pointer_motion && in_body(windows_[top].rect, pointer_)) {
            long error = send(top, event);
            if (error < 0)
                return error;
        }
        long error = repaint(old_cursor);
        return error < 0 ? error : repaint(compositor_.cursor_rect());
    }

    // Where a pixel window of the given outer size first appears.
    Point place(int index, int width, int height) const {
        Point origin = CORNER_MARGIN;
        switch (windows_[index].spec().placement) {
            case Placement::Center: origin = {(screen_.width - width) / 2, (screen_.height - height) / 2}; break;
            case Placement::Right: origin = {screen_.width - width - RIGHT_MARGIN.x, RIGHT_MARGIN.y}; break;
            case Placement::Corner: break;
        }
        // Windows on the right edge step leftwards so they stay on the screen.
        Point shift = cascade(windows_[index].step);
        if (windows_[index].spec().placement == Placement::Right)
            shift.x = -shift.x;
        return {clamp(origin.x + shift.x, screen_.width - width), clamp_top(origin.y + shift.y, height)};
    }

    long receive_pixels(int index) {
        auto& window = windows_[index];
        auto& pixels = surface(index);
        switch (window.client.receive_pixels(pixels, screen_.width - 2 * FRAME,
                                             screen_.height - TITLE_HEIGHT - FRAME)) {
            case PixelSurface::Result::Pending: return 0;
            case PixelSurface::Result::Configured: {
                int width = outer_width(static_cast<int>(pixels.width()));
                int height = outer_height(static_cast<int>(pixels.height()));
                window.rect = {place(index, width, height), width, height};
                long error = chrome_.configure(index, window.app, width);
                return error < 0 ? error : repaint(bounds(window.rect));
            }
            case PixelSurface::Result::Updated: {
                auto dirty = pixels.damage();
                Point origin = client_origin(window.rect);
                return repaint({{origin.x + static_cast<int>(dirty.x), origin.y + static_cast<int>(dirty.y)},
                                static_cast<int>(dirty.width),
                                static_cast<int>(dirty.height)});
            }
            case PixelSurface::Result::EditText: {
                if (window.app != APP_FILES)
                    return close_window(index);
                int previous = top_window();
                long editor = edit_text(pixels.edit_path());
                if (editor < 0)
                    return send(index, {INPUT_EDIT_ERROR, 0, static_cast<int>(editor), 0});
                return focus(static_cast<int>(editor), previous, 0);
            }
            case PixelSurface::Result::Closed: return close_window(index);
        }
        return 0;
    }

    long repaint_client(int index) {
        const auto& window = windows_[index];
        if (window.protocol() != Protocol::Text)
            return repaint(window.rect);
        const auto& dirty = window.client.damage();
        if (dirty.empty())
            return 0;
        Point origin = client_origin(window.rect);
        return repaint({{origin.x + dirty.left * GLYPH_WIDTH, origin.y + dirty.top * GLYPH_HEIGHT},
                        (dirty.right - dirty.left) * GLYPH_WIDTH,
                        (dirty.bottom - dirty.top) * GLYPH_HEIGHT});
    }

    long repaint(const Rect& damage) {
        int right = clamp(static_cast<long>(damage.origin.x) + damage.width, screen_.width);
        int bottom = clamp(static_cast<long>(damage.origin.y) + damage.height, screen_.height);
        for (int y = clamp(damage.origin.y, screen_.height); y < bottom;) {
            int height = clamp(bottom - y, TILE_SIZE);
            for (int x = clamp(damage.origin.x, screen_.width); x < right;) {
                int width = clamp(right - x, TILE_SIZE);
                for (int row = 0; row < height; ++row)
                    for (int col = 0; col < width; ++col)
                        pixels_[row * width + col] = compositor_.color({x + col, y + row});
                DisplayPresent area{static_cast<unsigned int>(x),          static_cast<unsigned int>(y),
                                    static_cast<unsigned int>(width),      static_cast<unsigned int>(height),
                                    static_cast<unsigned int>(width * 4),  0,
                                    reinterpret_cast<long>(pixels_.data())};
                long error = sys_ioctl(fd_, DISPLAY_PRESENT, reinterpret_cast<long>(&area));
                if (error < 0)
                    return error;
                x += width;
            }
            y += height;
        }
        return 0;
    }
};

}  // namespace

extern "C" int main(int argc, char**) {
    if (argc != 1)
        return command_usage("Usage: desktop\n");
    int fd = sys_open((char*)"/dev/display", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return command_error("desktop", "/dev/display", fd);
    DisplayInfo mode{};
    long error = sys_ioctl(fd, DISPLAY_GET_INFO, reinterpret_cast<long>(&mode));
    if (error == 0 && (mode.format != DISPLAY_FORMAT_XRGB8888 || mode.width < 160 || mode.height < 120 ||
                       mode.width > 0x7FFFFFFF || mode.height > 0x7FFFFFFF))
        error = SYS_ERROR_NOT_SUPPORTED;
    psf::Font font{};
#ifndef ZONIX_PIXEL_TERMINAL
    if (error == 0 && (!psf::parse(terminal_font, &font) || font.width != GLYPH_WIDTH || font.height != GLYPH_HEIGHT))
        error = SYS_ERROR_NOT_SUPPORTED;
#endif
    if (error == 0)
        error = command_text(
            STDOUT_FD, "Desktop: click the Dock or press F2 Terminal, F4 Files, F12 exit. Shift opens another window.\n");
    if (error == 0)
        error = sys_ioctl(fd, DISPLAY_ACQUIRE, DISPLAY_ACQUIRE_EVENTS | DISPLAY_ACQUIRE_KEYBOARD_CTRL_C);
    if (error == 0) {
        Desktop desktop(fd, static_cast<int>(mode.width), static_cast<int>(mode.height), font);
        error = desktop.run();
        long release = sys_ioctl(fd, DISPLAY_RELEASE, 0);
        if (error == 0)
            error = release;
    }
    if (sys_close(fd) < 0 && error == 0)
        error = SYS_ERROR_IO;
    return error < 0 ? command_error("desktop", "/dev/display", error) : 0;
}
