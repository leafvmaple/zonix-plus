#include "../command.h"
#include "../gui/platform.h"
#include <2d/ZCLabel.h>
#include <base/ZCDirector.h>
#include <base/ZCEventListener.h>
#include <base/keyboard.h>
#include <ui/UIButton.h>

namespace {

constexpr int WIDTH = 500;
constexpr int HEIGHT = 344;
constexpr int PAGE_SIZE = 14;
constexpr float LABEL_SCALE = 0.65f;
constexpr float ROW_HEIGHT = 18.f;

class DirectoryRow final : public zocos::ui::Widget {
public:
    static DirectoryRow* create(zocos::Label* label, int row) {
        auto* widget = new (mstd::nothrow) DirectoryRow;
        if (!widget)
            return nullptr;
        widget->init();
        widget->autorelease();
        widget->setAnchorPoint({0.f, 0.f});
        widget->setContentSize({WIDTH - 24.f, ROW_HEIGHT});
        widget->setPosition({12.f, HEIGHT - 61.f - (row + 1) * ROW_HEIGHT});
        label->setPosition({4.f, 0.f});
        widget->addChild(label);
        return widget;
    }
};

class Browser {
public:
    long run() {
        FileStat font{};
        long error = sys_stat(const_cast<char*>(zonix_gui::UI_FONT_PATH), &font);
        if (error < 0)
            return error;

        auto& director = zocos::Director::getInstance();
        if (!director.init(WIDTH, HEIGHT, "Files"))
            return SYS_ERROR_IO;

        auto* scene = zocos::Scene::create();
        if (scene && build_scene(director, *scene)) {
            refresh_directory();
            update_controls();
            director.runWithScene(scene);
            while (!done_ && director.mainLoop()) {}
        } else {
            error = SYS_ERROR_FULL;
        }
        director.shutdown();
        return error;
    }

private:
    struct Entry {
        DirectoryEntry item;
        long index;
    };
    struct Row {
        DirectoryRow* widget;
        zocos::Label* label;
    };
    sys::array<Entry, PAGE_SIZE> page_{};
    sys::array<Row, PAGE_SIZE> rows_{};
    zocos::Label* path_label_{};
    zocos::Label* status_label_{};
    zocos::ui::Button* up_{};
    char path_[PROCESS_MAX_PATH]{"/"};
    const char* message_{};
    long first_{};
    int count_{};
    int selected_{};
    bool more_{};
    bool done_{};

    static zocos::Label* label(zocos::Director& director, const char* text = "") {
        auto* node = zocos::Label::createWithTTF(director, text, zonix_gui::UI_FONT_PATH, 24.f);
        if (node) {
            node->setAnchorPoint({0.f, 0.f});
            node->setScale(LABEL_SCALE);
        }
        return node;
    }

    bool build_scene(zocos::Director& director, zocos::Scene& scene) {
        up_ = zocos::ui::Button::create(director, "Up");
        path_label_ = label(director);
        status_label_ = label(director);
        auto* heading = label(director, "Name");
        if (!up_ || !path_label_ || !status_label_ || !heading)
            return false;
        up_->setAnchorPoint({0.f, 0.f});
        up_->setPosition({12.f, HEIGHT - 40.f});
        up_->setContentSize({60.f / LABEL_SCALE, 28.f / LABEL_SCALE});
        up_->setScale(LABEL_SCALE);
        up_->addEventListener([this](zocos::ui::Widget&) {
            parent();
            update_controls();
        });
        scene.addChild(up_);
        path_label_->setPosition({86.f, HEIGHT - 34.f});
        scene.addChild(path_label_);
        heading->setPosition({16.f, HEIGHT - 60.f});
        scene.addChild(heading);
        status_label_->setPosition({16.f, 9.f});
        scene.addChild(status_label_);
        for (int i = 0; i < PAGE_SIZE; ++i) {
            auto* name = label(director);
            auto* widget = name ? DirectoryRow::create(name, i) : nullptr;
            if (!widget)
                return false;
            rows_[i] = {widget, name};
            widget->addEventListener([this, i](zocos::ui::Widget&) {
                if (i >= count_)
                    return;
                selected_ = i;
                message_ = nullptr;
                open_selected();
                update_controls();
            });
            scene.addChild(widget);
        }

        auto* keys = zocos::EventListenerKeyboard::create([this](zocos::EventKeyboard& event) { handle_key(event); });
        if (!keys)
            return false;
        director.getEventDispatcher().addEventListenerWithNodePriority(keys, &scene);
        return true;
    }

