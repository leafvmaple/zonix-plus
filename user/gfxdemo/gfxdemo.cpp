#include "../command.h"
#include <sys/array.hpp>

class Demo {
public:
    static int run(int argc) {
        if (argc != 1)
            return command_usage("Usage: gfxdemo (mouse: move, Space: redraw, q/Esc: exit)\n");
        int fd = sys_open((char*)"/dev/display", O_RDWR | O_CLOEXEC);
        if (fd < 0)
            return command_error("gfxdemo", "/dev/display", fd);
        DisplayInfo mode{};
        long error = sys_ioctl(fd, DISPLAY_GET_INFO, reinterpret_cast<long>(&mode));
        if (error == 0 && mode.format != DISPLAY_FORMAT_XRGB8888)
            error = SYS_ERROR_NOT_SUPPORTED;
        if (error == 0 && printf("gfxdemo: %dx%d, mouse moves, Shift highlights, Space redraws, q/Esc exits\n",
                                 static_cast<int>(mode.width), static_cast<int>(mode.height)) < 0)
            error = SYS_ERROR_IO;
        bool acquired = false;
        if (error == 0) {
            error = sys_ioctl(fd, DISPLAY_ACQUIRE, DISPLAY_ACQUIRE_EVENTS);
            acquired = error == 0;
        }
        if (error == 0)
            error = interact(fd, mode);
        if (acquired && sys_ioctl(fd, DISPLAY_RELEASE, 0) < 0)
            error = SYS_ERROR_IO;
        if (sys_close(fd) < 0)
            error = SYS_ERROR_IO;
        return error < 0 ? command_error("gfxdemo", "/dev/display", error) : 0;
    }

private:
    struct Pointer {
        unsigned int x;
        unsigned int y;
        unsigned int buttons{};
        bool visible{};
    };

    static constexpr unsigned int POINTER_SIZE = 12;
    static constexpr unsigned int TILE_SIZE = 64;
    inline static sys::array<unsigned int, TILE_SIZE * TILE_SIZE> tile_{};

    static unsigned int pixel(const DisplayInfo& mode, unsigned int x, unsigned int y, unsigned int phase) {
        if (x < 3 || y < 3 || x >= mode.width - 3 || y >= mode.height - 3)
            return 0xFFFFFF;
        if (y >= mode.height - mode.height / 4)
            return ((x / 32 + y / 32 + phase) % 2) ? 0x333333 : 0xCCCCCC;
        unsigned int col = phase ? mode.width - x - 1 : x;
        return col < mode.width / 3 ? 0xFF0000 : (col < mode.width * 2 / 3 ? 0x00FF00 : 0x0000FF);
    }
    static long paint(int fd, const DisplayInfo& mode, unsigned int x, unsigned int y, unsigned int size,
                      unsigned int phase, unsigned int color = 0) {
        unsigned int width = mode.width - x < size ? mode.width - x : size;
        unsigned int height = mode.height - y < size ? mode.height - y : size;
        for (unsigned int row = 0; row < height; ++row)
            for (unsigned int col = 0; col < width; ++col)
                tile_[row * width + col] = color != 0 ? color : pixel(mode, x + col, y + row, phase);
        DisplayPresent rectangle{x, y, width, height, width * 4, 0, reinterpret_cast<long>(tile_.data())};
        return sys_ioctl(fd, DISPLAY_PRESENT, reinterpret_cast<long>(&rectangle));
    }

    static long draw(int fd, const DisplayInfo& mode, unsigned int phase) {
        for (unsigned int y = 0; y < mode.height; y += TILE_SIZE)
            for (unsigned int x = 0; x < mode.width; x += TILE_SIZE) {
                long error = paint(fd, mode, x, y, TILE_SIZE, phase);
                if (error < 0)
                    return error;
            }
        return 0;
    }

    static unsigned int move(unsigned int position, int delta, unsigned int limit) {
        long next = static_cast<long>(position) + delta;
        if (next < 0)
            return 0;
        return next >= limit ? limit - 1 : static_cast<unsigned int>(next);
    }

    static long interact(int fd, const DisplayInfo& mode) {
        Pointer pointer{mode.width / 2, mode.height / 2};
        unsigned int phase = 0;
        long error = draw(fd, mode, phase);
        while (error == 0) {
            DisplayEvent event{};
            error = sys_ioctl(fd, DISPLAY_READ_EVENT, reinterpret_cast<long>(&event));
            if (error < 0)
                break;
            bool pressed = event.type == DISPLAY_EVENT_KEY && event.value == DISPLAY_KEY_PRESS;
            if (pressed && (event.code == DISPLAY_KEY_Q || event.code == DISPLAY_KEY_ESCAPE))
                return 0;
            if ((pressed && event.code == DISPLAY_KEY_SPACE) ||
                (event.type == DISPLAY_EVENT_REL && event.code == DISPLAY_REL_WHEEL)) {
                error = draw(fd, mode, phase ^= 1);
            } else if (event.type == DISPLAY_EVENT_REL) {
                if (pointer.visible)
                    error = paint(fd, mode, pointer.x, pointer.y, POINTER_SIZE, phase);
                if (event.code == DISPLAY_REL_X)
                    pointer.x = move(pointer.x, event.value, mode.width);
                else if (event.code == DISPLAY_REL_Y)
                    pointer.y = move(pointer.y, event.value, mode.height);
                pointer.visible = true;
            } else if (event.type == DISPLAY_EVENT_BUTTON) {
                unsigned int button = 1U << (event.code - DISPLAY_BUTTON_LEFT);
                if (event.value == DISPLAY_KEY_PRESS)
                    pointer.buttons |= button;
                else
                    pointer.buttons &= ~button;
                pointer.visible = true;
            } else if (event.type == DISPLAY_EVENT_RESET) {
                pointer.buttons = 0;
            }
            if (error == 0 && pointer.visible) {
                unsigned int color = pointer.buttons != 0                         ? 0xFF00FF
                                     : (event.modifiers & DISPLAY_MOD_SHIFT) != 0 ? 0xFFFF00
                                                                                  : 0xFFFFFF;
                error = paint(fd, mode, pointer.x, pointer.y, POINTER_SIZE, phase, color);
            }
        }
        return error;
    }
};

extern "C" int main(int argc, char**) {
    return Demo::run(argc);
}
