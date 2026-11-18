#include "window_channel.h"
#include "platform.h"
#include "../command.h"

namespace zonix_gui {
using namespace desktop_ipc;

long WindowChannel::open(unsigned int width, unsigned int height) {
    if (!width || !height || width > PIXEL_MAX_WIDTH || height > PIXEL_MAX_HEIGHT)
        return SYS_ERROR_INVALID;
    PixelHeader size{PixelRequest::Size, {0, 0, width, height}};
    return command_write_all(STDOUT_FD, &size, sizeof(size));
}

long WindowChannel::edit_text(const EditPath& path) {
    EditTextPacket request{{PixelRequest::EditText, {}}, path};
    return command_write_all(STDOUT_FD, &request, sizeof(request));
}

long WindowChannel::read(Input& input) {
    long ready = sys_wait_readable(1U << STDIN_FD);
    if (ready < 0)
        return ready;
    long count = sys_read(STDIN_FD, &input, sizeof(input));
    if (count == 0)
        return 0;
    return count == sizeof(input) ? 1 : SYS_ERROR_IO;
}

long WindowChannel::present(const zocos::PixelBuffer& buffer, PixelRect dirty) {
    if (!dirty.width || !dirty.height)
        return 0;
    while (dirty.height != 0) {
        unsigned int rows = mstd::min(dirty.height, PIXEL_PACKET_CAPACITY / dirty.width);
        auto request = rows == dirty.height ? PixelRequest::Update : PixelRequest::UpdatePart;
        packet_.header = {request, {dirty.x, dirty.y, dirty.width, rows}};
        for (unsigned int row = 0; row < rows; ++row) {
            const auto* rgba = buffer.pixels + (dirty.y + row) * buffer.rowPitchBytes + dirty.x * 4;
            for (unsigned int col = 0; col < dirty.width; ++col, rgba += 4)
                packet_.pixels[row * dirty.width + col] = xrgb(rgba);
        }
        long error = command_write_all(STDOUT_FD, &packet_, sizeof(PixelHeader) + dirty.width * rows * 4);
        if (error < 0)
            return error;
        dirty.y += rows;
        dirty.height -= rows;
    }
    return 0;
}

}  // namespace zonix_gui
