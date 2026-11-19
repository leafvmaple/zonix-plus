#include "../command.h"
#include "directory.h"
#include "../gui/controls/button.h"
#include "../gui/controls/context_menu.h"
#include "../gui/controls/dialog.h"
#include "../gui/controls/icon_grid.h"
#include "../gui/controls/toolbar.h"
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
using zonix_gui::Dialog;
using zonix_gui::IconGrid;
using zonix_gui::control_style::Color;
using zonix_gui::control_style::Glyph;
using zonix_gui::control_style::Palette;
using zonix_gui::control_style::place_label;

// The window has the same size as the Terminal. Layout rectangles use a
// top-left origin, like the window, unless noted otherwise.
constexpr int WIDTH = 512;
constexpr int HEIGHT = 320;
constexpr float TOOLBAR_HEIGHT = zonix_gui::Toolbar::HEIGHT;
constexpr float STATUS_HEIGHT = 28.f;
constexpr float STATUS_TOP = HEIGHT - STATUS_HEIGHT;
constexpr float STATUS_CENTER = STATUS_TOP + STATUS_HEIGHT / 2.f;
// The last column of the sidebar is the divider.
constexpr float SIDEBAR_WIDTH = 120.f;
constexpr float BODY_HEIGHT = STATUS_TOP - TOOLBAR_HEIGHT;
constexpr float MARGIN = 8.f;
// Sidebar text lines up with the labels of the location buttons.
constexpr float SIDEBAR_TEXT_X = MARGIN + zonix_gui::control_style::TEXT_INSET;
constexpr float PLACES_TOP = TOOLBAR_HEIGHT + 32.f;
constexpr float PLACE_PITCH = 30.f;
constexpr zocos::Size PAGER_BUTTON{24.f, 20.f};
constexpr float PAGE_LABEL_WIDTH = 64.f;
constexpr float NEXT_X = WIDTH - MARGIN - PAGER_BUTTON.width;
constexpr float PREVIOUS_X = NEXT_X - PAGE_LABEL_WIDTH - PAGER_BUTTON.width;
constexpr float STATUS_TEXT_X = 12.f;
// Contents between the toolbar, shortcuts and status bar, with a bottom-left origin.
constexpr zocos::Rect GRID_AREA{SIDEBAR_WIDTH, STATUS_HEIGHT, WIDTH - SIDEBAR_WIDTH, BODY_HEIGHT};

class Panels final : public zocos::Node {
public:
    explicit Panels(zocos::Director& director) : palette_(director) {}

    bool init() override { return Node::init() && palette_.init(); }

    void draw(zocos::Renderer& renderer, const zocos::Mat4& world) override {
        palette_.box(renderer, world, {0.f, STATUS_HEIGHT, SIDEBAR_WIDTH - 1.f, BODY_HEIGHT}, Color::Sidebar);
        palette_.box(renderer, world, {SIDEBAR_WIDTH - 1.f, STATUS_HEIGHT, 1.f, BODY_HEIGHT}, Color::Divider);
        palette_.box(renderer, world, {0.f, 0.f, WIDTH, STATUS_HEIGHT}, Color::Panel);
        palette_.box(renderer, world, {0.f, STATUS_HEIGHT - 1.f, WIDTH, 1.f}, Color::Divider);
    }

private:
    Palette palette_;
};

