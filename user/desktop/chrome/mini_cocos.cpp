#include "../../command.h"
#include "../../gui/platform.h"
#include "../chrome.h"

#include <2d/ZCLabel.h>
#include <base/ZCAutoreleasePool.h>
#include <base/ZCDirector.h>
#include <sys/vector.hpp>
#include <ui/UIButton.h>

namespace desktop_chrome {

namespace {

using desktop_ipc::WINDOW_COUNT;

constexpr int STRIP_ROWS = WINDOW_COUNT * TITLE_HEIGHT;
constexpr int BUFFER_HEIGHT = STRIP_ROWS + 1;
constexpr zocos::Color4B DESKTOP_BACKGROUND{23, 38, 53, 255};
constexpr unsigned char TITLE_BACKGROUND[]{38, 61, 81, 255};
constexpr const char* TITLES[]{"Example A", "Example B", "Terminal", "mini-cocos", "Files"};
constexpr const char* SHORT_TITLES[]{"A", "B", "Term", "Cocos", "Files"};
static_assert(sizeof(TITLES) / sizeof(*TITLES) == WINDOW_COUNT);

class TitleBands final : public zocos::Node {
public:
    explicit TitleBands(zocos::RenderDevice& device) : device_(device) {}
    ~TitleBands() override {
        if (texture_.isValid())
            device_.destroyTexture(texture_);
    }

    static TitleBands* create(zocos::RenderDevice& device, int width) {
        auto* bands = new (mstd::nothrow) TitleBands(device);
        if (!bands)
            return nullptr;
        if (!bands->init() || !bands->paint(width)) {
            delete bands;
            return nullptr;
        }
        bands->autorelease();
        return bands;
    }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        renderer.addDrawSprite(world, getContentSize(), texture_, 1.f);
    }

private:
    zocos::RenderDevice& device_;
    zocos::TextureHandle texture_{};

    bool paint(int width) {
        texture_ = device_.createTexture({1, 1, zocos::TextureFormat::RGBA8Unorm, {TITLE_BACKGROUND}});
        setAnchorPoint({0.f, 0.f});
        setContentSize({static_cast<float>(width), static_cast<float>(STRIP_ROWS)});
        setPosition({0.f, 1.f});
        return texture_.isValid();
    }
};

// All strips share one scene and font atlas. The last buffer row is the
// renderer's clear color and supplies the solid desktop background.
constexpr float strip_bottom(int index) {
    return static_cast<float>(BUFFER_HEIGHT - (index + 1) * TITLE_HEIGHT);
}

}  // namespace

struct Chrome::Scene {
    struct Title {
        zocos::Label* label{};
        zocos::ui::Button* close{};
        int width{};
    };

    sys::array<Title, WINDOW_COUNT> titles;
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
        rgba.resize(stride * BUFFER_HEIGHT);
        auto& engine = zocos::Director::getInstance();
        if (!zonix_gui::init_offscreen(engine, {rgba.data(), width, BUFFER_HEIGHT, stride, rgba.size()},
                                       DESKTOP_BACKGROUND))
            return false;
        director = &engine;

        zocos::AutoreleasePool nodes("desktop chrome");
        auto* scene = zocos::Scene::create();
        auto* bands = TitleBands::create(*engine.getRenderDevice(), width);
        if (!scene || !bands)
            return false;
        scene->addChild(bands);
        for (int index = 0; index < WINDOW_COUNT; ++index) {
            float bottom = strip_bottom(index);
            auto& title = titles[index];
            title.close = zocos::ui::Button::create(engine, "×");
            title.label = zocos::Label::createWithTTF(engine, "", zonix_gui::UI_FONT_PATH, 24.f);
            if (!title.close || !title.label)
                return false;
            constexpr float button_scale = 0.7f;
            title.close->setAnchorPoint({0.f, 0.f});
            title.close->setContentSize({CLOSE_SIZE / button_scale, CLOSE_SIZE / button_scale});
            title.close->setScale(button_scale);
            title.label->setAnchorPoint({0.f, 0.f});
            title.label->setPosition({11.f, bottom + 5.f});
            title.label->setScale(0.72f);
            scene->addChild(title.close);
            scene->addChild(title.label);
            position(index, widths[index]);
        }
        engine.runWithScene(scene);
        return engine.mainLoop();
    }

    void position(int index, int title_width) {
        auto& title = titles[index];
        title.width = title_width;
        title.label->setVisible(title_width >= 48);
        title.close->setVisible(title_width > 9);
        title.label->setString(title_width >= 124 ? TITLES[index] : SHORT_TITLES[index]);
        title.close->setPosition(
            {static_cast<float>(close_left(title_width)), strip_bottom(index) + TITLE_HEIGHT - CLOSE_TOP - CLOSE_SIZE});
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

unsigned int Chrome::background() const {
    return scene_->pixel(0, STRIP_ROWS);
}

unsigned int Chrome::title_pixel(int index, int, int x, int y) const {
    return scene_->pixel(x, index * TITLE_HEIGHT + y);
}

}  // namespace desktop_chrome
