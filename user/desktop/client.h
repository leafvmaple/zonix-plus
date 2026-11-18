#pragma once

#include "pixel_surface.h"
#include "text_frame.h"

namespace desktop_ipc {

inline constexpr int TERMINAL_WINDOW = 2;
inline constexpr int PIXEL_WINDOW = 3;
inline constexpr int FILE_WINDOW = 4;
inline constexpr int WINDOW_COUNT = 5;

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

class Client {
public:
    Client() = default;
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    static const char* program(int index);
    long start(int index);
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