    void handle_key(const zocos::EventKeyboard& event) {
        if (!event.isPressed())
            return;
        auto code = static_cast<unsigned int>(event.getKeyCode());
        int key = keyboard::KeyboardState::character(code, event.getModifiers());
        if (code == DISPLAY_KEY_Q || code == DISPLAY_KEY_ESCAPE || key == 3) {
            done_ = true;
            return;
        }
        if (key == static_cast<int>(keyboard::Key::Up))
            move_up();
        else if (key == static_cast<int>(keyboard::Key::Down))
            move_down();
        else if (key == '\n' || key == static_cast<int>(keyboard::Key::Right))
            open_selected();
        else if (key == '\b' || key == static_cast<int>(keyboard::Key::Left))
            parent();
        else
            return;
        update_controls();
    }

    static bool dot_entry(const DirectoryEntry& entry) {
        return entry.name[0] == '.' && (entry.name[1] == 0 || (entry.name[1] == '.' && entry.name[2] == 0));
    }

    bool root() const { return path_[0] == '/' && path_[1] == 0; }

    void read_page() {
        count_ = 0;
        more_ = false;
        for (long index = first_;; ++index) {
            DirectoryEntry entry{};
            long ready = sys_readdir((char*)".", index, &entry);
            if (ready <= 0) {
                if (ready < 0)
                    message_ = command_error_text(ready);
                break;
            }
            if (dot_entry(entry))
                continue;
            if (count_ == PAGE_SIZE) {
                more_ = true;
                break;
            }
            page_[count_++] = {entry, index};
        }
        if (selected_ >= count_)
            selected_ = count_ != 0 ? count_ - 1 : 0;
    }

    void refresh_directory() {
        message_ = nullptr;
        first_ = 0;
        selected_ = 0;
        long result = sys_getcwd(path_, sizeof(path_));
        if (result < 0)
            message_ = command_error_text(result);
        read_page();
    }

    void enter(char* name) {
        long result = sys_chdir(name);
        if (result < 0)
            message_ = command_error_text(result);
        else
            refresh_directory();
    }

    void parent() {
        if (!root())
            enter((char*)"..");
    }

    void open_selected() {
        if (count_ == 0)
            return;
        auto& entry = page_[selected_].item;
        if (entry.type == FILE_TYPE_DIRECTORY)
            enter(entry.name);
        else
            message_ = "Select a directory to open";
    }

    void move_up() {
        message_ = nullptr;
        if (selected_ > 0) {
            --selected_;
            return;
        }
        // Move the viewport back by one visible entry without caching the
        // directory. Raw indices account for structural FAT entries.
        for (long index = first_; index > 0;) {
            --index;
            DirectoryEntry entry{};
            long ready = sys_readdir((char*)".", index, &entry);
            if (ready < 0) {
                message_ = command_error_text(ready);
                return;
            }
            if (ready != 0 && !dot_entry(entry)) {
                first_ = index;
                read_page();
                return;
            }
        }
    }

    void move_down() {
        message_ = nullptr;
        if (selected_ + 1 < count_) {
            ++selected_;
        } else if (more_) {
            first_ = page_[1].index;
            read_page();
        }
    }

    void update_controls() {
        mstd::string path(path_);
        constexpr unsigned int path_columns = 26;
        path_label_->setString(path.size() > path_columns ? "..." + path.substr(path.size() - path_columns) : path);
        for (int i = 0; i < PAGE_SIZE; ++i) {
            auto& row = rows_[i];
            row.widget->setVisible(i < count_);
            if (i >= count_)
                continue;
            const auto& entry = page_[i].item;
            mstd::string name(i == selected_ ? ">  " : "    ");
            name += entry.name;
            if (entry.type == FILE_TYPE_DIRECTORY)
                name += "/";
            row.label->setString(name);
        }
        if (message_)
            status_label_->setString(message_);
        else if (count_ == 0)
            status_label_->setString("Empty directory");
        else if (more_)
            status_label_->setString("Down for more    Enter opens    Backspace goes up");
        else
            status_label_->setString("Enter opens    Backspace goes up");
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
