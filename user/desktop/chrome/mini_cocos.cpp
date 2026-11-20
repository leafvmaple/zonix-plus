#include "../../command.h"
#include "../../gui/controls/style.h"
#include "../../gui/platform.h"
#include "../../gui/settings.h"
#include "../desktop_icons.h"
#include "../dock.h"

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
// The Dock's icons are drawn side by side under the title strips.
constexpr int DOCK_ICON = desktop_dock::ICON;
// The menu bar's two ends are drawn in the next rows: the names from the left
// edge of the buffer and the clock up to its right edge.
constexpr int MENU_ROW = STRIP_ROWS + DOCK_ICON;
// The desktop icons' cells are side by side in the last rows. Only these rows
// stay transparent, so the compositor can put the icons over the background.
constexpr int ICON_ROW = MENU_ROW + MENU_HEIGHT;
constexpr int ICON_ROWS = desktop_icons::CELL_HEIGHT;
constexpr int BUFFER_ROWS = ICON_ROW + ICON_ROWS;
constexpr int MAX_ICONS = sizeof(desktop_icons::ITEMS) / sizeof(*desktop_icons::ITEMS);
constexpr int MENU_CLOCK_WIDTH = 160;
constexpr int MENU_NAME_GAP = 14;
constexpr float CLOSE_RADIUS = CLOSE_SIZE / 2.f;
// Space kept between the title and the close button.
constexpr int LABEL_GAP = 8;
// What the close button takes from each side of a centred title.
constexpr int TITLE_SIDE = CLOSE_LEFT + CLOSE_SIZE + LABEL_GAP;

// All strips share one scene and font atlas, with the first slot's strip at
// the top of the buffer. The buffer is cleared to transparent black, and
// everything above the icon rows has the title background behind it.
constexpr float strip_bottom(int index) {
    return static_cast<float>(BUFFER_ROWS - (index + 1) * TITLE_HEIGHT);
}

// The Dock's icons, each in a DOCK_ICON square at the bottom of the buffer.
class DockIcons final : public zocos::Node {
public:
    static DockIcons* create(zocos::Director& director) {
        auto* icons = new (mstd::nothrow) DockIcons(director);
        if (!icons)
            return nullptr;
        if (!icons->init() || !icons->palette_.init()) {
            delete icons;
            return nullptr;
        }
        icons->autorelease();
        icons->setAnchorPoint({0.f, 0.f});
        return icons;
    }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        for (int item = 0; item < desktop_dock::ITEM_COUNT; ++item) {
            const float x = static_cast<float>(item * DOCK_ICON);
            auto shape = [&](zocos::Rect area, float radius, Color color) {
                palette_.round_box(renderer, world, {x + area.x, area.y, area.width, area.height}, radius, color);
            };
            switch (desktop_dock::ITEMS[item]) {
                case desktop_ipc::APP_TERMINAL:
                    // The prompt is a label over this tile.
                    palette_.frame(renderer, world, {x + 3.f, 3.f, 38.f, 38.f}, 8.f, Color::Field, Color::Border);
                    break;
                case desktop_ipc::APP_SETTINGS:
                    // A cog: a disc with four teeth and a hole.
                    shape({20.f, 9.f, 4.f, 26.f}, 1.f, Color::Text);
                    shape({9.f, 20.f, 26.f, 4.f}, 1.f, Color::Text);
                    shape({14.f, 14.f, 16.f, 16.f}, 8.f, Color::Text);
                    shape({18.f, 18.f, 8.f, 8.f}, 4.f, Color::Panel);
                    break;
                case desktop_ipc::APP_EDITOR:
                    // A page of text with a folded corner and a cursor.
                    shape({10.f, 4.f, 24.f, 36.f}, 3.f, Color::File);
                    shape({26.f, 32.f, 8.f, 8.f}, 3.f, Color::FileFold);
                    shape({15.f, 26.f, 9.f, 2.f}, 0.f, Color::FileShade);
                    shape({15.f, 20.f, 14.f, 2.f}, 0.f, Color::FileShade);
                    shape({15.f, 14.f, 14.f, 2.f}, 0.f, Color::FileShade);
                    shape({15.f, 7.f, 2.f, 5.f}, 0.f, Color::Accent);
                    break;
                case desktop_ipc::APP_FILES:
                    shape({7.f, 31.f, 15.f, 8.f}, 3.f, Color::FolderTab);
                    shape({5.f, 9.f, 34.f, 26.f}, 4.f, Color::FolderShade);
                    shape({5.f, 9.f, 34.f, 21.f}, 4.f, Color::Folder);
                    break;
                default:
                    // A ring that is open at the top, with a bar through the gap.
                    shape({14.f, 12.f, 16.f, 16.f}, 8.f, Color::Text);
                    shape({16.f, 14.f, 12.f, 12.f}, 6.f, Color::Panel);
                    shape({18.f, 22.f, 8.f, 8.f}, 0.f, Color::Panel);
                    shape({21.f, 20.f, 2.f, 11.f}, 0.f, Color::Text);
                    break;
            }
        }
    }

