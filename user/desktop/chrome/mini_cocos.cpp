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

using desktop_ipc::MAX_WINDOWS;
using zonix_gui::control_style::Color;
using zonix_gui::control_style::Palette;

constexpr int STRIP_ROWS = MAX_WINDOWS * TITLE_HEIGHT;
constexpr float CLOSE_RADIUS = CLOSE_SIZE / 2.f;
// Space kept between the title and the close button.
constexpr int LABEL_GAP = 8;
// What the close button takes from each side of a centred title.
constexpr int TITLE_SIDE = CLOSE_LEFT + CLOSE_SIZE + LABEL_GAP;

// All strips share one scene and font atlas, with the first slot's strip at
// the top of the buffer. The renderer's clear colour is the title background.
constexpr float strip_bottom(int index) {
    return static_cast<float>(STRIP_ROWS - (index + 1) * TITLE_HEIGHT);
}

// The close buttons.
class Decorations final : public zocos::Node {
public:
    static Decorations* create(zocos::Director& director) {
        auto* decorations = new (mstd::nothrow) Decorations(director);
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

    // A title bar too narrow for the button shows none.
    void show_close(int index, bool shown) { shown_[index] = shown; }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        for (int index = 0; index < MAX_WINDOWS; ++index) {
            if (!shown_[index])
                continue;
            zocos::Rect button{CLOSE_LEFT, strip_bottom(index) + TITLE_HEIGHT - CLOSE_TOP - CLOSE_SIZE, CLOSE_SIZE,
                               CLOSE_SIZE};
            palette_.round_box(renderer, world, button, CLOSE_RADIUS, Color::Close);
        }
    }

private:
    explicit Decorations(zocos::Director& director) : palette_(director) {}

    Palette palette_;
    sys::array<bool, MAX_WINDOWS> shown_{};
};

}  // namespace

struct Chrome::Scene {
    struct Title {
        zocos::Label* label{};
        int width{};
    };

    sys::array<Title, MAX_WINDOWS> titles;
    Decorations* decorations{};
    sys::vector<unsigned char> rgba;
    int width{};
    zocos::Director* director{};

    ~Scene() {
        if (director)
            director->shutdown();
        zonix_gui::finish();
    }

    bool init(int max_width) {
        width = max_width;
        if (width <= 0 || width > 32768)
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
        decorations = Decorations::create(engine);
        if (!scene || !decorations)
            return false;
        scene->addChild(decorations);
        for (auto& title : titles) {
            title.label = zonix_gui::control_style::make_label(engine, "");
            if (!title.label)
                return false;
            title.label->setVisible(false);
            scene->addChild(title.label);
        }
        engine.runWithScene(scene);
        return engine.mainLoop();
    }

    void position(int index, int app, int title_width) {
        auto& title = titles[index];
        title.width = title_width;
        const int room = title_width - 2 * TITLE_SIDE;
        decorations->show_close(index, title_width >= CLOSE_LEFT + CLOSE_SIZE + CLOSE_MARGIN);
        title.label->setVisible(room >= 24);
        const auto& spec = desktop_ipc::APPS[app];
        title.label->setString(title_width >= 180 ? spec.title : spec.short_title);
        if (room >= 24)
            zonix_gui::control_style::place_label(*title.label, title_width / 2.f,
                                                  strip_bottom(index) + TITLE_HEIGHT / 2.f, 0.5f,
                                                  static_cast<float>(room));
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

long Chrome::init(int max_width) {
    FileStat font{};
    long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
    if (error < 0) {
        command_error("desktop", zonix_gui::UI_FONT_PATH, error);
        return error;
    }
    scene_.reset(new (sys::nothrow) Scene);
    if (!scene_)
        return SYS_ERROR_FULL;
    for (auto& app : apps_)
        app = desktop_ipc::NO_APP;
    return scene_->init(max_width) ? 0 : SYS_ERROR_IO;
}

long Chrome::configure(int slot, int app, int width) {
    if (apps_[slot] == app && scene_->titles[slot].width == width)
        return 0;
    apps_[slot] = app;
    scene_->position(slot, app, width);
    return scene_->director->mainLoop() ? 0 : SYS_ERROR_IO;
}

unsigned int Chrome::title_pixel(int index, int, int x, int y) const {
    return scene_->pixel(x, index * TITLE_HEIGHT + y);
}

}  // namespace desktop_chrome
