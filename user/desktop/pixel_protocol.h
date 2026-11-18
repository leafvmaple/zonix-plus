#pragma once

#include <sys/array.hpp>

namespace desktop_ipc {

inline constexpr unsigned int PIXEL_MAX_WIDTH = 512;
inline constexpr unsigned int PIXEL_MAX_HEIGHT = 384;
inline constexpr unsigned int PIXEL_PACKET_CAPACITY = 960;

enum class PixelRequest : unsigned int { Size = 1, Update, UpdatePart };

struct PixelRect {
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
};

struct PixelHeader {
    PixelRequest request;
    PixelRect rect;
};

// Size carries no payload. Both update requests carry width*height packed
// XRGB8888 words in top-left order, with no stride, pointer or padding.
// UpdatePart continues a batch; Update ends it or carries a standalone update.
struct PixelPacket {
    PixelHeader header;
    sys::array<unsigned int, PIXEL_PACKET_CAPACITY> pixels;
};

static_assert(sizeof(PixelHeader) == 20);
static_assert(sizeof(PixelPacket) == 20 + PIXEL_PACKET_CAPACITY * 4 && sizeof(PixelPacket) <= 4096);

}  // namespace desktop_ipc