private:
    explicit DockIcons(zocos::Director& director) : palette_(director) {}

    Palette palette_;
};

// The title background behind the strips, the Dock's icons and the menu bar.
class Backing final : public zocos::Node {
public:
    static Backing* create(zocos::Director& director, int width) {
        auto* backing = new (mstd::nothrow) Backing(director, width);
        if (!backing)
            return nullptr;
        if (!backing->init() || !backing->palette_.init()) {
            delete backing;
            return nullptr;
        }
        backing->autorelease();
        return backing;
    }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        palette_.box(renderer, world, {0.f, ICON_ROWS, static_cast<float>(width_), BUFFER_ROWS - ICON_ROWS},
                     Color::Panel);
    }

private:
    Backing(zocos::Director& director, int width) : palette_(director), width_(width) {}

    Palette palette_;
    int width_;
};

// The desktop icons, each in a cell of the icon rows with its label drawn over
// it. A selected icon has a dark backdrop and its label an accent pill.
class DesktopIcons final : public zocos::Node {
public:
    static constexpr float CELL_WIDTH = desktop_icons::CELL_WIDTH;
    static constexpr float CELL_HEIGHT = desktop_icons::CELL_HEIGHT;
    static constexpr float LABEL_ROOM = CELL_WIDTH - 2.f * desktop_icons::LABEL_PADDING;

    static DesktopIcons* create(zocos::Director& director) {
        auto* icons = new (mstd::nothrow) DesktopIcons(director);
        if (!icons)
            return nullptr;
        if (!icons->init() || !icons->palette_.init()) {
            delete icons;
            return nullptr;
        }
        icons->autorelease();
        icons->setAnchorPoint({0.f, 0.f});
        return icons;
    }