class Browser {
public:
    long run() {
        FileStat font{};
        long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
        if (error < 0)
            return error;
        auto& director = zocos::Director::getInstance();
        auto view = zonix_gui::make_window_view([this](int error) {
            message_ = error == SYS_ERROR_BUSY ? "Close a window first" : command_error_text(error);
            update_controls();
        });
        if (!director.init(WIDTH, HEIGHT, "Files", mstd::move(view)))
            return SYS_ERROR_IO;
        {
            zocos::AutoreleasePool startup("Files controls");
            auto* scene = zocos::Scene::create();
            if (scene && build_scene(director, *scene)) {
                refresh_directory();
                dir_.remember();
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
    // Keep one byte past the FAT limit so an overlong name is rejected.
    static constexpr mstd::size_t NAME_INPUT_LIMIT = sizeof(DirectoryEntry::name);
    files::Directory dir_;
    // The directory revision whose entries the grid shows.
    unsigned int shown_revision_{};
    IconGrid* grid_{};
    Dialog* dialog_{};
    zonix_gui::Toolbar* toolbar_{};
    zocos::Label* status_{};
    zocos::Label* empty_{};
    zocos::Label* page_label_{};
    Button* back_{};
    Button* forward_{};
    Button* up_{};
    sys::array<Button*, 3> places_{};
    Button* previous_{};
    Button* next_{};
    const char* message_{};
    bool done_{};

    static constexpr const char* PLACES[]{"Root", "Home", "Apps"};
    static constexpr const char* PATHS[]{"/", "/home", "/opt"};

    using Action = mstd::function<void()>;

    Action refresh_after(Action action) {
        return [this, action = mstd::move(action)] {
            action();
            update_controls();
        };
    }

    // The content is a label or a glyph.
    template <typename Content>
    Button* add_button(zocos::Director& director, zocos::Scene& scene, Content content, zocos::Rect area, Action action,
                       Button::Alignment alignment = Button::Alignment::Center) {
        auto* button = Button::create(director, content, refresh_after(mstd::move(action)));
        if (button) {
            button->layout({area.width, area.height}, alignment);
            button->setPosition({area.x, HEIGHT - area.y - area.height});
            scene.addChild(button);
        }
        return button;
    }

    // The position is the left edge or centre of the text and the middle of its line.
    static zocos::Label* add_text(zocos::Director& director, zocos::Scene& scene, const char* text,
                                  zocos::Vec2 position, float align, float opacity) {
        auto* label = zonix_gui::control_style::make_label(director, text);
        if (label) {
            place_label(*label, position.x, HEIGHT - position.y, align);
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
        toolbar_ = zonix_gui::Toolbar::create(director, WIDTH);
        if (!toolbar_)
            return false;
        toolbar_->setPosition({0.f, HEIGHT - TOOLBAR_HEIGHT});
        scene.addChild(toolbar_);
        back_ = toolbar_->add_button(Glyph::Back, refresh_after([this] { history_step(-1); }));
        forward_ = toolbar_->add_button(Glyph::Forward, refresh_after([this] { history_step(1); }));
        up_ = toolbar_->add_button(Glyph::Up, refresh_after([this] { parent(); }));

        using zonix_gui::control_style::SECONDARY_OPACITY;
        constexpr float pager_top = STATUS_TOP + (STATUS_HEIGHT - PAGER_BUTTON.height) / 2.f;
        previous_ = add_button(director, scene, Glyph::Back,
                               {PREVIOUS_X, pager_top, PAGER_BUTTON.width, PAGER_BUTTON.height},
                               [this] { change_page(-1); });
        next_ = add_button(director, scene, Glyph::Forward,
                           {NEXT_X, pager_top, PAGER_BUTTON.width, PAGER_BUTTON.height}, [this] { change_page(1); });
        status_ = add_text(director, scene, "", {STATUS_TEXT_X, STATUS_CENTER}, 0.f, 0.85f);
        page_label_ = add_text(director, scene, "", {NEXT_X - PAGE_LABEL_WIDTH / 2.f, STATUS_CENTER}, 0.5f, 0.85f);
        empty_ = add_text(director, scene, "Empty folder",
                          {SIDEBAR_WIDTH + (WIDTH - SIDEBAR_WIDTH) / 2.f, TOOLBAR_HEIGHT + BODY_HEIGHT / 2.f}, 0.5f,
                          SECONDARY_OPACITY);
        if (!back_ || !forward_ || !up_ || !previous_ || !next_ || !status_ || !page_label_ || !empty_ ||
            !add_text(director, scene, "Locations", {SIDEBAR_TEXT_X, TOOLBAR_HEIGHT + 18.f}, 0.f, SECONDARY_OPACITY))
            return false;

        for (int i = 0; i < 3; ++i) {
            places_[i] = add_button(
                director, scene, PLACES[i],
                {MARGIN, PLACES_TOP + i * PLACE_PITCH, SIDEBAR_WIDTH - 2.f * MARGIN,
                 zonix_gui::control_style::CONTROL_HEIGHT},
                [this, i] { navigate(PATHS[i]); }, Button::Alignment::Left);
            if (!places_[i])
                return false;
            places_[i]->set_style(Button::Style::Flat);
        }

        grid_ = IconGrid::create(director, {GRID_AREA.width, GRID_AREA.height}, [this](IconGrid::Interaction action) {
            message_ = nullptr;
            if (action == IconGrid::Interaction::Activate)
                open_selected();
            update_controls();
        });
        if (!grid_)
            return false;
        grid_->setPosition({GRID_AREA.x, GRID_AREA.y});
        scene.addChild(grid_);
        dir_.set_page_size(grid_->capacity());

        dialog_ = Dialog::create(director);
        if (!dialog_)
            return false;
        scene.addChild(dialog_, dialog_->getLocalZOrder());

        auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) { handle_key(event); });
        if (!keys)
            return false;
        director.getEventDispatcher().addEventListenerWithNodePriority(keys, &scene);
        return zonix_gui::ContextMenu::attach(director, scene, [this](zocos::Vec2 pointer) {
            return grid_->select_at(pointer) ? context_items() : zonix_gui::ContextMenu::Items{};
        });
    }

    zonix_gui::ContextMenu::Items context_items() {
        message_ = nullptr;
        update_controls();

        zonix_gui::ContextMenu::Items items;
        int selected = grid_->selection();
        if (selected != IconGrid::NO_ITEM) {
            const auto& entry = dir_.entry(selected);
            if (entry.type == FILE_TYPE_FILE || entry.type == FILE_TYPE_DIRECTORY) {
                items.push_back({"Open", refresh_after([this] { open_selected(); })});
                items.push_back({"Rename", [this, entry] { rename_item(entry); }});
                items.push_back({"Delete", [this, entry] { delete_item(entry); }});
            }
        }
        items.push_back({"New file", [this] { new_file(); }});
        items.push_back({"New folder", [this] { new_folder(); }});
        if (!dir_.at_root())
            items.push_back({"Go up", refresh_after([this] { parent(); })});
        items.push_back({"Refresh", refresh_after([this] { refresh_directory(); })});
        return items;
    }

    static bool is_leaf_name(const mstd::string& name) {
        if (name.empty() || name == "." || name == "..")
            return false;
        for (char ch : name) {
            if (ch == '/' || ch == '\\')
                return false;
        }
        return true;
    }

    const char* finish_mutation(long error) {
        if (error < 0)
            return command_error_text(error);
        refresh_directory();
        update_controls();
        return nullptr;
    }

    // Creates an empty file. An existing name is reported, not replaced.
    void new_file() {
        dialog_->prompt("New file", "", NAME_INPUT_LIMIT, [this](const mstd::string& name) {
            if (!is_leaf_name(name))
                return "Enter a single file name";
            long fd = sys_open(const_cast<char*>(name.c_str()), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC);
            if (fd == SYS_ERROR_INVALID)
                return "Use a valid ASCII name of up to 31 characters";
            return finish_mutation(fd < 0 ? fd : sys_close(static_cast<int>(fd)));
        });
    }

    void new_folder() {
        dialog_->prompt("New folder", "", NAME_INPUT_LIMIT, [this](const mstd::string& name) {
            if (!is_leaf_name(name))
                return "Enter a single folder name";
            long error = sys_mkdir(const_cast<char*>(name.c_str()));
            return error == SYS_ERROR_INVALID ? "Use a valid ASCII name of up to 31 characters" : finish_mutation(error);
        });
    }

    void rename_item(const DirectoryEntry& entry) {
        dialog_->prompt("Rename", entry.name, NAME_INPUT_LIMIT,
                        [this, source = mstd::string(entry.name)](const mstd::string& name) {
            if (!is_leaf_name(name))
                return "Enter a single file or folder name";
            long error = sys_rename(const_cast<char*>(source.c_str()), const_cast<char*>(name.c_str()));
            return error == SYS_ERROR_INVALID ? "Use a valid ASCII name of up to 31 characters" : finish_mutation(error);
        });
    }

    void delete_item(const DirectoryEntry& entry) {
        dialog_->confirm("Permanently delete this item?", entry.name,
                         [this, name = mstd::string(entry.name), folder = entry.type == FILE_TYPE_DIRECTORY]
                         (const mstd::string&) {
            auto* path = const_cast<char*>(name.c_str());
            return finish_mutation(folder ? sys_rmdir(path) : sys_unlink(path));
        });
    }

    void handle_key(const zocos::EventKeyboard& event) {
        auto code = static_cast<unsigned int>(event.getKeyCode());
        int key = keyboard::KeyboardState::character(code, event.getModifiers());
        if (code == DISPLAY_KEY_Q || code == DISPLAY_KEY_ESCAPE || key == 3) {
            done_ = true;
            return;
        }
        if (key == '\n')
            open_selected();
        else if (key == '\b')
            parent();
        else if (key == static_cast<int>(keyboard::Key::Up))
            move_selection(-grid_->columns());
        else if (key == static_cast<int>(keyboard::Key::Down))
            move_selection(grid_->columns());
        else if (key == static_cast<int>(keyboard::Key::Left))
            move_selection(-1);
        else if (key == static_cast<int>(keyboard::Key::Right))
            move_selection(1);
        else
            return;
        update_controls();
    }

    // Shows the failure of the directory's last operation, or clears the message.
    void show_error() { message_ = dir_.error() < 0 ? command_error_text(dir_.error()) : nullptr; }

    // Loads the grid when the directory has read a new page.
    void show_page() {
        if (shown_revision_ == dir_.revision())
            return;
        shown_revision_ = dir_.revision();
        mstd::vector<IconGrid::Item> items;
        items.reserve(dir_.size());
        for (int i = 0; i < dir_.size(); ++i) {
            const auto& entry = dir_.entry(i);
            items.push_back({entry.name, entry.type == FILE_TYPE_DIRECTORY ? IconGrid::Icon::Folder
                                                                           : IconGrid::Icon::File});
        }
        grid_->set_items(items);
    }

    void refresh_directory() {
        dir_.refresh();
        show_error();
        show_page();
    }

    void navigate(const char* path) {
        dir_.open(path);
        show_error();
        show_page();
    }

    void history_step(int delta) {
        if (dir_.step(delta)) {
            show_error();
            show_page();
        }
    }

    void parent() {
        if (dir_.up()) {
            show_error();
            show_page();
        }
    }

    bool selected_folder() const {
        int selected = grid_->selection();
        return selected != IconGrid::NO_ITEM && dir_.entry(selected).type == FILE_TYPE_DIRECTORY;
    }

    void open_selected() {
        int selected = grid_->selection();
        if (selected == IconGrid::NO_ITEM)
            return;
        const auto& entry = dir_.entry(selected);
        if (entry.type == FILE_TYPE_DIRECTORY) {
            navigate(entry.name);
        } else if (entry.type == FILE_TYPE_FILE) {
            desktop_ipc::EditPath path{};
            auto directory = strlen(dir_.path());
            auto name = strlen(entry.name);
            auto separator = dir_.at_root() ? 0U : 1U;
            if (directory + separator + name >= path.size()) {
                message_ = "File path is too long";
                return;
            }
            memcpy(path.data(), dir_.path(), directory);
            if (separator != 0)
                path[directory++] = '/';
            memcpy(path.data() + directory, entry.name, name + 1);
            long error = zonix_gui::WindowChannel::edit_text(path);
            if (error < 0)
                message_ = command_error_text(error);
        }
    }

    bool change_page(int delta) {
        bool changed = dir_.turn_page(delta);
        show_error();
        show_page();
        return changed;
    }

    void move_selection(int delta) {
        message_ = nullptr;
        int selected = grid_->selection();
        int target = selected == IconGrid::NO_ITEM ? 0 : selected + delta;
        if (target >= dir_.size() && change_page(1))
            target -= grid_->capacity();
        else if (target < 0 && change_page(-1))
            target += grid_->capacity();
        grid_->select(dir_.empty() ? IconGrid::NO_ITEM : mstd::clamp(target, 0, dir_.size() - 1));
    }

    void update_controls() {
        mstd::string path(dir_.path());
        constexpr unsigned int path_columns = 48;
        toolbar_->set_text(path.size() > path_columns ? "..." + path.substr(path.size() - path_columns) : path);
        for (int i = 0; i < 3; ++i)
            places_[i]->set_selected(path == PATHS[i]);
        back_->set_enabled(dir_.can_go_back());
        forward_->set_enabled(dir_.can_go_forward());
        up_->set_enabled(!dir_.at_root());
        previous_->set_enabled(dir_.has_previous_page());
        next_->set_enabled(dir_.has_next_page());
        empty_->setVisible(dir_.empty() && !message_);
        page_label_->setString("Page " + mstd::to_string(dir_.page_number()));
        place_label(*page_label_, NEXT_X - PAGE_LABEL_WIDTH / 2.f, STATUS_HEIGHT / 2.f, 0.5f, PAGE_LABEL_WIDTH);
        int selected = grid_->selection();
        if (message_)
            status_->setString(message_);
        else if (selected != IconGrid::NO_ITEM) {
            const auto& entry = dir_.entry(selected);
            status_->setString(mstd::string(entry.name) +
                               (selected_folder() ? "  ·  Folder" : "  ·  " + mstd::to_string(entry.size) + " B"));
        } else {
            status_->setString(mstd::to_string(dir_.size()) + " shown");
        }
        // Long messages shrink instead of running under the pager.
        place_label(*status_, STATUS_TEXT_X, STATUS_HEIGHT / 2.f, 0.f, PREVIOUS_X - 2.f * STATUS_TEXT_X);
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], (char*)"--window") != 0)
        return command_usage("Open Files with F4 in Desktop.\n");
    zonix_gui::control_style::TextSize::set(zonix_gui::Settings::load().text_size);
    Browser browser;
    long error = browser.run();
    zonix_gui::finish();
    return error < 0 ? command_error("files", nullptr, error) : 0;
}
