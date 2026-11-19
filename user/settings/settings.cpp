#include "../command.h"
#include "../gui/controls/button.h"
#include "../gui/platform.h"
#include "../gui/settings.h"
#include "../gui/window_channel.h"
#include <2d/ZCLabel.h>
#include <base/ZCAutoreleasePool.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <base/keyboard.h>
#include <sys/array.hpp>

namespace {

using zonix_gui::Button;
using zonix_gui::Settings;
using zonix_gui::control_style::Color;
using zonix_gui::control_style::Glyph;
using zonix_gui::control_style::Palette;
using zonix_gui::control_style::place_label;
using zonix_gui::control_style::SECONDARY_OPACITY;

// Layout rectangles use a top-left origin, like the window.
constexpr int WIDTH = 420;
constexpr int HEIGHT = 236;
constexpr float INSET = 20.f;
constexpr float ROW_HEIGHT = 52.f;
constexpr float FIRST_ROW = 40.f;
constexpr float CONTROL = zonix_gui::control_style::CONTROL_HEIGHT;
// Controls line up on the right of their rows.
constexpr float STEP_BUTTON = CONTROL;
constexpr float OFFSET_WIDTH = 96.f;
constexpr float SIZE_BUTTON = 84.f;
constexpr float SIZE_GAP = 6.f;
constexpr float NOTE_TOP = FIRST_ROW + 2.f * ROW_HEIGHT - 6.f;
constexpr float NOTE_PITCH = 20.f;
constexpr float STATUS_HEIGHT = 28.f;
constexpr int SIZE_COUNT = sizeof(Settings::TEXT_SIZES) / sizeof(*Settings::TEXT_SIZES);
constexpr const char* SIZE_NAMES[SIZE_COUNT] = {"Small", "Medium", "Large"};

// The line between the rows and the status bar.
class Panels final : public zocos::Node {
public:
    explicit Panels(zocos::Director& director) : palette_(director) {}

    bool init() override { return Node::init() && palette_.init(); }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        palette_.box(renderer, world, {INSET, HEIGHT - FIRST_ROW - ROW_HEIGHT / 2.f, WIDTH - 2.f * INSET, 1.f},
                     Color::Divider);
        palette_.box(renderer, world, {0.f, 0.f, WIDTH, STATUS_HEIGHT}, Color::Panel);
        palette_.box(renderer, world, {0.f, STATUS_HEIGHT - 1.f, WIDTH, 1.f}, Color::Divider);
    }

private:
    Palette palette_;
};

class Preferences {
public:
    long run() {
        FileStat font{};
        long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
        if (error < 0)
            return error;
        settings_ = Settings::load();
        zonix_gui::control_style::TextSize::set(settings_.text_size);
        auto& director = zocos::Director::getInstance();
        if (!director.init(WIDTH, HEIGHT, "Settings", zonix_gui::make_window_view({})))
            return SYS_ERROR_IO;
        {
            zocos::AutoreleasePool startup("Settings controls");
            auto* scene = zocos::Scene::create();
            if (scene && build_scene(director, *scene)) {
                update_controls();
                director.runWithScene(scene);
                while (!done_ && director.mainLoop()) {}
            } else {
                error = SYS_ERROR_FULL;
            }
        }
        director.shutdown();
        return error;
    }

private:
    Settings settings_;
    zocos::Label* offset_{};
    zocos::Label* status_{};
    Button* earlier_{};
    Button* later_{};
    sys::array<Button*, SIZE_COUNT> sizes_{};
    const char* message_ = "";
    bool done_{};

    // The content is a label or a glyph. Every button changes a setting, which
    // is saved at once.
    template <typename Content>
    Button* add_button(zocos::Director& director, zocos::Scene& scene, Content content, zocos::Rect area,
                       Button::Action action) {
        auto* button = Button::create(director, content, [this, action = mstd::move(action)] {
            action();
            save();
        });
        if (button) {
            button->layout({area.width, area.height});
            button->setPosition({area.x, HEIGHT - area.y - area.height});
            scene.addChild(button);
        }
        return button;
    }

    // The position is the left edge, centre or right edge of the text and
    // the middle of its line.
    static zocos::Label* add_text(zocos::Director& director, zocos::Scene& scene, const char* text,
                                  zocos::Vec2 position, float align, float opacity, float max_width = 0.f) {
        auto* label = zonix_gui::control_style::make_label(director, text);
        if (label) {
            place_label(*label, position.x, HEIGHT - position.y, align, max_width);
            label->setOpacity(opacity);
            scene.addChild(label);
        }
        return label;
    }

