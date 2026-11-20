#pragma once

#include "pixel_surface.h"
#include "text_frame.h"

namespace desktop_ipc {

// How a client exchanges input and output with Desktop.
enum class Protocol {
    Frame,   // One small frame per input, scaled to the window.
    Text,    // A character grid, sent whenever it changes.
    Pixels,  // A surface of the client's own size, updated in rectangles.
};

// The programs Desktop can run in a window.
inline constexpr int NO_APP = -1;
inline constexpr int APP_EXAMPLE_A = 0;
inline constexpr int APP_EXAMPLE_B = 1;
inline constexpr int APP_TERMINAL = 2;
inline constexpr int APP_COCOS = 3;
inline constexpr int APP_FILES = 4;
inline constexpr int APP_EDITOR = 5;
inline constexpr int APP_SETTINGS = 6;
inline constexpr int APP_COUNT = 7;

// Windows are slots shared by all programs. Every slot keeps a full-size
// surface, because a process cannot ask the kernel for memory after it starts.
// The count follows from the kernel's 24 descriptors per process: Desktop
// holds four of its own and two pipes per window, and starting a client needs
// four more for a moment.
inline constexpr int MAX_WINDOWS = 8;

#ifdef ZONIX_PIXEL_TERMINAL
inline constexpr Protocol TERMINAL_PROTOCOL = Protocol::Pixels;
#else
inline constexpr Protocol TERMINAL_PROTOCOL = Protocol::Text;
#endif

// Scancodes of the keys that open windows.
inline constexpr unsigned int KEY_1 = 2;
inline constexpr unsigned int KEY_2 = 3;
inline constexpr unsigned int KEY_F2 = 60;
inline constexpr unsigned int KEY_F3 = 61;
inline constexpr unsigned int KEY_F4 = 62;

// Where a pixel window appears once its client reports a size.
enum class Placement { Corner, Center, Right };

// Everything that distinguishes one program's windows from another's.
struct AppSpec {
    const char* title;
    const char* short_title;  // For a title bar too narrow for the title.
    const char* program;
    const char* argument;  // Passed to the program, or null.
    Placement placement;
    unsigned int key;  // Focuses the program's front window or opens one, or 0.
    Protocol protocol;
    // More than one window may be open. Shift with the key opens another.
    bool multiple = false;
    // The client also receives pointer movement over its window.
    bool pointer_motion = false;
    // The client is asked to close and exits by itself, so it can keep
    // unsaved work. A second request closes the window regardless.
    bool asks_before_closing = false;
};

inline constexpr AppSpec APPS[APP_COUNT] = {
    {"Example A", "A", "/opt/bin/deskapp", "a", Placement::Corner, KEY_1, Protocol::Frame},
    {"Example B", "B", "/opt/bin/deskapp", "b", Placement::Corner, KEY_2, Protocol::Frame},
    {"Terminal", "Term", "/opt/bin/terminal", nullptr, Placement::Center, KEY_F2, TERMINAL_PROTOCOL, true},
    {"mini-cocos", "Cocos", "/opt/bin/minicoco", "--window", Placement::Corner, KEY_F3, Protocol::Pixels, false, true},
    {"Files", "Files", "/opt/bin/files", "--window", Placement::Right, KEY_F4, Protocol::Pixels, true, true},
    {"Text Editor", "Editor", "/opt/bin/editor", nullptr, Placement::Center, 0, Protocol::Pixels, true, true, true},
    {"Settings", "Set", "/opt/bin/settings", "--window", Placement::Center, 0, Protocol::Pixels, false, true},
};

class Client {
public:
    // Clients live in static storage, so they have no destructor. Desktop
    // stops the ones still running when it exits.
    Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // `path` follows the program's own argument when it is not null: the file
    // an editor opens or the folder Files shows.
    long start(const AppSpec& spec, const char* path = nullptr);
    void stop();
    bool alive() const { return pid_ > 0; }
    unsigned int mask() const;
    const Frame& frame() const { return frame_; }
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
    Protocol protocol_ = Protocol::Frame;
    // Used by frame and text clients. Pixel clients draw into a surface.
    Frame frame_{};
    Frame pending_{};
    TextDamage damage_;
    unsigned int used_{};
    bool waiting_{};
    sys::array<Input, QUEUE_SIZE> queue_{};
    unsigned int head_{};
    unsigned int count_{};
};

}  // namespace desktop_ipc
