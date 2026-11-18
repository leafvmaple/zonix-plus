#include "../command.h"
#include "chrome.h"
#include <kernel/psf.h>
#include <sys/array.hpp>

#ifndef ZONIX_PIXEL_TERMINAL
extern "C" const unsigned char terminal_font[];
#endif

namespace {

using namespace desktop_ipc;
using desktop_chrome::CLOSE_SIZE;
using desktop_chrome::LABEL_COLOR;
using desktop_chrome::TITLE_HEIGHT;

struct Point {
    int x;
    int y;
};

struct Rect {
    Point origin;
    int width;
    int height;

    bool contains(Point point) const {
        return point.x >= origin.x && point.x < static_cast<long>(origin.x) + width && point.y >= origin.y &&
               point.y < static_cast<long>(origin.y) + height;
    }
};

struct Window {
    Rect rect{};
    desktop_ipc::Client client;
    bool open() const { return client.alive(); }
};

class Desktop {
public:
    Desktop(int fd, int width, int height, psf::Font font)
        : fd_(fd), font_(font), screen_{{0, 0}, width, height}, pointer_{width / 2, height / 2} {
        windows_[0].rect = {{width / 8, height / 5}, width / 5 * 2, height / 2};
        windows_[1].rect = {{width / 3, height / 3}, width / 5 * 2, height / 2};
        constexpr int terminal_width = TERMINAL_WIDTH + 4;
        constexpr int terminal_height = TERMINAL_HEIGHT + TITLE_HEIGHT + 2;
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
        if (sys_stat(const_cast<char*>(Client::program(0)), &example) == 0 && example.type == FILE_TYPE_FILE) {
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
            DisplayEvent event{};
            error = sys_ioctl(fd_, DISPLAY_READ_EVENT, reinterpret_cast<long>(&event));
            if (error < 0)
                break;
            if (event.type == DISPLAY_EVENT_KEY && event.value == DISPLAY_KEY_PRESS) {
                bool client_shortcuts = top_window() >= TERMINAL_WINDOW;
                if (event.code == KEY_F12 ||
                    (!client_shortcuts && (event.code == DISPLAY_KEY_ESCAPE || event.code == DISPLAY_KEY_Q)))
                    return 0;
                if (!client_shortcuts && event.code == KEY_C && (event.modifiers & DISPLAY_MOD_CTRL) != 0)
                    sys_exit(PROCESS_STATUS_KILLED);
            }
            error = handle(event);
        }
        return error;
    }


private:
    static constexpr unsigned int KEY_1 = 2;
    static constexpr unsigned int KEY_2 = 3;
    static constexpr unsigned int KEY_C = 46;
    static constexpr unsigned int KEY_F2 = 60;
    static constexpr unsigned int KEY_F4 = 62;
    static constexpr unsigned int KEY_F12 = 88;
    static constexpr int NO_WINDOW = -1;
    static constexpr int CURSOR_SIZE = 11;
    static constexpr int TILE_SIZE = 64;
    static constexpr unsigned int FOCUSED_BORDER = 0x72CFCC;
    static constexpr unsigned int UNFOCUSED_BORDER = 0x536777;
    static constexpr unsigned int TERMINAL_BACKGROUND = 0x111D29;
    inline static sys::array<unsigned int, TILE_SIZE * TILE_SIZE> pixels_{};
    inline static sys::array<PixelSurface, WINDOW_COUNT - FIRST_PIXEL_WINDOW> surfaces_{};

    static PixelSurface& surface(int index) { return surfaces_[index - FIRST_PIXEL_WINDOW]; }

    int fd_;
    psf::Font font_;
    desktop_chrome::Chrome chrome_;
    Rect screen_;
    sys::array<Window, WINDOW_COUNT> windows_;
    Point pointer_;
    Point drag_offset_{};
    sys::array<int, WINDOW_COUNT> order_{{1, 0, TERMINAL_WINDOW, PIXEL_WINDOW, FILE_WINDOW}};
    bool terminal_fits_{};
    int drag_ = NO_WINDOW;
    sys::array<int, 3> button_targets_{{NO_WINDOW, NO_WINDOW, NO_WINDOW}};

    static int clamp(long value, int limit) {
        if (value < 0)
            return 0;
        return value > limit ? limit : static_cast<int>(value);
    }

    Rect cursor_rect() const {
        return {{pointer_.x - CURSOR_SIZE / 2, pointer_.y - CURSOR_SIZE / 2}, CURSOR_SIZE, CURSOR_SIZE};
    }

    static Rect close_rect(const Rect& window) {
        return {
            {window.origin.x + desktop_chrome::close_left(window.width), window.origin.y + desktop_chrome::CLOSE_TOP},
            CLOSE_SIZE,
            CLOSE_SIZE};
    }

    int top_window() const {
        for (int index : order_)
            if (windows_[index].open())
                return index;
        return NO_WINDOW;
    }

    int window_at(Point point) const {
        for (int index : order_)
            if (windows_[index].open() && windows_[index].rect.contains(point))
                return index;
        return NO_WINDOW;
    }

