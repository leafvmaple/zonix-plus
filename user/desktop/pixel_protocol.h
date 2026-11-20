#pragma once

#include <process.h>
#include <sys/array.hpp>

namespace desktop_ipc {

// Desktop further limits a surface to what fits on the screen.
inline constexpr unsigned int PIXEL_MAX_WIDTH = 2048;
inline constexpr unsigned int PIXEL_MAX_HEIGHT = 2048;
inline constexpr unsigned int PIXEL_PACKET_CAPACITY = 960;

enum class PixelRequest : unsigned int { Size = 1, Update, UpdatePart, EditText };

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

// Size carries no payload. The first one creates the surface; a later one,
// the answer to INPUT_RESIZE, resizes it. Both update requests carry width*height packed
// XRGB8888 words in top-left order, with no stride, pointer or padding.
// UpdatePart continues a batch; Update ends it or carries a standalone update.
struct PixelPacket {
    PixelHeader header;
    sys::array<unsigned int, PIXEL_PACKET_CAPACITY> pixels;
};

using EditPath = sys::array<char, PROCESS_MAX_PATH>;

// Files asks Desktop to open an absolute path in the text editor.
struct EditTextPacket {
    PixelHeader header;
    EditPath path;
};

static_assert(sizeof(PixelHeader) == 20);
static_assert(sizeof(PixelPacket) == 20 + PIXEL_PACKET_CAPACITY * 4 && sizeof(PixelPacket) <= 4096);
static_assert(sizeof(EditTextPacket) == sizeof(PixelHeader) + PROCESS_MAX_PATH);

}  // namespace desktop_ipc
