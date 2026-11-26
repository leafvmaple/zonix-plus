#include "../command.h"
#include "compositor.h"
#include "../gui/settings.h"
#include "../net/zonix_net.h"
#include <sys/algorithm.hpp>
#include <sys/array.hpp>
#include <sys/cstdlib.hpp>

namespace {

using namespace desktop;
using namespace desktop_ipc;
using desktop_chrome::CLOSE_SIZE;

// Owns the windows and the pointer, routes input to clients and decides which
// parts of the screen to repaint. The Compositor supplies the pixels.
class Desktop {
public:
    // With `bench` Desktop drives itself through a fixed session instead of
    // reading input, logs how long it took and returns.
    Desktop(int fd, int width, int height, bool bench)
        : fd_(fd),
          bench_(bench),
          screen_{{0, 0}, width, height},
          pointer_{width / 2, height / 2},
          dock_(desktop_dock::bounds(width, height)),
          // A screen with room for the Dock also gets the menu bar.
          menu_height_(dock_.width != 0 ? desktop_chrome::MENU_HEIGHT : 0),
          // So does the column of desktop icons, between the two.
          icons_(menu_height_ != 0 ? desktop_icons::bounds(width, menu_height_, dock_.origin.y) : Rect{}),
          compositor_(
              {windows_, order_, surfaces_, chrome_, pointer_, screen_, dock_, icons_, menu_height_, resize_outline_}) {
        terminal_fits_ = width >= TERMINAL_OUTER_WIDTH && height >= TERMINAL_OUTER_HEIGHT;
        timezone_minutes_ = zonix_gui::Settings::load().timezone_minutes;
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
        error = compositor_.init();
        if (error < 0)
            return error;
        band_.reset(static_cast<unsigned int*>(malloc(PRESENT_PIXELS * sizeof(unsigned int))));
        if (!band_)
            return SYS_ERROR_FULL;
        error = start_hardware_cursor();
        if (error < 0)
            return error;
        FileStat example{};
        if (sys_stat(const_cast<char*>(APPS[APP_EXAMPLE_A].program), &example) == 0 && example.type == FILE_TYPE_FILE) {
            open_at_front(APP_EXAMPLE_A);
            open_at_front(APP_EXAMPLE_B);
        }
        error = repaint(screen_);
        if (error == 0 && bench_)
            return bench();
        const unsigned int display_mask = 1U << fd_;
        while (error == 0 && !quit_) {
            unsigned int mask = display_mask;
            for (const auto& window : windows_)
                mask |= window.client.mask();
            long clock_wait_ms = 0;
            error = update_menu(clock_wait_ms);
            if (error < 0)
                break;
            // Damage collects while input and client updates are waiting, so a
            // burst of pointer motion costs one repaint. A client that never
            // stops sending still sees its updates about once a frame.
            long ready = sys_wait_readable_for(mask, 0);
            if (ready < 0)
                return ready;
            if (ready == 0 || (damage_count_ != 0 && sys_uptime_ms() - damage_since_ms_ >= FRAME_MS)) {
                error = flush();
                if (error < 0)
                    break;
            }
            // With a menu bar, wake when its clock moves to the next minute.
            if (ready == 0)
                ready = menu_height_ != 0 ? sys_wait_readable_for(mask, clock_wait_ms) : sys_wait_readable(mask);
            if (ready < 0)
                return ready;
            error = receive_clients(static_cast<unsigned long>(ready));
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
    // The most pixels one DISPLAY_PRESENT carries, which bounds the kernel's copy.
    static constexpr int PRESENT_PIXELS = 65536;
    static constexpr int MAX_DAMAGE = 6;
    // Two damaged areas are repainted as one when that adds at most this many pixels.
    static constexpr long MERGE_SLACK = 8192;
    static constexpr long FRAME_MS = 16;
    static constexpr long MINUTE_MS = 60000;
    static constexpr long NETWORK_POLL_MS = 5000;
    // Margins for a window placed at the right edge or in the top-left corner.
    static constexpr Point RIGHT_MARGIN{24, 56};
    static constexpr Point CORNER_MARGIN{16, 40};
    // Each further window of a program moves this far down and to the right,
    // starting over after CASCADE_STEPS windows.
    static constexpr int CASCADE_OFFSET = 26;
    static constexpr int CASCADE_STEPS = 6;
    static constexpr int TERMINAL_OUTER_WIDTH = outer_width(TERMINAL_WIDTH);
    static constexpr int TERMINAL_OUTER_HEIGHT = outer_height(TERMINAL_HEIGHT);
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

    struct FreePixels {
        void operator()(unsigned int* pixels) const noexcept { free(pixels); }
    };

    int fd_;
    bool bench_;
    // The display draws the pointer on its cursor plane.
    bool hardware_cursor_{};
    // Rows composed for one DISPLAY_PRESENT.
    sys::unique_ptr<unsigned int, FreePixels> band_;
    // Areas to repaint once no input or client update is waiting, and when
    // the first of them was added.
    sys::array<Rect, MAX_DAMAGE> damage_{};
    int damage_count_{};
    long damage_since_ms_{};
    desktop_chrome::Chrome chrome_;
    Rect screen_;
    Point pointer_;
    Point drag_offset_{};
    Rect dock_;
    int menu_height_;
    Rect icons_;
    // The window being resized, its new outer rectangle, and the pointer's
    // distance from that rectangle's bottom-right corner.
    int resizing_ = NO_WINDOW;
    Rect resize_outline_{};
    Point resize_offset_{};
    // The local time's offset from UTC.
    int timezone_minutes_{};
    // The network status last read from netd, and when.
    int network_state_ = NET_ERROR_NO_SERVICE;
    sys::array<char, 16> network_address_{};
    long network_checked_ms_ = -1;
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
    // The last press on a desktop icon, whose item is in `window`.
    Click last_icon_press_{};
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

    static Rect minimize_rect(const Rect& window) {
        return {{window.origin.x + desktop_chrome::MINIMIZE_LEFT, window.origin.y + desktop_chrome::CLOSE_TOP},
                CLOSE_SIZE,
                CLOSE_SIZE};
    }

    static Rect close_rect(const Rect& window) {
        return {{window.origin.x + desktop_chrome::CLOSE_LEFT, window.origin.y + desktop_chrome::CLOSE_TOP},
                CLOSE_SIZE,
                CLOSE_SIZE};
    }

    int top_window() const { return desktop::top_window(windows_, order_); }

    int window_at(Point point) const {
        for (int index : order_)
            if (windows_[index].visible() && windows_[index].rect.contains(point))
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
        if (repaint_desk(dock_) < 0) {
            window.client.stop();
            return SYS_ERROR_IO;
        }
        window.close_requested = false;
        window.minimized = false;
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
        for (int index = 0; index < MAX_WINDOWS; ++index) {
            auto& window = windows_[index];
            if (window.open() && window.app == APP_EDITOR && strcmp(window.path.data(), const_cast<char*>(path)) == 0)
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

    // Focuses the front window of a program, showing it again if it is
    // minimized. Opens one when there is none, or another when Shift is held
    // and the program allows several.
    long show(int app, unsigned int modifiers) {
        int previous = top_window();
        int index = front_window(app);
        bool another = APPS[app].multiple && (modifiers & DISPLAY_MOD_SHIFT) != 0;
        if (index != NO_WINDOW && !another)
            windows_[index].minimized = false;
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
        window.minimized = false;
        surface(index).reset();
        window.rect = {};
        if (drag_ == index)
            drag_ = NO_WINDOW;
        if (resizing_ == index)
            cancel_resize();
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
        if (error == 0)
            error = repaint_desk(dock_);
        // Settings may have changed what Desktop itself shows.
        if (error == 0 && window.app == APP_SETTINGS)
            error = apply_settings();
        return error;
    }

    long apply_settings() {
        auto settings = zonix_gui::Settings::load();
        timezone_minutes_ = settings.timezone_minutes;
        long changed = chrome_.set_text_size(settings.text_size);
        return changed > 0 ? repaint_desk(screen_) : changed;
    }

    // Hides a window until its program is shown again. The window below it
    // takes the keyboard.
    long minimize(int index) {
        auto& window = windows_[index];
        long error = send(index, {DISPLAY_EVENT_RESET, 0, 0, 0});
        if (error < 0 || !window.open())
            return error;
        window.minimized = true;
        if (drag_ == index)
            drag_ = NO_WINDOW;
        if (resizing_ == index)
            cancel_resize();
        if (last_click_.window == index)
            last_click_ = {};
        for (auto& target : button_targets_) {
            if (target == index)
                target = NO_WINDOW;
        }
        error = repaint(bounds(window.rect));
        int next = top_window();
        if (error == 0 && next != NO_WINDOW)
            error = repaint(bounds(windows_[next].rect));
        return error;
    }

    // Alt-Tab sends the front window to the back, so repeated presses visit
    // every window. With Shift the back window comes to the front instead.
    long cycle(bool backwards) {
        int top = top_window();
        if (top == NO_WINDOW)
            return 0;
        int target = NO_WINDOW;
        if (backwards) {
            for (int index : order_) {
                if (windows_[index].visible())
                    target = index;
            }
        } else {
            int slot = 0;
            while (order_[slot] != top)
                ++slot;
            for (; slot + 1 < MAX_WINDOWS; ++slot)
                order_[slot] = order_[slot + 1];
            order_[MAX_WINDOWS - 1] = top;
            target = top_window();
        }
        return target == top ? 0 : focus(target, top, 0);
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

    // Writes the network status in front of the clock: the address, "No
    // network" while netd has none, nothing without netd. netd is asked again
    // every NETWORK_POLL_MS, so a lease or a netd started later shows up.
    char* network_status(char* out, long& wait_ms) {
        long now = sys_uptime_ms();
        if (network_checked_ms_ < 0 || now - network_checked_ms_ >= NETWORK_POLL_MS) {
            NetInfo info;
            network_state_ = net_info(&info);
            if (network_state_ == 0)
                net_format_ipv4(info.address, network_address_.data());
            network_checked_ms_ = now;
        }
        wait_ms = NETWORK_POLL_MS - (now - network_checked_ms_);
        if (network_state_ == 0)
            out = put_text(put_text(out, network_address_.data()), "    ");
        else if (network_state_ != NET_ERROR_NO_SERVICE)
            out = put_text(out, "No network    ");
        return out;
    }

    // Keeps the menu bar showing the program in front, the network status and
    // the clock. Reports how long that text stays right.
    long update_menu(long& wait_ms) {
        if (menu_height_ == 0)
            return 0;
        char clock[desktop_chrome::MENU_CLOCK_CAPACITY];
        long network_wait_ms = 0;
        menu_clock(network_status(clock, network_wait_ms), wait_ms);
        wait_ms = sys::min(wait_ms, network_wait_ms);
        int top = top_window();
        long changed = chrome_.set_menu(top == NO_WINDOW ? NO_APP : windows_[top].app, clock);
        return changed > 0 ? repaint_desk({{0, 0}, screen_.width, menu_height_}) : changed;
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

    // A left press selects the desktop icon under the pointer, or none. A
    // second press on the same icon soon after opens its folder in Files.
    long press_icon(int item, unsigned long timestamp_ms) {
        bool again = item != desktop_icons::NO_ITEM && last_icon_press_.window == item &&
                     near(last_icon_press_.position, pointer_) &&
                     timestamp_ms - last_icon_press_.timestamp_ms <= DOUBLE_CLICK_MS;
        last_icon_press_ = item != desktop_icons::NO_ITEM && !again ? Click{item, pointer_, timestamp_ms} : Click{};
        long changed = chrome_.select_icon(item);
        long error = changed > 0 ? repaint_desk(icons_) : changed;
        if (error < 0 || !again)
            return error;
        const char* path = desktop_icons::ITEMS[item].path;
        int previous = top_window();
        long slot = open(APP_FILES, path);
        if (slot < 0) {
            command_error("desktop", path, slot);
            return 0;
        }
        return focus(static_cast<int>(slot), previous, 0);
    }

    long handle(const DisplayTimedEvent& input) {
        const auto& event = input.event;
        if (event.type == DISPLAY_EVENT_KEY && (event.modifiers & DISPLAY_MOD_ALT) != 0 &&
            (event.code == KEY_TAB || event.code == KEY_M)) {
            if (event.value == DISPLAY_KEY_RELEASE)
                return 0;
            if (event.code == KEY_TAB)
                return cycle((event.modifiers & DISPLAY_MOD_SHIFT) != 0);
            int top = top_window();
            return top == NO_WINDOW ? 0 : minimize(top);
        }
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
            cancel_resize();
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
            if (left && resizing_ != NO_WINDOW)
                return finish_resize();
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
        if (left) {
            int icon = index == NO_WINDOW ? desktop_icons::item_at(icons_, pointer_) : desktop_icons::NO_ITEM;
            long error = press_icon(icon, timestamp_ms);
            if (error < 0)
                return error;
            if (icon != desktop_icons::NO_ITEM)
                return send(top_window(), reset);
        }
        if (index == NO_WINDOW) {
            int item = left ? desktop_dock::item_at(dock_, pointer_) : desktop_dock::NO_ITEM;
            if (item != desktop_dock::NO_ITEM)
                return activate(desktop_dock::ITEMS[item], event.modifiers);
            return send(top_window(), reset);
        }
        const auto& rect = windows_[index].rect;
        if (left && close_rect(rect).contains(pointer_))
            return request_close(index);
        if (left && rect.width >= desktop_chrome::BUTTONS_WIDTH && minimize_rect(rect).contains(pointer_))
            return minimize(index);
        long error = focus(index, top_window(), event.modifiers);
        if (error < 0)
            return error;
        if (left && windows_[index].spec().resizable && surface(index).configured() && in_resize_grip(rect, pointer_)) {
            resizing_ = index;
            resize_outline_ = rect;
            resize_offset_ = {rect.origin.x + rect.width - pointer_.x, rect.origin.y + rect.height - pointer_.y};
            return repaint_outline(resize_outline_);
        }
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
        if (resizing_ != NO_WINDOW) {
            long error = drag_resize();
            if (error < 0)
                return error;
        } else if (drag_ != NO_WINDOW) {
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
        // The cursor plane moves the pointer at once, even while composing
        // lags behind.
        if (hardware_cursor_) {
            DisplayCursorPosition position{static_cast<unsigned int>(pointer_.x),
                                           static_cast<unsigned int>(pointer_.y)};
            return sys_ioctl(fd_, DISPLAY_MOVE_CURSOR, reinterpret_cast<long>(&position));
        }
        long error = repaint(old_cursor);
        return error < 0 ? error : repaint(compositor_.cursor_rect());
    }

    // Hands the pointer to the display's cursor plane if it has one.
    // Returns 0 either way, or a negative error.
    long start_hardware_cursor() {
        sys::array<unsigned int, DISPLAY_CURSOR_SIZE * DISPLAY_CURSOR_SIZE> image{};
        Compositor::cursor_image(image.data());
        DisplayCursorPosition position{static_cast<unsigned int>(pointer_.x), static_cast<unsigned int>(pointer_.y)};
        long error = sys_ioctl(fd_, DISPLAY_MOVE_CURSOR, reinterpret_cast<long>(&position));
        DisplayCursor cursor{0, 0, reinterpret_cast<long>(image.data())};
        if (error == 0)
            error = sys_ioctl(fd_, DISPLAY_SET_CURSOR, reinterpret_cast<long>(&cursor));
        if (error == SYS_ERROR_NOT_SUPPORTED)
            return 0;
        hardware_cursor_ = error == 0;
        compositor_.set_hardware_cursor(hardware_cursor_);
        return error;
    }

    // The smallest window a resize drag makes. Clients may clamp further.
    static constexpr int MIN_RESIZE_WIDTH = 160;
    static constexpr int MIN_RESIZE_HEIGHT = TITLE_HEIGHT + 80;

    long repaint_outline(const Rect& outline) {
        if (outline.width == 0)
            return 0;
        const Point& o = outline.origin;
        long error = repaint({o, outline.width, RESIZE_OUTLINE});
        if (error == 0)
            error = repaint({{o.x, o.y + outline.height - RESIZE_OUTLINE}, outline.width, RESIZE_OUTLINE});
        if (error == 0)
            error = repaint({o, RESIZE_OUTLINE, outline.height});
        if (error == 0)
            error = repaint({{o.x + outline.width - RESIZE_OUTLINE, o.y}, RESIZE_OUTLINE, outline.height});
        return error;
    }

    void cancel_resize() {
        if (resizing_ == NO_WINDOW)
            return;
        Rect outline = resize_outline_;
        resizing_ = NO_WINDOW;
        resize_outline_ = {};
        repaint_outline(outline);
    }

    // Follows the pointer with the outline of the new size.
    long drag_resize() {
        const Rect& window = windows_[resizing_].rect;
        int width = pointer_.x + resize_offset_.x - window.origin.x;
        int height = pointer_.y + resize_offset_.y - window.origin.y;
        width = sys::max(MIN_RESIZE_WIDTH, sys::min(width, screen_.width - window.origin.x));
        height = sys::max(MIN_RESIZE_HEIGHT, sys::min(height, screen_.height - window.origin.y));
        if (width == resize_outline_.width && height == resize_outline_.height)
            return 0;
        Rect old = resize_outline_;
        resize_outline_ = {window.origin, width, height};
        long error = repaint_outline(old);
        return error < 0 ? error : repaint_outline(resize_outline_);
    }

    // Asks the client for the outline's size. The window changes when the
    // client answers with its new surface.
    long finish_resize() {
        int index = resizing_;
        Rect outline = resize_outline_;
        resizing_ = NO_WINDOW;
        resize_outline_ = {};
        long error = repaint_outline(outline);
        const Rect& window = windows_[index].rect;
        if (error < 0 || !windows_[index].open() || (outline.width == window.width && outline.height == window.height))
            return error;
        Input request{{INPUT_RESIZE, 0, 0, 0}, outline.width - 2 * FRAME, outline.height - TITLE_HEIGHT - FRAME};
        return windows_[index].client.send(request) ? 0 : close_window(index);
    }

    // Where a pixel window of the given outer size first appears.
    Point place(int index, int width, int height) const {
        Point origin = CORNER_MARGIN;
        switch (windows_[index].spec().placement) {
            case Placement::Center: origin = {(screen_.width - width) / 2, (screen_.height - height) / 2}; break;
            case Placement::Right: {
                // Left of the desktop icons, so they stay in view.
                int right = icons_.width != 0 ? icons_.origin.x : screen_.width;
                origin = {right - width - RIGHT_MARGIN.x, RIGHT_MARGIN.y};
                break;
            }
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
        switch (
            window.client.receive_pixels(pixels, screen_.width - 2 * FRAME, screen_.height - TITLE_HEIGHT - FRAME)) {
            case PixelSurface::Result::Pending: return 0;
            case PixelSurface::Result::Configured: {
                int width = outer_width(static_cast<int>(pixels.width()));
                int height = outer_height(static_cast<int>(pixels.height()));
                window.rect = {place(index, width, height), width, height};
                long error = chrome_.configure(index, window.app, width);
                return error < 0 ? error : repaint(bounds(window.rect));
            }
            case PixelSurface::Result::Resized: {
                Rect old = window.rect;
                window.rect.width = outer_width(static_cast<int>(pixels.width()));
                window.rect.height = outer_height(static_cast<int>(pixels.height()));
                window.rect.origin = {clamp(old.origin.x, screen_.width - window.rect.width),
                                      clamp_top(old.origin.y, window.rect.height)};
                long error = chrome_.configure(index, window.app, window.rect.width);
                if (error == 0)
                    error = repaint(bounds(old));
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

    // Reads what the clients whose descriptors are set in `ready` sent.
    long receive_clients(unsigned long ready) {
        long error = 0;
        for (int index = 0; index < MAX_WINDOWS && error == 0; ++index) {
            auto& window = windows_[index];
            if ((ready & window.client.mask()) == 0)
                continue;
            if (has_pixels(index))
                error = receive_pixels(index);
            else
                error = window.client.receive_frame() ? repaint(window.rect) : close_window(index);
        }
        return error;
    }

    // The benchmark opens Terminal and Files, drags the front window and then
    // moves the pointer alone, presenting every step, and logs each result as
    // "desktop bench: <what> <count> <total ms> <slowest ms>".
    static constexpr int BENCH_STEPS = 120;
    // An opened program counts as ready once it has sent nothing for this long.
    static constexpr long BENCH_QUIET_MS = 500;
    static constexpr long BENCH_OPEN_LIMIT_MS = 8000;

    // Waits up to `wait_ms` for any client and reads what arrived. Returns 1
    // if something did, 0 if not, or a negative error.
    long serve_clients(long wait_ms) {
        unsigned int mask = 0;
        for (const auto& window : windows_)
            mask |= window.client.mask();
        long ready = mask != 0 ? sys_wait_readable_for(mask, wait_ms) : 0;
        if (ready <= 0)
            return ready;
        long error = receive_clients(static_cast<unsigned long>(ready));
        return error < 0 ? error : 1;
    }

    static void bench_report(const char* what, long count, long total_ms, long slowest_ms) {
        char line[96];
        char* out = put_text(put_text(line, "desktop bench: "), what);
        *out++ = ' ';
        out = put_number(out, count, 1);
        *out++ = ' ';
        out = put_number(out, total_ms, 1);
        *out++ = ' ';
        out = put_number(out, slowest_ms, 1);
        *out++ = '\n';
        *out = 0;
        command_log(6, line);
    }

    // Serves the clients until they stay quiet. Reports the time to their
    // last update, how many repaints that took and the slowest of them.
    long bench_open(int app, const char* what) {
        long error = show(app, 0);
        long start = sys_uptime_ms();
        long last = start;
        long repaints = 0;
        long slowest = 0;
        while (error == 0 && sys_uptime_ms() - last < BENCH_QUIET_MS && sys_uptime_ms() - start < BENCH_OPEN_LIMIT_MS) {
            long served = serve_clients(20);
            if (served < 0)
                return served;
            if (served > 0)
                last = sys_uptime_ms();
            if (error == 0 && damage_count_ != 0) {
                long before = sys_uptime_ms();
                error = flush();
                slowest = sys::max(slowest, sys_uptime_ms() - before);
                ++repaints;
            }
        }
        if (error == 0)
            bench_report(what, repaints, last - start, slowest);
        return error;
    }

    // Moves the pointer back and forth, presenting every step.
    long bench_motion(const char* what) {
        long start = sys_uptime_ms();
        long slowest = 0;
        for (int step = 0; step < BENCH_STEPS; ++step) {
            long before = sys_uptime_ms();
            int dx = step / 30 % 2 == 0 ? 6 : -6;
            int dy = step / 15 % 2 == 0 ? 3 : -3;
            long error = move({DISPLAY_EVENT_REL, DISPLAY_REL_X, dx, 0});
            if (error == 0)
                error = move({DISPLAY_EVENT_REL, DISPLAY_REL_Y, dy, 0});
            if (error == 0)
                error = flush();
            // A client that follows the pointer answers with its own updates.
            if (error == 0)
                error = sys::min(serve_clients(0), 0L);
            if (error < 0)
                return error;
            slowest = sys::max(slowest, sys_uptime_ms() - before);
        }
        bench_report(what, BENCH_STEPS, sys_uptime_ms() - start, slowest);
        return 0;
    }

    long bench() {
        long start = sys_uptime_ms();
        long error = flush();
        if (error == 0)
            bench_report("screen", 1, sys_uptime_ms() - start, sys_uptime_ms() - start);
        if (error == 0)
            error = bench_open(APP_TERMINAL, "open-terminal");
        if (error == 0)
            error = bench_open(APP_FILES, "open-files");
        int top = top_window();
        if (error == 0 && top != NO_WINDOW) {
            drag_ = top;
            drag_offset_ = {pointer_.x - windows_[top].rect.origin.x, pointer_.y - windows_[top].rect.origin.y};
            error = bench_motion("drag");
            drag_ = NO_WINDOW;
        }
        if (error == 0)
            error = bench_motion("pointer");
        return error;
    }

    static long area_of(const Rect& rect) { return static_cast<long>(rect.width) * rect.height; }

    static Rect unite(const Rect& a, const Rect& b) {
        int left = sys::min(a.origin.x, b.origin.x);
        int top = sys::min(a.origin.y, b.origin.y);
        int right = sys::max(a.origin.x + a.width, b.origin.x + b.width);
        int bottom = sys::max(a.origin.y + a.height, b.origin.y + b.height);
        return {{left, top}, right - left, bottom - top};
    }

    // Marks an area to be repainted from the current state once no input or
    // client update is waiting. An area close to one already marked joins it.
    long repaint(const Rect& damage) {
        Rect area = Compositor::intersect(damage, screen_);
        if (area.width == 0)
            return 0;
        if (damage_count_ == 0)
            damage_since_ms_ = sys_uptime_ms();
        // The marked area that grows least by taking this one. Overlapping
        // areas grow by less than nothing.
        int best = -1;
        long best_growth = 0;
        for (int index = 0; index < damage_count_; ++index) {
            long growth = area_of(unite(damage_[index], area)) - area_of(damage_[index]) - area_of(area);
            if (best < 0 || growth < best_growth) {
                best = index;
                best_growth = growth;
            }
        }
        if (best >= 0 && (best_growth <= MERGE_SLACK || damage_count_ == MAX_DAMAGE))
            damage_[best] = unite(damage_[best], area);
        else
            damage_[damage_count_++] = area;
        return 0;
    }

    // Marks an area whose desk layer changed: background, menu bar, icons or Dock.
    long repaint_desk(const Rect& damage) {
        compositor_.refresh_desk(damage);
        return repaint(damage);
    }

    // Composes every marked area in bands of rows and presents each band.
    long flush() {
        int count = damage_count_;
        damage_count_ = 0;
        for (int index = 0; index < count; ++index) {
            const Rect& area = damage_[index];
            int rows = sys::max(1, PRESENT_PIXELS / area.width);
            for (int y = area.origin.y; y < area.origin.y + area.height; y += rows) {
                Rect band{{area.origin.x, y}, area.width, sys::min(rows, area.origin.y + area.height - y)};
                compositor_.render(band, band_.get(), band.width);
                DisplayPresent present{
                    static_cast<unsigned int>(band.origin.x),  static_cast<unsigned int>(band.origin.y),
                    static_cast<unsigned int>(band.width),     static_cast<unsigned int>(band.height),
                    static_cast<unsigned int>(band.width * 4), 0,
                    reinterpret_cast<long>(band_.get())};
                long error = sys_ioctl(fd_, DISPLAY_PRESENT, reinterpret_cast<long>(&present));
                if (error < 0)
                    return error;
            }
        }
        return 0;
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    bool bench = argc == 2 && strcmp(argv[1], const_cast<char*>("--bench")) == 0;
    if (argc != 1 && !bench)
        return command_usage("Usage: desktop [--bench]\n");
    int fd = sys_open((char*)"/dev/display", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return command_error("desktop", "/dev/display", fd);
    DisplayInfo mode{};
    long error = sys_ioctl(fd, DISPLAY_GET_INFO, reinterpret_cast<long>(&mode));
    if (error == 0 && (mode.format != DISPLAY_FORMAT_XRGB8888 || mode.width < 160 || mode.height < 120 ||
                       mode.width > 0x7FFFFFFF || mode.height > 0x7FFFFFFF))
        error = SYS_ERROR_NOT_SUPPORTED;
    if (error == 0)
        error = command_text(
            STDOUT_FD,
            "Desktop: click the Dock or press F2 Terminal, F4 Files, F12 exit. Shift opens another window.\n");
    if (error == 0)
        error = sys_ioctl(fd, DISPLAY_ACQUIRE, DISPLAY_ACQUIRE_EVENTS | DISPLAY_ACQUIRE_KEYBOARD_CTRL_C);
    // Ahead of its applications, the compositor answers input as soon as it
    // arrives, even while one of them is busy; each application it starts is
    // put back to the default priority.
    if (error == 0)
        error = sys_set_priority(0, PROCESS_PRIORITY_HIGHEST);
    if (error == 0) {
        Desktop desktop(fd, static_cast<int>(mode.width), static_cast<int>(mode.height), bench);
        error = desktop.run();
        long release = sys_ioctl(fd, DISPLAY_RELEASE, 0);
        if (error == 0)
            error = release;
    }
    if (sys_close(fd) < 0 && error == 0)
        error = SYS_ERROR_IO;
    return error < 0 ? command_error("desktop", "/dev/display", error) : 0;
}