    int selected() const { return selected_; }
    void select(int item) { selected_ = item; }
    // The width of an item's label as drawn.
    void set_label_width(int item, float width) { label_widths_[item] = mstd::min(width, LABEL_ROOM); }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        using namespace desktop_icons;
        for (int item = 0; item < ITEM_COUNT; ++item) {
            const float left = item * CELL_WIDTH;
            if (item == selected_) {
                constexpr float backdrop = BACKDROP;
                palette_.round_box(renderer, world,
                                   {left + (CELL_WIDTH - backdrop) / 2.f, CELL_HEIGHT - BACKDROP_TOP - backdrop,
                                    backdrop, backdrop},
                                   8.f, Color::Shadow);
                const float pill = label_widths_[item] + 2.f * LABEL_PADDING;
                palette_.round_box(renderer, world,
                                   {left + (CELL_WIDTH - pill) / 2.f, CELL_HEIGHT - LABEL_CENTER - LABEL_HEIGHT / 2.f,
                                    pill, static_cast<float>(LABEL_HEIGHT)},
                                   6.f, Color::Selected);
            }
            // The icon's own square, with a bottom-left origin.
            const float x = left + (CELL_WIDTH - ICON) / 2.f;
            const float y = CELL_HEIGHT - ICON_TOP - ICON;
            auto shape = [&](zocos::Rect area, float radius, Color color) {
                palette_.round_box(renderer, world, {x + area.x, y + area.y, area.width, area.height}, radius, color);
            };
            switch (ITEMS[item].kind) {
                case Kind::Disk:
                    // A drive seen from the front, with a light at the right.
                    shape({3.f, 12.f, 42.f, 24.f}, 6.f, Color::Text);
                    shape({3.f, 12.f, 42.f, 9.f}, 4.f, Color::Border);
                    shape({3.f, 20.f, 42.f, 2.f}, 0.f, Color::Text);
                    shape({35.f, 15.f, 4.f, 4.f}, 2.f, Color::Accent);
                    break;
                case Kind::Folder:
                    shape({6.f, 36.f, 18.f, 8.f}, 3.f, Color::FolderTab);
                    shape({4.f, 8.f, 40.f, 32.f}, 5.f, Color::FolderShade);
                    shape({4.f, 8.f, 40.f, 26.f}, 5.f, Color::Folder);
                    break;
            }
        }
    }

