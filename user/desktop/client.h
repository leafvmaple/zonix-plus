#pragma once

#include "pixel_surface.h"
#include "text_frame.h"

namespace desktop_ipc {

inline constexpr int TERMINAL_WINDOW = 2;
inline constexpr int PIXEL_WINDOW = 3;
inline constexpr int FILE_WINDOW = 4;
inline constexpr int EDITOR_WINDOW = 5;
inline constexpr int WINDOW_COUNT = 6;

#ifdef ZONIX_PIXEL_TERMINAL
inline constexpr int FIRST_PIXEL_WINDOW = TERMINAL_WINDOW;
#else
inline constexpr int FIRST_PIXEL_WINDOW = PIXEL_WINDOW;
#endif

inline constexpr bool is_pixel_window(int index) {
    return index >= FIRST_PIXEL_WINDOW;
}

inline constexpr bool is_text_window(int index) {
    return index == TERMINAL_WINDOW && !is_pixel_window(index);
}

// Scancodes of the keys that open windows.
inline constexpr unsigned int KEY_1 = 2;
inline constexpr unsigned int KEY_2 = 3;
inline constexpr unsigned int KEY_F2 = 60;
inline constexpr unsigned int KEY_F3 = 61;
inline constexpr unsigned int KEY_F4 = 62;

// Where a pixel window appears once its client reports a size.
enum class Placement { Corner, Center, Right };

// Everything that distinguishes one window slot from another.
struct WindowSpec {
    const char* title;
    const char* short_title;  // For a title bar too narrow for the title.
    const char* program;
    const char* argument;  // Passed to the program, or null. The editor passes the file instead.
    Placement placement;
    unsigned int key;  // Opens or focuses the window, or 0.
    // The client is asked to close and exits by itself, so it can keep
    // unsaved work. A second request closes the window regardless.
    bool asks_before_closing = false;
};

inline constexpr WindowSpec WINDOWS[WINDOW_COUNT] = {
    {"Example A", "A", "/opt/bin/deskapp", "a", Placement::Corner, KEY_1},
    {"Example B", "B", "/opt/bin/deskapp", "b", Placement::Corner, KEY_2},
    {"Terminal", "Term", "/opt/bin/terminal", nullptr, Placement::Center, KEY_F2},
    {"mini-cocos", "Cocos", "/opt/bin/minicoco", "--window", Placement::Corner, KEY_F3},
    {"Files", "Files", "/opt/bin/files", "--window", Placement::Right, KEY_F4},
    {"Text Editor", "Editor", "/opt/bin/editor", nullptr, Placement::Center, 0, true},
};

class Client {
public:
    Client() = default;
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    long start(int index, const char* edit_path = nullptr);
    void stop();
    bool alive() const { return pid_ > 0; }
    unsigned int mask() const;
    const Frame& frame() const { return buffers().frame; }
    const TextDamage& damage() const { return damage_; }
    bool receive_frame();
    PixelSurface::Result receive_pixels(PixelSurface& surface, unsigned int width, unsigned int height) {
        return surface.receive(output_fd_, width, height);
    }
    bool send(const Input& input);

private:
    static constexpr unsigned int QUEUE_SIZE = 16;
    bool flush();

    int pid_{};
    int input_fd_ = -1;
    int output_fd_ = -1;
    struct Buffers {
        Frame frame;
        Frame pending;
    };
    // Pixel clients use separate surfaces.
    inline static sys::array<Buffers, FIRST_PIXEL_WINDOW> buffers_{};
    Buffers& buffers() const { return buffers_[index_]; }
    int index_{};
    TextDamage damage_;
    unsigned int used_{};
    bool waiting_{};
    sys::array<Input, QUEUE_SIZE> queue_{};
    unsigned int head_{};
    unsigned int count_{};
};

}  // namespace desktop_ipc
