#include "../command.h"
#include "../gui/controls/button.h"
#include "../gui/controls/context_menu.h"
#include "../gui/controls/dialog.h"
#include "../gui/controls/icon_grid.h"
#include "../gui/controls/toolbar.h"
#include "../gui/platform.h"
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
            message_ = error == SYS_ERROR_BUSY ? "Close the current editor first" : command_error_text(error);
            update_controls();
        });
        if (!director.init(WIDTH, HEIGHT, "Files", mstd::move(view)))
            return SYS_ERROR_IO;
        {
            zocos::AutoreleasePool startup("Files controls");
            auto* scene = zocos::Scene::create();
            if (scene && build_scene(director, *scene)) {
                refresh_directory();
                record_location();
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
    struct Entry {
        DirectoryEntry item;
        long index;
    };
    static constexpr int HISTORY_SIZE = 16;
    // Keep one byte past the FAT limit so an overlong name is rejected.
    static constexpr mstd::size_t NAME_INPUT_LIMIT = sizeof(DirectoryEntry::name);
    mstd::vector<Entry> page_;
    IconGrid* grid_{};
    Dialog* dialog_{};
    sys::array<mstd::string, HISTORY_SIZE> history_{};
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
    char path_[PROCESS_MAX_PATH]{"/"};
    const char* message_{};
    long first_{};
    long next_first_{};
    int page_number_ = 1;
    int history_count_{};
    int history_index_ = -1;
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
        page_.reserve(grid_->capacity());

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
            const auto& entry = page_[selected].item;
            if (entry.type == FILE_TYPE_FILE || entry.type == FILE_TYPE_DIRECTORY) {
                items.push_back({"Open", refresh_after([this] { open_selected(); })});
                items.push_back({"Rename", [this, entry] { rename_item(entry); }});
                items.push_back({"Delete", [this, entry] { delete_item(entry); }});
            }
        }
        items.push_back({"New folder", [this] { new_folder(); }});
        if (!root())
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

    static bool dot_entry(const DirectoryEntry& entry) {
        return entry.name[0] == '.' && (entry.name[1] == 0 || (entry.name[1] == '.' && entry.name[2] == 0));
    }

    bool root() const { return path_[0] == '/' && path_[1] == 0; }

    void read_page() {
        page_.clear();
        next_first_ = 0;
        for (long index = first_;; ++index) {
            DirectoryEntry entry{};
            long result = sys_readdir((char*)".", index, &entry);
            if (result <= 0) {
                if (result < 0)
                    message_ = command_error_text(result);
                break;
            }
            if (dot_entry(entry))
                continue;
            if (static_cast<int>(page_.size()) == grid_->capacity()) {
                next_first_ = index;
                break;
            }
            page_.push_back({entry, index});
        }
        mstd::vector<IconGrid::Item> items;
        items.reserve(page_.size());
        for (const auto& entry : page_) {
            auto icon = entry.item.type == FILE_TYPE_DIRECTORY ? IconGrid::Icon::Folder : IconGrid::Icon::File;
            items.push_back({entry.item.name, icon});
        }
        grid_->set_items(items);
    }

    void refresh_directory() {
        message_ = nullptr;
        first_ = 0;
        page_number_ = 1;
        long result = sys_getcwd(path_, sizeof(path_));
        if (result < 0)
            message_ = command_error_text(result);
        read_page();
        if (!page_.empty())
            first_ = page_[0].index;
    }

    bool change_directory(const char* path) {
        long result = sys_chdir(const_cast<char*>(path));
        if (result < 0) {
            message_ = command_error_text(result);
            return false;
        }
        refresh_directory();
        return true;
    }

    void record_location() {
        if (history_index_ >= 0 && history_[history_index_] == path_)
            return;
        if (history_index_ == HISTORY_SIZE - 1) {
            for (int i = 1; i < HISTORY_SIZE; ++i)
                history_[i - 1] = mstd::move(history_[i]);
            --history_index_;
        }
        history_[++history_index_] = path_;
        history_count_ = history_index_ + 1;
    }

    void navigate(const char* path) {
        if (change_directory(path))
            record_location();
    }

    void history_step(int delta) {
        int target = history_index_ + delta;
        if (target >= 0 && target < history_count_ && change_directory(history_[target].c_str()))
            history_index_ = target;
    }

    void parent() {
        if (!root())
            navigate("..");
    }

    bool selected_folder() const {
        int selected = grid_->selection();
        return selected != IconGrid::NO_ITEM && page_[selected].item.type == FILE_TYPE_DIRECTORY;
    }

    void open_selected() {
        int selected = grid_->selection();
        if (selected == IconGrid::NO_ITEM)
            return;
        const auto& entry = page_[selected].item;
        if (entry.type == FILE_TYPE_DIRECTORY) {
            navigate(entry.name);
        } else if (entry.type == FILE_TYPE_FILE) {
            desktop_ipc::EditPath path{};
            auto directory = strlen(path_);
            auto name = strlen(entry.name);
            auto separator = root() ? 0U : 1U;
            if (directory + separator + name >= path.size()) {
                message_ = "File path is too long";
                return;
            }
            memcpy(path.data(), path_, directory);
            if (separator != 0)
                path[directory++] = '/';
            memcpy(path.data() + directory, entry.name, name + 1);
            long error = zonix_gui::WindowChannel::edit_text(path);
            if (error < 0)
                message_ = command_error_text(error);
        }
    }

    bool change_page(int delta) {
        message_ = nullptr;
        if (delta > 0 && next_first_ > 0) {
            first_ = next_first_;
            ++page_number_;
        } else if (delta < 0 && page_number_ > 1) {
            int remaining = grid_->capacity();
            long index = first_;
            while (index > 0 && remaining > 0) {
                DirectoryEntry entry{};
                long result = sys_readdir((char*)".", --index, &entry);
                if (result < 0) {
                    message_ = command_error_text(result);
                    return false;
                }
                if (result > 0 && !dot_entry(entry))
                    --remaining;
            }
            first_ = index;
            --page_number_;
        } else {
            return false;
        }
        read_page();
        return true;
    }

    void move_selection(int delta) {
        message_ = nullptr;
        int selected = grid_->selection();
        int target = selected == IconGrid::NO_ITEM ? 0 : selected + delta;
        if (target >= static_cast<int>(page_.size()) && change_page(1))
            target -= grid_->capacity();
        else if (target < 0 && change_page(-1))
            target += grid_->capacity();
        grid_->select(page_.empty() ? IconGrid::NO_ITEM : mstd::clamp(target, 0, static_cast<int>(page_.size()) - 1));
    }

    void update_controls() {
        mstd::string path(path_);
        constexpr unsigned int path_columns = 48;
        toolbar_->set_text(path.size() > path_columns ? "..." + path.substr(path.size() - path_columns) : path);
        for (int i = 0; i < 3; ++i)
            places_[i]->set_selected(path == PATHS[i]);
        back_->set_enabled(history_index_ > 0);
        forward_->set_enabled(history_index_ + 1 < history_count_);
        up_->set_enabled(!root());
        previous_->set_enabled(page_number_ > 1);
        next_->set_enabled(next_first_ > 0);
        empty_->setVisible(page_.empty() && !message_);
        page_label_->setString("Page " + mstd::to_string(page_number_));
        place_label(*page_label_, NEXT_X - PAGE_LABEL_WIDTH / 2.f, STATUS_HEIGHT / 2.f, 0.5f, PAGE_LABEL_WIDTH);
        int selected = grid_->selection();
        if (message_)
            status_->setString(message_);
        else if (selected != IconGrid::NO_ITEM) {
            const auto& entry = page_[selected].item;
            status_->setString(mstd::string(entry.name) +
                               (selected_folder() ? "  ·  Folder" : "  ·  " + mstd::to_string(entry.size) + " B"));
        } else {
            status_->setString(mstd::to_string(page_.size()) + " shown");
        }
        // Long messages shrink instead of running under the pager.
        place_label(*status_, STATUS_TEXT_X, STATUS_HEIGHT / 2.f, 0.f, PREVIOUS_X - 2.f * STATUS_TEXT_X);
    }
};

}  // namespace

extern "C" int main(int argc, char** argv) {
    if (argc != 2 || strcmp(argv[1], (char*)"--window") != 0)
        return command_usage("Open Files with F4 in Desktop.\n");
    Browser browser;
    long error = browser.run();
    zonix_gui::finish();
    return error < 0 ? command_error("files", nullptr, error) : 0;
}
