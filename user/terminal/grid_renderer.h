#pragma once

#include "../desktop/text_frame.h"
#include "../gui/mono_font.h"
#include "../gui/window_channel.h"

class GridRenderer {
public:
    long init();
    long publish(const desktop_ipc::Frame& frame);

private:
    zocos::SoftwareRenderDevice device_;
    zonix_gui::MonoFont font_;
    zocos::RenderCommand glyphs_;
    zonix_gui::WindowChannel channel_;
    sys::vector<unsigned char> pixels_;
    desktop_ipc::Frame previous_{};
};
