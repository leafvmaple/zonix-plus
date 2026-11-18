#pragma once

#include "pixel_protocol.h"

namespace desktop_ipc {

class PixelSurface {
public:
    enum class Result { Pending, Configured, Updated, EditText, Closed };

    Result receive(int fd, unsigned int max_width, unsigned int max_height);
    void reset();
    bool configured() const { return width_ != 0; }
    unsigned int width() const { return width_; }
    unsigned int height() const { return height_; }
    PixelRect damage() const { return damage_; }
    const char* edit_path() const { return edit_path_.data(); }
    unsigned int pixel(unsigned int x, unsigned int y) const { return surface_[y * width_ + x]; }

private:
    Result receive_edit_path(int fd);

    sys::array<unsigned int, PIXEL_MAX_WIDTH * PIXEL_MAX_HEIGHT> surface_{};
    sys::array<unsigned int, PIXEL_PACKET_CAPACITY> pending_{};
    PixelHeader header_{};
    EditPath edit_path_{};
    PixelRect batch_damage_{};
    PixelRect damage_{};
    unsigned int width_{};
    unsigned int height_{};
    unsigned int header_bytes_{};
    unsigned int payload_bytes_{};
};

}  // namespace desktop_ipc
