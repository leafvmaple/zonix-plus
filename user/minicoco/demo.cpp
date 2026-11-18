#include "../command.h"
#include "../gui/platform.h"
#include "../gui/window_channel.h"
#include <base/keyboard.h>
#include <platform/software/ZCSoftwareRenderDevice.h>

namespace {

using namespace desktop_ipc;

constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;
constexpr int STRIP_ROWS = 16;
constexpr int HALF_SIDE = 28;
constexpr int ROTATED_RADIUS = 40;
constexpr unsigned int KEY_C = 46;

using zonix_gui::WindowChannel;

class Output : public zocos::PixelBufferPresenter {
public:
    virtual long read_input(Input& input) = 0;
    long error() const { return error_; }
    void set_damage(PixelRect damage) { damage_ = damage; }

protected:
    PixelRect damage_{0, 0, WIDTH, HEIGHT};
    long error_{};
};

class Presenter final : public Output {
public:
    Presenter(int fd, const DisplayInfo& mode)
        : fd_(fd), mode_(mode), left_((mode.width - WIDTH) / 2), top_((mode.height - HEIGHT) / 2) {}

    long clear_screen() {
        for (auto& pixel : scratch_)
            pixel = 0x101923;
        for (unsigned int y = 0; y < mode_.height;) {
            unsigned int height = mstd::min(static_cast<unsigned int>(STRIP_ROWS), mode_.height - y);
            for (unsigned int x = 0; x < mode_.width;) {
                unsigned int width = mstd::min(static_cast<unsigned int>(WIDTH), mode_.width - x);
                long error = upload(x, y, width, height);
                if (error < 0)
                    return error;
                x += width;
            }
            y += height;
        }
        return 0;
    }

    void present(const zocos::PixelBuffer& buffer) override {
        for (int y = 0; y < buffer.height && error_ == 0;) {
            int rows = mstd::min(STRIP_ROWS, buffer.height - y);
            for (int row = 0; row < rows; ++row) {
                const auto* rgba = buffer.pixels + (y + row) * buffer.rowPitchBytes;
                for (int x = 0; x < buffer.width; ++x, rgba += 4) {
                    scratch_[row * buffer.width + x] = zonix_gui::xrgb(rgba);
                }
            }
            error_ = upload(left_, top_ + y, buffer.width, rows);
            y += rows;
        }
    }

    long read_input(Input& input) override {
        long error = sys_ioctl(fd_, DISPLAY_READ_EVENT, reinterpret_cast<long>(&input.event));
        return error < 0 ? error : 1;
    }

private:
    inline static sys::array<unsigned int, WIDTH * STRIP_ROWS> scratch_{};
    int fd_;
    DisplayInfo mode_;
    unsigned int left_;
    unsigned int top_;

    long upload(unsigned int x, unsigned int y, unsigned int width, unsigned int height) const {
        DisplayPresent area{x, y, width, height, width * 4, 0, reinterpret_cast<long>(scratch_.data())};
        return sys_ioctl(fd_, DISPLAY_PRESENT, reinterpret_cast<long>(&area));
    }
};

class WindowOutput final : public Output {
public:
    long configure() { return WindowChannel::open(WIDTH, HEIGHT); }
    long read_input(Input& input) override { return WindowChannel::read(input); }
    void present(const zocos::PixelBuffer& buffer) override { error_ = channel_.present(buffer, damage_); }

private:
    WindowChannel channel_;
};

class Demo {
public:
    static long run(Output& output, bool windowed) {
        zocos::SoftwareRenderDevice device(&output);
        if (!device.setOutputBuffer({rgba_.data(), WIDTH, HEIGHT, WIDTH * 4, rgba_.size()}))
            return SYS_ERROR_INVALID;
        device.setClearColor({26, 44, 59, 255});
        constexpr unsigned char texture[] = {
            255, 255, 255, 255, 145, 145, 145, 255, 145, 145, 145, 255, 255, 255, 255, 255,
        };
        zocos::RenderCommand quad;
        quad.texture = device.createTexture({2, 2, zocos::TextureFormat::RGBA8Unorm, {texture}});
        if (!quad.texture.isValid())
            return SYS_ERROR_IO;
        quad.vertices = {{{-HALF_SIDE, -HALF_SIDE}, {0, 1}, {}}, {{HALF_SIDE, -HALF_SIDE}, {1, 1}, {}},
                         {{HALF_SIDE, HALF_SIDE}, {1, 0}, {}},   {{-HALF_SIDE, -HALF_SIDE}, {0, 1}, {}},
                         {{HALF_SIDE, HALF_SIDE}, {1, 0}, {}},   {{-HALF_SIDE, HALF_SIDE}, {0, 0}, {}}};

        Demo scene;
        auto projection = zocos::Mat4::ortho(0.f, WIDTH, HEIGHT, 0.f, -1.f, 1.f);
        long result = 0;
        while (true) {
            scene.draw(device, projection, quad);
            result = output.error();
            if (result < 0)
                break;
            int old_x = scene.x_;
            int old_y = scene.y_;
            bool redraw = false;
            while (!redraw) {
                Input input{};
                result = output.read_input(input);
                if (result <= 0 || is_exit(input.event)) {
                    device.destroyTexture(quad.texture);
                    return result < 0 ? result : 0;
                }
                redraw = scene.on_event(input, windowed);
            }
            output.set_damage(scene.damage(old_x, old_y));
        }
        device.destroyTexture(quad.texture);
        return result;
    }


private:
    inline static sys::array<unsigned char, WIDTH * HEIGHT * 4> rgba_{};
    int x_ = WIDTH / 2;
    int y_ = HEIGHT / 2;
    bool alternate_{};
    bool pressed_{};