    static bool in_body(const Rect& rect, Point point) {
        return point.x >= rect.origin.x + 2 && point.x < rect.origin.x + rect.width - 2 &&
               point.y >= rect.origin.y + TITLE_HEIGHT && point.y < rect.origin.y + rect.height - 2;
    }

    static Point surface_point(const Rect& rect, Point point) {
        constexpr int size = desktop_ipc::SURFACE_SIZE;
        int width = rect.width - 4;
        int height = rect.height - TITLE_HEIGHT - 2;
        int x = clamp(static_cast<long>(point.x) - rect.origin.x - 2, width - 1);
        int y = clamp(static_cast<long>(point.y) - rect.origin.y - TITLE_HEIGHT, height - 1);
        return {static_cast<int>(static_cast<long>(x) * size / width),
                static_cast<int>(static_cast<long>(y) * size / height)};
    }

    void start_client(int index) {
        long error = windows_[index].client.start(index);
        if (error < 0)
            command_error("desktop", Client::program(index), error);
    }

    long send(int index, const DisplayEvent& event) {
        if (index == NO_WINDOW || !windows_[index].open())
            return 0;
        auto& window = windows_[index];
        if (is_pixel_window(index) && !surface(index).configured())
            return 0;
        Point point = is_pixel_window(index) ? Point{pointer_.x - window.rect.origin.x - 2,
                                                     pointer_.y - window.rect.origin.y - TITLE_HEIGHT}
                                             : surface_point(window.rect, pointer_);
        return window.client.send({event, point.x, point.y}) ? 0 : close_window(index);
    }

    long show(int index, unsigned int modifiers) {
        if (index == TERMINAL_WINDOW && !terminal_fits_)
            return 0;
        if (!windows_[index].open())
            start_client(index);
        return windows_[index].open() ? focus(index, modifiers) : 0;
    }

    long focus(int index, unsigned int modifiers) {
        int back = top_window();
        if (index == back)
            return repaint(windows_[index].rect);
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
        long error = send(back, {DISPLAY_EVENT_RESET, 0, 0, modifiers});
        if (error == 0 && back != NO_WINDOW && windows_[back].open())
            error = repaint(windows_[back].rect);
        return error < 0 ? error : repaint(windows_[index].rect);
    }

    long close_window(int index) {
        auto& window = windows_[index];
        bool focused = index == top_window();
        Rect old = window.rect;
        window.client.stop();
        if (is_pixel_window(index)) {
            surface(index).reset();
            window.rect = {};
        }
        if (drag_ == index)
            drag_ = NO_WINDOW;
        for (auto& target : button_targets_) {
            if (target == index)
                target = NO_WINDOW;
        }
        int next = top_window();
        if (focused && next != NO_WINDOW) {
            long error = repaint(windows_[next].rect);
            if (error < 0)
                return error;
        }
        return repaint(old);
    }