    bool build_scene(zocos::Director& director, zocos::Scene& scene) {
        auto* panels = new (mstd::nothrow) Panels(director);
        if (!panels || !panels->init()) {
            delete panels;
            return false;
        }
        panels->autorelease();
        scene.addChild(panels);

        constexpr float right = WIDTH - INSET;
        constexpr float zone_top = FIRST_ROW - CONTROL / 2.f;
        constexpr float offset_left = right - STEP_BUTTON - OFFSET_WIDTH;
        earlier_ = add_button(director, scene, Glyph::Back, {offset_left - STEP_BUTTON, zone_top, STEP_BUTTON, CONTROL},
                              [this] { settings_.timezone_minutes -= Settings::TIMEZONE_STEP; });
        later_ = add_button(director, scene, Glyph::Forward, {right - STEP_BUTTON, zone_top, STEP_BUTTON, CONTROL},
                            [this] { settings_.timezone_minutes += Settings::TIMEZONE_STEP; });
        offset_ = add_text(director, scene, "", {offset_left + OFFSET_WIDTH / 2.f, FIRST_ROW}, 0.5f, 1.f);

        constexpr float size_top = FIRST_ROW + ROW_HEIGHT - CONTROL / 2.f;
        constexpr float sizes_left = right - SIZE_COUNT * SIZE_BUTTON - (SIZE_COUNT - 1) * SIZE_GAP;
        for (int index = 0; index < SIZE_COUNT; ++index) {
            sizes_[index] = add_button(director, scene, SIZE_NAMES[index],
                                       {sizes_left + index * (SIZE_BUTTON + SIZE_GAP), size_top, SIZE_BUTTON, CONTROL},
                                       [this, index] { settings_.text_size = Settings::TEXT_SIZES[index]; });
            if (!sizes_[index])
                return false;
        }

        constexpr float note_width = WIDTH - 2.f * INSET;
        status_ = add_text(director, scene, "", {INSET - 8.f, HEIGHT - STATUS_HEIGHT / 2.f}, 0.f, 0.85f);
        auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) {
            auto code = static_cast<unsigned int>(event.getKeyCode());
            if (code == DISPLAY_KEY_ESCAPE || code == DISPLAY_KEY_Q)
                done_ = true;
        });
        if (!earlier_ || !later_ || !offset_ || !status_ || !keys ||
            !add_text(director, scene, "Time zone", {INSET, FIRST_ROW}, 0.f, 1.f) ||
            !add_text(director, scene, "Text size", {INSET, FIRST_ROW + ROW_HEIGHT}, 0.f, 1.f) ||
            !add_text(director, scene, "The time zone applies when this window closes.", {INSET, NOTE_TOP + NOTE_PITCH},
                      0.f, SECONDARY_OPACITY, note_width) ||
            !add_text(director, scene, "Text size applies to windows opened next, and to", {INSET, NOTE_TOP + 2.f * NOTE_PITCH},
                      0.f, SECONDARY_OPACITY, note_width) ||
            !add_text(director, scene, "the menu bar and titles when Desktop restarts.", {INSET, NOTE_TOP + 3.f * NOTE_PITCH},
                      0.f, SECONDARY_OPACITY, note_width))
            return false;
        director.getEventDispatcher().addEventListenerWithNodePriority(keys, &scene);
        return true;
    }

    void save() {
        long error = settings_.save();
        message_ = error < 0 ? command_error_text(error) : "Saved";
        update_controls();
    }

    void update_controls() {
        char offset[4 + Settings::OFFSET_CAPACITY] = "UTC";
        Settings::format_offset(offset + 3, settings_.timezone_minutes);
        offset_->setString(offset);
        place_label(*offset_, WIDTH - INSET - STEP_BUTTON - OFFSET_WIDTH / 2.f, HEIGHT - FIRST_ROW, 0.5f);
        earlier_->set_enabled(settings_.timezone_minutes > Settings::MIN_TIMEZONE);
        later_->set_enabled(settings_.timezone_minutes < Settings::MAX_TIMEZONE);
        for (int index = 0; index < SIZE_COUNT; ++index) {
            bool chosen = settings_.text_size == Settings::TEXT_SIZES[index];
            sizes_[index]->set_style(chosen ? Button::Style::Accent : Button::Style::Raised);
        }
        status_->setString(message_);
        place_label(*status_, INSET - 8.f, STATUS_HEIGHT / 2.f, 0.f, WIDTH - 2.f * INSET);
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], const_cast<char*>("--window")) != 0)
        return command_usage("Open Settings from the Dock in Desktop.\n");
    Preferences preferences;
    long error = preferences.run();
    zonix_gui::finish();
    return error < 0 ? command_error("settings", nullptr, error) : 0;
}
