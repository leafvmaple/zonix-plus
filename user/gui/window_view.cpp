#include "platform.h"
#include "window_channel.h"
#include "../command.h"
#include <base/ZCDirector.h>
#include <base/ZCPlatformFactory.h>
#include <base/ZCView.h>

namespace {

using desktop_ipc::Input;
using desktop_ipc::PixelRect;
using zonix_gui::WindowChannel;

class SoftwareView : public zocos::View, public zocos::PixelBufferPresenter {
public:
    explicit SoftwareView(zocos::Color4B background) : background_(background) {}

    bool init(int width, int height, const char*) override {
        if (width <= 0 || height <= 0)
            return false;
        width_ = width;
        height_ = height;
        return true;
    }
    void shutdown() override { closed_ = true; }
    bool shouldClose() const override { return closed_; }
    void pollEvents() override {}
    void swapBuffers() override {}
    void present(const zocos::PixelBuffer&) override {}
    int getFramebufferWidth() const override { return width_; }
    int getFramebufferHeight() const override { return height_; }
    bool getMousePosition(float& x, float& y) const override {
        x = mouse_x_;
        y = mouse_y_;
        return has_mouse_;
    }
    // Both backends draw on demand. There is no userspace animation clock yet.
    double getTimeSeconds() const override { return 0.0; }
    void setDelegate(zocos::ViewDelegate* delegate) override { delegate_ = delegate; }

    mstd::unique_ptr<zocos::RenderDevice> create_device() {
        auto device = mstd::make_unique<zocos::SoftwareRenderDevice>(this);
        device->setClearColor(background_);
        return device;
    }

protected:
    zocos::ViewDelegate* delegate_{};
    int width_{};
    int height_{};
    float mouse_x_{};
    float mouse_y_{};
    bool has_mouse_{};
    bool closed_{};

private:
    zocos::Color4B background_;
};

class WindowView final : public SoftwareView {
public:
    WindowView() : SoftwareView({18, 29, 43, 255}) {}

    bool init(int width, int height, const char* title) override {
        if (!SoftwareView::init(width, height, title) || WindowChannel::open(width, height) < 0)
            return false;
        pixels_.resize(static_cast<sys::size_t>(width) * height);
        return true;
    }

    void pollEvents() override {
        if (!painted_)
            return;
        Input input{};
        long status = WindowChannel::read(input);
        if (status <= 0) {
            closed_ = true;
            return;
        }
        if (!delegate_)
            return;
        const auto& event = input.event;
        if (event.type == DISPLAY_EVENT_RESET) {
            // Cancel an active touch and let clients dismiss pointer-driven overlays.
            delegate_->onViewMouseButtonEvent(0, event.modifiers, false, zonix_gui::POINTER_RESET_COORDINATE,
                                              zonix_gui::POINTER_RESET_COORDINATE);
            return;
        }
        if (event.type == DISPLAY_EVENT_KEY) {
            delegate_->onViewKeyEvent(event.code, event.code, event.modifiers, event.value != DISPLAY_KEY_RELEASE,
                                      event.value == DISPLAY_KEY_REPEAT);
            return;
        }
        float x = static_cast<float>(input.x);
        float y = static_cast<float>(height_ - 1 - input.y);
        if (event.type == DISPLAY_EVENT_BUTTON && event.code >= DISPLAY_BUTTON_LEFT &&
            event.code <= DISPLAY_BUTTON_MIDDLE) {
            int button = static_cast<int>(event.code - DISPLAY_BUTTON_LEFT);
            bool down = event.value != DISPLAY_KEY_RELEASE;
            delegate_->onViewMouseButtonEvent(button, event.modifiers, down, x, y);
        } else if (event.type == DISPLAY_EVENT_REL && (event.code == DISPLAY_REL_X || event.code == DISPLAY_REL_Y)) {
            delegate_->onViewMouseMoveEvent(x, y, has_mouse_ ? x - mouse_x_ : 0.f, has_mouse_ ? y - mouse_y_ : 0.f);
        } else {
            return;
        }
        mouse_x_ = x;
        mouse_y_ = y;
        has_mouse_ = true;
    }

    void present(const zocos::PixelBuffer& buffer) override {
        if (closed_)
            return;
        PixelRect dirty = changed(buffer);
        if (!painted_)
            dirty = {0, 0, static_cast<unsigned int>(width_), static_cast<unsigned int>(height_)};
        if (channel_.present(buffer, dirty) < 0)
            closed_ = true;
        painted_ = true;
    }

private:
    WindowChannel channel_;
    sys::vector<unsigned int> pixels_;
    bool painted_{};

    PixelRect changed(const zocos::PixelBuffer& buffer) {
        int left = width_;
        int top = height_;
        int right = 0;
        int bottom = 0;
        for (int y = 0; y < height_; ++y) {
            const auto* src = buffer.pixels + y * buffer.rowPitchBytes;
            auto* previous = pixels_.data() + y * width_;
            for (int x = 0; x < width_; ++x, src += 4) {
                auto color = zonix_gui::xrgb(src);
                if (color != previous[x]) {
                    previous[x] = color;
                    left = mstd::min(left, x);
                    top = mstd::min(top, y);
                    right = mstd::max(right, x + 1);
                    bottom = y + 1;
                }
            }
        }
        if (right <= left)
            return {};
        return {static_cast<unsigned int>(left), static_cast<unsigned int>(top),
                static_cast<unsigned int>(right - left), static_cast<unsigned int>(bottom - top)};
    }
};

}  // namespace

namespace zocos {

mstd::unique_ptr<View> createDefaultView() {
    return mstd::make_unique<WindowView>();
}

mstd::unique_ptr<RenderDevice> createDefaultRenderDevice(View& view) {
    return static_cast<SoftwareView&>(view).create_device();
}

}  // namespace zocos


namespace zonix_gui {

bool is_double_click(unsigned int modifiers) {
    return (modifiers & desktop_ipc::INPUT_MOD_DOUBLE_CLICK) != 0;
}

bool init_offscreen(zocos::Director& director, const zocos::PixelBuffer& buffer, zocos::Color4B background) {
    if (!director.init(buffer.width, buffer.height, "", mstd::make_unique<SoftwareView>(background)))
        return false;
    auto* device = static_cast<zocos::SoftwareRenderDevice*>(director.getRenderDevice());
    if (device->setOutputBuffer(buffer))
        return true;
    director.shutdown();
    return false;
}

}  // namespace zonix_gui
