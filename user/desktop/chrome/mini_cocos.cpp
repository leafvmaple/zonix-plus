#include "../../command.h"
#include "../../gui/controls/style.h"
#include "../../gui/platform.h"
#include "../chrome.h"

#include <2d/ZCLabel.h>
#include <base/ZCAutoreleasePool.h>
#include <base/ZCDirector.h>
#include <sys/vector.hpp>

namespace desktop_chrome {

namespace {

using desktop_ipc::WINDOW_COUNT;
using zonix_gui::control_style::Color;
using zonix_gui::control_style::Glyph;
using zonix_gui::control_style::Palette;

constexpr int STRIP_ROWS = WINDOW_COUNT * TITLE_HEIGHT;
constexpr float CLOSE_RADIUS = 5.f;
// Space kept between the title and the close button.
constexpr int LABEL_GAP = 8;
constexpr int HIDDEN = -1;

// All strips share one scene and font atlas, with the first window's strip at
// the top of the buffer. The renderer's clear colour is the title background.
constexpr float strip_bottom(int index) {
    return static_cast<float>(STRIP_ROWS - (index + 1) * TITLE_HEIGHT);
}

// The divider under each title and the close buttons.
class Decorations final : public zocos::Node {
public:
    static Decorations* create(zocos::Director& director, int width) {
        auto* decorations = new (mstd::nothrow) Decorations(director, width);
        if (!decorations)
            return nullptr;
        if (!decorations->init() || !decorations->palette_.init()) {
            delete decorations;
            return nullptr;
        }
        decorations->autorelease();
        decorations->setAnchorPoint({0.f, 0.f});
        return decorations;
    }

    // `left` is the button's left edge in the strip, or HIDDEN.
    void set_close(int index, int left) { close_left_[index] = left; }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        for (int index = 0; index < WINDOW_COUNT; ++index) {
            float bottom = strip_bottom(index);
            palette_.box(renderer, world, {0.f, bottom, width_, 1.f}, Color::TitleDivider);
            if (close_left_[index] == HIDDEN)
                continue;
            zocos::Rect button{static_cast<float>(close_left_[index]),
                               bottom + TITLE_HEIGHT - CLOSE_TOP - CLOSE_SIZE, CLOSE_SIZE, CLOSE_SIZE};
            palette_.round_box(renderer, world, button, CLOSE_RADIUS, Color::Danger);
            palette_.glyph(renderer, world, button, Glyph::Close, Color::Text);
        }
    }

private:
    Decorations(zocos::Director& director, int width) : palette_(director), width_(static_cast<float>(width)) {
        for (auto& left : close_left_)
            left = HIDDEN;
    }

    Palette palette_;
    float width_;
    sys::array<int, WINDOW_COUNT> close_left_{};
};

}  // namespace

struct Chrome::Scene {
    struct Title {
        zocos::Label* label{};
        int width{};
    };

    sys::array<Title, WINDOW_COUNT> titles;
    Decorations* decorations{};
    sys::vector<unsigned char> rgba;
    int width{};
    zocos::Director* director{};

    ~Scene() {
        if (director)
            director->shutdown();
        zonix_gui::finish();
    }

    bool init(int screen_width, const sys::array<int, WINDOW_COUNT>& widths) {
        width = mstd::min(screen_width, static_cast<int>(desktop_ipc::PIXEL_MAX_WIDTH) + 4);
        for (int initial : widths)
            width = mstd::max(width, initial);
        if (width > 32768)
            return false;
        auto stride = static_cast<mstd::size_t>(width) * 4;
        rgba.resize(stride * STRIP_ROWS);
        auto& engine = zocos::Director::getInstance();
        if (!zonix_gui::init_offscreen(engine, {rgba.data(), width, STRIP_ROWS, stride, rgba.size()},
                                       zonix_gui::opaque_color(zonix_gui::theme::TITLE_BACKGROUND)))
            return false;
        director = &engine;

        zocos::AutoreleasePool nodes("desktop chrome");
        auto* scene = zocos::Scene::create();
        decorations = Decorations::create(engine, width);
        if (!scene || !decorations)
            return false;
        scene->addChild(decorations);
        for (int index = 0; index < WINDOW_COUNT; ++index) {
            auto& title = titles[index];
            title.label = zonix_gui::control_style::make_label(engine, "");
            if (!title.label)
                return false;
            scene->addChild(title.label);
            position(index, widths[index]);
        }
        engine.runWithScene(scene);
        return engine.mainLoop();
    }

    void position(int index, int title_width) {
        auto& title = titles[index];
        title.width = title_width;
        const int close = close_left(title_width);
        const int room = close - LABEL_GAP - TITLE_INSET;
        decorations->set_close(index, close >= CLOSE_MARGIN ? close : HIDDEN);
        title.label->setVisible(room >= 24);
        const auto& spec = desktop_ipc::WINDOWS[index];
        title.label->setString(title_width >= 132 ? spec.title : spec.short_title);
        if (room >= 24)
            zonix_gui::control_style::place_label(*title.label, TITLE_INSET, strip_bottom(index) + TITLE_HEIGHT / 2.f,
                                                  0.f, static_cast<float>(room));
    }

    unsigned int pixel(int x, int y) const {
        return zonix_gui::xrgb(rgba.data() + (static_cast<sys::size_t>(y) * width + x) * 4);
    }
};

void Chrome::SceneDeleter::operator()(Scene* scene) const noexcept {
    delete scene;
}

Chrome::Chrome() = default;
Chrome::~Chrome() = default;

long Chrome::init(int screen_width, const sys::array<int, WINDOW_COUNT>& widths) {
    FileStat font{};
    long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
    if (error < 0) {
        command_error("desktop", zonix_gui::UI_FONT_PATH, error);
        return error;
    }
    scene_.reset(new (sys::nothrow) Scene);
    if (!scene_)
        return SYS_ERROR_FULL;
    return scene_->init(screen_width, widths) ? 0 : SYS_ERROR_IO;
}

long Chrome::configure(int index, int width) {
    if (scene_->titles[index].width == width)
        return 0;
    scene_->position(index, width);
    return scene_->director->mainLoop() ? 0 : SYS_ERROR_IO;
}

unsigned int Chrome::title_pixel(int index, int, int x, int y) const {
    return scene_->pixel(x, index * TITLE_HEIGHT + y);
}

}  // namespace desktop_chrome