    static bool is_exit(const DisplayEvent& event) {
        return event.type == DISPLAY_EVENT_KEY && event.value == DISPLAY_KEY_PRESS &&
               (event.code == DISPLAY_KEY_Q || event.code == DISPLAY_KEY_ESCAPE ||
                (event.code == KEY_C && (event.modifiers & DISPLAY_MOD_CTRL) != 0));
    }

    void move(int dx, int dy) {
        x_ = static_cast<int>(mstd::clamp(static_cast<long>(x_) + dx, 0L, static_cast<long>(WIDTH - 1)));
        y_ = static_cast<int>(mstd::clamp(static_cast<long>(y_) + dy, 0L, static_cast<long>(HEIGHT - 1)));
    }

    PixelRect damage(int old_x, int old_y) const {
        int left = mstd::max(0, mstd::min(x_, old_x) - ROTATED_RADIUS);
        int top = mstd::max(0, mstd::min(y_, old_y) - ROTATED_RADIUS);
        int right = mstd::min(WIDTH, mstd::max(x_, old_x) + ROTATED_RADIUS + 1);
        int bottom = mstd::min(HEIGHT, mstd::max(y_, old_y) + ROTATED_RADIUS + 1);
        return {static_cast<unsigned int>(left), static_cast<unsigned int>(top),
                static_cast<unsigned int>(right - left), static_cast<unsigned int>(bottom - top)};
    }

    bool on_event(const Input& input, bool windowed) {
        const auto& event = input.event;
        if (event.type == DISPLAY_EVENT_RESET) {
            bool changed = pressed_;
            pressed_ = false;
            return changed;
        }
        if (event.type == DISPLAY_EVENT_BUTTON && event.code == DISPLAY_BUTTON_LEFT) {
            pressed_ = event.value == DISPLAY_KEY_PRESS;
            return true;
        }
        if (event.type == DISPLAY_EVENT_REL && (event.code == DISPLAY_REL_X || event.code == DISPLAY_REL_Y)) {
            int old_x = x_;
            int old_y = y_;
            if (windowed) {
                x_ = mstd::clamp(input.x, 0, WIDTH - 1);
                y_ = mstd::clamp(input.y, 0, HEIGHT - 1);
            } else {
                move(event.code == DISPLAY_REL_X ? event.value : 0, event.code == DISPLAY_REL_Y ? event.value : 0);
            }
            return x_ != old_x || y_ != old_y;
        }
        if (event.type != DISPLAY_EVENT_KEY || event.value == DISPLAY_KEY_RELEASE)
            return false;
        if (event.code == DISPLAY_KEY_SPACE) {
            if (event.value != DISPLAY_KEY_PRESS)
                return false;
            alternate_ = !alternate_;
            return true;
        }
        switch (static_cast<keyboard::Key>(keyboard::KeyboardState::character(event.code, event.modifiers))) {
            case keyboard::Key::Up: move(0, -8); return true;
            case keyboard::Key::Down: move(0, 8); return true;
            case keyboard::Key::Left: move(-8, 0); return true;
            case keyboard::Key::Right: move(8, 0); return true;
            default: return false;
        }
    }

    void draw(zocos::SoftwareRenderDevice& device, const zocos::Mat4& projection, zocos::RenderCommand& quad) const {
        // A fixed 45-degree rotation exercises the engine's affine path.
        constexpr float diagonal = 0.70710678f;
        auto transform = zocos::Mat4::translate(static_cast<float>(x_), static_cast<float>(y_));
        transform.m[0] = transform.m[1] = transform.m[5] = diagonal;
        transform.m[4] = -diagonal;
        quad.world = transform;
        zocos::Color4B tint{64, 180, 240, 255};
        if (alternate_)
            tint = {237, 116, 155, 255};
        if (pressed_)
            tint = {250, 191, 88, 255};
        for (auto& vertex : quad.vertices)
            vertex.color = tint;
        device.beginFrame(projection, WIDTH, HEIGHT);
        device.submit(quad);
        device.endFrame();
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], (char*)"--window") == 0) {
        WindowOutput output;
        long error = output.configure();
        if (error == 0)
            error = Demo::run(output, true);
        return error < 0 ? command_error("minicoco", "Desktop IPC", error) : 0;
    }
    if (argc != 1)
        return command_usage("Usage: minicoco\n");
    int fd = sys_open((char*)"/dev/display", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return command_error("minicoco", "/dev/display", fd);
    DisplayInfo mode{};
    long error = sys_ioctl(fd, DISPLAY_GET_INFO, reinterpret_cast<long>(&mode));
    if (error == 0 && (mode.format != DISPLAY_FORMAT_XRGB8888 || mode.width < WIDTH || mode.height < HEIGHT))
        error = SYS_ERROR_NOT_SUPPORTED;
    if (error == 0)
        error = command_text(STDOUT_FD,
                             "mini-cocos: arrows/mouse move, Space changes color, click highlights, q/Esc exits.\n");
    if (error == 0)
        error = sys_ioctl(fd, DISPLAY_ACQUIRE, DISPLAY_ACQUIRE_EVENTS);
    if (error == 0) {
        Presenter view(fd, mode);
        error = view.clear_screen();
        if (error == 0)
            error = Demo::run(view, false);
        long released = sys_ioctl(fd, DISPLAY_RELEASE, 0);
        if (error == 0)
            error = released;
    }
    if (sys_close(fd) < 0 && error == 0)
        error = SYS_ERROR_IO;
    return error < 0 ? command_error("minicoco", "/dev/display", error) : 0;
}