private:
    explicit DesktopIcons(zocos::Director& director) : palette_(director) {}

    Palette palette_;
    int selected_ = desktop_icons::NO_ITEM;
    sys::array<float, MAX_ICONS> label_widths_{};
};

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
    DesktopIcons* icons{};
    zocos::Label* brand{};
    zocos::Label* program{};
    zocos::Label* clock{};
    sys::vector<unsigned char> rgba;
    int width{};
    zocos::Director* director{};

    ~Scene() {
        if (director)
            director->shutdown();
        zonix_gui::finish();
    }

    bool init(int max_width) {
        width = mstd::max(mstd::max(max_width, desktop_dock::ITEM_COUNT * DOCK_ICON),
                          desktop_icons::ITEM_COUNT * desktop_icons::CELL_WIDTH);
        if (width <= 0 || width > 32768)
            return false;
        auto stride = static_cast<mstd::size_t>(width) * 4;
        rgba.resize(stride * BUFFER_ROWS);
        auto& engine = zocos::Director::getInstance();
        if (!zonix_gui::init_offscreen(engine, {rgba.data(), width, BUFFER_ROWS, stride, rgba.size()},
                                       zocos::Color4B{0, 0, 0, 0}))
            return false;
        director = &engine;

        zocos::AutoreleasePool nodes("desktop chrome");
        auto* scene = zocos::Scene::create();
        auto* backing = Backing::create(engine, width);
        icons = DesktopIcons::create(engine);
        decorations = Decorations::create(engine);
        auto* dock = DockIcons::create(engine);
        auto* prompt = zonix_gui::control_style::make_label(engine, ">_");
        brand = zonix_gui::control_style::make_label(engine, MENU_BRAND);
        program = zonix_gui::control_style::make_label(engine, "");
        clock = zonix_gui::control_style::make_label(engine, "");
        if (!scene || !backing || !icons || !decorations || !dock || !prompt || !brand || !program || !clock)
            return false;
        scene->addChild(backing, -1);
        scene->addChild(icons);
        if (!add_icon_labels(engine, *scene))
            return false;
        scene->addChild(decorations);
        // The Dock's row is above the menu bar's, which is above the icons'.
        dock->setPosition({0.f, static_cast<float>(ICON_ROWS + MENU_HEIGHT)});
        scene->addChild(dock);
        scene->addChild(prompt);
        for (int item = 0; item < desktop_dock::ITEM_COUNT; ++item) {
            if (desktop_dock::ITEMS[item] == desktop_ipc::APP_TERMINAL)
                zonix_gui::control_style::place_label(*prompt, (item + 0.5f) * DOCK_ICON,
                                                      ICON_ROWS + MENU_HEIGHT + DOCK_ICON / 2.f, 0.5f);
        }
        scene->addChild(brand);
        scene->addChild(program);
        scene->addChild(clock);
        program->setOpacity(0.85f);
        clock->setOpacity(0.85f);
        zonix_gui::control_style::place_label(*brand, MENU_INSET, MENU_MIDDLE, 0.f);
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

    static constexpr float MENU_MIDDLE = ICON_ROWS + MENU_HEIGHT / 2.f;

    bool add_icon_labels(zocos::Director& engine, zocos::Scene& scene) {
        using namespace desktop_icons;
        for (int item = 0; item < ITEM_COUNT; ++item) {
            auto* label = zonix_gui::control_style::make_label(engine, ITEMS[item].label);
            if (!label)
                return false;
            zonix_gui::control_style::place_label(*label, (item + 0.5f) * CELL_WIDTH,
                                                  static_cast<float>(CELL_HEIGHT - LABEL_CENTER), 0.5f,
                                                  DesktopIcons::LABEL_ROOM);
            icons->set_label_width(item, label->getContentSize().width);
            scene.addChild(label);
        }
        return true;
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

    void set_menu(const char* name, const char* time) {
        program->setString(name);
        clock->setString(time);
        float name_left = MENU_INSET + brand->getContentSize().width + MENU_NAME_GAP;
        zonix_gui::control_style::place_label(*program, name_left, MENU_MIDDLE, 0.f,
                                              width - MENU_CLOCK_WIDTH - name_left);
        zonix_gui::control_style::place_label(*clock, static_cast<float>(width - MENU_INSET), MENU_MIDDLE, 1.f,
                                              MENU_CLOCK_WIDTH - MENU_INSET);
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
    zonix_gui::control_style::TextSize::set(zonix_gui::Settings::load().text_size);
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

long Chrome::set_menu(int app, const char* clock) {
    if (!menu_changed(app, clock))
        return 0;
    scene_->set_menu(app >= 0 ? desktop_ipc::APPS[app].title : MENU_NO_PROGRAM, menu_clock_.data());
    return scene_->director->mainLoop() ? 1 : SYS_ERROR_IO;
}

// The names come from the left of the buffer and the clock from its right.
unsigned int Chrome::menu_pixel(int screen_width, int x, int y) const {
    int row = MENU_ROW + y;
    if (x >= screen_width - MENU_CLOCK_WIDTH)
        return scene_->pixel(scene_->width - (screen_width - x), row);
    if (x < scene_->width - MENU_CLOCK_WIDTH)
        return scene_->pixel(x, row);
    return zonix_gui::theme::TITLE_BACKGROUND;
}

long Chrome::select_icon(int item) {
    if (item < 0)
        item = desktop_icons::NO_ITEM;
    if (scene_->icons->selected() == item)
        return 0;
    scene_->icons->select(item);
    return scene_->director->mainLoop() ? 1 : SYS_ERROR_IO;
}

// The cell holds premultiplied colour, so the background shows through by
// whatever the icon leaves uncovered.
unsigned int Chrome::icon_pixel(int item, int x, int y, unsigned int below) const {
    const auto* rgba = scene_->rgba.data() +
                       (static_cast<sys::size_t>(ICON_ROW + y) * scene_->width + item * desktop_icons::CELL_WIDTH + x) * 4;
    unsigned int clear = 255U - rgba[3];
    unsigned int pixel = 0;
    for (int channel = 0; channel < 3; ++channel) {
        unsigned int back = (below >> (16 - 8 * channel)) & 0xFF;
        unsigned int value = rgba[channel] + (back * clear + 127) / 255;
        pixel |= (value > 255 ? 255 : value) << (16 - 8 * channel);
    }
    return pixel;
}

unsigned int Chrome::dock_pixel(int item, int x, int y) const {
    return scene_->pixel(item * DOCK_ICON + x, STRIP_ROWS + y);
}

}  // namespace desktop_chrome