    long handle(const DisplayEvent& event) {
        if (event.type == DISPLAY_EVENT_KEY) {
            if (event.code >= KEY_F2 && event.code <= KEY_F4) {
                int index = TERMINAL_WINDOW + static_cast<int>(event.code - KEY_F2);
                return event.value == DISPLAY_KEY_PRESS ? show(index, event.modifiers) : 0;
            }
            int top = top_window();
            if (top >= TERMINAL_WINDOW)
                return send(top, event);
            if (event.code == KEY_1 || event.code == KEY_2) {
                if (event.value == DISPLAY_KEY_PRESS)
                    return show(event.code == KEY_1 ? 0 : 1, event.modifiers);
                return 0;
            }
            if (event.code == DISPLAY_KEY_ESCAPE || event.code == DISPLAY_KEY_Q)
                return 0;
            return send(top, event);
        }
        if (event.type == DISPLAY_EVENT_RESET) {
            drag_ = NO_WINDOW;
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
            return button(event);
        return 0;
    }

    long button(const DisplayEvent& event) {
        if (event.code < DISPLAY_BUTTON_LEFT || event.code > DISPLAY_BUTTON_MIDDLE)
            return 0;
        bool left = event.code == DISPLAY_BUTTON_LEFT;
        int& target = button_targets_[event.code - DISPLAY_BUTTON_LEFT];
        if (event.value == DISPLAY_KEY_RELEASE) {
            if (left)
                drag_ = NO_WINDOW;
            int index = target;
            target = NO_WINDOW;
            return send(index, event);
        }
        int index = window_at(pointer_);
        if (index == NO_WINDOW)
            return 0;
        const auto& rect = windows_[index].rect;
        if (left && close_rect(rect).contains(pointer_))
            return close_window(index);
        long error = focus(index, event.modifiers);
        if (error < 0)
            return error;
        if (left && pointer_.y - rect.origin.y < TITLE_HEIGHT) {
            drag_ = index;
            drag_offset_ = {pointer_.x - rect.origin.x, pointer_.y - rect.origin.y};
        } else if (in_body(rect, pointer_)) {
            target = index;
            return send(index, event);
        }
        return 0;
    }

    long move(const DisplayEvent& event) {
        Rect old_cursor = cursor_rect();
        if (event.code == DISPLAY_REL_X)
            pointer_.x = clamp(static_cast<long>(pointer_.x) + event.value, screen_.width - 1);
        else
            pointer_.y = clamp(static_cast<long>(pointer_.y) + event.value, screen_.height - 1);
        if (drag_ != NO_WINDOW) {
            Rect& window = windows_[drag_].rect;
            Rect old_window = window;
            window.origin = {clamp(pointer_.x - drag_offset_.x, screen_.width - window.width),
                             clamp(pointer_.y - drag_offset_.y, screen_.height - window.height)};
            long error = repaint(old_window);
            if (error == 0)
                error = repaint(window);
            if (error < 0)
                return error;
        } else if (int top = top_window(); top > TERMINAL_WINDOW && in_body(windows_[top].rect, pointer_)) {
            long error = send(top, event);
            if (error < 0)
                return error;
        }
        long error = repaint(old_cursor);
        return error < 0 ? error : repaint(cursor_rect());
    }

    unsigned int color(Point point) const {
        Rect cursor = cursor_rect();
        if (cursor.contains(point) && (point.x == pointer_.x || point.y == pointer_.y))
            return LABEL_COLOR;
        int index = window_at(point);
        if (index == NO_WINDOW)
            return chrome_.background();
        Rect window = windows_[index].rect;
        int x = point.x - window.origin.x;
        int y = point.y - window.origin.y;
        if (x < 2 || y < 2 || x >= window.width - 2 || y >= window.height - 2)
            return index == top_window() ? FOCUSED_BORDER : UNFOCUSED_BORDER;
        if (y >= TITLE_HEIGHT) {
            if (is_text_window(index))
                return text_color(index, x - 2, y - TITLE_HEIGHT);
            if (is_pixel_window(index))
                return surface(index).pixel(x - 2, y - TITLE_HEIGHT);
            Point sample = surface_point(window, point);
            return windows_[index].client.frame()[sample.y * desktop_ipc::SURFACE_SIZE + sample.x];
        }
        return chrome_.title_pixel(index, window.width, x, y);
    }

    long receive_pixels(int index) {
        auto& window = windows_[index];
        auto& pixels = surface(index);
        switch (window.client.receive_pixels(pixels, screen_.width - 4, screen_.height - TITLE_HEIGHT - 2)) {
            case PixelSurface::Result::Pending: return 0;
            case PixelSurface::Result::Configured: {
                int width = static_cast<int>(pixels.width()) + 4;
                int height = static_cast<int>(pixels.height()) + TITLE_HEIGHT + 2;
                int x = 16;
                int y = 16;
                if (index == TERMINAL_WINDOW) {
                    x = (screen_.width - width) / 2;
                    y = (screen_.height - height) / 2;
                } else if (index == FILE_WINDOW) {
                    x = screen_.width - width - 24;
                    y = 56;
                }
                window.rect = {{clamp(x, screen_.width - width), clamp(y, screen_.height - height)}, width, height};
                long error = chrome_.configure(index, width);
                return error < 0 ? error : repaint(window.rect);
            }
            case PixelSurface::Result::Updated: {
                auto dirty = pixels.damage();
                return repaint({{window.rect.origin.x + 2 + static_cast<int>(dirty.x),
                                 window.rect.origin.y + TITLE_HEIGHT + static_cast<int>(dirty.y)},
                                static_cast<int>(dirty.width),
                                static_cast<int>(dirty.height)});
            }
            case PixelSurface::Result::Closed: return close_window(index);
        }
        return 0;
    }

    unsigned int text_color(int index, int x, int y) const {
        const auto* data = reinterpret_cast<const unsigned char*>(windows_[index].client.frame().data());
        int row = y / GLYPH_HEIGHT;
        int col = x / GLYPH_WIDTH;
        if (data[CURSOR_VISIBLE] && row == data[CURSOR_ROW] && col == data[CURSOR_COL] &&
            y % GLYPH_HEIGHT >= GLYPH_HEIGHT - 2)
            return FOCUSED_BORDER;
        auto* glyph = psf::glyph(&font_, data[row * TERMINAL_COLS + col]);
        return (glyph[y % GLYPH_HEIGHT] & (0x80U >> (x % GLYPH_WIDTH))) != 0 ? LABEL_COLOR : TERMINAL_BACKGROUND;
    }

    long repaint_client(int index) {
        const auto& window = windows_[index];
        if (!is_text_window(index))
            return repaint(window.rect);
        const auto& dirty = window.client.damage();
        if (dirty.empty())
            return 0;
        return repaint({{window.rect.origin.x + 2 + dirty.left * GLYPH_WIDTH,
                         window.rect.origin.y + TITLE_HEIGHT + dirty.top * GLYPH_HEIGHT},
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
                        pixels_[row * width + col] = color({x + col, y + row});
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
