#include "../command.h"
#include "compositor.h"
#include <kernel/psf.h>
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
          compositor_({windows_, order_, surfaces_, chrome_, pointer_, screen_, font}) {
        windows_[0].rect = {{width / 8, height / 5}, width / 5 * 2, height / 2};
        windows_[1].rect = {{width / 3, height / 3}, width / 5 * 2, height / 2};
        constexpr int terminal_width = outer_width(TERMINAL_WIDTH);
        constexpr int terminal_height = outer_height(TERMINAL_HEIGHT);
        if (is_text_window(TERMINAL_WINDOW))
            windows_[TERMINAL_WINDOW].rect = {
                {(width - terminal_width) / 2, (height - terminal_height) / 2}, terminal_width, terminal_height};
        terminal_fits_ = width >= terminal_width && height >= terminal_height;
    }

    long run() {
        sys::array<int, WINDOW_COUNT> widths{};
        for (int index = 0; index < WINDOW_COUNT; ++index)
            widths[index] = windows_[index].rect.width;
        long error = chrome_.init(screen_.width, widths);
        if (error < 0)
            return error;
        FileStat example{};
        if (sys_stat(const_cast<char*>(WINDOWS[0].program), &example) == 0 && example.type == FILE_TYPE_FILE) {
            start_client(0);
            start_client(1);
        }
        error = repaint(screen_);
        const unsigned int display_mask = 1U << fd_;
        while (error == 0) {
            unsigned int mask = display_mask;
            for (const auto& window : windows_)
                mask |= window.client.mask();
            long ready = sys_wait_readable(mask);
            if (ready < 0)
                return ready;
            for (int index = 0; index < WINDOW_COUNT && error == 0; ++index) {
                auto& window = windows_[index];
                if ((static_cast<unsigned long>(ready) & window.client.mask()) == 0)
                    continue;
                if (is_pixel_window(index))
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
                bool client_shortcuts = top_window() >= TERMINAL_WINDOW;
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
    // Margins for a window placed at the right edge or in the top-left corner.
    static constexpr Point RIGHT_MARGIN{24, 56};
    static constexpr Point CORNER_MARGIN{16, 16};
    inline static sys::array<unsigned int, TILE_SIZE * TILE_SIZE> pixels_{};
    inline static Surfaces surfaces_{};

    static PixelSurface& surface(int index) { return surfaces_[index - FIRST_PIXEL_WINDOW]; }

    int fd_;
    desktop_chrome::Chrome chrome_;
    Rect screen_;
    Windows windows_;
    Point pointer_;
    Point drag_offset_{};
    Order order_{{1, 0, TERMINAL_WINDOW, PIXEL_WINDOW, FILE_WINDOW, EDITOR_WINDOW}};
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
            {window.origin.x + desktop_chrome::close_left(window.width), window.origin.y + desktop_chrome::CLOSE_TOP},
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

    // The window a key opens, or NO_WINDOW.
    static int window_for_key(unsigned int code) {
        for (int index = 0; index < WINDOW_COUNT; ++index)
            if (WINDOWS[index].key != 0 && WINDOWS[index].key == code)
                return index;
        return NO_WINDOW;
    }

    void start_client(int index) {
        long error = windows_[index].client.start(index);
        if (error < 0)
            command_error("desktop", WINDOWS[index].program, error);
    }

    long edit_text(const char* path) {
        if (!is_pixel_window(TERMINAL_WINDOW) || !terminal_fits_)
            return SYS_ERROR_NOT_SUPPORTED;
        auto& editor = windows_[EDITOR_WINDOW];
        if (editor.open())
            return SYS_ERROR_BUSY;
        FileStat st{};
        long error = sys_stat(const_cast<char*>(path), &st);
        if (error < 0)
            return error;
        if (st.type != FILE_TYPE_FILE)
            return SYS_ERROR_INVALID;
        return editor.client.start(EDITOR_WINDOW, path);
    }

    long send(int index, const DisplayEvent& event) {
        if (index == NO_WINDOW || !windows_[index].open())
            return 0;
        auto& window = windows_[index];
        if (is_pixel_window(index) && !surface(index).configured())
            return 0;
        // The client is responding to the user again, so the next close
        // request is a new one.
        if (event.type == DISPLAY_EVENT_KEY || event.type == DISPLAY_EVENT_BUTTON)
            window.close_requested = false;
        Point origin = client_origin(window.rect);
        Point point = is_pixel_window(index) ? Point{pointer_.x - origin.x, pointer_.y - origin.y}
                                             : surface_point(window.rect, pointer_);
        return window.client.send({event, point.x, point.y}) ? 0 : close_window(index);
    }

    long show(int index, unsigned int modifiers) {
        if (index == TERMINAL_WINDOW && !terminal_fits_)
            return 0;
        int previous = top_window();
        if (!windows_[index].open())
            start_client(index);
        return windows_[index].open() ? focus(index, previous, modifiers) : 0;
    }

    long focus(int index, int previous, unsigned int modifiers) {
        if (index == previous)
            return repaint(bounds(windows_[index].rect));
        last_click_ = {};
        left_click_ = false;
        for (int slot = 0; slot < WINDOW_COUNT; ++slot) {
            if (order_[slot] != index)
                continue;
            while (slot > 0) {
                order_[slot] = order_[slot - 1];
                --slot;
            }
            order_[0] = index;
            break;
        }
        long error = send(previous, {DISPLAY_EVENT_RESET, 0, 0, modifiers});
        if (error == 0 && previous != NO_WINDOW && windows_[previous].open())
            error = repaint(bounds(windows_[previous].rect));
        return error < 0 ? error : repaint(bounds(windows_[index].rect));
    }

    // The close button. A client that asks before closing gets one chance to
    // exit by itself.
    long request_close(int index) {
        auto& window = windows_[index];
        if (!WINDOWS[index].asks_before_closing || window.close_requested || !surface(index).configured())
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
        if (is_pixel_window(index)) {
            surface(index).reset();
            window.rect = {};
        }
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
        return repaint(bounds(old));
    }

    long handle(const DisplayTimedEvent& input) {
        const auto& event = input.event;
        if (event.type == DISPLAY_EVENT_KEY) {
            int shortcut = window_for_key(event.code);
            // Application windows open from anywhere. The example windows' keys
            // belong to the focused application while one has the keyboard.
            if (shortcut >= TERMINAL_WINDOW)
                return event.value == DISPLAY_KEY_PRESS ? show(shortcut, event.modifiers) : 0;
            int top = top_window();
            if (top >= TERMINAL_WINDOW)
                return send(top, event);
            if (shortcut != NO_WINDOW)
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
            for (int index = 0; index < WINDOW_COUNT; ++index) {
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
        if (index == NO_WINDOW)
            return send(top_window(), reset);
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
                             clamp(pointer_.y - drag_offset_.y, screen_.height - window.height)};
            long error = repaint(bounds(old_window));
            if (error == 0)
                error = repaint(bounds(window));
            if (error < 0)
                return error;
        } else if (int top = top_window(); top > TERMINAL_WINDOW && in_body(windows_[top].rect, pointer_)) {
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
        switch (WINDOWS[index].placement) {
            case Placement::Center: origin = {(screen_.width - width) / 2, (screen_.height - height) / 2}; break;
            case Placement::Right: origin = {screen_.width - width - RIGHT_MARGIN.x, RIGHT_MARGIN.y}; break;
            case Placement::Corner: break;
        }
        return {clamp(origin.x, screen_.width - width), clamp(origin.y, screen_.height - height)};
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
                long error = chrome_.configure(index, width);
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
                if (index != FILE_WINDOW)
                    return close_window(index);
                int previous = top_window();
                long error = edit_text(pixels.edit_path());
                if (error < 0)
                    return send(index, {INPUT_EDIT_ERROR, 0, static_cast<int>(error), 0});
                return focus(EDITOR_WINDOW, previous, 0);
            }
            case PixelSurface::Result::Closed: return close_window(index);
        }
        return 0;
    }

    long repaint_client(int index) {
        const auto& window = windows_[index];
        if (!is_text_window(index))
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
            STDOUT_FD, "Desktop: F2 Terminal, F4 Files, F12 exit. Drag titles to move, X closes.\n");
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
