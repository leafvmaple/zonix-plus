#pragma once

#include "../desktop/pixel_protocol.h"
#include "../desktop/protocol.h"
#include <platform/software/ZCSoftwareRenderDevice.h>

namespace zonix_gui {

class WindowChannel {
public:
    static long open(unsigned int width, unsigned int height);
    static long edit_text(const desktop_ipc::EditPath& path);
    static long read(desktop_ipc::Input& input);
    long present(const zocos::PixelBuffer& buffer, desktop_ipc::PixelRect dirty);

private:
    desktop_ipc::PixelPacket packet_;
};

}  // namespace zonix_gui
