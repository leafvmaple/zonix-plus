#include "../command.h"
#include "../desktop/protocol.h"

namespace {

using desktop_ipc::SURFACE_SIZE;

class Demo {
public:
    explicit Demo(bool blue) : blue_(blue) {}

    int run() {
        while (true) {
            draw();
            if (sys_write(STDOUT_FD, frame_.data(), sizeof(frame_)) != sizeof(frame_))
                return 1;
            if (sys_wait_readable(1U << STDIN_FD) < 0)
                return 1;
            desktop_ipc::Input input{};
            long count = sys_read(STDIN_FD, &input, sizeof(input));
            if (count == 0)
                return 0;  // The compositor closed its side, including on a fault.
            if (count != sizeof(input))
                return 1;
            handle(input);
        }
    }

private:
    desktop_ipc::Frame frame_{};
    int x_ = SURFACE_SIZE / 2;
    int y_ = SURFACE_SIZE / 2;
    bool blue_;
    bool alternate_{};
    bool held_{};

    void handle(const desktop_ipc::Input& input) {
        const auto& event = input.event;
        if (event.type == DISPLAY_EVENT_RESET) {
            held_ = false;
        } else if (event.type == DISPLAY_EVENT_BUTTON && event.code == DISPLAY_BUTTON_LEFT) {
            held_ = event.value == DISPLAY_KEY_PRESS;
            if (held_) {
                x_ = input.x;
                y_ = input.y;
            }
        } else if (event.type == DISPLAY_EVENT_KEY && event.code == DISPLAY_KEY_SPACE &&
                   event.value == DISPLAY_KEY_PRESS) {
            alternate_ = !alternate_;
        }
    }

    void draw() {
        unsigned int field = blue_ ? 0x29465C : 0x3D415E;
        unsigned int accent = blue_ ? 0x68CAAE : 0xCD9CE4;
        if (alternate_) {
            unsigned int color = field;
            field = accent;
            accent = color;
        }
        for (int y = 0; y < SURFACE_SIZE; ++y)
            for (int x = 0; x < SURFACE_SIZE; ++x) {
                int dx = x - x_;
                int dy = y - y_;
                unsigned int pixel = field;
                if (dx * dx + dy * dy <= 9)
                    pixel = held_ ? 0xF1F5F9 : accent;
                else if ((x + y) % 11 == 0)
                    pixel = blue_ ? 0x36566B : 0x515571;
                frame_[y * SURFACE_SIZE + x] = pixel;
            }
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc != 2 || (argv[1][0] != 'a' && argv[1][0] != 'b') || argv[1][1] != 0)
        return 2;
    Demo demo(argv[1][0] == 'a');
    return demo.run();
}
